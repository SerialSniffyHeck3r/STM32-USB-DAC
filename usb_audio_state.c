// usb_audio_state.c
#include "usb_audio_state.h"
#include <string.h>

usb_audio_state_t g_usb_audio_state;

void USB_AudioState_Init(void)
{
    // 전부 0으로 초기화
    memset((void *)&g_usb_audio_state, 0, sizeof(g_usb_audio_state));

    // 기본 샘플레이트를 48k로 가정 (필요하면 바꿔도 됨)
    g_usb_audio_state.sample_rate = 48000;
    g_usb_audio_state.sample_bits = 24;   /* ★ 기본 16 -> 호스트가 바꾸면 OnFormat으로 갱신 */
}

void USB_AudioState_OnFormat(uint32_t sample_rate, uint8_t bits)
{
    g_usb_audio_state.sample_rate = sample_rate;
    g_usb_audio_state.sample_bits = bits;
}

void USB_AudioState_OnInit(uint32_t sample_rate, uint32_t volume)
{
    g_usb_audio_state.sample_rate = sample_rate;
    g_usb_audio_state.host_volume = (uint8_t)volume;
}

void USB_AudioState_OnVolume(uint8_t vol)
{
    g_usb_audio_state.host_volume = vol;
}

void USB_AudioState_OnMute(uint8_t mute)
{
    g_usb_audio_state.host_mute = (mute ? 1u : 0u);
}

void USB_AudioState_OnInterface(uint8_t iface, uint8_t alt)
{
    g_usb_audio_state.cur_interface   = iface;
    g_usb_audio_state.cur_alt_setting = alt;
}

void USB_AudioState_OnSetup(const USBD_SetupReqTypedef *req)
{
    g_usb_audio_state.last_bmRequestType = req->bmRequest;
    g_usb_audio_state.last_bRequest      = req->bRequest;
    g_usb_audio_state.last_wValue        = req->wValue;
    g_usb_audio_state.last_wIndex        = req->wIndex;
    g_usb_audio_state.last_wLength       = req->wLength;

    USBAudio_RequestToast_Start(req->bmRequest, req->bRequest);
}

void USB_AudioState_OnSampleRate(uint32_t sr)
{
    g_usb_audio_state.sample_rate = sr;
}


uint8_t USB_AudioState_IsHostMuted(void) {
    return g_usb_audio_state.host_mute ? 1u : 0u;
}

