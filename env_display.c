/* ===== env_display.c ===== */
#include "env_display.h"
#include "dht.h"
#include "LCDUI.h"       /* ★ 전역 부분-갱신 유틸만 사용 */
#include <stdio.h>
#include <string.h>

/* ── 하드웨어 매핑(원 코드 동일) ───────────────────────── */
#define DHT_GPIO_Port   GPIOE
#define DHT_Pin         GPIO_PIN_14
extern TIM_HandleTypeDef htim7;   /* DHT us 타이밍용 타이머 */

/* lcd_ui용 커스텀 문자(°) 슬롯 */
#define DEG_SLOT        3u
extern const uint8_t degree[8];

/* 퍼블릭 전역 */
volatile uint8_t DHT22_FAIL   = 1;
volatile uint8_t TempUnitF    = 0;
uint16_t Temperature          = 0;   /* 0.1°C */
uint16_t Humidity             = 0;   /* 0..99 */
volatile uint8_t g_dht_req    = 0;

/* (옵션) 보정 파라미터 */
__attribute__((weak)) volatile float Cal_TempC_Offset = 0.0f;
__attribute__((weak)) volatile float Cal_TempC_Scale  = 1.0f;
__attribute__((weak)) volatile float Cal_RH_Offset    = 0.0f;
__attribute__((weak)) volatile float Cal_RH_Scale     = 1.0f;

/* 로컬 상태 */
static DHT_t g_dht;

/* 유틸 */
static inline uint16_t clamp_u16_int(int v, int lo, int hi) {
    if (v < lo) return (uint16_t)lo;
    if (v > hi) return (uint16_t)hi;
    return (uint16_t)v;
}
static inline uint16_t to_tenth_deg(float tC) {
    if (tC < 0)    tC = 0;
    if (tC > 99.9f)tC = 99.9f;
    return (uint16_t)(tC * 10.0f + 0.5f);
}
static inline uint16_t to_rh_uint(float rh) {
    if (rh < 0)     rh = 0;
    if (rh > 99.0f) rh = 99.0f;
    return (uint16_t)(rh + 0.5f);
}

static int DHT22_ReadBlocking(float *outTempC, float *outRH) {
    float t=0.0f, h=0.0f;
    if (!DHT_readData(&g_dht, &t, &h)) return -1;
    if (outTempC) *outTempC = t;
    if (outRH)    *outRH    = h;
    return 0;
}

void EnvDisplay_Init(void)
{
    /* DHT 드라이버 초기화 (APB1 84MHz ⇒ 84) */
    DHT_init(&g_dht, DHT_Type_DHT22, &htim7, 84, DHT_GPIO_Port, DHT_Pin);

    /* ° 커스텀 등록 (lcd_ui 섀도/invalid와 연동) */
    LCDUI_DefineCustom(DEG_SLOT, degree);

    /* 부팅 직후엔 실패 상태로 시작(표시는 Service(row,col) 호출 시점에 수행) */
    DHT22_FAIL = 1;
}

void EnvDisplay_OnExti(uint16_t pin)
{
    if (pin == DHT_Pin) {
        DHT_pinChangeCallBack(&g_dht);
    }
}

void EnvDisplay_2sTicker(void)
{
    static uint16_t s_ms = 0;
    if (++s_ms >= 2000u) { s_ms = 0; g_dht_req = 1; }
}

