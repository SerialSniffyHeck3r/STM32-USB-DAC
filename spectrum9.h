#ifndef SPECTRUM9_H
#define SPECTRUM9_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SPEC_SRC_USB=0, SPEC_SRC_I2S2=1, SPEC_SRC_MIXER=2 } Spectrum9Source;

/* 48 kHz에서만 동작 */
void Spectrum9_Init(uint32_t fs_hz);
void Spectrum9_SetSampleRate(uint32_t fs_hz);
void Spectrum9_SetActiveSource(Spectrum9Source src);

/* ISR(usbd_audio_if.c)에서 부르는 기존 이름을 그대로 유지 */
void Spectrum9_PushInt16Stereo_USBDecim8(const int16_t *lr, uint32_t frames);

/* 1ms 주기에서 호출 (필수) */
void Spectrum9_Task_1ms(void);

/* 선택: 메인루프 훅 */
void Spectrum9_ProcessMain(void);

#ifdef __cplusplus
}
#endif
#endif
