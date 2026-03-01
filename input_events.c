/* ===== input_events.c (drop-in) ===== */
#include "input_events.h"
#include <string.h>








/* (선택) 디버그 틱 카운터: TIM6 1ms ISR에서 증가시키고 싶으면 외부 정의 */
extern volatile uint32_t IE_DebugTick;

/* === 공개 전역 === */
volatile ButtonEvent IE_ButtonEvent[3] = {
  BUTTON_EVENT_NONE, BUTTON_EVENT_NONE, BUTTON_EVENT_NONE
};
volatile RotaryEvent IE_RotAEvent = ROTARY_EVENT_NONE;
volatile RotaryEvent IE_RotBEvent = ROTARY_EVENT_NONE;
volatile uint8_t     IE_PowerState = 0;
volatile uint8_t     IE_PowerEdge  = 0;

/* === 내부 상태 === */
static uint8_t  s_btn_hw[3];       /* 1=눌림(액티브-로우) 생 레벨 */
static uint8_t  s_btn_stable[3];   /* 디바운스된 레벨 */
static uint16_t s_btn_cnt[3];      /* 동일 레벨 지속 ms 카운터 */
static uint32_t s_btn_down_tick[3];
static uint8_t  s_btn_long_sent[3];

static uint8_t  s_pwr_hw = 0;      /* 1=ON */
static uint8_t  s_pwr_stable = 0;
static uint16_t s_pwr_cnt = 0;

/* === 파라미터 === */
#ifndef IE_ROT_STEP
#define IE_ROT_STEP 4   /* 엔코더 한 detent당 펄스 수에 맞춰 조정 */
#endif

/* === 헬퍼: 핀 읽기 === */
static inline uint8_t read_btn_i(int i){
  switch(i){
    case 0: return (HAL_GPIO_ReadPin(IE_BTN0_PORT, IE_BTN0_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
    case 1: return (HAL_GPIO_ReadPin(IE_BTN1_PORT, IE_BTN1_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
    case 2: return (HAL_GPIO_ReadPin(IE_BTN2_PORT, IE_BTN2_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
    default: return 0u;
  }
}
static inline uint8_t read_power_raw(void){
  /* active-high */
  return (HAL_GPIO_ReadPin(IE_PWR_PORT, IE_PWR_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

/* === 초기화 === */
void IE_Init(void){
  memset((void*)IE_ButtonEvent, 0, sizeof(IE_ButtonEvent));
  IE_RotAEvent = IE_RotBEvent = ROTARY_EVENT_NONE;
  IE_PowerEdge = 0;

  for (int i=0;i<3;i++){
    s_btn_hw[i] = s_btn_stable[i] = read_btn_i(i);
    s_btn_cnt[i] = 0;
    s_btn_down_tick[i] = 0;
    s_btn_long_sent[i] = 0;
  }
  s_pwr_hw = s_pwr_stable = read_power_raw();
  s_pwr_cnt = 0;
  IE_PowerState = s_pwr_stable;
}

/* === 1ms 틱: 버튼/파워 디바운스 & 이벤트 생성 === */
void IE_Tick_1ms(void){
  uint32_t now = HAL_GetTick();

  /* 버튼 3개 */
  for (int i=0;i<3;i++){
    uint8_t lv = read_btn_i(i); /* 1=눌림 */
    if (lv == s_btn_hw[i]){
      if (s_btn_cnt[i] < IE_BTN_DEBOUNCE_MS) s_btn_cnt[i]++;
    } else {
      s_btn_hw[i] = lv;
      s_btn_cnt[i] = 0;
    }

    if (s_btn_cnt[i] == IE_BTN_DEBOUNCE_MS){
      if (s_btn_stable[i] != s_btn_hw[i]){
        s_btn_stable[i] = s_btn_hw[i];
        if (s_btn_stable[i]){ /* 눌림 시작 */
          s_btn_down_tick[i] = now;
          s_btn_long_sent[i] = 0;
        } else { /* 놓임 → 숏 */
          uint32_t dt = now - s_btn_down_tick[i];
          if (!s_btn_long_sent[i] && dt < IE_BTN_LONG_MS){
            IE_ButtonEvent[i] = BUTTON_EVENT_SHORT_PRESS;
          }
        }
      }
    }
    /* 눌림 유지 → 롱 */
    if (s_btn_stable[i] && !s_btn_long_sent[i]){
      if ((now - s_btn_down_tick[i]) >= IE_BTN_LONG_MS){
        IE_ButtonEvent[i] = BUTTON_EVENT_LONG_PRESS;
        s_btn_long_sent[i] = 1;
      }
    }
  }

  /* 파워 슬라이드 스위치 */
  uint8_t pw = read_power_raw(); /* 1=ON */
  if (pw == s_pwr_hw){
    if (s_pwr_cnt < IE_PWR_DEBOUNCE_MS) s_pwr_cnt++;
  } else {
    s_pwr_hw = pw;
    s_pwr_cnt = 0;
  }
  if (s_pwr_cnt == IE_PWR_DEBOUNCE_MS){
    if (s_pwr_stable != s_pwr_hw){
      s_pwr_stable = s_pwr_hw;
      IE_PowerState = s_pwr_stable;
      IE_PowerEdge  = 1; /* 상태 변화 에지 알림 */
    }
  }

  if (&IE_DebugTick) { IE_DebugTick++; } /* 선택: 디버그용 */
}

/* === 로터리 폴링(타이머 카운터 차분) === */
static inline void poll_rotary_core(TIM_HandleTypeDef *htim, int16_t *prev_cnt, volatile RotaryEvent *ev_out){
  int16_t now  = (int16_t)__HAL_TIM_GET_COUNTER(htim);
  int16_t diff = (int16_t)(now - *prev_cnt);

  if (diff >= IE_ROT_STEP){
    *ev_out = ROTARY_EVENT_CCW;   /* 방향이 반대로 느껴지면 두 분기만 바꿔주면 됨 */
    *prev_cnt = now;
  } else if (diff <= -IE_ROT_STEP){
    *ev_out = ROTARY_EVENT_CW;
    *prev_cnt = now;
  } else {
    *ev_out = ROTARY_EVENT_NONE;
  }
}

void IE_Poll_RotaryA(TIM_HandleTypeDef *htimA, int16_t *prev_cntA){ poll_rotary_core(htimA, prev_cntA, &IE_RotAEvent); }
void IE_Poll_RotaryB(TIM_HandleTypeDef *htimB, int16_t *prev_cntB){ poll_rotary_core(htimB, prev_cntB, &IE_RotBEvent); }

/* === EXTI 헬퍼(선택): 깨어나기/디버깅 용도. 반드시 필요하진 않음. === */
void IE_OnGPIOEdge(uint16_t GPIO_Pin){
  (void)GPIO_Pin;
}
