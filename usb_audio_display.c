// usb_audio_display.c
#include "usb_audio_display.h"
#include "usb_audio_state.h"
#include "LCDUI.h"
#include <stdint.h>

#include <math.h>    /* log10f 등 쓸 수도 있으니(필요 없으면 지워도 됨) */


/* --- volume externs --- */
extern volatile uint8_t PCAudioVolume;   /* main.c 에 선언됨 */
float AudioVol_StepTo_dBFS(uint8_t step);/* audioget.c 에서 제공 */




/* 소프트웨어 볼륨 현재 스텝(0..120)을 3칸에 공백-억제(리딩 제로 없음)로 표시.
   예) "  0", " 12", "120"  */
extern volatile uint8_t PCAudioVolume;
void USBAudio_VolumeDisplaySteps(uint8_t row, uint8_t col)
{
    uint8_t v = PCAudioVolume;  // 0..120
    char c100 = ' ';
    char c10  = ' ';
    char c1   = '0' + (char)(v % 10);

    if (v >= 100) {
        c100 = '1';
        c10  = '0' + (char)((v - 100) / 10);
    } else if (v >= 10) {
        c10  = '0' + (char)(v / 10);
    }
    LCDUI_WriteCharAt(row, col + 0u, c100);
    LCDUI_WriteCharAt(row, col + 1u, c10);
    LCDUI_WriteCharAt(row, col + 2u, c1);
}

/* dBFS(-XX.XdB) 고정 자릿수 포맷.
   - 시작 위치 [col]에는 항상 부호 칸을 예약(음수면 '-', 아니면 공란).
   - 그 다음 두 칸은 정수부(10의 자리 공백 허용 → 리딩 제로 없음),
   - 소수점 한 자리,
   - 뒤에 'd','B'.
   예) -5.0dB 를 col=2 기준: [2]='-' [3]=' ' [4]='5' [5]='.' [6]='0' [7]='d' [8]='B' */
float AudioVol_StepTo_dBFS(uint8_t step);  // audioget.c에서 제공
void USBAudio_VolumeDisplaydBFS(uint8_t row, uint8_t col)
{
    // 스텝→dBFS (정확히 -60.0..0.0, 0.5 dB step)
    float dB = AudioVol_StepTo_dBFS(PCAudioVolume);

    // 부호 및 절대값
    char sign = (dB < 0.0f) ? '-' : ' ';
    float mag = (dB < 0.0f) ? -dB : dB;

    // 소수 첫째자리 반올림(텐스 단위): tenths = round(mag*10)
    int tenths = (int)(mag * 10.0f + 0.5f);
    if (tenths > 600) tenths = 600;

    int ip  = tenths / 10;   // 정수부 0..60
    int dec = tenths % 10;   // 소수부 0 또는 5

    char tens = (ip >= 10) ? (char)('0' + (ip / 10) % 10) : ' ';
    char ones = (char)('0' + (ip % 10));
    char frac = (char)('0' + dec);

    LCDUI_WriteCharAt(row, col + 0u, sign);
    LCDUI_WriteCharAt(row, col + 1u, tens);
    LCDUI_WriteCharAt(row, col + 2u, ones);
    LCDUI_WriteCharAt(row, col + 3u, '.');
    LCDUI_WriteCharAt(row, col + 4u, frac);
    LCDUI_WriteCharAt(row, col + 5u, 'd');
    LCDUI_WriteCharAt(row, col + 6u, 'B');
}



/* --- VU meter: dB 값 가져오기 (audioget.c에서 제공) --- */
void AudioVU_GetUSB_dB(float *outL, float *outR);
void AudioVU_GetI2S2_dB(float *outL, float *outR);
void AudioVU_GetMixerMono_dB(float *usb_in_db, float *i2s2_in_db);

/* --- VU meter: 커스텀 문자 슬롯 (CGRAM index 고정) --- */
/* 이 슬롯 순서대로 customchar.c에서 LCDUI_DefineCustom() 해줘야 함. */
enum {
    LCDCHAR_VU_HALF   = 0u,
    LCDCHAR_VU_REV    = 1u,
    LCDCHAR_VU_FULL   = 2u,
    LCDCHAR_VU_DEGREE = 3u
};

#define LCDCHAR_VU_MINI_L  LCDCHAR_VU_REV    /* fullscreen 에서 mini L */
#define LCDCHAR_VU_MINI_R  LCDCHAR_VU_DEGREE /* fullscreen 에서 mini R */


