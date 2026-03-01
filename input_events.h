/* ===== input_events.h (drop-in) ===== */
#ifndef INPUT_EVENTS_H
#define INPUT_EVENTS_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === 핀맵: 현재 보드 하드웨어와 1:1 매칭 ===
 *  - 버튼 3개: PE2 / PE3 / PE4  (액티브-로우, EXTI 상승/하강, 풀업)
 *  - 파워 스위치: PB5 (액티브-하이, EXTI 상승/하강, 풀업)
 */
#define IE_BTN0_PORT   GPIOE
#define IE_BTN0_PIN    GPIO_PIN_2   /* active-low */
#define IE_BTN1_PORT   GPIOE
#define IE_BTN1_PIN    GPIO_PIN_3   /* active-low */
#define IE_BTN2_PORT   GPIOE
#define IE_BTN2_PIN    GPIO_PIN_4   /* active-low */
#define IE_PWR_PORT    GPIOB
#define IE_PWR_PIN     GPIO_PIN_5   /* slide, active-high */

/* === 타이밍 파라미터 === */
#define IE_BTN_DEBOUNCE_MS  10u     /* 버튼 디바운스 */
#define IE_BTN_LONG_MS      700u    /* 롱프레스 임계 */
#define IE_PWR_DEBOUNCE_MS  30u     /* 전원 스위치 디바운스 */

/* === 이벤트 타입 === */
typedef enum {
  BUTTON_EVENT_NONE = 0,
  BUTTON_EVENT_SHORT_PRESS,
  BUTTON_EVENT_LONG_PRESS
} ButtonEvent;

typedef enum {
  ROTARY_EVENT_NONE = 0,
  ROTARY_EVENT_CW,
  ROTARY_EVENT_CCW
} RotaryEvent;

/* === 공개 전역 === */
extern volatile ButtonEvent IE_ButtonEvent[3];
extern volatile RotaryEvent IE_RotAEvent;
extern volatile RotaryEvent IE_RotBEvent;
extern volatile uint8_t     IE_PowerState; /* 1=ON */
extern volatile uint8_t     IE_PowerEdge;  /* 상태 변화 에지 1틱 힌트 */

/* === API === */
void IE_Init(void);
void IE_Tick_1ms(void);
void IE_Poll_RotaryA(TIM_HandleTypeDef *htimA, int16_t *prev_cntA);  /* TIM4 */
void IE_Poll_RotaryB(TIM_HandleTypeDef *htimB, int16_t *prev_cntB);  /* TIM1 */
void IE_OnGPIOEdge(uint16_t GPIO_Pin); /* 선택: EXTI 콜백에서 깨우기 용도 */

#ifdef __cplusplus
}
#endif
#endif /* INPUT_EVENTS_H */
