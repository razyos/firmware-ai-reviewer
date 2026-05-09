/*
 * eval_suite/07_crypto_key_leak.c
 *
 * Planted bugs:
 *   SEC-001 (line 36): keyMaterial not zeroized after AESCCM_oneStepEncrypt completes
 *   SEC-003 (line 14): hardcoded IV literal passed directly to AESCCM encrypt operation
 *
 * Platform: TI CC2652R7, SimpleLink SDK crypto drivers
 */

#include <stdint.h>
#include <stddef.h>
#include "ti/drivers/AESCCM.h"
#include "ti/drivers/CryptoKey.h"
#include "ti/drivers/cryptoutils/cryptoutils.h"

/* BUG SEC-003: hardcoded IV literal visible in the firmware binary */
static const uint8_t g_iv[7] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

/* Key buffer loaded at runtime from secure storage */
static uint8_t g_keyMaterial[16];

static uint8_t g_cipherOut[64];
static uint8_t g_tag[4];

extern void loadKeyFromFlash(uint8_t *dst, size_t len);

int encryptSensorPayload(const uint8_t *plaintext, size_t plaintextLen)
{
    loadKeyFromFlash(g_keyMaterial, sizeof(g_keyMaterial));

    CryptoKey cryptoKey;
    CryptoKey_initKey(&cryptoKey, g_keyMaterial, sizeof(g_keyMaterial));

    AESCCM_Handle handle = AESCCM_open(0, NULL);
    if (handle == NULL) {
        return -1;
    }

    AESCCM_OneStepOperation op;
    AESCCM_OneStepOperation_init(&op);
    op.key            = &cryptoKey;
    op.nonce          = g_iv;          /* hardcoded IV — SEC-003 */
    op.nonceLength    = sizeof(g_iv);
    op.input          = plaintext;
    op.inputLength    = plaintextLen;
    op.output         = g_cipherOut;
    op.mac            = g_tag;
    op.macLength      = sizeof(g_tag);

    /* BUG SEC-001: g_keyMaterial not zeroized after this call */
    int ret = AESCCM_oneStepEncrypt(handle, &op);   /* line 36 */

    /* cipherOut is consumed here and zeroized — SEC-005 does not apply */
    transmitOverBLE(g_cipherOut, plaintextLen + sizeof(g_tag));
    CryptoUtils_memset(g_cipherOut, 0, sizeof(g_cipherOut));

    AESCCM_close(handle);

    /* g_keyMaterial still contains key bytes — never cleared */
    return ret;
}
