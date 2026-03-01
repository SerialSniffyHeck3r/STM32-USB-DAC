#include "spectrum9.h"
#include "LEDcontrol.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

#if defined(__GNUC__)
  #define CCMRAM  __attribute__((section(".ccmram"))) __attribute__((aligned(4)))
#else
  #define CCMRAM
#endif

/* ============================================================================
 * Spectrum9 — 9-band 정수 FFT 기반 오디오 스펙트럼 애널라이저
 *
 *  - 외부 인터페이스(Spectrum9_XXX, Spectrum9Source, LED_SetExternalLinear)
 *    는 기존 코드와 100% 호환.
 *  - 내부 구현:
 *      * 정수 Q15 기반 실수 FFT (N=256 고정)
 *      * 48kHz 기준, 프레임 길이 256샘플 → 약 187fps (공간적으로는 더 부드럽고,
 *        LED 레벨은 attack/release 로 60Hz 정도의 체감 응답속도만 남김)
 *      * 실시간 경로에는 float 전혀 사용하지 않음.
 *      * float 연산은 Init/샘플레이트 변경 시 테이블 생성에만 사용.
 *
 *  - 튜닝 가능한 파라미터들은 전부 #define 으로 노출.
 *    (밴드 주파수 / 다이내믹 레인지 / attack-release / 수직 해상도 등)
 * ========================================================================== */

/* ===== 기본 설정 ===== */

/* 설계 기준 샘플레이트 (호출자가 Spectrum9_Init(fs) 로 알려 줌.
 *  기본값은 48kHz.  */
#define S9_FS_DEFAULT          48000u

/* FFT 길이 (2의 거듭제곱만 지원). 여기서는 256 고정. */
#define S9_N                   256u
#define S9_FFT_LOG2N           8u

/* 입력 FIFO (모노 int16) 길이 — FFT 한 프레임(256)보다 넉넉하게 */
#define S9_FIFO_LEN            1024u

/* 밴드 개수 (LEDcontrol.c 의 LED_SetExternalLinear(9) 과 일치) */
#define S9_NBANDS              9u

/* ===== 밴드 설정 (여기 값만 바꿔서 튜닝) =============================
 *
 *  - 중심 주파수(Hz) — 대략적인 9밴드 그래픽 EQ 느낌
 *  - 실제 밴드 엣지(f_lo/f_hi)는 중심 주파수의 기하평균으로 자동 계산.
 *    (E0/E9은 아래 매크로에서 하드코딩 가능)
 * ==================================================================== */
#define S9_BAND_CENTER_0       63.0f
#define S9_BAND_CENTER_1       160.0f
#define S9_BAND_CENTER_2       315.0f
#define S9_BAND_CENTER_3       630.0f
#define S9_BAND_CENTER_4       1250.0f
#define S9_BAND_CENTER_5       2500.0f
#define S9_BAND_CENTER_6       5000.0f
#define S9_BAND_CENTER_7       10000.0f
#define S9_BAND_CENTER_8       16000.0f

/* 가장 바깥쪽 밴드 엣지(Hz) — 필요하면 수정 */
#define S9_EDGE_FMIN           32.0f
#define S9_EDGE_FMAX           20000.0f

/* ===== 프레임 레벨(로그 도메인) 스무딩 ==============================
 *  - env_log2[b]에 log2(power)를 근사해서 넣고,
 *    attack / release 시프트로 반응속도 조절.
 *  - SHIFT 값이 작을수록 빨리, 클수록 천천히 따라감.
 * ==================================================================== */
#define S9_ATTACK_SHIFT        2      /* 올라갈 때 1/4 정도씩 따라감 */
#define S9_RELEASE_SHIFT       4      /* 내려갈 때 1/16 정도씩 따라감 */

