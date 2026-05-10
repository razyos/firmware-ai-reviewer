/*
 * eval_suite/stm32/02_hal_callback_isr_misuse.c
 *
 * Platform: STM32F407 (Cortex-M4, no D-Cache)
 * HAL version: STM32F4xx HAL Driver
 */

#include <stdint.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

extern UART_HandleTypeDef huart2;

static SemaphoreHandle_t g_txDoneSem;
static QueueHandle_t     g_rxQueue;

/* -----------------------------------------------------------------------
 * HAL DMA TX complete callback — fired from DMA interrupt
 * ----------------------------------------------------------------------- */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    xSemaphoreGive(g_txDoneSem);
}

/* -----------------------------------------------------------------------
 * HAL DMA RX complete callback — fired from DMA interrupt
 * ----------------------------------------------------------------------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint8_t dummy = 0xAA;
    xQueueSend(g_rxQueue, &dummy, 0);
}

/* -----------------------------------------------------------------------
 * Application task
 * ----------------------------------------------------------------------- */
void vCommTask(void *pv)
{
    static uint8_t txBuf[32] = "hello";
    static uint8_t rxBuf[32];

    g_txDoneSem = xSemaphoreCreateBinary();
    g_rxQueue   = xQueueCreate(8, sizeof(uint8_t));

    HAL_UART_Receive_DMA(&huart2, rxBuf, sizeof(rxBuf));

    for (;;) {
        HAL_UART_Transmit_DMA(&huart2, txBuf, 5);
        xSemaphoreTake(g_txDoneSem, portMAX_DELAY);

        uint8_t b;
        if (xQueueReceive(g_rxQueue, &b, pdMS_TO_TICKS(100)) == pdTRUE) {
            txBuf[0] = b;
        }
    }
}
