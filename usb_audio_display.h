// usb_audio_display.h
#ifndef USB_AUDIO_DISPLAY_H
#define USB_AUDIO_DISPLAY_H

#include <stdint.h>

/**
 * @brief USB 오디오 샘플레이트를 "--.-k" 형식으로 출력
 *
 * @param row LCD 행 (1 또는 2, 1-based)
 * @param col 시작 열 (1..16, 1-based)
 *
 * 출력 포맷(5글자 고정):
 *   - 44.1k
 *   - 48.0k
 *   - 96.0k
 *   - --.-k (유효한 샘플레이트 없음/인식 불가)
 */
void USBAudio_DisplaySampleRate(uint8_t row, uint8_t col);

/**
 * @brief 호스트 볼륨을 "---%" / "---%" / "MUTE" 형식으로 출력
 *
 * @param row LCD 행 (1 또는 2, 1-based)
 * @param col 시작 열 (1..16, 1-based)
 *
 * 출력 포맷(4글자 고정):
 *   - "  0%" ~ "100%" : 정상 값
 *   - "---%"          : 부적합한 값 (예: 100 초과 등)
 *   - "MUTE"          : 뮤트 상태(host_mute != 0)
 *
 * 0~9   → "  X%"
 * 10~99 → " XX%"
 * 100   → "100%"
 * 리딩 제로 대신 공백 사용.
 */
void USBAudio_DisplayVolumePct(uint8_t row, uint8_t col);

/* 이미 있던 샘플레이트/볼륨 표시 함수 선언은 그대로 두고, 그 아래에 추가 */

/* --- VU: 외부에서 채워 넣을 0..30 세그먼트 값들 --- */
/* USB Audio 입력(L/R) */
extern volatile uint8_t g_usb_vu_usbL_30;
extern volatile uint8_t g_usb_vu_usbR_30;

/* I2S2 출력(L/R) */
extern volatile uint8_t g_usb_vu_i2s2L_30;
extern volatile uint8_t g_usb_vu_i2s2R_30;

/* 믹서 모드용 두 채널(원하는 의미로 써도 됨: 예) PC / I2S2) */
extern volatile uint8_t g_usb_vu_mixA_30;
extern volatile uint8_t g_usb_vu_mixB_30;

/* --- Mini VU (8칸 + 8칸) --- */
void USBAudio_DisplayMiniVU_USB_LR_Row1(void);   /* 1행: USB L/R */
void USBAudio_DisplayMiniVU_I2S2_LR_Row2(void);  /* 2행: I2S2 L/R */

/* --- Full-screen VU (L=1행, R=2행) --- */
void USBAudio_DisplayFullScreenVU_USB(void);
void USBAudio_DisplayFullScreenVU_I2S2(void);

/* --- Mixer 모드 VU: "[FF/HF] [FF/HF]" 포맷 --- */
/* row, col: '[' 가 찍힐 시작 위치 */
void USBAudio_DisplayMixerVU(uint8_t row, uint8_t col);


#endif

