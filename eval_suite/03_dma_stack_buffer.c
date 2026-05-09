/*
 * eval_suite/03_dma_stack_buffer.c
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

static volatile uint32_t g_dmaTxDone = 0U;

/* DMA completion ISR — called when the UART TX DMA channel finishes */
void UDMA_IRQHandler(void)
{
    /* clear channel interrupt and signal completion */
    g_dmaTxDone = 1U;
}

/* -----------------------------------------------------------------------
 * Start an asynchronous UART DMA transmit
 * ----------------------------------------------------------------------- */
void UART_StartDmaTx(const uint8_t *data, size_t len)
{
    uint8_t tx_buf[TX_BUF_SIZE];

    size_t copy_len = (len < TX_BUF_SIZE) ? len : TX_BUF_SIZE;
    memcpy(tx_buf, data, copy_len);

    g_dmaTxDone = 0U;

    uDMAChannelTransferSet(
        UDMA_CHAN_UART0_TX | UDMA_PRI_SELECT,
        UDMA_MODE_BASIC,
        (void *)tx_buf,
        (void *)(UART0_BASE + UART_O_DR),
        copy_len
    );

    uDMAChannelEnable(UDMA_CHAN_UART0_TX);

    uint8_t first_byte = tx_buf[0];
    (void)first_byte;
}