/* ===== 다이내믹 레인지 / 세로 해상도 ================================
 *  - log2(power) 가 S9_ENV_LOG2_MIN 보다 작으면 LED=0
 *  - S9_ENV_LOG2_MAX 이상이면 LED FULL
 *  - 그 사이를 S9_LEVEL_STEPS(기본 64단계 ≒ 6bit)로 선형 매핑 후
 *    0..4095 로 다시 확장.
 *
 *  ★ 스윕/음악 넣어보면서 LED가 너무 항상 꽉 차 있거나 거의 안 뜨면
 *    S9_ENV_LOG2_MIN / MAX 를 조절해서 맞추면 됨.
 * ==================================================================== */
#define S9_ENV_LOG2_MIN        10      /* 더 작으면 바닥 취급 */
#define S9_ENV_LOG2_MAX        30      /* 더 크면 천장 취급  */

#define S9_LEVEL_STEPS         64u     /* 6bit 수직 해상도 정도면 충분 */
#define S9_LED_MAX             4095u

/* math.h 에 M_PI 가 없는 환경 대비 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ===== 내부 상태 구조체 ===== */
typedef struct {
    uint32_t        fs;                    /* 현재 유효 샘플레이트 */
    uint8_t         enabled;
    Spectrum9Source active_src;

    /* 입력 FIFO (ISR → 메인루프) */
    volatile uint16_t w, r;
    int16_t           fifo[S9_FIFO_LEN];

    /* 현재 FFT 프레임 버퍼 (시간 영역, 모노) */
    int16_t           frame[S9_N];
    uint16_t          frame_pos;           /* 0..S9_N-1 */

    /* FFT 작업 버퍼 (정수 Q15를 확장해서 int32로 유지) */
    int32_t           xr[S9_N];
    int32_t           xi[S9_N];

    /* FFT 테이블들 (초기화 시 1회 생성) */
    int16_t           tw_re[S9_N/2];       /* twiddle: cos */
    int16_t           tw_im[S9_N/2];       /* twiddle: -sin */
    uint16_t          bitrev[S9_N];        /* 비트 리버스 인덱스 */
    int16_t           win[S9_N];           /* Hann window (Q1.15) */

    /* 밴드별 bin 범위 [start, end] (1..S9_N/2-1) */
    uint16_t          band_bin_start[S9_NBANDS];
    uint16_t          band_bin_end[S9_NBANDS];

    /* 로그 도메인 엔벨로프 log2(power) */
    int16_t           env_log2[S9_NBANDS];

    /* LEDcontrol 로 넘길 0..4095 선형 값 */
    uint16_t          led[S9_NBANDS];
} S9State;

/* 전체 상태를 CCMRAM 에 배치 */
static CCMRAM S9State g;

/* ===== 내부 유틸 ===== */

static inline uint16_t fifo_advance(uint16_t x)
{
    return (uint16_t)((x + 1u) % S9_FIFO_LEN);
}

/* FIFO: 모노 샘플 한 개 push (오버런 시 조용히 drop) */
static inline void fifo_push_mono(int16_t m)
{
    uint16_t w = g.w;
    uint16_t n = fifo_advance(w);
    if (n == g.r) {
        return; /* full */
    }
    g.fifo[w] = m;
    g.w       = n;
}

/* FIFO: 모노 샘플 한 개 pop (성공 시 1, 비어있으면 0) */
static int fifo_pop_mono(int16_t *out)
{
    uint16_t r = g.r;
    uint16_t w = g.w;
    if (r == w) {
        return 0; /* empty */
    }
    *out = g.fifo[r];
    g.r  = fifo_advance(r);
    return 1;
}

/* 내부 상태 전체 클리어 */
static void s9_clear_internal_state(void)
{
    g.w         = 0;
    g.r         = 0;
    g.frame_pos = 0;

    memset((void *)g.fifo,     0, sizeof(g.fifo));
    memset((void *)g.frame,    0, sizeof(g.frame));
    memset((void *)g.xr,       0, sizeof(g.xr));
    memset((void *)g.xi,       0, sizeof(g.xi));
    memset((void *)g.env_log2, 0, sizeof(g.env_log2));
    memset((void *)g.led,      0, sizeof(g.led));
}