/* --- 외부에서 채워 넣을 0..30 VU 세그먼트 값들 실제 정의 --- */

volatile uint8_t g_usb_vu_usbL_30  = 0;
volatile uint8_t g_usb_vu_usbR_30  = 0;

volatile uint8_t g_usb_vu_i2s2L_30 = 0;
volatile uint8_t g_usb_vu_i2s2R_30 = 0;

volatile uint8_t g_usb_vu_mixA_30  = 0;
volatile uint8_t g_usb_vu_mixB_30  = 0;

/* mini VU마다 독립적인 EMA 상태 */




/* 내부 유틸: sr(Hz)가 target(Hz) 주변 tol 이내인지 확인 */
static uint8_t is_sr_near(uint32_t sr, uint32_t target, uint32_t tol)
{
    if (sr == 0u) return 0u;
    if (sr + tol < target) return 0u;
    if (sr > target + tol) return 0u;
    return 1u;
}

/**
 * 샘플레이트 "--.-k"
 */
void USBAudio_DisplaySampleRate(uint8_t row, uint8_t col)
{
    uint32_t sr = g_usb_audio_state.sample_rate;  // Hz 기준

    /* 기본값: "--.-k" */
    char d1 = '-';
    char d2 = '-';
    char d3 = '-';

    /* 44.1 / 48.0 / 96.0 kHz 근처일 때만 숫자로 표시
     *  - 토러런스는 ±200 Hz 정도로 가정 (필요하면 조정 가능)
     */
    if (is_sr_near(sr, 44100u, 200u) ||
        is_sr_near(sr, 48000u, 200u) ||
        is_sr_near(sr, 96000u, 200u))
    {
        /* 0.1kHz 단위로 반올림: 44100 → 441, 48000 → 480, 96000 → 960 */
        uint32_t sr10 = (sr + 50u) / 100u;   // 0.1 kHz 단위

        /* 441 → 4,4,1 / 480 → 4,8,0 / 960 → 9,6,0 */
        uint32_t hundreds = (sr10 / 100u) % 10u;
        uint32_t tens     = (sr10 /  10u) % 10u;
        uint32_t ones     =  sr10         % 10u;

        d1 = (char)('0' + (char)hundreds);
        d2 = (char)('0' + (char)tens);
        d3 = (char)('0' + (char)ones);
    }

    /* [d1][d2][.][d3][k] */
    LCDUI_WriteCharAt(row, col + 0u, d1);
    LCDUI_WriteCharAt(row, col + 1u, d2);
    LCDUI_WriteCharAt(row, col + 2u, '.');
    LCDUI_WriteCharAt(row, col + 3u, d3);
    LCDUI_WriteCharAt(row, col + 4u, 'K');
}

/**
 * 볼륨 "---%" / "---%" / "MUTE"
 */
void USBAudio_DisplayVolumePct(uint8_t row, uint8_t col)
{
    uint8_t mute = g_usb_audio_state.host_mute;
    uint8_t v    = g_usb_audio_state.host_volume;  // 원래 0..255일 수 있음

    /* 1) 뮤트 우선: MUTE */
    if (mute != 0u)
    {
        LCDUI_WriteCharAt(row, col + 0u, 'M');
        LCDUI_WriteCharAt(row, col + 1u, 'U');
        LCDUI_WriteCharAt(row, col + 2u, 'T');
        LCDUI_WriteCharAt(row, col + 3u, 'E');
        return;
    }

    /* 2) 부적합한 값 → "---%"
     * 여기서는 0~100 사이가 정상이라고 가정하고,
     * 그 밖의 값은 전부 "부적합" 처리.
     */
    if (v > 100u)
    {
        LCDUI_WriteCharAt(row, col + 0u, '-');
        LCDUI_WriteCharAt(row, col + 1u, '-');
        LCDUI_WriteCharAt(row, col + 2u, '-');
        LCDUI_WriteCharAt(row, col + 3u, '%');
        return;
    }

    /* 3) 정상 값 → "---%" (리딩 제로 대신 공백) */
    char c100 = ' ';
    char c10  = ' ';
    char c1   = '0';

    if (v == 100u)
    {
        c100 = '1';
        c10  = '0';
        c1   = '0';
    }
    else
    {
        uint8_t tens = v / 10u;
        uint8_t ones = v % 10u;

        if (tens > 0u) {
            c10 = (char)('0' + tens);
        } else {
            c10 = ' ';
        }

        c1 = (char)('0' + ones);
        c100 = ' ';   // 0~99 구간은 항상 공백
    }

    LCDUI_WriteCharAt(row, col + 0u, c100);
    LCDUI_WriteCharAt(row, col + 1u, c10);
    LCDUI_WriteCharAt(row, col + 2u, c1);
    LCDUI_WriteCharAt(row, col + 3u, '%');
}



