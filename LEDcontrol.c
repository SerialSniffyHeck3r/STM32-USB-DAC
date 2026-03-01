/* ============================================================================
 * LEDcontrol.c — Legacy-style (LEDModeSet), numeric dispatcher, STM32F407
 *  - Patterns defined first
 *  - Dispatcher switch over numeric mode
 *  - Phase-continuous breathing envelope (half-cos ramps)
 *  - Exponential smoothing to avoid “sudden pop”
 * ========================================================================== */
#include "LEDcontrol.h"
#include <math.h>
#include <string.h>


#if defined(__GNUC__)
  #define CCMRAM  __attribute__((section(".ccmram"))) __attribute__((aligned(4)))
#else
  #define CCMRAM
#endif

/* ---- External TIM handles (PWM outputs) ---- */
extern TIM_HandleTypeDef htim3;   // CH3, CH4
extern TIM_HandleTypeDef htim5;   // CH1, CH3, CH4
extern TIM_HandleTypeDef htim9;   // CH1, CH2
extern TIM_HandleTypeDef htim12;  // CH1, CH2

/* ---- Hardware mapping: 9 LED outputs ---- */
enum { NLED = 9 };
typedef struct { TIM_HandleTypeDef* h; uint32_t ch; } LedOut;
static LedOut s_led[NLED] = {
  { &htim3,  TIM_CHANNEL_3 }, // 0
  { &htim3,  TIM_CHANNEL_4 }, // 1
  { &htim5,  TIM_CHANNEL_1 }, // 2
  { &htim5,  TIM_CHANNEL_3 }, // 3
  { &htim5,  TIM_CHANNEL_4 }, // 4 (center)
  { &htim9,  TIM_CHANNEL_1 }, // 5
  { &htim9,  TIM_CHANNEL_2 }, // 6
  { &htim12, TIM_CHANNEL_1 }, // 7
  { &htim12, TIM_CHANNEL_2 }, // 8
};





/* ---- PWM base (CubeMX와 일치해야 함) ---- */
#define PWM_ARR   41999u
#define PWM_MAX   PWM_ARR






/* ---- State ---- */
static volatile uint8_t  s_mode   = LED_MODE_OFF; /* ← LEDModeSet()로 변경, 유지 */
static uint32_t s_breath_t_ms     = 0;            /* phase time(ms), 1kHz 누산 */

static uint32_t s_in_ms   = 4100;  /* 들숨 */
static uint32_t s_hold_ms =  700;  /* 유지 */
static uint32_t s_out_ms  = 3800;  /* 날숨 */
static uint32_t s_rest_ms = 2000;  /* 휴식 */

static uint8_t  s_level   = 2;     /* 전역 밝기 단계: 0/1/2 */
CCMRAM static uint16_t s_gamma[4096 + 1]; /* 감마 LUT (선형→PWM) */
static const uint16_t s_level_pm[3] = { 300, 600, 1000 }; /* permille */

static uint16_t s_ccr_cur[NLED];   /* 스무딩된 현재 CCR */
static uint8_t  s_smooth_shift = 4;/* 지수 평활 강도: 0=off, 4=1/16 (기본) */







/* ---- Utils ---- */
static inline void pwm_start(TIM_HandleTypeDef* h, uint32_t ch){
  __HAL_TIM_SET_COMPARE(h, ch, 0);
  HAL_TIM_PWM_Start(h, ch);
}
static inline void led_apply(uint8_t idx, uint16_t ccr){
  if (idx >= NLED) return;
  if (ccr > PWM_MAX) ccr = PWM_MAX;
  __HAL_TIM_SET_COMPARE(s_led[idx].h, s_led[idx].ch, ccr);
}

/* Gamma LUT (γ=2.2) */
static void BuildGammaLUT(void){
  for (uint32_t i=0; i<=4096; ++i){
    float x = (float)i / 4096.0f;
    float y = powf(x, 2.2f);
    uint32_t v = (uint32_t)lroundf(y * (float)PWM_MAX);
    if (v > PWM_MAX) v = PWM_MAX;
    s_gamma[i] = (uint16_t)v;
  }
}



/* === 파일 상단 static들 아래 어딘가에 추가 ============================ */
static volatile uint16_t s_ext_lin[9];
static volatile uint8_t  s_ext_valid = 0;


/* --- 패턴 하나 더: External Linear(0..4096 그대로 복사) --- */
static void P_ExternalLinear(uint16_t out_lin[9]){
  if (!s_ext_valid){
    for (int i=0;i<9;i++) out_lin[i] = 0;
    return;
  }
  for (int i=0;i<9;i++){
    uint16_t v = s_ext_lin[i];
    if (v > 4096u) v = 4096u;
    out_lin[i] = v;
  }
}

/* --- 디스패처 스위치에 케이스 추가 ------------------------- */
static void LED_Dispatch(uint8_t mode, uint16_t out_lin[9]){
  switch (mode){
    case LED_MODE_BREATH_CENTER:  P_BreathCenter(out_lin); break;
    case LED_MODE_SPECTRUM9:      P_ExternalLinear(out_lin); break;  /* ★ 추가 */
    case LED_MODE_OFF:
    default:                      P_Off(out_lin);          break;
  }
}

void LED_SetExternalLinear(const uint16_t lin9[9]){
  if (!lin9){ s_ext_valid = 0; return; }
  for (int i=0;i<9;i++){
    uint16_t v = lin9[i];
    if (v > 4096u) v = 4096u;
    s_ext_lin[i] = v;
  }
  s_ext_valid = 1;
}





