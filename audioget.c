// audio_mixer.c  — I2S2/I2S3 + PC(stub) 3원 합성 (Philips 24bit, DMA half pipeline)
// 원본 notch.c의 더블버퍼/half-콜백/패킹 원리를 그대로 사용.
//
// 필요 extern: CubeMX가 만든 hi2s2/hi2s3, 그리고 SystemClock/MX_* 초기화 등.
// I2S2: Master TX, Standard=Philips, Data=24b, FullDuplex ENABLE
// I2S3: Master RX,  Standard=Philips, Data=24b
//
// 공개 볼륨: 0..50 (50=0dB, 0=MUTE) — 감마 LUT 동일 적용

#include "main.h"
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "spectrum9.h"

// ---- CubeMX extern I2S handles (원본 notch.c와 동일한 참조 방식) ----
extern I2S_HandleTypeDef hi2s2;   // SPI2 (TX, FD Enable)
extern I2S_HandleTypeDef hi2s3;   // SPI3 (RX)

// ---- 공개 볼륨 (요구사항) ----
extern volatile uint8_t I2S2Volume ;   // 0..50
extern volatile uint8_t I2S3Volume ;   // 0..50
extern volatile uint8_t PCAudioVolume ; // 0..50 (미구현 스텁)




extern uint8_t USB_AudioState_IsHostMuted(void);
volatile uint8_t PCAudioMute;




// ---- 블록/버퍼 크기: 원본 그대로 ----
#define BLOCK_SIZE_U16  (2048u)        // u16 halfwords per half-buffer (Lhi,Llo,Rhi,Rlo)*512
static uint16_t rxBuf[BLOCK_SIZE_U16 * 2];  // I2S2 RX (FD) — DMA double buffer
static uint16_t txBuf[BLOCK_SIZE_U16 * 2];  // I2S2 TX (FD) — DMA double buffer

// I2S3 RX 버퍼 (Philips 24bit in 32b frame, u16 4개/프레임 레이아웃 동일하게 유지)
#define I2S3_DMA_U16   (BLOCK_SIZE_U16)
#define I2S3_HALF_U16  (I2S3_DMA_U16/2)
static uint16_t mic3RxBuf[I2S3_DMA_U16 * 2];

// ---- 콜백 상태 플래그 (원본 스타일) ----
static volatile uint8_t g_dma_half      = 0; // 0: first half, 1: second half (I2S2 side)
static volatile uint8_t g_dma_half_mic3 = 0; // 0/1 (I2S3 side)

// ---- 임시 float 작업 버퍼 (스택 보호, 8바이트 정렬) ----
static float s_tmpL[512] __attribute__((aligned(8)));
static float s_tmpR[512] __attribute__((aligned(8)));

/* ========================= 볼륨 LUT (정확 dBFS 스케일) =========================
 * 0..120 step을 -60.0dB..0.0dB로 0.5dB/step 매핑. 표시(dBFS)와 실제 게인이 1:1 일치.
 */
// ========================= 볼륨 LUT (정확한 0.5 dBFS 스텝) =========================
// 0..120 → -60.0 dBFS .. 0.0 dBFS (0.5 dBFS step)
static uint8_t  s_lut_ready = 0;
static float    s_vol_lut[121];

static inline float db_to_lin(float dB)
{
    if (dB <= -120.0f) return 0.0f;
    if (dB >=   0.0f)  return 1.0f;
    // 20*log10(x) = dB → x = 10^(dB/20)
    return powf(10.0f, dB / 20.0f);
}

static inline void build_vol_lut(void){
    for (int v=0; v<=120; ++v){
        float dB = -60.0f + 0.5f * (float)v;   // 0→-60.0, 120→0.0
        s_vol_lut[v] = db_to_lin(dB);
    }
    s_lut_ready = 1;
}

static inline float vol_to_lin(uint8_t v){
    if (!s_lut_ready) build_vol_lut();

    // ★ Host MUTE 또는 로컬 MUTE면 무조건 0게인
    if (USB_AudioState_IsHostMuted()) return 0.0f;
    if (PCAudioMute)                  return 0.0f;


    if (v>120) v=120;
    return s_vol_lut[v];





}

/* === UI/표시용 dBFS 헬퍼 === */
float AudioVol_StepTo_dBFS(uint8_t step)
{
    if (step > 120) step = 120;
    return -60.0f + 0.5f * (float)step;
}
float AudioVol_GetPC_dBFS(void)
{
    extern volatile uint8_t PCAudioVolume;
    return AudioVol_StepTo_dBFS(PCAudioVolume);
}