/* 1) 뮤트 플래그 표시: "MUTE" / "    " */
void USBAudio_DisplayMuteFlag(uint8_t row, uint8_t col)
{
    uint8_t mute = g_usb_audio_state.host_mute;

    if (mute != 0u)
    {
        LCDUI_WriteCharAt(row, col + 0u, 'M');
        LCDUI_WriteCharAt(row, col + 1u, 'U');
        LCDUI_WriteCharAt(row, col + 2u, 'T');
        LCDUI_WriteCharAt(row, col + 3u, 'E');
    }
    else
    {
        LCDUI_WriteCharAt(row, col + 0u, ' ');
        LCDUI_WriteCharAt(row, col + 1u, ' ');
        LCDUI_WriteCharAt(row, col + 2u, ' ');
        LCDUI_WriteCharAt(row, col + 3u, ' ');
    }
}

/* 2) 인터페이스/Alt 표시: "I0A1" / "----" */
void USBAudio_DisplayIFAlt(uint8_t row, uint8_t col)
{
    uint8_t iface = g_usb_audio_state.cur_interface;
    uint8_t alt   = g_usb_audio_state.cur_alt_setting;

    if (iface == 0u && alt == 0u)
    {
        /* 아직 세션이 없거나 Alt0 (스트림 OFF)일 때는 "----" */
        LCDUI_WriteCharAt(row, col + 0u, '-');
        LCDUI_WriteCharAt(row, col + 1u, '-');
        LCDUI_WriteCharAt(row, col + 2u, '-');
        LCDUI_WriteCharAt(row, col + 3u, '-');
        return;
    }

    /* I#A# 포맷 */
    LCDUI_WriteCharAt(row, col + 0u, 'I');
    LCDUI_WriteCharAt(row, col + 1u, (char)('0' + (iface % 10u)));
    LCDUI_WriteCharAt(row, col + 2u, 'A');
    LCDUI_WriteCharAt(row, col + 3u, (char)('0' + (alt   % 10u)));
}

/* 내부: 한 자리 16진수 문자 */
static char hex_digit(uint8_t v)
{
    v &= 0x0Fu;
    if (v < 10u) return (char)('0' + v);
    return (char)('A' + (v - 10u));
}

/* 3) 마지막 bRequest 표시 */
void USBAudio_DisplayLastRequest(uint8_t row, uint8_t col)
{
    uint8_t br = g_usb_audio_state.last_bRequest;

    char c0 = ' ';
    char c1 = ' ';
    char c2 = ' ';
    char c3 = ' ';

    switch (br)
    {
        case 0x05:  // SET_ADDRESS
            c0='S'; c1='A'; c2=' '; c3=' '; break;
        case 0x09:  // SET_CONFIGURATION
            c0='S'; c1='C'; c2=' '; c3=' '; break;
        case 0x0B:  // SET_INTERFACE
            c0='S'; c1='I'; c2='F'; c3=' '; break;

        case 0x01:  // SET_CUR (Class)
            c0='C'; c1='U'; c2='R'; c3=' '; break;
        case 0x81:  // GET_CUR (Class)
            c0='G'; c1='C'; c2='U'; c3='R'; break;

        default:
            /* 모르는 값은 16진수로 "Rxx " */
            c0 = 'R';
            c1 = hex_digit((uint8_t)(br >> 4));
            c2 = hex_digit((uint8_t)(br & 0x0F));
            c3 = ' ';
            break;
    }

    LCDUI_WriteCharAt(row, col + 0u, c0);
    LCDUI_WriteCharAt(row, col + 1u, c1);
    LCDUI_WriteCharAt(row, col + 2u, c2);
    LCDUI_WriteCharAt(row, col + 3u, c3);
}

/* ======================================================================== */
/* VU meter rendering                                                       */
/* ======================================================================== */