/* 포맷: [T10][T1][.][T0.1][°][ ][H10][H1][%]  (총 9칸) */
void EnvDisplay_Service(uint8_t row, uint8_t col)
{
    /* 1) 필요시 측정 수행(원 로직: 2초마다 요청) */
    if (g_dht_req) {
        g_dht_req = 0;

        #ifndef DHT_READ_RETRIES
        #define DHT_READ_RETRIES          8
        #endif
        #ifndef DHT_READ_RETRY_DELAY_MS
        #define DHT_READ_RETRY_DELAY_MS   10u
        #endif
        #ifndef DHT_FAIL_DEBOUNCE_COUNT
        #define DHT_FAIL_DEBOUNCE_COUNT   5
        #endif

        static uint8_t s_fail_consec = 0;

        float tC_raw=0.0f, rh_raw=0.0f; uint8_t ok=0;
        for (int a=0; a<DHT_READ_RETRIES; ++a){
            if (DHT22_ReadBlocking(&tC_raw, &rh_raw) == 0) {
                if (tC_raw>=-40.0f && tC_raw<=80.0f && rh_raw>=0.0f && rh_raw<=100.0f) { ok=1; break; }
            }
            HAL_Delay(DHT_READ_RETRY_DELAY_MS);
        }

        if (ok){
            float tC = tC_raw * Cal_TempC_Scale + Cal_TempC_Offset;
            float rh = rh_raw * Cal_RH_Scale   + Cal_RH_Offset;

            /* 단위 변환(표시만, 문자 C/F는 출력 안 함) */
            if (TempUnitF) { tC = (tC * 9.0f/5.0f) + 32.0f; }

            Temperature = to_tenth_deg(tC); /* 0..999 */
            Humidity    = to_rh_uint(rh);   /* 0..99  */
            DHT22_FAIL  = 0;
            s_fail_consec = 0;
        } else {
            if (++s_fail_consec >= DHT_FAIL_DEBOUNCE_COUNT) {
                DHT22_FAIL = 1;
            }
        }
    }

    /* 2) 렌더(호출될 때만 그린다) — LCDUI 부분 갱신만 사용 */
    uint8_t c = col;

    if (DHT22_FAIL) {
        /* "--.-° --%" */
        LCDUI_WriteCharAt(row, c++, '-');    /* T10 */
        LCDUI_WriteCharAt(row, c++, '-');    /* T1  */
        LCDUI_WriteCharAt(row, c++, '.');
        LCDUI_WriteCharAt(row, c++, '-');    /* T0.1 */
        LCDUI_WriteCustomAt(row, c++, DEG_SLOT);
        LCDUI_WriteCharAt(row, c++, ' ');
        LCDUI_WriteCharAt(row, c++, '-');    /* H10 */
        LCDUI_WriteCharAt(row, c++, '-');    /* H1  */
        LCDUI_WriteCharAt(row, c++, '%');
        return;
    }

    /* 성공 표시: 리딩 제로는 스페이스로 */
    /* Temperature (0.1 단위) → 정수/소수 분리 */
    uint16_t t01 = clamp_u16_int(Temperature, 0, 999);
    uint8_t  t_int  = (uint8_t)(t01 / 10);   /* 0..99 */
    uint8_t  t_fr1  = (uint8_t)(t01 % 10);

    char t10c = (t_int >= 10) ? ('0' + (t_int / 10)) : ' ';  /* 리딩 제로 → 스페이스 */
    char t1c  = ('0' + (t_int % 10));
    char tfc  = ('0' + t_fr1);

    /* Humidity (0..99) */
    uint16_t rh = clamp_u16_int(Humidity, 0, 99);
    char h10c = (rh >= 10) ? ('0' + (rh / 10)) : ' ';
    char h1c  = ('0' + (rh % 10));

    /* [T10][T1][.][T0.1][°][ ][H10][H1][%] */
    LCDUI_WriteCharAt (row, c++, t10c);
    LCDUI_WriteCharAt (row, c++, t1c);
    LCDUI_WriteCharAt (row, c++, '.');
    LCDUI_WriteCharAt (row, c++, tfc);
    LCDUI_WriteCustomAt(row, c++, DEG_SLOT);
    LCDUI_WriteCharAt (row, c++, ' ');
    LCDUI_WriteCharAt (row, c++, h10c);
    LCDUI_WriteCharAt (row, c++, h1c);
    LCDUI_WriteCharAt (row, c++, '%');
}
