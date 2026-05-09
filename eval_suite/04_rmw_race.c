/*
 * eval_suite/04_rmw_race.c
 *
 * Planted bugs:
 *   MEM-004 — Non-atomic read-modify-write on shared GPIO DOUT register;
 *             an ISR modifying the same register between the read and write
 *             silently overwrites the ISR's change
 *   RTOS-003 — Binary semaphore used as a mutex (no priority inheritance);
 *              classic priority inversion: low-priority holder is not boosted
 *              when a high-priority task blocks on it
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
#define GPIO_O_DOUT31_0    0x080UL   /* Data output for DIO 0-31 (RMW required) */
#define GPIO_O_DOUTSET31_0 0x090UL   /* Atomic set:   write 1 to set bit   */
#define GPIO_O_DOUTCLR31_0 0x0A0UL   /* Atomic clear: write 1 to clear bit */

#define LED_RED_MASK   (1u << 6)   /* DIO6 */
#define LED_GREEN_MASK (1u << 7)   /* DIO7 */

/*
 * BUG [RTOS-003]: Created with xSemaphoreCreateBinary(), not xSemaphoreCreateMutex().
 * Binary semaphores have no priority-inheritance protocol.
 *
 * Scenario: vLowPriorityTask holds g_gpioLock. vHighPriorityTask preempts and
 * tries to take the same semaphore — it blocks. vMediumPriorityTask then runs
 * and starves vLowPriorityTask, which never releases the lock.
 * vHighPriorityTask is effectively blocked at the priority of vLowPriorityTask —
 * a classic priority inversion with no self-correction mechanism.
 *
 * Fix: g_gpioLock = xSemaphoreCreateMutex();
 */
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

    /*
     * BUG [MEM-004]: *dout |= led_mask expands to three instructions:
     *   LDR r0, [dout]        ; read current output state
     *   ORR r0, r0, led_mask  ; set the target bit
     *   STR r0, [dout]        ; write back
     *
     * If an ISR runs between LDR and STR and toggles a different bit in
     * the same register, the STR overwrites that change — the ISR's write
     * is silently lost.
     *
     * Fix: use the atomic set/clear shadow registers:
     *   if (on)  *(volatile uint32_t *)(GPIO_BASE + GPIO_O_DOUTSET31_0) = led_mask;
     *   else     *(volatile uint32_t *)(GPIO_BASE + GPIO_O_DOUTCLR31_0) = led_mask;
     */
    if (on) {
        *dout |= led_mask;
    } else {
        *dout &= ~led_mask;
    }

    xSemaphoreGive(g_gpioLock);
}

/* Task bodies omitted for brevity — bugs are in the primitives above */