/* dBFS → half-step(½칸) 개수로 매핑
 *  - db <= min_db  → 0 half-step
 *  - db >= max_db  → (cells*2) half-step
 */
static uint8_t vu_map_db_to_halfsteps(float db, float min_db, float max_db, uint8_t cells)
{
    uint8_t max_half = (uint8_t)(cells * 2u);
    if (db <= min_db) return 0u;
    if (db >= max_db) return max_half;

    float t  = (db - min_db) / (max_db - min_db);   // 0..1
    float hf = t * (float)max_half;                 // 0..max_half
    uint8_t steps = (uint8_t)(hf + 0.5f);           // 반올림
    if (steps > max_half) steps = max_half;
    return steps;
}

/* dBFS → 셀 개수 매핑 (믹서용 단순 버전) */
static uint8_t vu_map_db_to_cells_simple(float db, float min_db, float max_db, uint8_t cells)
{
    if (cells == 0u) return 0u;
    if (db <= min_db) return 0u;
    if (db >= max_db) return cells;

    float t = (db - min_db) / (max_db - min_db);
    float f = t * (float)cells;
    uint8_t level = (uint8_t)(f + 0.5f);
    if (level > cells) level = cells;
    return level;
}

/* 왼쪽 → 오른쪽으로 자라는 half-step VU 바 (Half + Full 사용) */
static void vu_draw_halfbar_left(uint8_t row, uint8_t col_start,
                                 uint8_t width, uint8_t halfsteps)
{
    uint8_t max_half = (uint8_t)(width * 2u);
    if (halfsteps > max_half) halfsteps = max_half;

    for (uint8_t i = 0u; i < width; ++i) {
        int16_t remain = (int16_t)halfsteps - (int16_t)(2u * i);
        char ch;
        if (remain <= 0) {
            ch = ' ';
        } else if (remain == 1) {
            ch = (char)LCDCHAR_VU_HALF;
        } else {
            ch = (char)LCDCHAR_VU_FULL;
        }
        LCDUI_WriteCharAt(row, (uint8_t)(col_start + i), ch);
    }
}

/* 오른쪽 → 왼쪽으로 자라는 half-step VU 바 (Reverse + Full 사용) */
static void vu_draw_halfbar_right(uint8_t row, uint8_t col_end,
                                  uint8_t width, uint8_t halfsteps)
{
    uint8_t max_half = (uint8_t)(width * 2u);
    if (halfsteps > max_half) halfsteps = max_half;

    for (uint8_t i = 0u; i < width; ++i) {
        int16_t remain = (int16_t)halfsteps - (int16_t)(2u * i);
        char ch;
        if (remain <= 0) {
            ch = ' ';
        } else if (remain == 1) {
            ch = (char)LCDCHAR_VU_REV;
        } else {
            ch = (char)LCDCHAR_VU_FULL;
        }
        LCDUI_WriteCharAt(row, (uint8_t)(col_end - i), ch);
    }
}

/* 단순 FullFilledVU만 쓰는 바 (믹서용) */
static void vu_draw_bar_simple(uint8_t row, uint8_t col_start,
                               uint8_t width, uint8_t filled)
{
    if (filled > width) filled = width;

    for (uint8_t i = 0u; i < width; ++i) {
        char ch = (i < filled) ? (char)LCDCHAR_VU_FULL : ' ';
        LCDUI_WriteCharAt(row, (uint8_t)(col_start + i), ch);
    }
}

/* ------------------------------------------------------------------------ */
/* 1) USB AUDIO LR VU (Compact 모드)                                        */
/* ------------------------------------------------------------------------ */
/*
 * 레이아웃(1행 고정, 1~16 전체 사용):
 *
 *   col1 : '['
 *   col2~8 : L 채널 (좌 → 우, HalfFilledVU / FullFilledVU)
 *   col9~15: R 채널 (우 → 좌, ReverseHalfFilledVU / FullFilledVU)
 *   col16: ']'
 *
 * 스케일: -40 dBFS → 0칸, 0 dBFS → 풀
 */
