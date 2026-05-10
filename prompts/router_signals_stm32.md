Available domain labels:

  "STM32"    — STM32 HAL peripheral drivers, DMA streams, cache maintenance, HAL callbacks;
               signals: HAL_UART_Transmit(), HAL_UART_Transmit_DMA(), HAL_UART_Receive_DMA(),
               HAL_UART_TxCpltCallback(), HAL_UART_RxCpltCallback(),
               HAL_SPI_Transmit_DMA(), HAL_SPI_Receive_DMA(),
               HAL_SPI_TxCpltCallback(), HAL_SPI_RxCpltCallback(),
               HAL_I2C_Master_Transmit_DMA(), HAL_I2C_Master_Receive_DMA(),
               HAL_I2C_MasterTxCpltCallback(), HAL_I2C_MasterRxCpltCallback(),
               HAL_DMA_Start(), HAL_DMA_Start_IT(), HAL_DMA_Abort(),
               HAL_Init(), HAL_RCC_OscConfig(), HAL_RCC_ClockConfig(),
               HAL_NVIC_SetPriority(), HAL_NVIC_EnableIRQ(),
               HAL_NVIC_SetPriorityGrouping(),
               SCB_CleanDCache_by_Addr(), SCB_InvalidateDCache_by_Addr(),
               SCB_EnableDCache(), SCB_DisableDCache(),
               ALIGN_32BYTES(), __attribute__((aligned(32)))

  "RTOS"     — FreeRTOS tasks, queues, semaphores, mutexes, task notifications;
               signals: xTaskCreate(), xTaskCreateStatic(), xQueueSend(), xQueueReceive(),
               xSemaphoreGive(), xSemaphoreTake(), xTaskNotify(), vTaskDelay(),
               xSemaphoreCreateBinary(), xSemaphoreCreateMutex(), configASSERT(),
               vTaskStartScheduler(), portYIELD_FROM_ISR(), *FromISR() API calls

  "ISR"      — Interrupt handlers and HAL callbacks that execute in ISR context;
               signals: functions named *_IRQHandler, NVIC_SetPriority(),
               NVIC_EnableIRQ(), HAL_NVIC_SetPriority(), HAL_NVIC_EnableIRQ(),
               functions named HAL_*_TxCpltCallback, HAL_*_RxCpltCallback,
               HAL_*_ErrorCallback, HAL_*_AbortCpltCallback

  "DMA"      — DMA stream transfers (STM32 DMA controller);
               signals: HAL_DMA_Start(), HAL_DMA_Start_IT(), HAL_DMA_Abort(),
               HAL_UART_Transmit_DMA(), HAL_UART_Receive_DMA(),
               HAL_SPI_Transmit_DMA(), HAL_SPI_Receive_DMA(),
               HAL_I2C_Master_Transmit_DMA(), HAL_I2C_Master_Receive_DMA(),
               __HAL_DMA_GET_FLAG(), __HAL_DMA_CLEAR_FLAG()

  "MEMORY"   — Unsafe memory operations: pointer casts, integer promotion shifts,
               packed structs, sizeof on function parameters;
               signals: (volatile uint32_t*), __attribute__((packed)),
               (uint32_t*) cast on byte arrays, val<<N where val is uint8_t/uint16_t
               without prior cast, sizeof() on a function parameter

  "UART"     — UART peripheral operations via HAL or LL;
               signals: HAL_UART_Init(), HAL_UART_Transmit(), HAL_UART_Receive(),
               HAL_UART_Transmit_DMA(), HAL_UART_Receive_DMA(),
               LL_USART_TransmitData8(), LL_USART_ReceiveData8(),
               USART1_IRQHandler(), USART2_IRQHandler()

  "SPI"      — SPI bus operations via HAL or LL;
               signals: HAL_SPI_Init(), HAL_SPI_Transmit(), HAL_SPI_Receive(),
               HAL_SPI_TransmitReceive(), HAL_SPI_Transmit_DMA(), HAL_SPI_Receive_DMA()

  "I2C"      — I2C bus operations via HAL or LL;
               signals: HAL_I2C_Init(), HAL_I2C_Master_Transmit(),
               HAL_I2C_Master_Receive(), HAL_I2C_Master_Transmit_DMA(),
               HAL_I2C_Master_Receive_DMA(), HAL_I2C_IsDeviceReady()

  "POWER"    — STM32 low-power modes, RCC, clock configuration;
               signals: HAL_PWR_EnterSTOPMode(), HAL_PWR_EnterSTANDBYMode(),
               HAL_PWR_EnterSLEEPMode(), HAL_RCC_OscConfig(),
               HAL_RCC_ClockConfig(), __WFI(), __WFE(),
               HAL_RCCEx_PeriphCLKConfig()

  "SAFETY"   — Watchdog and fault handlers;
               signals: HAL_WWDG_Refresh(), HAL_IWDG_Refresh(),
               HAL_WWDG_Init(), HAL_IWDG_Init(),
               HardFault_Handler(), MemManage_Handler(), BusFault_Handler()

  "SECURITY" — Hardware crypto, RNG, hash;
               signals: HAL_CRYP_Init(), HAL_CRYP_Encrypt(), HAL_CRYP_Decrypt(),
               HAL_RNG_GenerateRandomNumber(), HAL_HASH_Init(), HAL_HASH_MD5_Start()

Example — STM32 DMA transfer with FreeRTOS callback signalling:
["STM32", "DMA", "RTOS", "ISR"]

Example — STM32 UART with HAL init only, no DMA:
["STM32", "UART"]

Example — STM32 HAL + FreeRTOS, DMA SPI, cache maintenance:
["STM32", "DMA", "SPI", "RTOS", "ISR"]
