/*
 * eval_suite/02_volatile_missing.c
 *
 * Planted bugs:
 *   MEM-001 — Polling loop reads MMIO register without volatile qualifier
 *             (optimizer eliminates the re-read at -O2, producing an infinite loop)
 *   MEM-003 — uint8_t shifted left by 8 bits without cast to uint32_t first
 *             (integer promotion to signed int; UB if bit 7 of result is set)
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
    /*
     * BUG [MEM-001]: The pointer lacks the volatile qualifier.
     * At -O2, GCC reads *status_reg once before the loop, caches the
     * value in a register, and never re-reads from the hardware address.
     * The loop condition is evaluated against the cached (stale) value
     * forever — an infinite loop that never sees the hardware update.
     *
     * Fix: volatile uint32_t *status_reg = (volatile uint32_t *)(GPT0_BASE + GPT_O_RIS);
     */
    uint32_t *status_reg = (uint32_t *)(GPT0_BASE + GPT_O_RIS);

    while (!(*status_reg & GPT_RIS_TATORIS)) {
        /* spin — waiting for timer to signal load complete */
    }
}

/* -----------------------------------------------------------------------
 * Configure which GPT clock domains to enable
 * ----------------------------------------------------------------------- */
void PRCM_EnableGptClocks(uint8_t domain_mask)
{
    volatile uint32_t *clk_reg = (volatile uint32_t *)(PRCM_BASE + PRCM_O_GPTCLKGR);

    /*
     * BUG [MEM-003]: domain_mask is uint8_t. The C standard promotes it
     * to signed int before the shift. If domain_mask >= 0x02 (bit 1 set),
     * shifting left by 8 can set bit 31 of a signed int — undefined
     * behaviour under C99/C11 (signed overflow). The compiler may
     * generate incorrect code or eliminate the write entirely.
     *
     * Fix: *clk_reg = (uint32_t)domain_mask << 8;
     */
    *clk_reg = domain_mask << 8;
}
