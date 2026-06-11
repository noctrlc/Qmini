/**
 * AES-128-CTR encryption with HMAC.
 * Portable version - uses embedded SHA-256 instead of Windows CryptoAPI.
 */
#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

#define CRYPTO_NONCE_SIZE  8
#define CRYPTO_HMAC_SIZE   4
#define CRYPTO_KEY_SIZE    16  /* AES-128 */

typedef struct {
    uint8_t key[CRYPTO_KEY_SIZE];
    uint8_t base_nonce[CRYPTO_NONCE_SIZE];
    int     initialized;
} crypto_ctx_t;

void crypto_init_from_password(crypto_ctx_t *ctx, const char *password);

int crypto_encrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *plaintext, uint8_t *ciphertext, int len);

int crypto_decrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *ciphertext, uint8_t *plaintext, int len);

int crypto_is_ready(crypto_ctx_t *ctx);

#endif
