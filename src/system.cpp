#include "system.h"
#include "tusb.h"

// WeAct Mini H743 HSE is 25MHz

void Error_Handler(void) {
  while (1) {
    // Blink LED or break
  }
}

/**
 * @brief  System Clock Configuration
 *         The system Clock is configured as follow :
 *            System Clock source            = PLL (HSE)
 *            SYSCLK(Hz)                     = 480000000 (CPU Clock)
 *            HCLK(Hz)                       = 240000000 (AXI and AHBs Clock)
 *            AHB Prescaler                  = 2
 *            D1CPRE                         = 1
 *            HSE Frequency(Hz)              = 25000000
 *            PLL_M                          = 5
 *            PLL_N                          = 192
 *            PLL_P                          = 2
 *            PLL_Q                          = 2
 *            PLL_R                          = 2
 *            VDD(V)                         = 3.3
 *            Flash Latency(WS)              = 4 (Wait states) - Actually 4 is
 * likely too low for 480MHz, usually 2WS per voltage scaling? At VOS0 (Scale0)
 * 480MHz, we need 4WS? No, 6/7 WS. Let's stick to 400MHz for stability if
 * unsure about VOS0.
 *
 *            Target: 400MHz
 *            HSE = 25MHz
 *            M = 5 -> 5MHz
 *            N = 160 -> 800MHz VCO
 *            P = 2 -> 400MHz SYSCLK
 *            Q = 4 -> 200MHz
 *            R = 2 -> 400MHz
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  // Supply configuration update enable
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  // Configure the main internal regulator output voltage
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
  }

  // Initializes the RCC Oscillators according to the specified parameters
  // Enable HSE for main PLL and HSI48 for USB
  RCC_OscInitStruct.OscillatorType =
      RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State =
      RCC_HSI48_ON; // Enable 48MHz oscillator for USB
  RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
  RCC_OscInitStruct.CSIState = RCC_CSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

  // PLL1 Setup for 400MHz
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 160;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  // --- USB Clock Configuration ---
  // Use HSI48 (internal 48MHz oscillator designed for USB)
  // HSI48 auto-calibrates with USB SOF signal via CRS peripheral
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
    Error_Handler();
  }

  // Enable USB Voltage Level Detector
  HAL_PWREx_EnableUSBVoltageDetector();

  // Enable GPIOA Clock (PA11/PA12 for USB)
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // Configure PA11 (DM) and PA12 (DP) to AF10 (OTG FS)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG1_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Enable USB OTG FS Clock
  __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();

  // CRITICAL: Disable ULPI clock - must NOT be enabled when using internal FS
  // PHY
  __HAL_RCC_USB2_OTG_FS_ULPI_CLK_DISABLE();

  // Enable USB Interrupt
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);

  // Initializes the CPU, AHB and APB buses clocks
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) !=
      HAL_OK) // 400Mhz needs more than Latency 2?
  // Manual says for VOS1 @ 400MHz, we need 4 wait states (FLASH_LATENCY_4)
  {
    // Retry with correct latency if above fails or manual adjustment
    // Ideally call ClockConfig with correct latency directly
  }
  // Re-call with Latency 4 to be safe
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * Configure MPU to ensure audio buffer in D2 SRAM is non-cacheable
 * to avoid cache coherency issues with DMA.
 */
void MPU_Config(void) {
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  // Disable the MPU
  HAL_MPU_Disable();

  // Configure the MPU attributes for the RAM_D2 region (0x30000000)
  // WeAct H743 has SRAM1, SRAM2, SRAM3 starting at 0x30000000
  // Total 288KB. Let's map the first 32KB as non-cacheable for buffers.
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1; // Cacheable or not?
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE; // Critical
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  // Enable the MPU
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

// Handler for SysTick (needed for HAL_Delay)
extern "C" void SysTick_Handler(void) { HAL_IncTick(); }

// TinyUSB Interrupt Handler for OTG_FS
extern "C" void OTG_FS_IRQHandler(void) { tud_int_handler(0); }