// ========================= VU METER (USB IN / I2S2 IN) =========================
//
// UI 쪽에서 그대로 쓰는 dB 값:
//  - I2S2 IN  : s_vu_i2s2_*_dB
//  - USB IN   : s_vu_usb_*_dB
//
// dB 계산 방식은 옛 notch.c 의 vu_compute_on_block 과 동일하게,
// 16bit 풀스케일을 기준으로 -60dBFS..0dBFS 범위를 로그 스케일로 사용.

static volatile float s_vu_usb_L_dB  = -60.0f;
static volatile float s_vu_usb_R_dB  = -60.0f;
static volatile float s_vu_i2s2_L_dB = -60.0f;
static volatile float s_vu_i2s2_R_dB = -60.0f;

// 0..1 정규화된 peak → dBFS로 바꾸는 공통 함수
static inline float vu_db_from_norm_peak(float n)
{
    const float FLOOR_DB = -60.0f;

    if (n < 1.0e-6f) {
        // 완전 무음이거나 너무 작으면 그냥 바닥값
        return FLOOR_DB;
    }

    float dB = 20.0f * log10f(n);
    if (dB < FLOOR_DB) dB = FLOOR_DB;
    if (dB > 0.0f)      dB = 0.0f;   // 혹시나 rounding으로 0 넘어가면 클램프
    return dB;
}

// ---- I2S2 IN VU 계산 (rxBuf half-buffer → 피크 → dBFS) ----
//
// rxBuf 는 Philips 24bit 가 32bit 프레임에 들어있고,
// u16 배열 기준으로 [L_hi][L_lo][R_hi][R_lo]가 반복.
// 옛 notch.c 의 vu_compute_on_block 과 똑같이 hi16 을 사용해서 근사 피크를 잡는다.
//
static void VU_Update_I2S2_FromRxHalf(uint32_t roff)
{
    uint16_t *rx = &rxBuf[roff];
    const uint32_t halfwords = BLOCK_SIZE_U16; // 반버퍼 길이 (u16)

    int32_t maxL = 0;
    int32_t maxR = 0;

    /* ★ 추가: 스펙트럼 푸시용 임시 버퍼 + 프레임 카운터 */
    static int16_t s_lr[2048];   /* interleaved L,R (프레임*2) */
    uint32_t wr = 0;


    for (uint32_t i = 0; i + 3 < halfwords; i += 4) {
        int16_t l = (int16_t)rx[i + 0]; // L hi16
        int16_t r = (int16_t)rx[i + 2]; // R hi16

        int32_t al = (l < 0) ? -(int32_t)l : (int32_t)l;
        int32_t ar = (r < 0) ? -(int32_t)r : (int32_t)r;

        if (al > maxL) maxL = al;
        if (ar > maxR) maxR = ar;

        /* ★ 스펙트럼 입력 버퍼 적재 */
        if (wr < (sizeof(s_lr)/sizeof(s_lr[0]))/2) {
            s_lr[2u*wr + 0u] = l;
            s_lr[2u*wr + 1u] = r;
            wr++;
        }
    }



    float nL = (float)maxL / 32768.0f; // 16bit full scale
    float nR = (float)maxR / 32768.0f;

    s_vu_i2s2_L_dB = vu_db_from_norm_peak(nL);
    s_vu_i2s2_R_dB = vu_db_from_norm_peak(nR);

    /* ★ 스펙트럼에 I2S2 입력 신호 전달 */
    if (wr) Spectrum9_PushInt16Stereo(SPEC_SRC_I2S2, s_lr, wr);

}

// ---- USB IN VU 계산 (USB 16bit push → 피크 → dBFS) ----
//
// USB 에서 들어오는 16bit 스테레오 샘플을 그대로 이용해서
// 블록마다 피크를 잡고 dBFS를 업데이트한다.
// (링버퍼에 쌓을 때 함께 계산하므로, 볼륨과 무관한 진짜 "USB IN" 레벨이 됨)
//
static void VU_Update_USB_FromHi16Peak(int32_t maxAbsL, int32_t maxAbsR)
{
    float nL = (float)maxAbsL / 32768.0f;
    float nR = (float)maxAbsR / 32768.0f;

    s_vu_usb_L_dB = vu_db_from_norm_peak(nL);
    s_vu_usb_R_dB = vu_db_from_norm_peak(nR);
}