void USBAudio_DisplayVU_USB_LR(uint8_t rowL, uint8_t rowR, uint8_t col_start)
{
    (void)rowL; (void)rowR; (void)col_start;
    CustomChar_ExitFullScreenVU();  // 기본 글리프로 복귀

    uint8_t segL=0u, segR=0u;
    AudioVU_GetUSB_Seg30_EMA(&segL, &segR);   // 0..30 half-steps(=15칸)

    // 6칸 영역 → halfsteps = 12
    uint8_t halfL = (uint8_t)((segL * 12u + 15u) / 30u);
    uint8_t halfR = (uint8_t)((segR * 12u + 15u) / 30u);

    // 괄호 배치
    LCDUI_WriteCharAt(1u, 1u,  '[');
    LCDUI_WriteCharAt(1u, 8u,  ']');
    LCDUI_WriteCharAt(1u, 9u,  '[');
    LCDUI_WriteCharAt(1u, 16u, ']');

    // L: 2~7 (6칸, 좌→우), R: 10~15 (6칸, 우→좌)
    vu_draw_halfbar_left (1u, 2u,  6u, halfL);
    vu_draw_halfbar_right(1u, 15u, 6u, halfR);
}

/* ------------------------------------------------------------------------ */
/* 2) I2S2 LR VU (Compact 모드)                                             */
/* ------------------------------------------------------------------------ */
void USBAudio_DisplayVU_I2S2_LR(uint8_t rowL, uint8_t rowR, uint8_t col_start)
{
    (void)rowL; (void)rowR; (void)col_start;
    CustomChar_ExitFullScreenVU();

    uint8_t segL=0u, segR=0u;
    AudioVU_GetI2S2_Seg30_EMA(&segL, &segR);

    uint8_t halfL = (uint8_t)((segL * 12u + 15u) / 30u);
    uint8_t halfR = (uint8_t)((segR * 12u + 15u) / 30u);

    LCDUI_WriteCharAt(1u, 1u,  '[');
    LCDUI_WriteCharAt(1u, 8u,  ']');
    LCDUI_WriteCharAt(1u, 9u,  '[');
    LCDUI_WriteCharAt(1u, 16u, ']');

    vu_draw_halfbar_left (1u, 2u,  6u, halfL);
    vu_draw_halfbar_right(1u, 15u, 6u, halfR);
}

/* ------------------------------------------------------------------------ */
/* 3) USB AUDIO Full Screen VU                                              */
/* ------------------------------------------------------------------------ */
/*
 * 레이아웃:
 *   Row1: col1 = mini L, col2~16 = L 채널 VU (좌 → 우)
 *   Row2: col1 = mini R, col2~16 = R 채널 VU (좌 → 우)
 *
 * 스케일: -60 dBFS → 0칸, 0 dBFS → 풀
 */
void USBAudio_DisplayVU_USB_Fullscreen(void)
{
    CustomChar_EnterFullScreenVU(); // 슬롯 0,1,2를 VU용으로 세팅

    // 1) USB의 "순수 dBFS"를 직접 가져온다 (-60..0dBFS)
    float dBL = -120.0f;
    float dBR = -120.0f;
    AudioVU_GetUSB_dB(&dBL, &dBR);

    // 2) 풀스크린은 -60dBFS에서 시작해서 0dBFS에 풀(15셀 * 2 half-steps)
    uint8_t halfL = vu_map_db_to_halfsteps(dBL, -60.0f, 0.0f, 15u);
    uint8_t halfR = vu_map_db_to_halfsteps(dBR, -60.0f, 0.0f, 15u);

    // (선택) 1열 미니 인디케이터는 유지/제거 자유
    LCDUI_WriteCharAt(1u, 1u,  (uint8_t)1u);  /* Lmini */
    LCDUI_WriteCharAt(2u, 1u,  (uint8_t)3u);  /* Rmini */

    // 3) Row1/Row2 : col2~16, 좌→우로 15칸을 half-step로 그린다
    vu_draw_halfbar_left(1u, 2u, 15u, halfL);
    vu_draw_halfbar_left(2u, 2u, 15u, halfR);
}

/* ------------------------------------------------------------------------ */
/* 4) I2S2 Full Screen VU                                                   */
/* ------------------------------------------------------------------------ */
void USBAudio_DisplayVU_I2S2_Fullscreen(void)
{
    CustomChar_EnterFullScreenVU();

    float dBL = -120.0f;
    float dBR = -120.0f;
    AudioVU_GetI2S2_dB(&dBL, &dBR);

    uint8_t halfL = vu_map_db_to_halfsteps(dBL, -60.0f, 0.0f, 15u);
    uint8_t halfR = vu_map_db_to_halfsteps(dBR, -60.0f, 0.0f, 15u);

    LCDUI_WriteCharAt(1u, 1u, (char)LCDCHAR_VU_MINI_L);
    LCDUI_WriteCharAt(2u, 1u, (char)LCDCHAR_VU_MINI_R);

    vu_draw_halfbar_left(1u, 2u, 15u, halfL);
    vu_draw_halfbar_left(2u, 2u, 15u, halfR);
}

