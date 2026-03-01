/*
 * customchar.c
 *
 *  Created on: Nov 29, 2025
 *      Author: cutekitty
 */

#include "main.h"


 const uint8_t heartChar[8] = {
    0b00000,
    0b01010,
    0b11111,
    0b11111,
    0b11111,
    0b01110,
    0b00100,
    0b00000
};


 const uint8_t MelodyIcon[8] = {
	0b00001,
	0b00011,
	0b00101,
	0b01001,
	0b01001,
	0b01011,
	0b11011,
	0b11000
};

 const uint8_t Paused[8] = {
	0b00000,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b00000
};

 const uint8_t Playing[8] = {
	0b00000,
	0b01100,
	0b01110,
	0b01111,
	0b01111,
	0b01110,
	0b01100,
	0b00000
};

 const uint8_t Loopback[8] = {
	0b01000,
	0b11111,
	0b01001,
	0b00001,
	0b10000,
	0b10010,
	0b11111,
	0b00010
};

 const uint8_t TuningFork[8] = {
		  0x0A,
		  0x0A,
		  0x0A,
		  0x0E,
		  0x0E,
		  0x04,
		  0x04,
		  0x04
};



 const uint8_t Timer[8] = {
	0b11111,
	0b11111,
	0b01110,
	0b00100,
	0b00100,
	0b01110,
	0b11111,
	0b11111
};



 const uint8_t MenuFirst[] = {
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00100,
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00000
};

 const uint8_t MenuSecond[] = {
	  0B00000,
	  0B01000,
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00010,
	  0B00000,
	  0B00000
};

 const uint8_t MenuThird[] = {
	  0B00000,
	  0B01000,
	  0B00000,
	  0B00100,
	  0B00000,
	  0B00010,
	  0B00000,
	  0B00000
};

 const uint8_t MenuFourth[] = {
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00000,
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00000
};

 const uint8_t MenuFifth[] = {
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00100,
	  0B00000,
	  0B01010,
	  0B00000,
	  0B00000
};

 const uint8_t MetronomeNow[] = {
	  0B00000,
	  0B00000,
	  0B01110,
	  0B01110,
	  0B01110,
	  0B01110,
	  0B00000,
	  0B00000
};

 const uint8_t MetronomePriv[] = {
	  0B00000,
	  0B00000,
	  0B00000,
	  0B00100,
	  0B00100,
	  0B00000,
	  0B00000,
	  0B00000
};


 const uint8_t SineShape1[] = {
		  0x0E,
		  0x11,
		  0x11,
		  0x11,
		  0x01,
		  0x01,
		  0x01,
		  0x00
};

 const uint8_t SineShape2[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x00,
		  0x02,
		  0x02,
		  0x02,
		  0x1C
};

 const uint8_t SquareShape1[] = {
		  0x1F,
		  0x11,
		  0x11,
		  0x11,
		  0x01,
		  0x01,
		  0x01,
		  0x01
};



 const uint8_t SquareShape2[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x00,
		  0x02,
		  0x02,
		  0x02,
		  0x1E
};


 const uint8_t TriangleShape1[] = {
		  0x00,
		  0x04,
		  0x0A,
		  0x11,
		  0x00,
		  0x00,
		  0x00,
		  0x00
};


 const uint8_t TriangleShape2[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x00,
		  0x11,
		  0x0A,
		  0x04,
		  0x00
};

 const uint8_t PitchShiftCheck[] = {
		  0x0E,
		  0x04,
		  0x00,
		  0x1F,
		  0x1F,
		  0x00,
		  0x04,
		  0x0E
};

 const uint8_t PitchShiftBar[] = {
		  0x00,
		  0x00,
		  0x00,
		  0x1F,
		  0x1F,
		  0x00,
		  0x00,
		  0x00
};

 const uint8_t flaticon[] = {
		  0x10,
		  0x10,
		  0x14,
		  0x1A,
		  0x13,
		  0x14,
		  0x08,
		  0x00
};

 const uint8_t VOLUMEONE[] = {
		0x0,0x0,0x0,0x0,0x0,0x0,0x1f
};


 const uint8_t VOLUMETWO[] = {
		0x0,0x0,0x0,0x0,0x0,0x1f,0x1f
};


 const uint8_t VOLUMETHREE[] = {
		0x0,0x0,0x0,0x0,0x1f,0x1f,0x1f
};


 const uint8_t VOLUMEFOUR[] = {
		0x0,0x0,0x0,0x1f,0x1f,0x1f,0x1f
};


 const uint8_t VOLUMEFIVE[] = {
		0x0,0x0,0x1f,0x1f,0x1f,0x1f,0x1f
};

 const uint8_t VOLUMESIX[] = {
		0x0,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f
};

 const uint8_t VOLUMESEVEN[] = {
		0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f
};

 const uint8_t OneFilledVU[] = {
  0x00,
  0x00,
  0x10,
  0x10,
  0x10,
  0x10,
  0x00,
  0x00
};

 const uint8_t HalfFilledVU[] = {
  0x00,
  0x00,
  0x18,
  0x18,
  0x18,
  0x18,
  0x00,
  0x00
};

 const uint8_t ReverseHalfFilledVU[] = {
		  0x00,
		  0x00,
		  0x03,
		  0x03,
		  0x03,
		  0x03,
		  0x00,
		  0x00
};

 const uint8_t ThreeFilledVU[] = {
		  0x00,
		  0x00,
		  0x1A,
		  0x1A,
		  0x1A,
		  0x1A,
		  0x00,
		  0x00
};

 const uint8_t FullFilledVU[] = {
  0x00,
  0x00,
  0x1B,
  0x1B,
  0x1B,
  0x1B,
  0x00,
  0x00
};






 const uint8_t Rmini[] = {
		  0x00,
		  0x1E,
		  0x12,
		  0x1E,
		  0x18,
		  0x14,
		  0x12,
		  0x00
};

 const uint8_t Lmini[] = {
		  0x00,
		  0x10,
		  0x10,
		  0x10,
		  0x10,
		  0x10,
		  0x1E,
		  0x00
};