// ---- UI에서 사용하는 Getter들 ----
//
// usb_audio_display.c 에서 호출하는 형태:
//   void AudioVU_GetUSB_dB(float *outL, float *outR);
//   void AudioVU_GetI2S2_dB(float *outL, float *outR);


// ========================= PC USB 오디오 링버퍼 =========================

// USB에서 들어오는 16bit 스테레오 샘플을 임시로 저장해두는 링버퍼.
// 내부에서는 Q31(상위 24bit 유효)로 들고 있다가 mix_pc_into_float()에서 꺼내쓴다.

#define PC_USB_RING_FRAMES  (4096u)  // 프레임 수 (너무 작지만 않으면 OK)

static int32_t s_pcL[PC_USB_RING_FRAMES];
static int32_t s_pcR[PC_USB_RING_FRAMES];
static volatile uint32_t s_pc_w = 0;
static volatile uint32_t s_pc_r = 0;

// USB 쪽(usbd_audio_if.c)에서 호출해서 채워넣는 함수.
// src: [L,R,L,R,...] 16bit 스테레오, frames: 샘플 프레임 수.
// USB 쪽(usbd_audio_if.c)에서 호출해서 채워넣는 함수.
// src: [L,R,L,R,...] 16bit 스테레오, frames: 샘플 프레임 수.
//
// 여기서 옛 notch 스타일로 블록 피크를 잡아서 USB IN VU를 업데이트한다.
void PCAudio_USB_Push16(const int16_t *src, uint32_t frames)
{
    int32_t maxAbsL = 0;
    int32_t maxAbsR = 0;

    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t next = (s_pc_w + 1u) % PC_USB_RING_FRAMES;
        if (next == s_pc_r) {
            // overflow: 새 데이터 버림
            break;
        }

        int16_t sl = src[2u * i + 0u];
        int16_t sr = src[2u * i + 1u];

        // VU용 16bit 피크 추적
        int32_t al = (sl < 0) ? -(int32_t)sl : (int32_t)sl;
        int32_t ar = (sr < 0) ? -(int32_t)sr : (int32_t)sr;
        if (al > maxAbsL) maxAbsL = al;
        if (ar > maxAbsR) maxAbsR = ar;

        // 16bit → Q31 상위 정렬 (24bit 유효라고 봐도 무방)
        s_pcL[s_pc_w] = ((int32_t)sl) << 16;
        s_pcR[s_pc_w] = ((int32_t)sr) << 16;

        s_pc_w = next;
    }

    // 이 블록에서 실제로 받은 샘플들로 USB IN VU 갱신
    VU_Update_USB_FromHi16Peak(maxAbsL, maxAbsR);
    Spectrum9_PushInt16Stereo(SPEC_SRC_USB, src, frames);

}


/* 24-bit (3바이트) 스테레오 프레임을 수신하여 링버퍼(Q31)에 적재 + VU 피크 */
static inline int32_t signext24_to_q31(const uint8_t b0, const uint8_t b1, const uint8_t b2)
{
    /* USB 오디오는 일반적으로 LSB-first(리틀엔디안) 24bit packed: [L0,L1,L2][R0,R1,R2]... */
    int32_t v24 = (int32_t)((uint32_t)b0 | ((uint32_t)b1<<8) | ((uint32_t)b2<<16));
    if (v24 & 0x00800000) v24 |= 0xFF000000;  /* sign extend */
    /* Q31로 스케일 업(상위 24비트 유효): <<7 */
    return (v24 << 7);
}

void PCAudio_USB_Push24(const uint8_t *src, uint32_t frames)
{
    int32_t maxAbsL = 0;
    int32_t maxAbsR = 0;

    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t next = (s_pc_w + 1u) % PC_USB_RING_FRAMES;
        if (next == s_pc_r) break; /* overflow 보호 */

        const uint8_t *p = &src[i * 6u];
        int32_t sl_q31 = signext24_to_q31(p[0], p[1], p[2]);
        int32_t sr_q31 = signext24_to_q31(p[3], p[4], p[5]);

        s_pcL[s_pc_w] = sl_q31;
        s_pcR[s_pc_w] = sr_q31;

        int32_t al = (sl_q31 < 0) ? -sl_q31 : sl_q31;
        int32_t ar = (sr_q31 < 0) ? -sr_q31 : sr_q31;
        if (al > maxAbsL) maxAbsL = al;
        if (ar > maxAbsR) maxAbsR = ar;

        s_pc_w = next;
    }

    /* 24bit 기준 정규화: full-scale = 2^31 (Q31) → |x| / 2^31 */
    float nL = (float)maxAbsL / 2147483648.0f;
    float nR = (float)maxAbsR / 2147483648.0f;
    s_vu_usb_L_dB = vu_db_from_norm_peak(nL);
    s_vu_usb_R_dB = vu_db_from_norm_peak(nR);


}


