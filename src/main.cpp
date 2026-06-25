#include "main.h"
#include "SynthEngine.h"
#include "audio_sai.h"
#include "stm32h7xx_hal.h"
#include "system.h"
#include <tusb.h>

// Instância global do nosso motor DSP
SynthEngine engine;

// Buffer de áudio (mantém a lógica original do DMA)
#define AUDIO_BLOCK_SIZE 48
extern int32_t audio_tx_buf[2 * AUDIO_BLOCK_SIZE]; // Stereo buffer
extern volatile uint8_t req_fill_first_half;
extern volatile uint8_t req_fill_second_half;

// Callback de Áudio do Hardware (DMA)
void AudioCallback(int32_t *buffer, size_t size) {
    // Aloca buffers temporários na stack para receber os floats do engine
    float out_l[AUDIO_BLOCK_SIZE];
    float out_r[AUDIO_BLOCK_SIZE];

    // O DSP é processado independentemente do formato de hardware
    engine.Process(out_l, out_r, size);

    // Conversão do float resultante para o I2S DAC de 32 bits
    for (size_t i = 0; i < size; i++) {
        float sample_l = out_l[i];
        float sample_r = out_r[i];

        // Defensive clamping
        if (sample_l > 1.0f) sample_l = 1.0f;
        else if (sample_l < -1.0f) sample_l = -1.0f;
        if (sample_r > 1.0f) sample_r = 1.0f;
        else if (sample_r < -1.0f) sample_r = -1.0f;

        buffer[i * 2] = (int32_t)(sample_l * 2147483647.0f);
        buffer[i * 2 + 1] = (int32_t)(sample_r * 2147483647.0f);
    }
}

// Callback MIDI do TinyUSB
void tud_midi_rx_cb(uint8_t itf) {
    uint8_t packet[4];
    while (tud_midi_packet_read(packet)) {
        uint8_t status = packet[1] & 0xF0;
        uint8_t data1 = packet[2];
        uint8_t data2 = packet[3];

        // Passa a mensagem diretamente para o engine
        engine.ProcessMidiMessage(status, data1, data2);

        // Feedback de LED (Simplificado)
        if (status == 0x90 && data2 > 0) {
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET); // LED ON
        } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
            // Nota off (Idealmente verificamos se todas as vozes estão off)
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET); // LED OFF
        }
    }
}

int main(void) {
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();
    SystemClock_Config();

    // Force VBUS Valid for OTG_FS BEFORE tusb_init()
    USB2_OTG_FS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;
    USB2_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN | USB_OTG_GOTGCTL_BVALOVAL;

    // TinyUSB Init
    tusb_init();

    // LED GPIO Init (PE3 - active low on WeAct)
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &gpio);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET); // LED off initially

    AudioInit();

    // Inicializa o Engine DSP
    engine.Init(48000.0f);

    AudioStart();

    while (1) {
        tud_task(); // TinyUSB Task

        if (req_fill_first_half) {
            req_fill_first_half = 0;
            AudioCallback(&audio_tx_buf[0], AUDIO_BLOCK_SIZE);
        }

        if (req_fill_second_half) {
            req_fill_second_half = 0;
            AudioCallback(&audio_tx_buf[2 * AUDIO_BLOCK_SIZE], AUDIO_BLOCK_SIZE);
        }
    }
}
