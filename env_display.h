/* ===== env_display.h — DHT22 header display (2s, call-on-demand) ===== */
#ifndef ENV_DISPLAY_H
#define ENV_DISPLAY_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 퍼블릭 전역(원 코드 호환) */
extern volatile uint8_t DHT22_FAIL;   /* 1=최근 측정 실패 */
extern volatile uint8_t TempUnitF;    /* 0=C, 1=F (문자 C/F는 출력 안 함) */
extern uint16_t Temperature;          /* 0.1°C 단위 (0..999 ⇒ 0.0..99.9) */
extern uint16_t Humidity;             /* 0..99 % */
extern volatile uint8_t g_dht_req;    /* 2초 타이머 틱에서 set */

/* 초기화/연동 */
void EnvDisplay_Init(void);                 /* DHT 초기화 + ° 등록 (CGRAM) */
void EnvDisplay_OnExti(uint16_t pin);       /* HAL_GPIO_EXTI_Callback()에서 포워드 */
void EnvDisplay_2sTicker(void);             /* TIM6 1kHz ISR에서 호출 */

/* 화면 표시는 “호출될 때만” 수행:
 * row/col = 숫자 시작 위치(1-based). 포맷은 정확히:
 * [T10][T1][.][T0.1][°][ ][H10][H1][%]
 * - 리딩 제로는 스페이스(' ')로 대체
 * - 실패시: "--.-° --%"
 * - LCDUI(섀도 버퍼)만 사용, 부분 갱신
 */
void EnvDisplay_Service(uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif
#endif /* ENV_DISPLAY_H */