void PCAudio_USB_Push24_PackedLE(const uint8_t *src, uint32_t frames)
{
    int32_t maxAbsL = 0, maxAbsR = 0;
    for (uint32_t i=0; i<frames; ++i) {
        uint32_t next = (s_pc_w + 1u) % PC_USB_RING_FRAMES;
        if (next == s_pc_r) break; /* overflow 보호 */

        const uint8_t *p = &src[i*6u];
        int32_t sl = signext24_to_q31(p[0], p[1], p[2]);
        int32_t sr = signext24_to_q31(p[3], p[4], p[5]);

        s_pcL[s_pc_w] = sl;
        s_pcR[s_pc_w] = sr;

        int32_t al = (sl < 0) ? -sl : sl;
        int32_t ar = (sr < 0) ? -sr : sr;
        if (al > maxAbsL) maxAbsL = al;
        if (ar > maxAbsR) maxAbsR = ar;

        s_pc_w = next;
    }

    /* Q31 정규화 피크 → dB */
    float nL = (float)maxAbsL / 2147483648.0f;
    float nR = (float)maxAbsR / 2147483648.0f;
    s_vu_usb_L_dB = vu_db_from_norm_peak(nL);
    s_vu_usb_R_dB = vu_db_from_norm_peak(nR);
}





// 링버퍼에 남은 프레임 수 (디버그용 / 필요하면 사용)
static uint32_t PCAudio_USB_AvailableFrames(void)
{
    if (s_pc_w >= s_pc_r) return (s_pc_w - s_pc_r);
    return (PC_USB_RING_FRAMES - (s_pc_r - s_pc_w));
}






// ========================= Q31 helpers (원본 형태 유지) =========================
static inline float   q31_to_f32(int32_t s) { return (float)s * (1.0f/2147483648.0f); } // = 2^-31
static inline int32_t float_to_q31_sat(float x){
    if (x >  0.999999f) x =  0.999999f;
    if (x < -1.000000f) x = -1.000000f;
    return (int32_t)(x * 2147483647.0f);
}



// ─── 여기 아래에 VU 관련 코드 추가 ─────────────────────────────────────────────


// RMS(0.0~1.0)를 안전하게 dBFS로 변환

static inline float vu_maxf(float a, float b)
{
    return (a > b) ? a : b;
}

// USB 오디오 입력 VU (L/R) 가져오기 (dBFS)
void AudioVU_GetUSB_dB(float *outL, float *outR)
{
    if (outL) *outL = s_vu_usb_L_dB;
    if (outR) *outR = s_vu_usb_R_dB;
}

// I2S2 입력 VU (L/R) 가져오기 (dBFS)
void AudioVU_GetI2S2_dB(float *outL, float *outR)
{
    if (outL) *outL = s_vu_i2s2_L_dB;
    if (outR) *outR = s_vu_i2s2_R_dB;
}

// 믹서 모드용: USB AUDIO IN / I2S2 IN 각각 "모노" 레벨(dBFS)
//  여기서는 L/R 중 더 큰 쪽을 사용 (max)
void AudioVU_GetMixerMono_dB(float *usb_in_db, float *i2s2_in_db)
{
    if (usb_in_db)  *usb_in_db  = vu_maxf(s_vu_usb_L_dB,  s_vu_usb_R_dB);
    if (i2s2_in_db) *i2s2_in_db = vu_maxf(s_vu_i2s2_L_dB, s_vu_i2s2_R_dB);
}


// ========================= 공통 EMA + 120Hz VU 세그먼트 API =========================

// 0..30 세그먼트용 EMA 게이트 상태
typedef struct {
    uint8_t gate_on;   // 0: 게이트 닫힘, 1: 열림
    float   ema;       // 0.0 ~ 1.0 정규화 레벨
    uint8_t seg30;     // 마지막 양자화된 0..30 세그 값
} VUEmaGateState;

// USB / I2S2 / Mixer 각각 L/R 또는 mono 상태
static VUEmaGateState s_usb_ema_L   = { 0u, 0.0f, 0u };
static VUEmaGateState s_usb_ema_R   = { 0u, 0.0f, 0u };
static VUEmaGateState s_i2s2_ema_L  = { 0u, 0.0f, 0u };
static VUEmaGateState s_i2s2_ema_R  = { 0u, 0.0f, 0u };
static VUEmaGateState s_mixer_ema   = { 0u, 0.0f, 0u }; // mixer는 mono로 보고 max 레벨 사용


