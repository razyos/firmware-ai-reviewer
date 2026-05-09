/*
 * eval_suite/02_volatile_missing.c
 *
 * Platform: TI CC2652R7
 */

#include <stdint.h>
#include <stdbool.h>

/* GPT0 register map (abbreviated) */
#define GPT0_BASE       0x40010000UL
#define GPT_O_RIS       0x01CUL          /* Raw Interrupt Status */
#define GPT_RIS_TATORIS (1u << 0)        /* Timer A Time-Out Raw Interrupt */

/* PRCM register map (abbreviated) */
#define PRCM_BASE        0x40082000UL
#define PRCM_O_GPTCLKGR  0x088UL        /* GPT Clock Gate for Run Mode */

/* -----------------------------------------------------------------------
 * Wait for the GPT0 timer-A load event
 * ----------------------------------------------------------------------- */
void Timer_WaitForLoadEvent(void)
{
    uint32_t *status_reg = (uint32_t *)(GPT0_BASE + GPT_O_RIS);

    while (!(*status_reg & GPT_RIS_TATORIS)) {
        /* spin — waiting for timer to signal load complete */
    }
}

/* -----------------------------------------------------------------------
 * Configure which GPT clock domains to enable.
 * domain_mask: one bit per domain (bits 0-7 mapped to the upper byte
 *              of the clock gate register at bits 31:24).
 * ----------------------------------------------------------------------- */
void PRCM_EnableGptClocks(uint8_t domain_mask)
{
    volatile uint32_t *clk_reg = (volatile uint32_t *)(PRCM_BASE + PRCM_O_GPTCLKGR);
    *clk_reg = domain_mask << 24;
}
