/*
 * eval_suite/stm32/01_dcache_dma_coherency.c
 *
 * Platform: STM32H743 (Cortex-M7, D-Cache enabled)
 * HAL version: STM32H7xx HAL Driver v1.11
 */

#include <stdint.h>
#include <string.h>
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* UART handle initialised in main() */
extern UART_HandleTypeDef huart1;

/* RX buffer — DMA writes here when a packet arrives */
static uint8_t g_rxBuf[128];

/* TX buffer — CPU builds the response here before transmitting */
static uint8_t g_txBuf[128];

static SemaphoreHandle_t g_rxDoneSem;

/* -----------------------------------------------------------------------
 * HAL DMA completion callback — invoked from UART/DMA IRQ
 * ----------------------------------------------------------------------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* Signal the processing task */
    BaseType_t xWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_rxDoneSem, &xWoken);
    portYIELD_FROM_ISR(xWoken);
}

/* -----------------------------------------------------------------------
 * Packet processing task
 * ----------------------------------------------------------------------- */
void vUartProcessTask(void *pv)
{
    g_rxDoneSem = xSemaphoreCreateBinary();

    /* Kick off first DMA receive */
    HAL_UART_Receive_DMA(&huart1, g_rxBuf, sizeof(g_rxBuf));

    for (;;) {
        /* Wait for DMA completion signal from callback */
        xSemaphoreTake(g_rxDoneSem, portMAX_DELAY);

        /* Build response based on received data */
        size_t respLen = buildResponse(g_rxBuf, g_txBuf, sizeof(g_txBuf));

        /* Transmit response */
        HAL_UART_Transmit_DMA(&huart1, g_txBuf, (uint16_t)respLen);

        /* Re-arm receive */
        HAL_UART_Receive_DMA(&huart1, g_rxBuf, sizeof(g_rxBuf));
    }
}