/* enable/disable 공통 처리 */
static void set_enabled(int on)
{
    if (on) {
        if (!g.enabled) {
            g.enabled = 1u;
            s9_clear_internal_state();
            LED_SetExternalLinear(g.led); /* 모두 0으로 초기 상태 전달 */
        }
    } else {
        g.enabled = 0u;
        memset((void *)g.led, 0, sizeof(g.led));
        LED_SetExternalLinear(g.led);
    }
}

/* log2 근사: 최상위 비트 위치 (0..63) */
static inline uint8_t s9_log2_u64(uint64_t x)
{
    if (!x) return 0u;
#if defined(__GNUC__)
    return (uint8_t)(63u - (uint8_t)__builtin_clzll(x));
#else
    uint8_t n = 0;
    while (x >>= 1) {
        ++n;
    }
    return n;
#endif
}

/* ===== FFT/밴드 테이블 생성 (Init/샘플레이트 변경 시 1회 호출) ===== */

/* 밴드 중심 */
static const float s_band_center_hz[S9_NBANDS] = {
    S9_BAND_CENTER_0,
    S9_BAND_CENTER_1,
    S9_BAND_CENTER_2,
    S9_BAND_CENTER_3,
    S9_BAND_CENTER_4,
    S9_BAND_CENTER_5,
    S9_BAND_CENTER_6,
    S9_BAND_CENTER_7,
    S9_BAND_CENTER_8
};

/* FFT 윈도우, twiddle, bitrev, 밴드 bin 등 생성 */
static void s9_build_fft_tables(void)
{
    /* 샘플레이트 */
    float fs = (g.fs > 0u) ? (float)g.fs : (float)S9_FS_DEFAULT;
    if (fs < 8000.0f) {
        fs = 8000.0f;
    }

    /* 1) Hann window (Q1.15) */
    for (uint32_t n = 0; n < S9_N; ++n) {
        float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)n / (float)(S9_N - 1u));
        int32_t v = (int32_t)lrintf(w * 32767.0f);
        if (v < 0)      v = 0;
        if (v > 32767) v = 32767;
        g.win[n] = (int16_t)v;
    }

    /* 2) twiddle: W_N^k = cos(2πk/N) - j sin(2πk/N), Q1.15 */
    for (uint32_t k = 0; k < S9_N / 2u; ++k) {
        float ang = -2.0f * (float)M_PI * (float)k / (float)S9_N;
        float cr  = cosf(ang);
        float ci  = sinf(ang);
        int32_t vr = (int32_t)lrintf(cr * 32767.0f);
        int32_t vi = (int32_t)lrintf(ci * 32767.0f);
        if (vr < -32768) vr = -32768; if (vr > 32767) vr = 32767;
        if (vi < -32768) vi = -32768; if (vi > 32767) vi = 32767;
        g.tw_re[k] = (int16_t)vr;
        g.tw_im[k] = (int16_t)vi;
    }

    /* 3) bit-reverse 테이블 */
    for (uint32_t i = 0; i < S9_N; ++i) {
        uint32_t x = i;
        uint32_t y = 0;
        for (uint32_t b = 0; b < S9_FFT_LOG2N; ++b) {
            y = (y << 1) | (x & 1u);
            x >>= 1;
        }
        g.bitrev[i] = (uint16_t)y;
    }

    /* 4) 밴드 bin 범위 계산 (로그 간격) */
    float df = fs / (float)S9_N;  /* bin 간격(Hz) */

    float edges[S9_NBANDS + 1u];
    edges[0]               = S9_EDGE_FMIN;
    edges[S9_NBANDS]       = S9_EDGE_FMAX;

    /* 중심 주파수의 기하 평균으로 중간 엣지 설정 */
    for (uint32_t b = 1; b < S9_NBANDS; ++b) {
        float f1 = s_band_center_hz[b - 1u];
        float f2 = s_band_center_hz[b];
        if (f1 < 10.0f)  f1 = 10.0f;
        if (f2 < f1)     f2 = f1 * 2.0f;
        edges[b] = sqrtf(f1 * f2);
    }

    uint32_t max_bin = (S9_N / 2u) - 1u;

    for (uint32_t b = 0; b < S9_NBANDS; ++b) {
        float f_lo = edges[b];
        float f_hi = edges[b + 1u];

        if (f_lo < 0.0f)       f_lo = 0.0f;
        if (f_hi > fs * 0.49f) f_hi = fs * 0.49f;
        if (f_hi <= f_lo)      f_hi = f_lo + df;

        int32_t bin_lo = (int32_t)lrintf(f_lo / df);
        int32_t bin_hi = (int32_t)lrintf(f_hi / df);

        if (bin_lo < 1)              bin_lo = 1;
        if ((uint32_t)bin_hi > max_bin) bin_hi = (int32_t)max_bin;
        if (bin_hi <= bin_lo)        bin_hi = bin_lo + 1;

        g.band_bin_start[b] = (uint16_t)bin_lo;
        g.band_bin_end[b]   = (uint16_t)bin_hi;
    }

    /* 엔벨로프 초기화 */
    memset((void *)g.env_log2, 0, sizeof(g.env_log2));
}

