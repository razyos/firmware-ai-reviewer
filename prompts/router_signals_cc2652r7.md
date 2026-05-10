Available domain labels:
  "RTOS"     — FreeRTOS tasks, queues, semaphores, mutexes, task notifications,
               and defensive assertions;
               signals: xTaskCreate(), xTaskCreateStatic(), xQueueSend(), xQueueReceive(),
               xSemaphoreGive(), xSemaphoreTake(), xTaskNotify(), vTaskDelay(),
               vTaskSuspendAll(), xTaskResumeAll(), xSemaphoreCreateBinary(),
               xSemaphoreCreateMutex(), configASSERT()
  "ISR"      — Interrupt handlers registered via NVIC, FreeRTOS, TI-RTOS, or driverlib;
               also includes SWI/software-interrupt contexts (ClockP callbacks run in SWI
               context and have ISR-level restrictions);
               signals grouped by layer:
                 Core:      functions named *_IRQHandler, NVIC_SetPriority(),
                            NVIC_EnableIRQ(), portYIELD_FROM_ISR(), *FromISR() API calls
                 TI-RTOS:   HwiP_construct(), HwiP_create(), HwiP_Params_init(),
                            ClockP_construct(), ClockP_create()
                 Driverlib: GPIOIntRegister(), IntRegister(), IntEnable(),
                            UARTIntRegister(), UARTIntEnable(),
                            SSIIntRegister(), SSIIntEnable(),
                            I2CIntRegister(), I2CIntEnable(),
                            WatchdogIntRegister()
  "DMA"      — Direct memory access transfers via bare-metal or TI driver abstraction;
               signals: uDMAChannelTransferSet(), uDMAChannelEnable(), uDMAChannelDisable(),
               UDMACC26XX_open(), UDMACC26XX_channelEnable(),
               HWREG(UDMA...)
  "MEMORY"   — Unsafe memory operations: unqualified peripheral register access,
               misaligned pointer casts, integer promotion in shifts, packed structs passed
               to DMA, sizeof() applied to a function parameter (array decay);
               signals: (volatile uint32_t*), __attribute__((packed)), (uint32_t*) cast
               on byte arrays, val<<N where val is uint8_t or uint16_t without prior cast,
               malloc(), alloca(), sizeof() on a function parameter
               NOTE: sizeof() fires MEMORY ONLY when applied to a named function parameter
               (array decay). Do NOT fire for sizeof(localVar), sizeof(Type), or sizeof(*ptr).
  "POINTER"  — Unsafe pointer arithmetic and function pointer indirection;
               signals: ptr++, ptr+offset, (**fn)(),
               function pointer typedef or call through pointer
  "I2C"      — I2C bus transactions and interrupt setup;
               signals: I2C_open(), I2CXfer(), I2CSend(), I2CReceive(), I2C_transfer(),
               I2CMasterBusBusy(), I2CIntRegister(), I2CIntEnable(),
               HWREG(I2C...)
  "SPI"      — SPI bus transactions and interrupt setup via TI Driver or driverlib;
               signals: SPI_open(), SPI_transfer(), SPITransfer(), SPI_Params_init(),
               SSIDataPut(), SSIDataGet(), SSIConfigSetExpClk(), SSIEnable(),
               SSIIntRegister(), SSIIntEnable()
  "POWER"    — Power management, sleep modes, constraints, peripheral clocks, timers;
               signals: Power_setConstraint(), Power_releaseConstraint(),
               Power_registerNotify(), PRCMPowerDomainOff(),
               PRCMPeripheralRunEnable(), PRCMPeripheralSleepEnable(),
               PRCMPeripheralDeepSleepEnable(), PRCMLoadSet(), PRCMLoadGet(),
               ClockP_construct(), ClockP_create(), ClockP_Params_init(),
               ClockP_start(), ClockP_stop(), __WFI(),
               TimerConfigure(), TimerLoadSet(), TimerEnable(),
               AONBatMonBatteryVoltageGet(), AONRTCCurrentCompareValueGet()
  "SAFETY"   — Watchdog timers, fault handlers, MPU, system resets;
               signals: WatchdogReloadSet(), WatchdogIntClear(), Watchdog_open(),
               WatchdogCC26X4_init(), HardFault_Handler(), MPU_config(),
               WatchdogIntRegister(),
               SysCtrlSystemReset(), SysCtrlDeepSleep(),
               HWREG(WDT...)
  "UART"     — UART peripheral transmit/receive and setup at any abstraction level;
               signals: UART_open(), UART_read(), UART_write(), UART_close(),
               UART_Params_init(),
               UART2_open(), UART2_read(), UART2_write(), UART2_close(),
               UARTprintf(), UARTCharPut(), UARTCharGet(), UARTCharPutNonBlocking(),
               UARTFIFOEnable(), UARTConfigSetExpClk(),
               UARTIntEnable(), UARTIntRegister(),
               HWREG(UART...)
  "BLE"      — RF Core driver, BLE command posting, RF callbacks, direct HCI commands;
               signals: RF_open(), RF_postCmd(), RF_runCmd(), RF_pendCmd(), RF_close(),
               rfClientEventCb(), RF_cmdBle5Adv(), RF_cmdBle5Scanner(),
               EasyLink_init(), EasyLink_transmit(), EasyLink_receive(),
               HCI_EXT_SetTxPowerCmd(), HCI_sendHCICmd(), bleStack_init()
  "SECURITY" — Hardware crypto engines, key management, RNG, secure zeroization;
               signals: AESCCM_open(), AESECB_open(), AESCBC_open(), AESGCM_open(),
               SHA2_open(), SHA2_addData(), SHA2_finalize(),
               TRNG_open(), TRNG_generateEntropy(),
               CryptoKey_initKey(), CryptoKey_initBlankKey(), CryptoUtils_memset(),
               ECDH_open(), ECDSA_open(), PKA_open(), AESCTRdrbg_generate(),
               CryptoCC26X2_init(), HWREG(CRYPTO...)

Example — file with a FreeRTOS queue and an ISR:
["RTOS", "ISR"]

Example — file with DMA and volatile register access:
["DMA", "MEMORY"]

Example — file with a FreeRTOS task, an ISR handler, and a shared volatile variable:
["RTOS", "ISR", "MEMORY"]

Example — file with RTOS tasks, ClockP SWI callback, I2C transactions, and power constraints:
["RTOS", "ISR", "I2C", "POWER"]
