#include <stdio.h>
#include <string.h>
#include "../src/crypto.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %d: %s ... ", tests_run, name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

void test_crypto_init(void) {
    TEST("crypto_init_from_password sets initialized flag");
    crypto_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    crypto_init_from_password(&ctx, "test_room_password");
    if (ctx.initialized) PASS(); else FAIL("not initialized");

    TEST("crypto_is_ready returns 1 after init");
    if (crypto_is_ready(&ctx)) PASS(); else FAIL("not ready");
}

void test_crypto_encrypt_decrypt_roundtrip(void) {
    TEST("encrypt then decrypt returns original plaintext");
    crypto_ctx_t ctx;
    crypto_init_from_password(&ctx, "my_secret_password");

    const uint8_t plaintext[] = "Hello, QminiDoctor!";
    int plain_len = (int)strlen((char*)plaintext);

    uint8_t encrypted[256];
    uint8_t decrypted[256];

    int enc_len = crypto_encrypt(&ctx, 1, plaintext, encrypted, plain_len);
    if (enc_len != plain_len) { FAIL("encrypted length mismatch"); return; }

    int dec_len = crypto_decrypt(&ctx, 1, encrypted, decrypted, enc_len + CRYPTO_HMAC_SIZE);
    if (dec_len != plain_len) { FAIL("decrypted length mismatch"); return; }

    if (memcmp(plaintext, decrypted, plain_len) == 0) PASS();
    else FAIL("decrypted data differs");
}

void test_crypto_different_keys(void) {
    TEST("different passwords produce different ciphertext");
    crypto_ctx_t ctx1, ctx2;
    crypto_init_from_password(&ctx1, "password_one");
    crypto_init_from_password(&ctx2, "password_two");

    const uint8_t plaintext[] = "Same message";
    int len = (int)strlen((char*)plaintext);

    uint8_t enc1[256], enc2[256];
    crypto_encrypt(&ctx1, 1, plaintext, enc1, len);
    crypto_encrypt(&ctx2, 1, plaintext, enc2, len);

    if (memcmp(enc1, enc2, len) != 0) PASS();
    else FAIL("ciphertext should differ");
}

void test_crypto_tamper_detection(void) {
    TEST("tampered ciphertext fails decryption");
    crypto_ctx_t ctx;
    crypto_init_from_password(&ctx, "test_password");

    const uint8_t plaintext[] = "Authentic message";
    int len = (int)strlen((char*)plaintext);

    uint8_t encrypted[256];
    int enc_len = crypto_encrypt(&ctx, 1, plaintext, encrypted, len);

    /* Tamper with ciphertext */
    encrypted[0] ^= 0xFF;

    uint8_t decrypted[256];
    int dec_len = crypto_decrypt(&ctx, 1, encrypted, decrypted, enc_len + CRYPTO_HMAC_SIZE);

    if (dec_len == 0) PASS();  /* Should fail */
    else FAIL("should have detected tamper");
}

int main(void) {
    printf("Crypto Module Tests\n");
    printf("===================\n");

    test_crypto_init();
    test_crypto_encrypt_decrypt_roundtrip();
    test_crypto_different_keys();
    test_crypto_tamper_detection();

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