//  → 이 값 줄이면 (0.3, 0.4) 더 리스폰시브하게 "휙" 올라감
#define VU_EMA_ALPHA_ATTACK   0.40f   // 올라갈 때: 빠르게 (이전값 40%, 새값 60%)
#define VU_EMA_ALPHA_RELEASE  0.40f   // 내려갈 때: 천천히 (이전값 97%, 새값 3%)

// dB 범위 & 게이트 히스테리시스 (mini VU 원리 응용)
#define VU_DB_FLOOR      (-60.0f)
#define VU_DB_CEIL       (  0.0f)
#define VU_DB_GATE_ON    (-40.0f)
#define VU_DB_GATE_OFF   (-42.0f)

#define VU_SEG30_MAX     (30u)


// 120Hz 근사: 8ms,8ms,9ms 패턴
static const uint8_t s_vu_refresh_pattern[3] = { 8u, 8u, 9u };

static uint8_t  s_vu_usb_pat_idx   = 0u;
static uint8_t  s_vu_i2s2_pat_idx  = 0u;
static uint8_t  s_vu_mix_pat_idx   = 0u;

static uint32_t s_vu_usb_next_ms   = 0u;
static uint32_t s_vu_i2s2_next_ms  = 0u;
static uint32_t s_vu_mix_next_ms   = 0u;

static uint8_t  s_vu_ema_inited    = 0u;

// dB 하나에 대해 게이트+EMA+0..30 세그 계산
static uint8_t vu_ema_step_db_to_seg30(float db, VUEmaGateState *st)
{
    // 1) dB 클램프
    if (db < VU_DB_FLOOR) db = VU_DB_FLOOR;
    if (db > VU_DB_CEIL)  db = VU_DB_CEIL;

    // 2) 히스테리시스 게이트
    if (!st->gate_on) {
        if (db >= VU_DB_GATE_ON) {
            st->gate_on = 1u;
        }
    } else {
        if (db <= VU_DB_GATE_OFF) {
            st->gate_on = 0u;
        }
    }

    // 3) 게이트 열려 있을 때만 0..1 정규화
    float target = 0.0f;
    if (st->gate_on) {
        float span = VU_DB_CEIL - VU_DB_GATE_ON; // 40dB
        if (span < 1e-3f) span = 1e-3f;
        target = (db - VU_DB_GATE_ON) / span;     // [-40,0] → [0,1]
        if (target < 0.0f) target = 0.0f;
        if (target > 1.0f) target = 1.0f;
    }

    // 4) EMA (공격/릴리즈 계수 다르게)
    float alpha = (target > st->ema) ? VU_EMA_ALPHA_ATTACK
                                     : VU_EMA_ALPHA_RELEASE;

    st->ema = alpha * st->ema + (1.0f - alpha) * target;

    // 게이트 닫힌 후에는 서서히 0으로 내려가다가 거의 0이면 완전 0 처리
    if (!st->gate_on && st->ema < 0.01f) {
        st->ema = 0.0f;
    }

    if (!st->gate_on && st->ema <= 0.0f) {
        st->seg30 = 0u;
        return 0u;
    }

    // 5) 0..1 → 0..30 세그
    float segf = st->ema * (float)VU_SEG30_MAX;
    int   seg  = (int)(segf + 0.5f);
    if (seg < 0) seg = 0;
    if (seg > (int)VU_SEG30_MAX) seg = VU_SEG30_MAX;

    st->seg30 = (uint8_t)seg;
    return st->seg30;
}

static void vu_ema_init_if_needed(void)
{
    if (s_vu_ema_inited) return;

    s_usb_ema_L  = (VUEmaGateState){ 0u, 0.0f, 0u };
    s_usb_ema_R  = (VUEmaGateState){ 0u, 0.0f, 0u };
    s_i2s2_ema_L = (VUEmaGateState){ 0u, 0.0f, 0u };
    s_i2s2_ema_R = (VUEmaGateState){ 0u, 0.0f, 0u };
    s_mixer_ema  = (VUEmaGateState){ 0u, 0.0f, 0u };

    uint32_t now = HAL_GetTick();
    s_vu_usb_next_ms  = now;
    s_vu_i2s2_next_ms = now;
    s_vu_mix_next_ms  = now;

    s_vu_usb_pat_idx  = 0u;
    s_vu_i2s2_pat_idx = 0u;
    s_vu_mix_pat_idx  = 0u;

    s_vu_ema_inited   = 1u;
}