/* ===== FFT 실행 + 밴드 전력 계산 + LED 매핑 ========================== */

/* 한 프레임(256샘플)이 채워졌을 때만 호출됨 (메인루프에서) */
static void s9_process_frame_and_update_led(void)
{
    /* 1) 시간 영역 → 복소 버퍼로 복사 + Hann window 적용 */
    for (uint32_t n = 0; n < S9_N; ++n) {
        int32_t x = (int32_t)g.frame[n];        /* 16bit → 32bit */
        int32_t w = (int32_t)g.win[n];          /* Q1.15 */
        int64_t t = (int64_t)x * (int64_t)w;    /* Q16.15 */
        t >>= 15;                               /* 다시 대략 Q16.0 */
        g.xr[n] = (int32_t)t;
        g.xi[n] = 0;
    }

    /* 2) bit-reverse reordering (in-place) */
    for (uint32_t i = 0; i < S9_N; ++i) {
        uint32_t j = g.bitrev[i];
        if (j > i) {
            int32_t tr = g.xr[i]; g.xr[i] = g.xr[j]; g.xr[j] = tr;
            int32_t ti = g.xi[i]; g.xi[i] = g.xi[j]; g.xi[j] = ti;
        }
    }

    /* 3) Cooley-Tukey radix-2 DIT FFT (스케일 포함) */
    for (uint32_t stage = 1; stage <= S9_FFT_LOG2N; ++stage) {
        uint32_t m    = 1u << stage;
        uint32_t m2   = m >> 1;
        uint32_t step = S9_N / m;

        for (uint32_t k = 0; k < S9_N; k += m) {
            for (uint32_t j = 0; j < m2; ++j) {
                uint32_t idx_tw = j * step;
                int16_t  wr     = g.tw_re[idx_tw];
                int16_t  wi     = g.tw_im[idx_tw];

                uint32_t i0 = k + j;
                uint32_t i1 = i0 + m2;

                int32_t xr0 = g.xr[i0];
                int32_t xi0 = g.xi[i0];
                int32_t xr1 = g.xr[i1];
                int32_t xi1 = g.xi[i1];

                /* t = W * (xr1 + j*xi1)  (Q1.15 * Q32) → Q32 */
                int64_t tr = (int64_t)xr1 * (int64_t)wr - (int64_t)xi1 * (int64_t)wi;
                int64_t ti = (int64_t)xr1 * (int64_t)wi + (int64_t)xi1 * (int64_t)wr;
                tr >>= 15;
                ti >>= 15;

                /* u = xr0 + j*xi0 */
                int32_t ur = xr0;
                int32_t ui = xi0;

                /* 스케일 포함 버터플라이: 결과를 /2 해서 전체적으로 1/N 스케일 */
                int32_t r0 = (int32_t)((ur + tr) >> 1);
                int32_t i0n= (int32_t)((ui + ti) >> 1);
                int32_t r1 = (int32_t)((ur - tr) >> 1);
                int32_t i1n= (int32_t)((ui - ti) >> 1);

                g.xr[i0] = r0;
                g.xi[i0] = i0n;
                g.xr[i1] = r1;
                g.xi[i1] = i1n;
            }
        }
    }

    /* 4) 밴드별 에너지 집계 (양수 bin만 1..N/2-1) */
    const int16_t log2_min   = (int16_t)S9_ENV_LOG2_MIN;
    const int16_t log2_max   = (int16_t)S9_ENV_LOG2_MAX;
    const int16_t log2_range = (int16_t)(log2_max - log2_min);

    if (log2_range <= 0) {
        return;
    }

    for (uint32_t b = 0; b < S9_NBANDS; ++b) {
        uint16_t bin_lo = g.band_bin_start[b];
        uint16_t bin_hi = g.band_bin_end[b];
        if (bin_lo >= (S9_N / 2u)) bin_lo = (S9_N / 2u) - 1u;
        if (bin_hi >= (S9_N / 2u)) bin_hi = (S9_N / 2u) - 1u;
        if (bin_hi < bin_lo)      bin_hi = bin_lo;

        uint64_t acc = 0;

        for (uint32_t k = bin_lo; k <= bin_hi; ++k) {
            int32_t re = g.xr[k];
            int32_t im = g.xi[k];
            int64_t p  = (int64_t)re * (int64_t)re +
                         (int64_t)im * (int64_t)im;
            if (p > 0) {
                acc += (uint64_t)p;
            }
        }

        /* log2(power) 근사 */
        uint8_t  log2_p = s9_log2_u64(acc);
        int16_t  newv   = (int16_t)log2_p;
        int16_t  env    = g.env_log2[b];

        if (newv > env) {
            /* Attack: env += (new-env)/2^S9_ATTACK_SHIFT */
            int16_t diff  = (int16_t)(newv - env);
            int16_t delta = (int16_t)((diff + ((1 << S9_ATTACK_SHIFT) - 1)) >> S9_ATTACK_SHIFT);
            env = (int16_t)(env + delta);
        } else {
            /* Release */
            int16_t diff  = (int16_t)(env - newv);
            int16_t delta = (int16_t)((diff + ((1 << S9_RELEASE_SHIFT) - 1)) >> S9_RELEASE_SHIFT);
            env = (int16_t)(env - delta);
        }

        g.env_log2[b] = env;

        /* env_log2 → 0..(LEVEL_STEPS-1) */
        int16_t norm = (int16_t)(env - log2_min);
        if (norm < 0)          norm = 0;
        if (norm > log2_range) norm = log2_range;

        uint32_t level =
            ((uint32_t)norm * (S9_LEVEL_STEPS - 1u)) / (uint32_t)log2_range;

        /* 0..(LEVEL_STEPS-1) → 0..S9_LED_MAX */
        uint32_t led =
            (level * S9_LED_MAX + (S9_LEVEL_STEPS - 1u) / 2u) / (S9_LEVEL_STEPS - 1u);
        if (led > S9_LED_MAX) led = S9_LED_MAX;

        g.led[b] = (uint16_t)led;
    }

    /* 5) LEDcontrol 에 전달 */
    LED_SetExternalLinear(g.led);
}

