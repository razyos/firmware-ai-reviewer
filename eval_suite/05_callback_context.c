/*
 * eval_suite/05_callback_context.c
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

/* I2C completion callback type */
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
 * I2C completion callback
 * ----------------------------------------------------------------------- */
void Sensor_I2cCb(void *ctx, uint32_t status)
{
    SensorCtx_t *s = (SensorCtx_t *)ctx;

    if (status == I2C_STATUS_OK) {
        xSemaphoreGive(s->done);
    }
}

/* -----------------------------------------------------------------------
 * Sensor task — initiates async I2C read and waits for completion
 * ----------------------------------------------------------------------- */
void vSensorTask(void *pvParameters)
{
    g_sensor.done = xSemaphoreCreateBinary();

    /*
     * Register completion callback.
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