// USB IN: 120Hz EMA 게이트 적용된 0..30 세그 (L/R)
void AudioVU_GetUSB_Seg30_EMA(uint8_t *segL, uint8_t *segR)
{
    vu_ema_init_if_needed();

    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_vu_usb_next_ms) >= 0) {
        s_vu_usb_next_ms = now + (uint32_t)s_vu_refresh_pattern[s_vu_usb_pat_idx];
        s_vu_usb_pat_idx = (uint8_t)((s_vu_usb_pat_idx + 1u) % 3u);

        (void)vu_ema_step_db_to_seg30(s_vu_usb_L_dB, &s_usb_ema_L);
        (void)vu_ema_step_db_to_seg30(s_vu_usb_R_dB, &s_usb_ema_R);
    }

    if (segL) *segL = s_usb_ema_L.seg30;
    if (segR) *segR = s_usb_ema_R.seg30;
}

// I2S2 IN: 120Hz EMA 게이트 적용된 0..30 세그 (L/R)
void AudioVU_GetI2S2_Seg30_EMA(uint8_t *segL, uint8_t *segR)
{
    vu_ema_init_if_needed();

    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_vu_i2s2_next_ms) >= 0) {
        s_vu_i2s2_next_ms = now + (uint32_t)s_vu_refresh_pattern[s_vu_i2s2_pat_idx];
        s_vu_i2s2_pat_idx = (uint8_t)((s_vu_i2s2_pat_idx + 1u) % 3u);

        (void)vu_ema_step_db_to_seg30(s_vu_i2s2_L_dB, &s_i2s2_ema_L);
        (void)vu_ema_step_db_to_seg30(s_vu_i2s2_R_dB, &s_i2s2_ema_R);
    }

    if (segL) *segL = s_i2s2_ema_L.seg30;
    if (segR) *segR = s_i2s2_ema_R.seg30;
}

// Mixer source: USB/I2S2 중 더 큰 mono dB 사용해서 0..30 세그 (mono)
void AudioVU_GetMixer_Seg30_EMA(uint8_t *segMono)
{
    vu_ema_init_if_needed();

    float usb_db  = vu_maxf(s_vu_usb_L_dB,  s_vu_usb_R_dB);
    float i2s2_db = vu_maxf(s_vu_i2s2_L_dB, s_vu_i2s2_R_dB);
    float mix_db  = vu_maxf(usb_db, i2s2_db);  // 필요에 따라 정책 바꿔도 됨

    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_vu_mix_next_ms) >= 0) {
        s_vu_mix_next_ms = now + (uint32_t)s_vu_refresh_pattern[s_vu_mix_pat_idx];
        s_vu_mix_pat_idx = (uint8_t)((s_vu_mix_pat_idx + 1u) % 3u);

        (void)vu_ema_step_db_to_seg30(mix_db, &s_mixer_ema);
    }

    if (segMono) *segMono = s_mixer_ema.seg30;
}
// 공통: packed 24bit stereo (Lhi,Llo,Rhi,Rlo) → L/R 각각 0..30 세그먼트
void VU_UpdateStereoSeg30_FromBuf(const uint16_t *src16,
                                  uint32_t u16_n,
                                  uint8_t *outSegL,
                                  uint8_t *outSegR)
{
    const uint32_t frames = u16_n / 4u; // (Lhi,Llo,Rhi,Rlo)
    const uint32_t HOP    = 8u;

    int32_t maxL = 0;
    int32_t maxR = 0;

    for (uint32_t i = 0; i < frames; i += HOP) {
        int32_t l = ((int32_t)(int16_t)src16[4*i + 0] << 16) | (int32_t)src16[4*i + 1];
        int32_t r = ((int32_t)(int16_t)src16[4*i + 2] << 16) | (int32_t)src16[4*i + 3];

        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > maxL) maxL = l;
        if (r > maxR) maxR = r;
    }

    // 아무 신호 없으면 0으로
    if (maxL == 0 && maxR == 0) {
        if (outSegL) *outSegL = 0;
        if (outSegR) *outSegR = 0;
        return;
    }

    // L
    if (outSegL) {
        float peakL = (float)maxL * (1.0f / 2147483648.0f);
        if (peakL <= 1e-8f) {
            *outSegL = 0;
        } else {
            float dbL = 8.685889638f * logf(peakL);  // 20*log10
            float FLOOR_DB = -60.0f;
            float db_c = (dbL < FLOOR_DB) ? FLOOR_DB : dbL;
            float frac = (db_c - FLOOR_DB) / (-FLOOR_DB);   // 0..1
            if (frac < 0.f) frac = 0.f;
            if (frac > 1.f) frac = 1.f;
            *outSegL = (uint8_t)(frac * 30.0f + 0.5f);      // 0..30
        }
    }

    // R
    if (outSegR) {
        float peakR = (float)maxR * (1.0f / 2147483648.0f);
        if (peakR <= 1e-8f) {
            *outSegR = 0;
        } else {
            float dbR = 8.685889638f * logf(peakR);
            float FLOOR_DB = -60.0f;
            float db_c = (dbR < FLOOR_DB) ? FLOOR_DB : dbR;
            float frac = (db_c - FLOOR_DB) / (-FLOOR_DB);
            if (frac < 0.f) frac = 0.f;
            if (frac > 1.f) frac = 1.f;
            *outSegR = (uint8_t)(frac * 30.0f + 0.5f);
        }
    }
}