/* ------------------------------------------------------------------------ */
/* 5) Mixer 모드 VU (USB IN vs I2S2 IN, Mono)                               */
/* ------------------------------------------------------------------------ */
/*
 * 레이아웃(1행 고정):
 *   col1    : '['
 *   col2~7  : USB AUDIO IN  (6칸)
 *   col8    : ']'
 *   col9    : '['
 *   col10~15: I2S2 IN       (6칸)
 *   col16   : ']'
 *
 * 스케일: -40 dBFS → 0칸, 0 dBFS → 풀
 */
void USBAudio_DisplayVU_MixerSources(void)
{
    CustomChar_ExitFullScreenVU();

    uint8_t usbL=0u, usbR=0u, i2sL=0u, i2sR=0u;
    AudioVU_GetUSB_Seg30_EMA (&usbL,  &usbR);
    AudioVU_GetI2S2_Seg30_EMA(&i2sL,  &i2sR);

    // 모노 기준(더 큰 쪽) → 6칸 셀수
    uint8_t seg_usb  = (usbL  > usbR)  ? usbL  : usbR;
    uint8_t seg_i2s2 = (i2sL > i2sR) ? i2sL : i2sR;
    uint8_t cells_usb  = (uint8_t)((seg_usb  + 5u) / 5u);  // 0..6
    uint8_t cells_i2s2 = (uint8_t)((seg_i2s2 + 5u) / 5u);  // 0..6

    // 괄호 배치
    LCDUI_WriteCharAt(1u, 1u,  '(');
    LCDUI_WriteCharAt(1u, 8u,  ')');
    LCDUI_WriteCharAt(1u, 9u,  '(');
    LCDUI_WriteCharAt(1u, 16u, ')');

    // L(USB): 2~7  / R(I2S2): 10~15  — 단순 Full 칸
    vu_draw_bar_simple(1u, 2u,  6u, cells_usb);
    vu_draw_bar_simple(1u, 10u, 6u, cells_i2s2);
}



//===================


/* ====== [추가 1] 고정 포맷 표시: "44.1k  -30dB" (2행 3열부터 권장) ======
 *  - 샘플레이트 5칸:  USBAudio_DisplaySampleRate(row, col)  => [3..7]
 *  - 스페이스 2칸:    [8..9]  = '  '
 *  - 볼륨 dB(정수):   [10..12]  (예: -30 → '-' '3' '0',  0dB → ' ' ' ' '0')
 *  - "dB" 2칸:       [13..14] = 'd','B'
 *  - MUTE/0스텝(-inf)은 공간 제약상 "-indB"로 축약 표기
 */
