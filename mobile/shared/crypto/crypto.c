/**
 * AES-128-CTR encryption with XOR-based HMAC.
 * Portable version - uses embedded SHA-256 instead of Windows CryptoAPI.
 */
#include "crypto.h"
#include "tiny_aes.h"
#include "sha256.h"
#include <string.h>

void crypto_init_from_password(crypto_ctx_t *ctx, const char *password) {
    uint8_t hash[32];
    memset(ctx, 0, sizeof(*ctx));

    /* SHA-256 hash of password */
    sha256((const uint8_t *)password, strlen(password), hash);

    /* First 16 bytes -> AES key */
    memcpy(ctx->key, hash, CRYPTO_KEY_SIZE);
    /* Next 8 bytes -> base nonce */
    memcpy(ctx->base_nonce, hash + 16, CRYPTO_NONCE_SIZE);

    ctx->initialized = 1;
    memset(hash, 0, sizeof(hash));
}

int crypto_encrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *plaintext, uint8_t *ciphertext, int len) {
    if (!ctx->initialized || len <= 0) return 0;

    /* Build nonce: base_nonce XOR seq */
    uint8_t nonce[16];
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, ctx->base_nonce, CRYPTO_NONCE_SIZE);
    nonce[0] ^= (seq & 0xFF);
    nonce[1] ^= ((seq >> 8) & 0xFF);

    /* AES-128-CTR encrypt */
    struct AES_ctx aes;
    AES_init_ctx_iv(&aes, ctx->key, nonce);
    memcpy(ciphertext, plaintext, len);
    AES_CTR_xcrypt_buffer(&aes, ciphertext, len);

    /* XOR-based HMAC */
    uint32_t mac = 0;
    for (int i = 0; i < len; i++) {
        mac ^= (uint32_t)ciphertext[i] << ((i % 4) * 8);
    }
    mac ^= seq;
    memcpy(ciphertext + len, &mac, CRYPTO_HMAC_SIZE);

    return len;
}

int crypto_decrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *ciphertext, uint8_t *plaintext, int len) {
    if (!ctx->initialized || len < CRYPTO_HMAC_SIZE) return 0;

    int data_len = len - CRYPTO_HMAC_SIZE;

    /* Verify HMAC */
    uint32_t received_mac, computed_mac = 0;
    memcpy(&received_mac, ciphertext + data_len, CRYPTO_HMAC_SIZE);

    for (int i = 0; i < data_len; i++) {
        computed_mac ^= (uint32_t)ciphertext[i] << ((i % 4) * 8);
    }
    computed_mac ^= seq;

    if (received_mac != computed_mac) return 0;

    /* Build nonce */
    uint8_t nonce[16];
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, ctx->base_nonce, CRYPTO_NONCE_SIZE);
    nonce[0] ^= (seq & 0xFF);
    nonce[1] ^= ((seq >> 8) & 0xFF);

    /* AES-128-CTR decrypt */
    struct AES_ctx aes;
    AES_init_ctx_iv(&aes, ctx->key, nonce);
    memcpy(plaintext, ciphertext, data_len);
    AES_CTR_xcrypt_buffer(&aes, plaintext, data_len);

    return data_len;
}

int crypto_is_ready(crypto_ctx_t *ctx) {
    return ctx->initialized;
}
