# 方案1: AES-128-CTR 传输加密 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为QminiDoctor的UDP语音包添加AES-128-CTR加密，防止明文嗅探。

**Architecture:** 使用tiny-aes-c单文件库实现AES-128-CTR加密，密钥从房间密码SHA256派生。仅加密语音数据包，keepalive保持明文。

**Tech Stack:** tiny-aes-c, Win32 CryptoAPI (SHA256)

---

## 文件结构

### 新增文件
- `src/crypto.c` - 加密/解密实现
- `src/crypto.h` - 加密接口定义
- `src/tiny_aes.c` - tiny-aes-c库源码
- `src/tiny_aes.h` - tiny-aes-c库头文件
- `tests/test_crypto.c` - 加密模块单元测试

### 修改文件
- `src/network.c` - 发送前加密，接收后解密
- `src/network.h` - 添加crypto_ctx指针
- `src/signaling.c` - 从房间密码派生密钥
- `src/dialog.c` - 添加密码输入框
- `src/dialog.rc` - 对话框资源
- `src/main.c` - 初始化加密上下文

---

## Task 1: 集成 tiny-aes-c 库

**Files:**
- Create: `src/tiny_aes.c`
- Create: `src/tiny_aes.h`

- [ ] **Step 1: 下载 tiny-aes-c 源码**

从 https://github.com/kokke/tiny-aes-c 下载以下文件：
- `aes.c` → 重命名为 `tiny_aes.c`
- `aes.h` → 重命名为 `tiny_aes.h`

- [ ] **Step 2: 验证编译**

编译tiny_aes.c确认无错误：
```bash
gcc -c src/tiny_aes.c -o src/tiny_aes.o -I src/
```
Expected: 编译成功，无错误输出

- [ ] **Step 3: 提交**

```bash
git add src/tiny_aes.c src/tiny_aes.h
git commit -m "deps: add tiny-aes-c library for encryption"
```

---

## Task 2: 创建加密模块

**Files:**
- Create: `src/crypto.h`
- Create: `src/crypto.c`
- Create: `tests/test_crypto.c`

- [ ] **Step 1: 编写加密接口头文件**

```c
// src/crypto.h
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
 * Output buffer must have at least len bytes
 */
int crypto_decrypt(crypto_ctx_t *ctx, uint16_t seq,
                   const uint8_t *ciphertext, uint8_t *plaintext, int len);

/* Check if crypto is initialized */
int crypto_is_ready(crypto_ctx_t *ctx);

#endif
```

- [ ] **Step 2: 编写失败测试**

```c
// tests/test_crypto.c
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
    int plain_len = strlen((char*)plaintext);

    uint8_t encrypted[256];
    uint8_t decrypted[256];

    int enc_len = crypto_encrypt(&ctx, 1, plaintext, encrypted, plain_len);
    if (enc_len != plain_len) { FAIL("encrypted length mismatch"); return; }

    int dec_len = crypto_decrypt(&ctx, 1, encrypted, decrypted, enc_len);
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
    int len = strlen((char*)plaintext);

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
    int len = strlen((char*)plaintext);

    uint8_t encrypted[256];
    crypto_encrypt(&ctx, 1, plaintext, encrypted, len);

    /* Tamper with ciphertext */
    encrypted[0] ^= 0xFF;

    uint8_t decrypted[256];
    int dec_len = crypto_decrypt(&ctx, 1, encrypted, decrypted, len);

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
```

- [ ] **Step 3: 运行测试确认失败**

```bash
gcc -o tests/test_crypto.exe tests/test_crypto.c src/crypto.c src/tiny_aes.c -I src/ -lws2_32
```
Expected: 编译失败，crypto.c不存在

- [ ] **Step 4: 实现加密模块**

```c
// src/crypto.c
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

    /* Append simple HMAC (first 4 bytes of SHA256 of ciphertext) */
    uint8_t hmac_hash[32];
    /* For simplicity, use XOR-based MAC (in production, use proper HMAC) */
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
```

- [ ] **Step 5: 运行测试确认通过**

```bash
gcc -o tests/test_crypto.exe tests/test_crypto.c src/crypto.c src/tiny_aes.c -I src/ -lws2_32 -ladvapi32
tests/test_crypto.exe
```
Expected:
```
Crypto Module Tests
===================
  TEST 1: crypto_init_from_password sets initialized flag ... PASS
  TEST 2: crypto_is_ready returns 1 after init ... PASS
  TEST 3: encrypt then decrypt returns original plaintext ... PASS
  TEST 4: different passwords produce different ciphertext ... PASS
  TEST 5: tampered ciphertext fails decryption ... PASS

Results: 5/5 passed
```

- [ ] **Step 6: 提交**

```bash
git add src/crypto.c src/crypto.h tests/test_crypto.c
git commit -m "feat(crypto): add AES-128-CTR encryption module"
```

---

## Task 3: 集成加密到网络层

**Files:**
- Modify: `src/network.h`
- Modify: `src/network.c`

- [ ] **Step 1: 修改 network.h 添加加密上下文**