/* =========================================================================
 * Patterns (0..4096 linear)
 * ========================================================================= */

/* OFF */
void P_Off(uint16_t out_lin[9]){
  for (int i=0;i<NLED;i++) out_lin[i] = 0;
}

/* Breath (center emphasized, phase-continuous) */
void P_BreathCenter(uint16_t out_lin[9]){
  uint32_t Tin = s_in_ms, Th = s_hold_ms, Tout = s_out_ms, Tr = s_rest_ms;
  uint32_t Ttot = Tin + Th + Tout + Tr; if (!Ttot) Ttot = 1;
  uint32_t t = s_breath_t_ms % Ttot;

  /* envelope a(t): half-cos ramps (C1-like, pop 억제) */
  float a;
  if (t < Tin){
    float x = (float)t / (float)Tin;                 /* 0→1 */
    a = 0.5f - 0.5f * cosf((float)M_PI * x);         /* fade-in */
  } else if (t < Tin + Th){
    a = 1.0f;                                        /* hold */
  } else if (t < Tin + Th + Tout){
    float x = (float)(t - (Tin + Th)) / (float)Tout; /* 0→1 */
    a = 0.5f + 0.5f * cosf((float)M_PI * x);         /* fade-out */
  } else {
    a = 0.0f;                                        /* rest */
  }

  /* spatial roll-off (center=4), raised-cos window */
  const int   center = 4;
  const float W_min = 0.7f, W_max = 3.0f;
  float W = W_min + (W_max - W_min) * a;

  for (int k=0;k<NLED;k++){
    float d = fabsf((float)k - (float)center);
    float x = d / W; if (x > 1.f) x = 1.f;
    float spat = 0.5f * (1.f + cosf((float)M_PI * x)); /* 1..0.. */
    float lin = a * spat;                               /* 0..1 */
    uint32_t v = (uint32_t)lroundf(lin * 4096.f);
    if (v > 4096u) v = 4096u;
    out_lin[k] = (uint16_t)v;
  }
}


/* =========================================================================
 * Render @1kHz: pattern → gamma → level → smoothing → CCR apply
 * ========================================================================= */
static void LED_Render_1kHz(void){
  /* 1) pattern */
  uint16_t lin[9]; LED_Dispatch(s_mode, lin);

  /* 2) gamma + global level */
  uint16_t level = (s_level > 2) ? 2 : s_level;
  uint32_t pm = s_level_pm[level];

  uint16_t tgt[NLED];
  for (int i=0;i<NLED;i++){
    uint32_t p = s_gamma[lin[i]];
    uint32_t scaled = (p * pm + 500u) / 1000u;
    if (scaled > PWM_MAX) scaled = PWM_MAX;
    tgt[i] = (uint16_t)scaled;
  }

  /* 3) smoothing (to avoid sudden “pop”) */
  if (s_smooth_shift == 0){
    for (int i=0;i<NLED;i++){ s_ccr_cur[i] = tgt[i]; led_apply((uint8_t)i, s_ccr_cur[i]); }
  } else {
    uint8_t sh = s_smooth_shift; /* e.g., 4 → 1/16 */
    for (int i=0;i<NLED;i++){
      int32_t diff = (int32_t)tgt[i] - (int32_t)s_ccr_cur[i];
      /* 부호 유지 쉬프트(ARM/GCC에서 산술 시프트) */
      s_ccr_cur[i] = (uint16_t)((int32_t)s_ccr_cur[i] + (diff >> sh));
      led_apply((uint8_t)i, s_ccr_cur[i]);
    }
  }
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void LED_Init(void)
{
  BuildGammaLUT();

  /* PWM start all channels */
  for (int i=0;i<NLED;i++){
    pwm_start(s_led[i].h, s_led[i].ch);
    s_ccr_cur[i] = 0;
  }

  /* defaults */
  s_mode = LED_MODE_OFF;  /* 예전처럼 모드 셋 전엔 OFF */
  s_level = 2;
  s_breath_t_ms = 0;
  s_smooth_shift = 4;     /* 1/16 smoothing (탁 밝아짐 완화) */
}

void LEDModeSet(uint8_t mode){
  s_mode = mode;          /* 숫자 그대로 저장 (디스패처에서 처리) */
}

void LED_SetGlobalLevel(uint8_t level_0_2){
  s_level = (level_0_2 > 2) ? 2 : level_0_2;
}

void LED_SetBreathTotalPeriod(uint32_t ms){
  if (ms < 1000u) ms = 1000u;
  /* 비율: 4.1 : 0.7 : 3.8 : 2.0 */
  float sum = 4.1f + 0.7f + 3.8f + 2.0f;
  s_in_ms   = (uint32_t)lroundf(ms * (4.1f/sum));
  s_hold_ms = (uint32_t)lroundf(ms * (0.7f/sum));
  s_out_ms  = (uint32_t)lroundf(ms * (3.8f/sum));
  s_rest_ms = (uint32_t)lroundf(ms * (2.0f/sum));
}

void LED_SetSmoothStrength(uint8_t pow2_shift){
  /* 0=off, 1=1/2, 2=1/4, 3=1/8, 4=1/16 ... */
  s_smooth_shift = (pow2_shift > 6) ? 6 : pow2_shift;
}

void LED_Tick_1kHz(void)
{
  s_breath_t_ms += 1u;  /* phase accumulate (continuous) */
  LED_Render_1kHz();
}
