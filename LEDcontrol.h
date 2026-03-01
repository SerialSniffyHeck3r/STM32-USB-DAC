#ifndef LEDCONTROL_H_
#define LEDCONTROL_H_

#include "main.h"
#include <stdint.h>

/* 모드 ID */
enum {
    LED_MODE_OFF = 0,
    LED_MODE_BREATH_CENTER = 1,
    LED_MODE_SPECTRUM9 = 10  /* ★ 신규: 외부 9채널 선형값(0..4096)을 직접 출력 */
};

/* 초기화/주기 호출 */
void LED_Init(void);
void LED_Tick_1kHz(void);

/* 공용 제어 */
void LEDModeSet(uint8_t mode);
void LED_SetGlobalLevel(uint8_t level_0_2);     /* 0/1/2 → 퍼밀 300/600/1000 */
void LED_SetBreathTotalPeriod(uint32_t ms);
void LED_SetSmoothStrength(uint8_t pow2_shift); /* 0=즉시, 4=1/16 등 */

/* ★ 스펙트럼 외부 입력 (선형 0..4096, 감마는 내부에서 진행) */
void LED_SetExternalLinear(const uint16_t lin9[9]);

#endif /* LEDCONTROL_H_ */