const uint8_t degree[] = {
  0x1C,
  0x14,
  0x1C,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00
};

const uint8_t clock_icon[8] = {
    0b00000,
    0b01110,
    0b10101,
    0b10111,
    0b10001,
    0b01110,
    0b00000,
    0b00000
};

const uint8_t USB_icon[8] = {
		  0B00100,
		  0B01110,
		  0B00100,
		  0B00100,
		  0B10100,
		  0B10101,
		  0B01101,
		  0B00110
};

const uint8_t External_In_Icon[8] = {
		  0B00010,
		  0B00010,
		  0B00010,
		  0B00111,
		  0B00111,
		  0B10111,
		  0B10010,
		  0B11110
};

const uint8_t Thermometer[8] = {
		  0x04,
		  0x0A,
		  0x0A,
		  0x0E,
		  0x0E,
		  0x1F,
		  0x1F,
		  0x0E
};





#include "LCDUI.h"   // 파일 상단 쪽에 추가되어 있어야 함
/* VU / Fullscreen VU용 커스텀 문자 제어 */

static uint8_t s_vu_fullscreen_active = 0u;

/* 기본 상태:
 *   slot 0: HalfFilledVU
 *   slot 1: ReverseHalfFilledVU
 *   slot 2: FullFilledVU
 *   slot 3: degree (온도 각도 표시용)
 *
 * 4,5,6,7 은 EnvDisplay, 시계 등 다른 코드에서 자유롭게 사용 가능.
 */
void CustomChar_InitVUChars(void)
{
    LCDUI_DefineCustom(0u, HalfFilledVU);
    LCDUI_DefineCustom(1u, ReverseHalfFilledVU);
    LCDUI_DefineCustom(2u, FullFilledVU);
    LCDUI_DefineCustom(3u, degree);

    s_vu_fullscreen_active = 0u;
}

/* Full Screen VU 진입:
 *   slot 1: Lmini (mini L)
 *   slot 3: Rmini (mini R)
 *  → ReverseHalf / degree 를 잠시 덮어씀
 */
void CustomChar_EnterFullScreenVU(void)
{
    if (s_vu_fullscreen_active) {
        return; /* 이미 풀스크린 상태 */
    }

    LCDUI_DefineCustom(1u, Lmini);  /* slot 1: mini L */
    LCDUI_DefineCustom(3u, Rmini);  /* slot 3: mini R */

    s_vu_fullscreen_active = 1u;
}

/* Full Screen VU 종료:
 *   slot 1, 3을 원래 그림으로 복구
 */
void CustomChar_ExitFullScreenVU(void)
{
    if (!s_vu_fullscreen_active) {
        return; /* 이미 기본 상태 */
    }

    LCDUI_DefineCustom(1u, ReverseHalfFilledVU);  /* slot 1: ReverseHalf */
    LCDUI_DefineCustom(3u, degree);               /* slot 3: 각도 기호 */

    s_vu_fullscreen_active = 0u;
}
