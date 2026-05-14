#include "crypto.h"
#include "tiny_aes.h"
#include <string.h>
#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

/* SHA-256 hash using Windows CryptoAPI */
static int sha256(const char *input, uint8_t *output) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    DWORD hash_len = 32;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return 0;

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return 0;
    }

    if (!CryptHashData(hHash, (const BYTE*)input, (DWORD)strlen(input), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return 0;
    }

    if (!CryptGetHashParam(hHash, HP_HASHVAL, output, &hash_len, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return 0;
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return 1;
}

void crypto_init_from_password(crypto_ctx_t *ctx, const char *password) {
    uint8_t hash[32];
    memset(ctx, 0, sizeof(*ctx));

    if (!sha256(password, hash)) return;

    /* Use first 16 bytes as AES key */
    memcpy(ctx->key, hash, CRYPTO_KEY_SIZE);

    /* Use next 8 bytes as base nonce */
    memcpy(ctx->base_nonce, hash + 16, CRYPTO_NONCE_SIZE);

    ctx->initialized = 1;

    /* Clear hash from memory */
    memset(hash, 0, sizeof(hash));
}

int crypto_encrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *plaintext, uint8_t *ciphertext, int len) {
    if (!ctx->initialized || len <= 0) return 0;

    /* Build nonce: base_nonce XOR seq (in little-endian) */
    uint8_t nonce[16];  /* AES block size */
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, ctx->base_nonce, CRYPTO_NONCE_SIZE);
    nonce[0] ^= (seq & 0xFF);
    nonce[1] ^= ((seq >> 8) & 0xFF);

    /* Encrypt using AES-128-CTR */
    struct AES_ctx aes;
    AES_init_ctx_iv(&aes, ctx->key, nonce);
    memcpy(ciphertext, plaintext, len);
    AES_CTR_xcrypt_buffer(&aes, ciphertext, len);

    /* Append simple HMAC (XOR-based MAC) */
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

    if (received_mac != computed_mac) return 0;  /* HMAC verification failed */

    /* Build nonce */
    uint8_t nonce[16];
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, ctx->base_nonce, CRYPTO_NONCE_SIZE);
    nonce[0] ^= (seq & 0xFF);
    nonce[1] ^= ((seq >> 8) & 0xFF);

    /* Decrypt using AES-128-CTR */
    struct AES_ctx aes;
    AES_init_ctx_iv(&aes, ctx->key, nonce);
    memcpy(plaintext, ciphertext, data_len);
    AES_CTR_xcrypt_buffer(&aes, plaintext, data_len);

    return data_len;
}

int crypto_is_ready(crypto_ctx_t *ctx) {
    return ctx->initialized;
}
