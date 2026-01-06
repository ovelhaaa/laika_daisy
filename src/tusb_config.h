#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// Defined by PlatformIO
#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENDPOINT0_SIZE 64

// MIDI Class
#define CFG_TUD_MIDI 1
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif // _TUSB_CONFIG_H_
