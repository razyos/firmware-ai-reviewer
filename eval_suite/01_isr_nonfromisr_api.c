/*
 * eval_suite/01_isr_nonfromisr_api.c
 *
 * Planted bugs:
 *   ISR-001 — xQueueSend() called from ISR context (must be xQueueSendFromISR)
 *   ISR-002 — portYIELD_FROM_ISR() is missing after the queue send
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

        /*
         * BUG [ISR-001]: xQueueSend is a blocking task-context API.
         * Calling it from Handler Mode (ISR) corrupts the FreeRTOS
         * scheduler's ready-list data structures. The crash typically
         * appears later, in an unrelated task, making root cause analysis
         * very difficult.
         *
         * Fix: xQueueSendFromISR(g_rxQueue, &byte, &xHigherPriorityTaskWoken)
         */
        xQueueSend(g_rxQueue, &byte, 0);

        /*
         * BUG [ISR-002]: portYIELD_FROM_ISR is never called.
         * Even after fixing ISR-001, a higher-priority task unblocked by
         * the queue send will not be scheduled until the next SysTick
         * interrupt — up to 1 ms of unnecessary latency.
         *
         * Fix: declare BaseType_t xWoken = pdFALSE; pass &xWoken to
         * xQueueSendFromISR(); call portYIELD_FROM_ISR(xWoken) here.
         */
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

    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
    /* NVIC_SetPriority and NVIC_EnableIRQ omitted for brevity */
}
