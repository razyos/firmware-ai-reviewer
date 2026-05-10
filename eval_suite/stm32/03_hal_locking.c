/*
 * eval_suite/stm32/03_hal_locking.c
 *
 * Platform: STM32F407 (Cortex-M4)
 * HAL version: STM32F4xx HAL Driver
 *
 * Telemetry gateway — periodic sensor uplink and operator command channel
 * share USART1 (PA9/PA10, APB2, up to 84 MHz clock, 8N1).
 */

#include <stdint.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* USART1 handle — initialised in SystemConfig() */
UART_HandleTypeDef huart1;

/* -----------------------------------------------------------------------
 * Telemetry task — transmits a framed sensor reading every 500 ms
 * Frame layout: 0xAA | seq_hi | seq_lo | adc_val | 0x55
 * ----------------------------------------------------------------------- */
void vTelemetryTask(void *pv)
{
    uint8_t  frame[5];
    uint16_t seq = 0;

    for (;;) {
        frame[0] = 0xAA;
        frame[1] = (uint8_t)(seq >> 8);
        frame[2] = (uint8_t)(seq & 0xFF);
        frame[3] = (uint8_t)readAdcChannel(0);
        frame[4] = 0x55;

        HAL_UART_Transmit(&huart1, frame, sizeof(frame), 100);
        seq++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* -----------------------------------------------------------------------
 * Command task — receives operator commands and sends acknowledgements
 * Command frame: 0xCC | cmd_id | seq_hi | seq_lo | 0xFF
 * Ack frame:     0xAC | cmd_id | seq_hi | seq_lo | 0xFF
 * ----------------------------------------------------------------------- */
void vCommandTask(void *pv)
{
    uint8_t rxBuf[5];
    uint8_t ack[5];

    for (;;) {
        if (HAL_UART_Receive(&huart1, rxBuf, sizeof(rxBuf), portMAX_DELAY) == HAL_OK) {
            ack[0] = 0xAC;
            ack[1] = rxBuf[1];
            ack[2] = rxBuf[2];
            ack[3] = rxBuf[3];
            ack[4] = 0xFF;
            HAL_UART_Transmit(&huart1, ack, sizeof(ack), 50);
        }
    }
}

/* -----------------------------------------------------------------------
 * Hardware and peripheral initialisation
 * ----------------------------------------------------------------------- */
static void SystemConfig(void)
{
    HAL_Init();

    /* USART1: PA9 TX, PA10 RX — alternate function AF7 */
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    /* 2 preemption bits, 2 sub-priority bits — retained from bootloader layout */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_2);
}

int main(void)
{
    SystemConfig();

    xTaskCreate(vTelemetryTask, "Telem", 256, NULL, 2, NULL);
    xTaskCreate(vCommandTask,   "Cmd",   256, NULL, 3, NULL);

    vTaskStartScheduler();
    for (;;) {}
}
