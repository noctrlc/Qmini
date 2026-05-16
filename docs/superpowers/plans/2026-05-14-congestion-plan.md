# 方案3: 自适应码率 实施计划

**Goal:** 根据丢包率动态调整Opus码率，平衡音质和流畅度。

**Architecture:** 统计每秒丢包率，根据阈值调整码率。使用Opus动态码率API。

---

## Task 1: 创建拥塞控制模块

**Files:**
- Create: `src/congestion.c/h`

- [ ] **Step 1: 创建接口**

```c
// src/congestion.h
#ifndef CONGESTION_H
#define CONGESTION_H

#include <stdint.h>

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_acked;
    uint32_t last_loss_check;    /* tick count */
    int      current_bitrate;
    uint32_t last_bitrate_change;
} congestion_ctrl_t;

void congestion_init(congestion_ctrl_t *cc);
void congestion_update_sent(congestion_ctrl_t *cc);
void congestion_update_acked(congestion_ctrl_t *cc, uint16_t seq);
int  congestion_get_bitrate(congestion_ctrl_t *cc);

#endif
```

- [ ] **Step 2: 实现**

```c
// src/congestion.c
#include "congestion.h"
#include <windows.h>

#define MIN_BITRATE  16000
#define MAX_BITRATE  64000
#define CHECK_INTERVAL 1000  /* 1 second */
#define CHANGE_INTERVAL 2000 /* 2 seconds min between changes */

void congestion_init(congestion_ctrl_t *cc) {
    cc->packets_sent = 0;
    cc->packets_acked = 0;
    cc->last_loss_check = GetTickCount();
    cc->current_bitrate = 32000;  /* default */
    cc->last_bitrate_change = 0;
}

void congestion_update_sent(congestion_ctrl_t *cc) {
    cc->packets_sent++;
}

void congestion_update_acked(congestion_ctrl_t *cc, uint16_t seq) {
    (void)seq;
    cc->packets_acked++;
}

int congestion_get_bitrate(congestion_ctrl_t *cc) {
    DWORD now = GetTickCount();

    /* Check every second */
    if (now - cc->last_loss_check < CHECK_INTERVAL)
        return cc->current_bitrate;

    /* Enforce minimum interval between changes */
    if (now - cc->last_bitrate_change < CHANGE_INTERVAL) {
        cc->last_loss_check = now;
        return cc->current_bitrate;
    }

    /* Calculate loss rate */
    if (cc->packets_sent < 10) {
        cc->last_loss_check = now;
        return cc->current_bitrate;
    }

    float loss = 1.0f - (float)cc->packets_acked / cc->packets_sent;
    if (loss < 0) loss = 0;

    /* Adjust bitrate based on loss */
    int new_bitrate = cc->current_bitrate;
    if (loss < 0.02f) {
        new_bitrate = 64000;
    } else if (loss < 0.05f) {
        new_bitrate = 48000;
    } else if (loss < 0.10f) {
        new_bitrate = 32000;
    } else {
        new_bitrate = 16000;
    }

    /* Apply change if different */
    if (new_bitrate != cc->current_bitrate) {
        cc->current_bitrate = new_bitrate;
        cc->last_bitrate_change = now;
    }

    /* Reset counters */
    cc->packets_sent = 0;
    cc->packets_acked = 0;
    cc->last_loss_check = now;

    return cc->current_bitrate;
}
```

- [ ] **Step 3: 提交**

```bash
git add src/congestion.c src/congestion.h
git commit -m "feat(congestion): add adaptive bitrate control module"
```

---

## Task 2: 集成到codec和main

**Files:**
- Modify: `src/codec.c/h` - 添加设置码率函数
- Modify: `src/main.c` - 初始化拥塞控制，定时调整
- Modify: `build.bat` - 添加编译

- [ ] **Step 1: 在codec.h添加设置码率函数**

```c
void codec_enc_set_bitrate(codec_enc_t *e, int bitrate);
```

- [ ] **Step 2: 在codec.c实现**

```c
void codec_enc_set_bitrate(codec_enc_t *e, int bitrate) {
    opus_encoder_ctl(e->enc, OPUS_SET_BITRATE(bitrate));
}
```

- [ ] **Step 3: 在main.c集成**

```c
#include "congestion.h"

static congestion_ctrl_t g_cc;

// 在初始化时
congestion_init(&g_cc);

// 在音频发送循环中
congestion_update_sent(&g_cc);
int bitrate = congestion_get_bitrate(&g_cc);
codec_enc_set_bitrate(g_enc, bitrate);
```

- [ ] **Step 4: 编译验证**

```bash
build.bat
```

- [ ] **Step 5: 提交**

```bash
git add src/codec.c src/codec.h src/main.c build.bat
git commit -m "feat(codec): integrate adaptive bitrate control"
```

---

## 完成检查清单

- [ ] 拥塞控制模块创建成功
- [ ] codec支持动态码率调整
- [ ] main.c集成拥塞控制
- [ ] 编译成功
