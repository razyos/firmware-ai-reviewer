/*
 * eval_suite/04_rmw_race.c
 *
 * Platform: TI CC2652R7 / FreeRTOS
 */

#include <stdint.h>
#include <stdbool.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* TI CC2652R7 GPIO register addresses */
#define GPIO_BASE          0x40022000UL
#define GPIO_O_DOUT31_0    0x080UL   /* Data output register, DIO 0-31 */
#define GPIO_O_DOUTSET31_0 0x090UL   /* DOUTSET31_0 */
#define GPIO_O_DOUTCLR31_0 0x0A0UL   /* DOUTCLR31_0 */

#define LED_RED_MASK   (1u << 6)   /* DIO6 */
#define LED_GREEN_MASK (1u << 7)   /* DIO7 */

static SemaphoreHandle_t g_gpioLock;

void GPIO_Init(void)
{
    g_gpioLock = xSemaphoreCreateBinary();
    xSemaphoreGive(g_gpioLock);   /* make it initially available */
}

/* -----------------------------------------------------------------------
 * Set or clear an LED from task context
 * ----------------------------------------------------------------------- */
void LED_Set(uint32_t led_mask, bool on)
{
    xSemaphoreTake(g_gpioLock, portMAX_DELAY);

    volatile uint32_t *dout = (volatile uint32_t *)(GPIO_BASE + GPIO_O_DOUT31_0);

    if (on) {
        *dout |= led_mask;
    } else {
        *dout &= ~led_mask;
    }

    xSemaphoreGive(g_gpioLock);
}

/* Task bodies omitted for brevity */