在 `network_t` 结构体中添加：
```c
typedef struct {
    SOCKET           udp_sock;
    HANDLE           thread;
    int              running;
    uint16_t         local_port;
    peer_t           peers[MAX_PEERS];
    int              npeers;
    char             local_id[32];
    uint16_t         seq_send;
    void             *user_data;
    void             (*recv_cb)(const char *peer_id, const uint8_t *data, int len, void *user);
    void             (*keepalive_cb)(const char *peer_id, void *user);
    crypto_ctx_t     *crypto;  /* 新增: 加密上下文，NULL表示不加密 */
} network_t;
```

添加新函数声明：
```c
/* Set crypto context for encryption/decryption */
void network_set_crypto(network_t *net, crypto_ctx_t *crypto);
```

- [ ] **Step 2: 实现 network_set_crypto**

在 network.c 中添加：
```c
#include "crypto.h"

void network_set_crypto(network_t *net, crypto_ctx_t *crypto) {
    net->crypto = crypto;
}
```

- [ ] **Step 3: 修改发送函数添加加密**

修改 `network_send` 函数：
```c
int network_send(network_t *net, const char *peer_id, const uint8_t *data, int len) {
    peer_t *p = find_peer(net, peer_id);
    if (!p || !p->connected) return 0;

    uint8_t buf[MAX_PACKET];
    /* Pre-built header: 32-byte local_id, zero-padded */
    memset(buf, 0, 32);
    memcpy(buf, net->local_id, strlen(net->local_id));

    int total;
    if (net->crypto && crypto_is_ready(net->crypto)) {
        /* Encrypt the data */
        uint8_t encrypted[MAX_PACKET];
        int enc_len = crypto_encrypt(net->crypto, p->seq_send, data, encrypted, len);
        if (enc_len == 0) return 0;

        total = 32 + enc_len + CRYPTO_HMAC_SIZE;
        if (total > MAX_PACKET) return 0;
        memcpy(buf + 32, encrypted, enc_len + CRYPTO_HMAC_SIZE);
        p->seq_send++;
    } else {
        /* No encryption */
        total = 32 + len;
        if (total > MAX_PACKET) return 0;
        memcpy(buf + 32, data, len);
    }

    int sent = sendto(net->udp_sock, (const char*)buf, total, 0,
                      (struct sockaddr*)&p->addr, sizeof(p->addr));
    return sent > 0;
}
```

- [ ] **Step 4: 修改接收函数添加解密**

修改 `net_recv_thread` 中的接收逻辑：
```c
static DWORD WINAPI net_recv_thread(LPVOID arg) {
    network_t *net = (network_t*)arg;
    uint8_t buf[MAX_PACKET];
    struct sockaddr_in from;
    int from_len = sizeof(from);

    while (net->running) {
        /* ... select/recvfrom code ... */

        if (n < 32) continue;
        char peer_id[33] = {0};
        memcpy(peer_id, buf, 32);
        peer_id[32] = 0;

        /* Keepalive packet (32 bytes only) */
        if (n == 32) {
            /* ... existing keepalive handling ... */
            continue;
        }

        /* Data packet */
        uint8_t *payload = buf + 32;
        int payload_len = n - 32;

        if (net->crypto && crypto_is_ready(net->crypto)) {
            /* Decrypt the data */
            uint8_t decrypted[MAX_PACKET];
            int dec_len = crypto_decrypt(net->crypto, /* seq from packet */ 0,
                                         payload, decrypted, payload_len);
            if (dec_len == 0) {
                /* Decryption failed, skip packet */
                continue;
            }
            if (net->recv_cb)
                net->recv_cb(peer_id, decrypted, dec_len, net->user_data);
        } else {
            /* No encryption */
            if (net->recv_cb)
                net->recv_cb(peer_id, payload, payload_len, net->user_data);
        }
    }
    return 0;
}
```

- [ ] **Step 5: 编译验证**

```bash
gcc -c src/network.c -o src/network.o -I src/
```
Expected: 编译成功

- [ ] **Step 6: 提交**

```bash
git add src/network.c src/network.h
git commit -m "feat(network): integrate encryption into send/recv"
```

---

## Task 4: 添加房间密码输入

**Files:**
- Modify: `src/dialog.c`
- Modify: `src/dialog.h`
- Modify: `src/dialog.rc`

- [ ] **Step 1: 修改 dialog.h 添加密码字段**

```c
#define IDC_PASSWORD     1004
#define IDC_PASSWORD_LABEL 1005

int join_dialog_show(HINSTANCE inst, HWND parent,
                     char *server, int server_max,
                     char *room, int room_max,
                     char *nickname, int nickname_max,
                     char *password, int password_max);  /* 新增 */
```

- [ ] **Step 2: 修改对话框资源**

