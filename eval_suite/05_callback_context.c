/*
 * eval_suite/05_callback_context.c
 *
 * Planted bugs:
 *   ISR-001 — Callback executed in ISR context calls xSemaphoreGive()
 *             (blocking task-context API) instead of xSemaphoreGiveFromISR()
 *   ISR-002 — portYIELD_FROM_ISR() is not called after the semaphore give;
 *             the waiting task is unblocked but waits up to 1 tick to run
 *
 * Context: A generic I2C driver fires a registered callback from its DMA
 * completion ISR. The application developer must treat this callback as
 * executing in Handler Mode — any FreeRTOS API called inside it must be
 * the FromISR variant.
 *
 * Platform: TI CC2652R7 / FreeRTOS
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* Callback type: driver calls this from its DMA completion ISR */
typedef void (*I2C_Callback_t)(void *ctx, uint32_t status);

#define I2C_STATUS_OK    0x00
#define I2C_STATUS_ERROR 0x01

/* Application-level state for one sensor */
typedef struct {
    SemaphoreHandle_t done;         /* binary semaphore — task blocks here  */
    uint8_t           rx_buf[8];    /* populated by DMA before callback fires */
} SensorCtx_t;

static SensorCtx_t g_sensor;

/* -----------------------------------------------------------------------
 * I2C completion callback — EXECUTES IN ISR CONTEXT (Handler Mode)
 * ----------------------------------------------------------------------- */
void Sensor_I2cCb(void *ctx, uint32_t status)
{
    SensorCtx_t *s = (SensorCtx_t *)ctx;

    if (status == I2C_STATUS_OK) {
        /*
         * BUG [ISR-001]: xSemaphoreGive is a task-context API.
         * Calling it from Handler Mode corrupts the FreeRTOS scheduler's
         * internal structures. The corruption is typically silent at the
         * point of the call and manifests as an assertion failure or crash
         * in an unrelated location seconds later.
         *
         * Fix: replace with:
         *   BaseType_t xWoken = pdFALSE;
         *   xSemaphoreGiveFromISR(s->done, &xWoken);
         *   portYIELD_FROM_ISR(xWoken);
         */
        xSemaphoreGive(s->done);

        /*
         * BUG [ISR-002]: portYIELD_FROM_ISR() is absent.
         * Even with the ISR-001 fix applied, omitting this call means
         * the task blocked on s->done will not be scheduled until the
         * next SysTick interrupt — up to 1 ms of added latency for a
         * time-sensitive sensor read.
         */
    }
}

/* -----------------------------------------------------------------------
 * Sensor task — initiates async I2C read and waits for completion
 * ----------------------------------------------------------------------- */
void vSensorTask(void *pvParameters)
{
    g_sensor.done = xSemaphoreCreateBinary();

    /*
     * Register callback: driver will call Sensor_I2cCb from its ISR.
     * I2C_RegisterCallback(handle, Sensor_I2cCb, &g_sensor);
     */

    for (;;) {
        /*
         * Kick off async I2C read (non-blocking — returns immediately).
         * I2C_ReadAsync(handle, SENSOR_ADDR, g_sensor.rx_buf, sizeof(g_sensor.rx_buf));
         */

        /* Block until callback signals completion */
        xSemaphoreTake(g_sensor.done, portMAX_DELAY);

        /* Safe to process rx_buf here — DMA has finished, callback has fired */
        (void)g_sensor.rx_buf[0];
    }
}
