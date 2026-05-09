/*
 * eval_suite/07_crypto_key_leak.c
 *
 * Platform: TI CC2652R7, SimpleLink SDK crypto drivers
 */

#include <stdint.h>
#include <stddef.h>
#include "ti/drivers/AESCCM.h"
#include "ti/drivers/CryptoKey.h"
#include "ti/drivers/cryptoutils/cryptoutils.h"

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
    op.nonce          = g_iv;
    op.nonceLength    = sizeof(g_iv);
    op.input          = plaintext;
    op.inputLength    = plaintextLen;
    op.output         = g_cipherOut;
    op.mac            = g_tag;
    op.macLength      = sizeof(g_tag);

    int ret = AESCCM_oneStepEncrypt(handle, &op);

    transmitOverBLE(g_cipherOut, plaintextLen + sizeof(g_tag));
    CryptoUtils_memset(g_cipherOut, 0, sizeof(g_cipherOut));
    CryptoUtils_memset(g_tag, 0, sizeof(g_tag));

    AESCCM_close(handle);

    return ret;
}