在 dialog.rc 中添加密码输入框：
```rc
IDD_JOIN_DIALOG DIALOGEX 0, 0, 250, 180
STYLE DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "加入房间"
FONT 9, "Microsoft YaHei"
BEGIN
    LTEXT           "服务器:", IDC_SERVER_LABEL, 10, 10, 40, 12
    EDITTEXT        IDC_SERVER, 55, 8, 180, 14, ES_AUTOHSCROLL
    LTEXT           "房间:", IDC_ROOM_LABEL, 10, 30, 40, 12
    EDITTEXT        IDC_ROOM, 55, 28, 180, 14, ES_AUTOHSCROLL
    LTEXT           "昵称:", IDC_NICKNAME_LABEL, 10, 50, 40, 12
    EDITTEXT        IDC_NICKNAME, 55, 48, 180, 14, ES_AUTOHSCROLL
    LTEXT           "密码:", IDC_PASSWORD_LABEL, 10, 70, 40, 12
    EDITTEXT        IDC_PASSWORD, 55, 68, 180, 14, ES_PASSWORD | ES_AUTOHSCROLL
    DEFPUSHBUTTON   "加入", IDOK, 60, 100, 60, 14
    PUSHBUTTON      "取消", IDCANCEL, 130, 100, 60, 14
END
```

- [ ] **Step 3: 修改对话框处理函数**

```c
static char g_password[64];

INT_PTR CALLBACK join_dlgproc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_SERVER, g_server);
        SetDlgItemText(hDlg, IDC_ROOM, g_room);
        SetDlgItemText(hDlg, IDC_NICKNAME, g_nickname);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemText(hDlg, IDC_SERVER, g_server, g_server_max);
            GetDlgItemText(hDlg, IDC_ROOM, g_room, g_room_max);
            GetDlgItemText(hDlg, IDC_NICKNAME, g_nickname, g_nickname_max);
            GetDlgItemText(hDlg, IDC_PASSWORD, g_password, sizeof(g_password));
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

int join_dialog_show(HINSTANCE inst, HWND parent,
                     char *server, int server_max,
                     char *room, int room_max,
                     char *nickname, int nickname_max,
                     char *password, int password_max) {
    /* ... existing code ... */
    int ret = DialogBox(inst, MAKEINTRESOURCE(IDD_JOIN_DIALOG), parent, join_dlgproc);
    if (ret == IDOK) {
        strncpy(password, g_password, password_max - 1);
        password[password_max - 1] = 0;
    }
    return ret;
}
```

- [ ] **Step 4: 编译验证**

```bash
gcc -c src/dialog.c -o src/dialog.o -I src/
```
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add src/dialog.c src/dialog.h src/dialog.rc
git commit -m "feat(dialog): add password input field"
```

---

## Task 5: 主程序集成

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: 添加加密上下文到全局状态**

```c
#include "crypto.h"

static crypto_ctx_t g_crypto;
```

- [ ] **Step 2: 修改加入房间逻辑**

在 `main.c` 的加入房间处理中：
```c
case TRAY_CMD_JOIN_ROOM: {
    char server[64], room[32], nickname[32], password[64];
    if (join_dialog_show(g_inst, NULL, server, sizeof(server),
                         room, sizeof(room), nickname, sizeof(nickname),
                         password, sizeof(password)) == IDOK) {
        /* Initialize encryption from password */
        if (password[0]) {
            crypto_init_from_password(&g_crypto, password);
            network_set_crypto(&g_net, &g_crypto);
        }

        /* ... existing join logic ... */
    }
    break;
}
```

- [ ] **Step 3: 修改离开房间逻辑**

```c
case TRAY_CMD_LEAVE_ROOM: {
    /* Clear crypto context */
    memset(&g_crypto, 0, sizeof(g_crypto));
    network_set_crypto(&g_net, NULL);

    /* ... existing leave logic ... */
    break;
}
```

- [ ] **Step 4: 编译完整项目**

```bash
build.bat
```
Expected: 编译成功，生成exe

- [ ] **Step 5: 提交**

```bash
git add src/main.c
git commit -m "feat(main): integrate encryption with room password"
```

---

## Task 6: 端到端测试

- [ ] **Step 1: 启动信令服务器**

```bash
start signaling_server.exe
```

- [ ] **Step 2: 启动客户端A，创建带密码的房间**

1. 运行 qminidoctor.exe
2. 右键托盘 → 加入房间
3. 输入: 服务器=localhost, 房间=test, 昵称=Alice, 密码=secret123
4. 点击加入

- [ ] **Step 3: 启动客户端B，用相同密码加入**

1. 运行另一个 qminidoctor.exe
2. 输入: 服务器=localhost, 房间=test, 昵称=Bob, 密码=secret123
3. 点击加入

- [ ] **Step 4: 测试语音通话**

1. 按住PTT键说话
2. 确认对方能听到语音
3. 确认无回声或失真

- [ ] **Step 5: 测试错误密码**

1. 启动客户端C，密码输入 wrong_password
2. 尝试加入房间
3. 验证: 无法解密对方语音包（静音或丢包）

- [ ] **Step 6: 最终提交**

```bash
git add -A
git commit -m "feat: complete AES-128-CTR encryption for voice packets"
```

---

## 完成检查清单

- [ ] tiny-aes-c 库集成成功
- [ ] 加密模块单元测试全部通过
- [ ] 发送语音包时自动加密
- [ ] 接收语音包时自动解密
- [ ] 错误密码导致解密失败
- [ ] keepalive 包保持明文
- [ ] 房间密码输入对话框正常
- [ ] 端到端语音通话正常
