#include "audio_sai.h"
#include "stm32h7xx_hal.h"
#include "system.h"

// --- Audio Defines ---
#define AUDIO_SAMPLE_RATE 48000
// We need enough space for Double Buffering
// Total Buffer = 2 * (BLOCK_SIZE samples * 2 channels)
// main.cpp defines BLOCK_SIZE as 48.
#define BLOCK_SIZE 48
#define TOTAL_BUFFER_SIZE (BLOCK_SIZE * 2 * 2)

SAI_HandleTypeDef hsai_BlockA;
DMA_HandleTypeDef hdma_sai;

// Buffer in D2 RAM (SRAM1/2/3) which is accessible by DMA1/2
// Note: DTCM (standard RAM on M7) is NOT accessible by DMA2 on some H7s without
// specific MPU config, but .ram_d2 section usually maps to D2 domain SRAM which
// is fine.
__attribute__((section(".ram_d2"))) int32_t audio_tx_buf[TOTAL_BUFFER_SIZE];

// Flags to indicate which half of the buffer needs filling
volatile uint8_t req_fill_first_half = 0;
volatile uint8_t req_fill_second_half = 0;

extern "C" void DMA2_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_sai); }

extern "C" void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai) {
  req_fill_first_half = 1;
}

extern "C" void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai) {
  req_fill_second_half = 1;
}

void AudioInit() {
  // 1. Clock Configuration for SAI1
  // Target: 48kHz * 256 = 12.288 MHz (exact for I2S MCLK)
  // HSE = 25 MHz on WeAct board
  //
  // PLL3 Configuration for 12.288 MHz:
  // - M = 25 (25 MHz / 25 = 1 MHz input to PLL)
  // - N = 196 + fractional (1 MHz * 196.608 = 196.608 MHz VCO)
  // - Q = 16 (196.608 MHz / 16 = 12.288 MHz)
  //
  // Fractional part: 0.608 * 8192 = 4980 (PLL3FRACN value)
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
  PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL3;
  PeriphClkInitStruct.PLL3.PLL3M = 25;  // 25 MHz / 25 = 1 MHz
  PeriphClkInitStruct.PLL3.PLL3N = 196; // Integer part: 196
  PeriphClkInitStruct.PLL3.PLL3P = 2;   // Not used for SAI1
  PeriphClkInitStruct.PLL3.PLL3Q = 16;  // 196.608 MHz / 16 = 12.288 MHz
  PeriphClkInitStruct.PLL3.PLL3R = 2;   // Not used
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0; // 1-2 MHz input range
  PeriphClkInitStruct.PLL3.PLL3VCOSEL =
      RCC_PLL3VCOMEDIUM;                     // Medium VCO for lower freq
  PeriphClkInitStruct.PLL3.PLL3FRACN = 4980; // Fractional: 0.608 * 8192

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
    // Error
  }

  // 2. Enable Periph Clocks
  __HAL_RCC_SAI1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE(); // Pins are here

  // 3. GPIO Configuration
  // PE2: MCLK, PE4: FS, PE5: SCK, PE6: SD (AF6 for SAI1)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  // 4. DMA Configuration
  hdma_sai.Instance = DMA2_Stream1;
  hdma_sai.Init.Request = DMA_REQUEST_SAI1_A;
  hdma_sai.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_sai.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_sai.Init.MemInc = DMA_MINC_ENABLE;
  hdma_sai.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_sai.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdma_sai.Init.Mode = DMA_CIRCULAR;
  hdma_sai.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_sai.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

  if (HAL_DMA_Init(&hdma_sai) != HAL_OK) {
    // Error
  }

  __HAL_LINKDMA(&hsai_BlockA, hdmatx, hdma_sai);

  // 5. SAI Configuration
  hsai_BlockA.Instance = SAI1_Block_A;
  hsai_BlockA.Init.Protocol = SAI_FREE_PROTOCOL;
  hsai_BlockA.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockA.Init.DataSize =
      SAI_DATASIZE_32; // Use full 32-bit to avoid wrapping issues with int32_t
  hsai_BlockA.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockA.Init.ClockStrobing =
      SAI_CLOCKSTROBING_FALLINGEDGE; // Data changed on falling, read on rising?
                                     // Standard I2S usually latch on rising.
  // SCK: Edge.
  // I2S: Data is valid on Rising Edge of SCK?
  // ST SAI: "ClockStrobing" defines the edge on which data is
  // captured/released. For TX: "The data is shifted out on the selected edge."
  // I2S Standard: Transmitter changes data on Falling Edge, Receiver reads on
  // Rising Edge.
  hsai_BlockA.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockA.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockA.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockA.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai_BlockA.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA.Init.MckOutput = SAI_MCK_OUTPUT_ENABLE;
  hsai_BlockA.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA.Init.TriState = SAI_OUTPUT_NOTRELEASED;

  // Frame
  hsai_BlockA.FrameInit.FrameLength = 64;
  hsai_BlockA.FrameInit.ActiveFrameLength = 32;
  hsai_BlockA.FrameInit.FSDefinition =
      SAI_FS_CHANNEL_IDENTIFICATION; // Left = Low (Standard I2S)
  hsai_BlockA.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  hsai_BlockA.FrameInit.FSOffset =
      SAI_FS_BEFOREFIRSTBIT; // Critical for I2S (1 bit delay)

  // Slot
  hsai_BlockA.SlotInit.FirstBitOffset = 0;
  hsai_BlockA.SlotInit.SlotSize = SAI_SLOTSIZE_32B;
  hsai_BlockA.SlotInit.SlotNumber = 2;
  hsai_BlockA.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1;

  if (HAL_SAI_Init(&hsai_BlockA) != HAL_OK) {
    // Error
  }

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  // Zero out buffer to prevent initial loud noise burst
  for (int i = 0; i < TOTAL_BUFFER_SIZE; i++)
    audio_tx_buf[i] = 0;
}

void AudioStart() {
  // Start DMA
  HAL_SAI_Transmit_DMA(
      &hsai_BlockA, (uint8_t *)audio_tx_buf,
      TOTAL_BUFFER_SIZE // size in "elements" if 16bit? No, function expects
                        // Size in DataSize units if 8bit?
      // HAL_SAI_Transmit_DMA: "Size Amount of data to be sent"
      // It usually means number of transfers.
      // If DataSize is 32b, is it count of 32b words?
      // Note: For DMA, Buffer is byte pointer.
  );
}