/* ===== 공개 API 구현 ===== */

void Spectrum9_SetActiveSource(Spectrum9Source src)
{
    g.active_src = src;
    s9_clear_internal_state();
}

/* 샘플레이트 설정/변경 */
void Spectrum9_SetSampleRate(uint32_t fs_hz)
{
    if (fs_hz < 8000u) {
        /* 너무 낮은 샘플레이트는 지원하지 않는 것으로 보고 비활성화 */
        g.fs = 0u;
        set_enabled(0);
        return;
    }

    g.fs = fs_hz;
    set_enabled(1);
    s9_build_fft_tables();
    s9_clear_internal_state();
}

/* 모듈 전체 초기화 (부팅 시 1회 호출) */
void Spectrum9_Init(uint32_t fs_hz)
{
    memset(&g, 0, sizeof(g));
    g.active_src = SPEC_SRC_USB; /* 기본 소스는 USB 로 */
    g.fs         = (fs_hz == 0u) ? S9_FS_DEFAULT : fs_hz;
    set_enabled(1);
    s9_build_fft_tables();
    s9_clear_internal_state();
}

/* 예전 호환용: USB 16bit 스테레오 (decim8 버퍼) */
void Spectrum9_PushInt16Stereo_USBDecim8(const int16_t *lr, uint32_t frames)
{
    /* 현재 구현에선 그냥 일반 스테레오 입력과 동일하게 처리.
     *  usbd_audio_if.c 에서 decimation 을 적용했다면,
     *  Spectrum9_SetSampleRate() 를 그에 맞춰 넣어주는 게 깔끔하다.
     */
    Spectrum9_PushInt16Stereo(SPEC_SRC_USB, lr, frames);
}