void USBAudio_DisplaySRk_Spc2_SimpledB(uint8_t row, uint8_t col)
{
    /* 1) 샘플레이트 5칸 */
    USBAudio_DisplaySampleRate(row, col);

    /* 2) 공백 2칸 */
    LCDUI_WriteCharAt(row, (uint8_t)(col + 5u), ' ');
    LCDUI_WriteCharAt(row, (uint8_t)(col + 6u), ' ');

    /* 3) 볼륨: 표시 기준을 g_usb_audio_state가 아니라 PCAudioVolume로 통일 */
    uint8_t v = PCAudioVolume;            /* 0..120 */
    uint8_t hostMuted = USB_AudioState_IsHostMuted();

    if (hostMuted == 1) {
        // "MUTE" 축약 표기(공간 제약)
        LCDUI_WriteCharAt(row, col+7,  ' ');
        LCDUI_WriteCharAt(row, col+8,  'M');
        LCDUI_WriteCharAt(row, col+9,  'U');
        LCDUI_WriteCharAt(row, col+10, 'T');
        LCDUI_WriteCharAt(row, col+11, 'E');
        return;
    }

    if (v == 0u) {
        /* "-indB" (mute/–inf) */
        LCDUI_WriteCharAt(row, (uint8_t)(col + 7u),  '-');
        LCDUI_WriteCharAt(row, (uint8_t)(col + 8u),  'i');
        LCDUI_WriteCharAt(row, (uint8_t)(col + 9u),  'n');
        LCDUI_WriteCharAt(row, (uint8_t)(col + 10u), 'd');
        LCDUI_WriteCharAt(row, (uint8_t)(col + 11u), 'B');
        return;
    }

    if (v > 120u) v = 120u;
    /* 0..120 → -60.0..0.0 dBFS (0.5dB 스텝) */
    float db = -60.0f + 0.5f * (float)v;

    /* 정수 dB로 반올림해 두 자리(리딩 공백) + 부호 한 칸 */
    int  idb  = (int)lroundf(-db);        /* 0..60 (표시부호용, 0이면 0dB) */
    char sign = '-';    /* 0dB일 때 부호 공백 */
    //char sign = (idb > 0) ? '-' : ' ';    /* 0dB일 때 부호 공백 */
    char tens = (idb >= 10) ? (char)('0' + (idb/10)%10) : ' ';
    char ones = (char)('0' + (idb % 10));

    LCDUI_WriteCharAt(row, (uint8_t)(col + 7u),  sign);
    LCDUI_WriteCharAt(row, (uint8_t)(col + 8u),  tens);
    LCDUI_WriteCharAt(row, (uint8_t)(col + 9u),  ones);
    LCDUI_WriteCharAt(row, (uint8_t)(col + 10u), 'd');
    LCDUI_WriteCharAt(row, (uint8_t)(col + 11u), 'B');
}



/* 토스트 활성 여부 */
extern uint32_t HAL_GetTick(void);
static uint32_t s_toast_deadline_ms;   /* 기존에 있다면 중복 선언 제거 */

uint8_t USBAudio_RequestToast_IsActive(void)
{
    if (s_toast_deadline_ms == 0u) return 0u;
    int32_t dt = (int32_t)(s_toast_deadline_ms - HAL_GetTick());
    return (dt > 0) ? 1u : 0u;
}

/* ====== [추가 2] USB 리퀘스트 Toast (3초간 2행 3~14에 출력) ======
 *  - 시작: USBAudio_RequestToast_Start(bmRequestType, bRequest)
 *  - 그리기: USBAudio_RequestToast_Service()  (메인 루프에서 주기 호출)
 *  - 표기 형식: 2,3에 '>' 고정,  2,4..2,14 (총 11칸)에 메시지
 */
extern uint32_t HAL_GetTick(void);  /* HAL 타이머 (extern으로 사용) */

static uint32_t s_toast_deadline_ms = 0;
static char     s_toast_buf[11];    /* 고정 11문자, 스페이스 패딩 */

static char hex_digit(uint8_t v);    /* 파일 내에 이미 동일 유틸이 있으면 중복 정의 금지 */

