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

/* Initialize crypto context from room password */
void crypto_init_from_password(crypto_ctx_t *ctx, const char *password);

/* Encrypt plaintext data
 * Returns encrypted data length (same as input), or 0 on error
 * Output buffer must have at least len + CRYPTO_HMAC_SIZE bytes
 */
int crypto_encrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *plaintext, uint8_t *ciphertext, int len);

/* Decrypt ciphertext data
 * Returns decrypted data length, or 0 on error/verification failure
 * Input buffer must have len bytes (including CRYPTO_HMAC_SIZE trailing bytes)
 * Output buffer must have at least len bytes
 */
int crypto_decrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *ciphertext, uint8_t *plaintext, int len);

/* Check if crypto is initialized */
int crypto_is_ready(crypto_ctx_t *ctx);

#endif
