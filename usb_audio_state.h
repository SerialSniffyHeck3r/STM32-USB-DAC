// usb_audio_state.h
#ifndef USB_AUDIO_STATE_H
#define USB_AUDIO_STATE_H

#include "usbd_def.h"
#include <stdint.h>

typedef struct
{
    // 호스트가 보고 있는 현재 샘플레이트(Hz)
    volatile uint32_t sample_rate;

    volatile uint8_t  sample_bits;     /* ★ 추가: 16 or 24 */

    // 호스트가 디바이스에 보낸 "하드웨어 볼륨" 값 (0..100, UAC1 스타일)
    volatile uint8_t  host_volume;    // 아직은 그대로 저장만
    volatile uint8_t  host_mute;      // 0 또는 1

    // 현재 오디오 스트리밍 인터페이스/Alt Setting
    volatile uint8_t  cur_interface;     // wIndex (인터페이스 번호)
    volatile uint8_t  cur_alt_setting;   // wValue (Alt)

    // 마지막으로 들어온 Setup 패킷 헤더를 그대로 저장 (디버그/로깅용)
    volatile uint8_t  last_bmRequestType;
    volatile uint8_t  last_bRequest;
    volatile uint16_t last_wValue;
    volatile uint16_t last_wIndex;
    volatile uint16_t last_wLength;
} usb_audio_state_t;

// 전역 상태 인스턴스
extern usb_audio_state_t g_usb_audio_state;

void USB_AudioState_Init(void);

// 콜백용 함수들 (여러 군데서 호출)
void USB_AudioState_OnInit(uint32_t sample_rate, uint32_t volume);
void USB_AudioState_OnVolume(uint8_t vol);
void USB_AudioState_OnMute(uint8_t mute);
void USB_AudioState_OnInterface(uint8_t iface, uint8_t alt);
void USB_AudioState_OnSetup(const USBD_SetupReqTypedef *req);
void USB_AudioState_OnSampleRate(uint32_t sr); // 나중에 샘플레이트 컨트롤 구현 시 사용
void USB_AudioState_OnFormat(uint32_t sample_rate, uint8_t bits);


#endif // USB_AUDIO_STATE_H