static void toast_fill_msg(uint8_t bm, uint8_t br, char out[11])
{
    /* 공백 패딩 */
    for (int i=0;i<11;i++) out[i] = ' ';

    /* 타입 판별: 표준(0x00), 클래스(0x20), 벤더(0x40) */
    uint8_t type = (uint8_t)(bm & 0x60u);

    if (type == 0x20u) {
        /* 클래스(Audio) 대표 요청 압축 표기 */
        switch (br) {
            case 0x01: out[0]='S'; out[1]='_'; out[2]='C'; out[3]='U'; out[4]='R'; break; /* SET_CUR */
            case 0x81: out[0]='G'; out[1]='_'; out[2]='C'; out[3]='U'; out[4]='R'; break; /* GET_CUR */
            case 0x82: out[0]='G'; out[1]='_'; out[2]='M'; out[3]='I'; out[4]='N'; break; /* GET_MIN */
            case 0x83: out[0]='G'; out[1]='_'; out[2]='M'; out[3]='A'; out[4]='X'; break; /* GET_MAX */
            case 0x84: out[0]='G'; out[1]='_'; out[2]='R'; out[3]='E'; out[4]='S'; break; /* GET_RES */
            default:

                /* 알 수 없으면 "CL Rxx" */
                out[0]='C'; out[1]='L'; out[2]=' ';
                out[3]='R'; out[4]=hex_digit((uint8_t)(br>>4)); out[5]=hex_digit((uint8_t)(br&0x0F));
                break;
        }
        return;
    }

    /* 표준요청: 대표 subset */
    switch (br)
    {
        case 0x05: out[0]='S'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='A'; out[5]='D'; out[6]='D'; out[7]='R'; break;       /* SET_ADDRESS */
        case 0x09: out[0]='S'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='C'; out[5]='O'; out[6]='N'; out[7]='F'; out[8]='I'; out[9]='G'; break; /* SET_CONFIGURATION */
        case 0x0B: out[0]='S'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='I'; out[5]='N'; out[6]='T'; out[7]='F'; break;      /* SET_INTERFACE */
        case 0x06: out[0]='G'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='D'; out[5]='E'; out[6]='S'; out[7]='C'; break;      /* GET_DESCRIPTOR */
        case 0x08: out[0]='G'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='C'; out[5]='O'; out[6]='N'; out[7]='F'; out[8]='I'; out[9]='G'; break; /* GET_CONFIGURATION */
        case 0x0A: out[0]='G'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='I'; out[5]='N'; out[6]='T'; out[7]='F'; break;      /* GET_INTERFACE */
        case 0x01: out[0]='C'; out[1]='L'; out[2]='R'; out[3]=' '; out[4]='F'; out[5]='E'; out[6]='A'; out[7]='T'; break;      /* CLEAR_FEATURE */
        case 0x03: out[0]='S'; out[1]='E'; out[2]='T'; out[3]=' '; out[4]='F'; out[5]='E'; out[6]='A'; out[7]='T'; break;      /* SET_FEATURE */
        default:
            /* 모르면 "Rxx" */
            out[0]='R'; out[1]=hex_digit((uint8_t)(br>>4)); out[2]=hex_digit((uint8_t)(br&0x0F));
            break;
    }
}

/* 토스트 시작 (3초 타이머) */
void USBAudio_RequestToast_Start(uint8_t bmRequestType, uint8_t bRequest)
{
    toast_fill_msg(bmRequestType, bRequest, s_toast_buf);
    s_toast_deadline_ms = HAL_GetTick() + 3000u;
}

/* 토스트 그리기(오버레이) — 3초 유효. 2,3='>' + 2,4..14=11자 */
void USBAudio_RequestToast_Service(void)
{
    uint32_t now = HAL_GetTick();
    if (s_toast_deadline_ms == 0u) return;
    if ((int32_t)(s_toast_deadline_ms - now) <= 0) { s_toast_deadline_ms = 0u; return; }

    LCDUI_WriteCharAt(2u, 3u, '>');
    for (uint8_t i = 0; i < 11u; ++i){
        LCDUI_WriteCharAt(2u, (uint8_t)(4u + i), s_toast_buf[i]);
    }
}

/* ====== [추가 3] 클리핑 경고: VU 과다 시 LCDColorSet(RED) 잠깐 ======
 *  - 조건: USB L/R 세그가 거의 꽉 찰 때 (seg30 기준 30에 도달)
 *  - 유지: 120ms 유지 후 해제 (히스테리시스 포함)
 *  - 이전 색 복귀: 경고 종료 시 g_lcd_last_color로 복귀
 */
extern void LCDColorSet(uint8_t LCDColor);
extern volatile uint8_t g_lcd_last_color;

static uint8_t  s_clip_active = 0u;
static uint8_t  s_saved_color = 0u;
static uint32_t s_clip_release_ms = 0u;

void USBAudio_ClipWarn_Service(void)
{
    /* usb_audio_display.c 안에 선언된 전역 seg30 값 활용 */
    extern volatile uint8_t g_usb_vu_usbL_30;
    extern volatile uint8_t g_usb_vu_usbR_30;

    uint8_t trig = (g_usb_vu_usbL_30 >= 30u) || (g_usb_vu_usbR_30 >= 30u);
    uint32_t now = HAL_GetTick();

    if (trig) {
        s_clip_release_ms = now + 120u;   /* 120ms 홀드 */
        if (!s_clip_active){
            s_saved_color = g_lcd_last_color;
            LCDColorSet(5u);              /* 5 = RED (프로젝트 정의) */
            s_clip_active = 1u;
        }
    } else {
        if (s_clip_active && (int32_t)(s_clip_release_ms - now) <= 0){
            /* 홀드 타임 지났으면 복귀 */
            LCDColorSet(s_saved_color);
            s_clip_active = 0u;
        }
    }
}