/* float 스테레오 입력: -1.0..+1.0 → int16 모노 평균으로 변환 후 FIFO에 적재 */
void Spectrum9_PushFloatStereo(Spectrum9Source src,
                               const float *L,
                               const float *R,
                               uint32_t frames)
{
    if (!g.enabled || src != g.active_src || !L || !R || frames == 0u) return;

    for (uint32_t i = 0; i < frames; ++i) {
        float mf = 0.5f * (L[i] + R[i]);
        if (mf >  1.0f) mf =  1.0f;
        if (mf < -1.0f) mf = -1.0f;

        int32_t m = (int32_t)lrintf(mf * 32767.0f);
        if (m >  32767) m =  32767;
        if (m < -32768) m = -32768;

        fifo_push_mono((int16_t)m);
    }
}

/* int16 스테레오 입력: LR 평균 모노로 변환 후 FIFO에 적재 */
void Spectrum9_PushInt16Stereo(Spectrum9Source src,
                               const int16_t *lr,
                               uint32_t frames)
{
    if (!g.enabled || src != g.active_src || !lr || frames == 0u) return;

    for (uint32_t i = 0; i < frames; ++i) {
        int32_t m = ((int32_t)lr[2u * i + 0u] + (int32_t)lr[2u * i + 1u]) >> 1;
        if (m >  32767) m =  32767;
        if (m < -32768) m = -32768;
        fifo_push_mono((int16_t)m);
    }
}

/* 1ms 타이머 ISR 에서 호출: 이 구현에서는 실제 처리 X, 단순 호환용 no-op */
void Spectrum9_Task_1ms(void)
{
    /* 필요하다면 나중에 여기에서 'tick' 카운트를 올려
     * Spectrum9_ProcessMain() 에서 처리 예산을 나누는 방식으로 확장 가능.
     * 현재는 메인루프가 충분히 빠르다는 전제 하에 no-op 유지.
     */
}

/* 메인 루프에서 주기적으로 호출: FIFO에서 샘플을 빼서 프레임 채우고,
 * 프레임이 꽉 찼을 때 FFT+LED 업데이트 수행.
 */
void Spectrum9_ProcessMain(void)
{
    if (!g.enabled) return;

    /* 1) 우선 FIFO에서 가능한 만큼 프레임 버퍼 채우기 */
    while (g.frame_pos < S9_N) {
        int16_t s;
        if (!fifo_pop_mono(&s)) {
            break;  /* 더 이상 쌓인 샘플 없음 */
        }
        g.frame[g.frame_pos++] = s;
    }

    /* 2) 프레임이 다 찼으면 FFT 실행 + LED 갱신 */
    if (g.frame_pos >= S9_N) {
        s9_process_frame_and_update_led();
        g.frame_pos = 0;
    }
}
