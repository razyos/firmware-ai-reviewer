/*
 * eval_suite/01_isr_nonfromisr_api.c
 *
 * Platform: TI CC2652R7 / FreeRTOS
 */

#include <stdint.h>
#include <stdbool.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* TI driverlib */
#include <ti/devices/cc26x2/driverlib/uart.h>
#include <ti/devices/cc26x2/inc/hw_memmap.h>

#define UART0_BASE  0x40001000UL
#define RX_QUEUE_DEPTH  64

static QueueHandle_t g_rxQueue;
static TaskHandle_t  g_rxTaskHandle;

/* -----------------------------------------------------------------------
 * UART0 interrupt handler — fires on each received byte
 * ----------------------------------------------------------------------- */
void UART0_IRQHandler(void)
{
    uint32_t status = UARTIntStatus(UART0_BASE, true);
    UARTIntClear(UART0_BASE, status);

    if (status & UART_INT_RX) {
        uint8_t byte = (uint8_t)(UARTCharGetNonBlocking(UART0_BASE) & 0xFF);
        xQueueSend(g_rxQueue, &byte, 0);
    }
}

/* -----------------------------------------------------------------------
 * Receive task — processes bytes from the queue
 * ----------------------------------------------------------------------- */
void vUartRxTask(void *pvParameters)
{
    uint8_t byte;
    for (;;) {
        if (xQueueReceive(g_rxQueue, &byte, portMAX_DELAY) == pdTRUE) {
            /* application processing */
            (void)byte;
        }
    }
}

void App_Init(void)
{
    g_rxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(uint8_t));
    xTaskCreate(vUartRxTask, "UartRx", 512, NULL, 3, &g_rxTaskHandle);

    UARTFIFOEnable(UART0_BASE);
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
    /* NVIC_SetPriority and NVIC_EnableIRQ omitted for brevity */
}