// ========================= Philips 24bit pack/unpack (MSB-aligned) =========================
// u16[Lhi,Llo,Rhi,Rlo] → int32 L/R (상위 24비트 유효, 이미 MSB-aligned)
static inline void unpack_stereo_q31_from_u16x4(const uint16_t *p, int32_t *outL, int32_t *outR){
    uint32_t wL = ((uint32_t)p[0] << 16) | p[1];
    uint32_t wR = ((uint32_t)p[2] << 16) | p[3];
    *outL = (int32_t)wL;
    *outR = (int32_t)wR;
}
static inline void pack_stereo_u16x4_from_q31(uint16_t *p, int32_t qL, int32_t qR){
    uint32_t ul = (uint32_t)qL;
    uint32_t ur = (uint32_t)qR;
    p[0] = (uint16_t)(ul >> 16); p[1] = (uint16_t)(ul & 0xFFFF);
    p[2] = (uint16_t)(ur >> 16); p[3] = (uint16_t)(ur & 0xFFFF);
}










// ========================= 소스별 float 도메인 합성 =========================
// I2S2 입력을 float 도메인으로 합성 + VU용 RMS 계산 (볼륨과 독립적인 "IN 레벨")
static inline void mix_i2s2_into_float(float *L, float *R, uint32_t frames, uint8_t which_half)
{
    const float g   = vol_to_lin(I2S2Volume);
    const uint16_t *src = &rxBuf[ which_half ? BLOCK_SIZE_U16 : 0u ];

    if (g <= 0.0f) {
        // 볼륨이 0이면 아무 것도 더하지 않음 (VU는 VU_Update_I2S2_FromRxHalf()에서 peak로 처리됨)
        return;
    }

    for (uint32_t i = 0u; i < frames; ++i) {
        int32_t qL, qR;
        unpack_stereo_q31_from_u16x4(&src[4u * i], &qL, &qR);

        // 입력 신호 (볼륨 적용 전) [-1.0, +1.0]
        float fL_in = q31_to_f32(qL);
        float fR_in = q31_to_f32(qR);

        // 믹서로 들어가는 실제 신호 (볼륨 적용 후)
        L[i] += g * fL_in;
        R[i] += g * fR_in;
    }
}


static inline void mix_i2s3_into_float(float *L, float *R, uint32_t frames, uint8_t which_half){
    const float g = vol_to_lin(I2S3Volume);
    if (g <= 0.0f) return;
    const uint16_t *src = &mic3RxBuf[ which_half ? I2S3_HALF_U16 : 0 ];
    for (uint32_t i=0; i<frames; ++i){
        int32_t qL, qR; unpack_stereo_q31_from_u16x4(&src[4*i], &qL, &qR);
        // 마이크는 일단 스테레오 그대로 합성 (필요하면 모노 다운믹스 해도 됨)
        L[i] += g * q31_to_f32(qL);
        R[i] += g * q31_to_f32(qR);
    }
}

#define BLOCK_SIZE_U16  (2048u)        // u16 halfwords per half-buffer (Lhi,Llo,Rhi,Rlo)*512
static uint16_t rxBuf[BLOCK_SIZE_U16 * 2];  // I2S2 RX (FD) — DMA double buffer
static uint16_t txBuf[BLOCK_SIZE_U16 * 2];  // I2S2 TX (FD) — DMA double buffer

/* ★ 스펙트럼용 USB 프리볼륨 float 버퍼 (한 half-buffer = 512 프레임) */
static float s_specUSB_L[BLOCK_SIZE_U16 / 4u];
static float s_specUSB_R[BLOCK_SIZE_U16 / 4u];


