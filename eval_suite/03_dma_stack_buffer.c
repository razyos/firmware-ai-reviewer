/*
 * eval_suite/03_dma_stack_buffer.c
 *
 * Planted bugs:
 *   HW-001 — DMA source buffer is stack-allocated; function returns while
 *            DMA still owns the buffer, causing writes into recycled stack memory
 *   HW-003 — CPU reads from the DMA buffer immediately after enabling the
 *            channel, before the transfer completes (ownership race)
 *
 * Platform: TI CC2652R7 / TI uDMA
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/* TI driverlib stubs (names match CC26x2 SDK) */
#include <ti/devices/cc26x2/driverlib/udma.h>
#include <ti/devices/cc26x2/driverlib/uart.h>
#include <ti/devices/cc26x2/inc/hw_memmap.h>

#define UART0_BASE   0x40001000UL
#define TX_BUF_SIZE  32

static volatile bool g_dmaTxDone = false;

/* DMA completion ISR — called when the UART TX DMA channel finishes */
void UDMA_IRQHandler(void)
{
    /* clear channel interrupt and signal completion */
    g_dmaTxDone = true;
}

/* -----------------------------------------------------------------------
 * Start an asynchronous UART DMA transmit
 * ----------------------------------------------------------------------- */
void UART_StartDmaTx(const uint8_t *data, size_t len)
{
    /*
     * BUG [HW-001]: tx_buf lives on the stack of this function.
     * uDMAChannelEnable() starts the DMA transfer and returns immediately.
     * When UART_StartDmaTx() returns, the stack frame is reclaimed by the
     * caller — the DMA engine now holds a dangling pointer and reads from
     * recycled stack memory that may belong to any subsequent call frame.
     *
     * Fix: declare tx_buf as static or as a module-level global so its
     * lifetime extends beyond this function's scope.
     */
    uint8_t tx_buf[TX_BUF_SIZE];

    size_t copy_len = (len < TX_BUF_SIZE) ? len : TX_BUF_SIZE;
    memcpy(tx_buf, data, copy_len);

    g_dmaTxDone = false;

    uDMAChannelTransferSet(
        UDMA_CHAN_UART0_TX | UDMA_PRI_SELECT,
        UDMA_MODE_BASIC,
        (void *)tx_buf,
        (void *)(UART0_BASE + UART_O_DR),
        copy_len
    );

    uDMAChannelEnable(UDMA_CHAN_UART0_TX);

    /*
     * BUG [HW-003]: CPU reads tx_buf[0] immediately after enabling the DMA
     * channel. The DMA engine now owns the buffer — the CPU has no guarantee
     * about when the first byte is consumed. This is a race: the read may
     * observe the original byte, a partially-modified byte, or stale cache
     * data. Ownership returns to the CPU only after g_dmaTxDone is set in
     * the completion ISR.
     *
     * Fix: read or modify tx_buf only after g_dmaTxDone is true.
     */
    uint8_t first_byte = tx_buf[0];   /* undefined: DMA may still be reading */
    (void)first_byte;
}