// USB 오디오 입력을 float 도메인으로 합성 + VU용 RMS 계산
static inline void mix_pc_stub_into_float(float *L, float *R, uint32_t frames)
{
    const float g = vol_to_lin(PCAudioVolume);

    if (g <= 0.0f) {
        // 볼륨 0이면 USB 소스는 믹서에 기여하지 않음
        // (VU는 PCAudio_USB_Push16() 안의 peak 기반 경로만 사용)
        return;
    }

    for (uint32_t i = 0u; i < frames; ++i) {
        if (s_pc_r == s_pc_w) {
            // 링버퍼 비었으면 0 샘플 취급
            break; // 남은 프레임은 그냥 0으로 둠
        }

        int32_t qL = s_pcL[s_pc_r];
        int32_t qR = s_pcR[s_pc_r];
        s_pc_r = (s_pc_r + 1u) % PC_USB_RING_FRAMES;

        float fL_in = q31_to_f32(qL);
        float fR_in = q31_to_f32(qR);

        // 믹서로 들어가는 실제 신호
        L[i] += g * fL_in;
        R[i] += g * fR_in;
    }
}





// ========================= 반버퍼 렌더(원본 파이프라인 원리) =========================
static inline void render_half(uint32_t roff){
    const uint8_t half   = (roff ? 1u : 0u);
    const uint32_t frames= BLOCK_SIZE_U16 / 4u; // u16 4개가 1 frame


    // ★ NEW: I2S2 IN VU 업데이트 (옛 notch vu_compute_on_block 방식)
    VU_Update_I2S2_FromRxHalf(roff);

    // 0) 작업 버퍼 클리어
    for(uint32_t i=0;i<frames;++i){ s_tmpL[i]=0.0f; s_tmpR[i]=0.0f; }

    // 1) 각 소스 float 도메인으로 합성
    mix_i2s2_into_float(s_tmpL, s_tmpR, frames, half);      // 라인/보드 입력(I2S2ext)
    mix_i2s3_into_float(s_tmpL, s_tmpR, frames, g_dma_half_mic3); // 마이크(I2S3)
    mix_pc_stub_into_float(s_tmpL, s_tmpR, frames);         // PC(스텁)

    /* ★ 믹서 출력(합성 결과)을 스펙트럼으로 전달 */
    Spectrum9_PushFloatStereo(SPEC_SRC_MIXER, s_tmpL, s_tmpR, frames);

    // 2) float → Q31 포화 → 24bit MSB-aligned 패킹
    uint16_t *tx = &txBuf[roff];
    for (uint32_t i=0;i<frames;++i){
        int32_t qL = float_to_q31_sat(s_tmpL[i]);
        int32_t qR = float_to_q31_sat(s_tmpR[i]);
        pack_stereo_u16x4_from_q31(&tx[4*i], qL, qR);
    }
}

// ========================= I2S DMA 콜백 (원본과 동일한 위치/흐름) =========================
void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s){
    if (hi2s == &hi2s2){
        g_dma_half = 0;
        render_half(0u);
    }
}
void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s){
    if (hi2s == &hi2s2){
        g_dma_half = 1;
        render_half(BLOCK_SIZE_U16);
    }
}
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s){
    if (hi2s == &hi2s3){
        g_dma_half_mic3 = 0;
    }
}
void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s){
    if (hi2s == &hi2s3){
        g_dma_half_mic3 = 1;
    }
}

// ========================= 시작/정지 헬퍼 =========================
HAL_StatusTypeDef AudioMixer_Start(void){
    memset(txBuf, 0, sizeof(txBuf));
    memset(rxBuf, 0, sizeof(rxBuf));
    memset(mic3RxBuf, 0, sizeof(mic3RxBuf));
    g_dma_half = g_dma_half_mic3 = 0;

    // Full-duplex I2S2: TX+RX 동시 (u16 count = BLOCK_SIZE_U16)
    if (HAL_I2SEx_TransmitReceive_DMA(&hi2s2, txBuf, rxBuf, BLOCK_SIZE_U16) != HAL_OK)
        return HAL_ERROR;

    // I2S3: RX only
    if (HAL_I2S_Receive_DMA(&hi2s3, mic3RxBuf, I2S3_DMA_U16) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}
void AudioMixer_Stop(void){
    HAL_I2S_DMAStop(&hi2s2);
    HAL_I2S_DMAStop(&hi2s3);
}
