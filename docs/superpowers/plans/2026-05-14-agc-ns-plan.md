# 方案4: AGC+NS 噪声抑制 实施计划

**Goal:** 添加自动增益控制(AGC)和噪声抑制(NS)，统一音量并降噪。

**Architecture:** 简单的RMS检测+增益调整(AGC)和频域噪声抑制(NS)。

---

## Task 1: 创建AGC模块

**Files:**
- Create: `src/agc.c/h`

- [ ] **Step 1: 创建接口**

```c
// src/agc.h
#ifndef AGC_H
#define AGC_H

#include <stdint.h>

typedef struct agc_t agc_t;

agc_t* agc_create(int sample_rate, int target_level_dbfs);
void   agc_destroy(agc_t *agc);
void   agc_process(agc_t *agc, short *frame, int samples);

#endif
```

- [ ] **Step 2: 实现简单AGC**

```c
// src/agc.c
#include "agc.h"
#include <stdlib.h>
#include <math.h>

struct agc_t {
    float target_level;
    float current_gain;
    float attack_coeff;
    float release_coeff;
};

agc_t* agc_create(int sample_rate, int target_level_dbfs) {
    (void)sample_rate;
    agc_t *agc = (agc_t*)calloc(1, sizeof(agc_t));
    if (!agc) return NULL;

    /* Convert dBFS to linear */
    agc->target_level = powf(10.0f, target_level_dbfs / 20.0f) * 32768.0f;
    agc->current_gain = 1.0f;
    agc->attack_coeff = 0.1f;   /* Fast attack */
    agc->release_coeff = 0.01f; /* Slow release */

    return agc;
}

void agc_destroy(agc_t *agc) {
    free(agc);
}

void agc_process(agc_t *agc, short *frame, int samples) {
    /* Calculate RMS level */
    float sum_sq = 0;
    for (int i = 0; i < samples; i++) {
        float s = (float)frame[i];
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / samples);

    if (rms < 1.0f) return;  /* Too quiet, skip */

    /* Calculate desired gain */
    float desired_gain = agc->target_level / rms;

    /* Limit gain range */
    if (desired_gain > 30.0f) desired_gain = 30.0f;
    if (desired_gain < 0.1f) desired_gain = 0.1f;

    /* Smooth gain transition */
    if (desired_gain < agc->current_gain) {
        /* Attack (reduce gain quickly) */
        agc->current_gain += agc->attack_coeff * (desired_gain - agc->current_gain);
    } else {
        /* Release (increase gain slowly) */
        agc->current_gain += agc->release_coeff * (desired_gain - agc->current_gain);
    }

    /* Apply gain with limiting */
    for (int i = 0; i < samples; i++) {
        float s = (float)frame[i] * agc->current_gain;
        if (s > 32767.0f) s = 32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        frame[i] = (short)s;
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add src/agc.c src/agc.h
git commit -m "feat(agc): add simple AGC module"
```

---

## Task 2: 创建NS模块

**Files:**
- Create: `src/ns.c/h`

- [ ] **Step 1: 创建接口**

```c
// src/ns.h
#ifndef NS_H
#define NS_H

#include <stdint.h>

typedef struct ns_t ns_t;

ns_t* ns_create(int sample_rate);
void  ns_destroy(ns_t *ns);
void  ns_process(ns_t *ns, short *frame, int samples);

#endif
```

- [ ] **Step 2: 实现简单NS**

```c
// src/ns.c
#include "ns.h"
#include <stdlib.h>
#include <math.h>

#define NS_FRAME_SIZE 256

struct ns_t {
    float noise_level[NS_FRAME_SIZE / 2 + 1];  /* Noise estimate per bin */
    float smoothing;
    int   initialized;
    int   frame_count;
};

ns_t* ns_create(int sample_rate) {
    (void)sample_rate;
    ns_t *ns = (ns_t*)calloc(1, sizeof(ns_t));
    if (!ns) return NULL;

    ns->smoothing = 0.95f;
    ns->frame_count = 0;

    return ns;
}

void ns_destroy(ns_t *ns) {
    free(ns);
}

/* Simple FFT (radix-2, in-place) */
static void simple_fft(float *real, float *imag, int n) {
    /* Bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float tmp = real[i]; real[i] = real[j]; real[j] = tmp;
            tmp = imag[i]; imag[i] = imag[j]; imag[j] = tmp;
        }
    }

    /* FFT */
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * 3.14159265f / len;
        float w_real = cosf(angle);
        float w_imag = sinf(angle);

        for (int i = 0; i < n; i += len) {
            float cur_real = 1.0f, cur_imag = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                float u_real = real[i + j];
                float u_imag = imag[i + j];
                float v_real = real[i + j + len/2] * cur_real - imag[i + j + len/2] * cur_imag;
                float v_imag = real[i + j + len/2] * cur_imag + imag[i + j + len/2] * cur_real;

                real[i + j] = u_real + v_real;
                imag[i + j] = u_imag + v_imag;
                real[i + j + len/2] = u_real - v_real;
                imag[i + j + len/2] = u_imag - v_imag;

                float new_real = cur_real * w_real - cur_imag * w_imag;
                cur_imag = cur_real * w_imag + cur_imag * w_real;
                cur_real = new_real;
            }
        }
    }
}

void ns_process(ns_t *ns, short *frame, int samples) {
    if (samples < NS_FRAME_SIZE) return;

    float real[NS_FRAME_SIZE], imag[NS_FRAME_SIZE];

    /* Window and FFT */
    for (int i = 0; i < NS_FRAME_SIZE; i++) {
        /* Hann window */
        float w = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / NS_FRAME_SIZE));
        real[i] = (float)frame[i] * w;
        imag[i] = 0.0f;
    }

    simple_fft(real, imag, NS_FRAME_SIZE);

    /* Process each frequency bin */
    int num_bins = NS_FRAME_SIZE / 2 + 1;
    for (int i = 0; i < num_bins; i++) {
        float magnitude = sqrtf(real[i] * real[i] + imag[i] * imag[i]);

        /* Initialize noise estimate */
        if (!ns->initialized || ns->frame_count < 20) {
            ns->noise_level[i] = magnitude;
            continue;
        }

        /* Update noise estimate (slow adaptation) */
        if (magnitude < ns->noise_level[i]) {
            ns->noise_level[i] = ns->smoothing * ns->noise_level[i] + (1 - ns->smoothing) * magnitude;
        }

        /* Spectral subtraction */
        float gain = 1.0f;
        if (ns->noise_level[i] > 0) {
            gain = (magnitude - ns->noise_level[i]) / magnitude;
            if (gain < 0.0f) gain = 0.0f;
            if (gain > 1.0f) gain = 1.0f;
        }

        real[i] *= gain;
        imag[i] *= gain;
        if (i > 0 && i < num_bins - 1) {
            real[NS_FRAME_SIZE - i] *= gain;
            imag[NS_FRAME_SIZE - i] *= gain;
        }
    }

    if (!ns->initialized) ns->initialized = 1;
    ns->frame_count++;

    /* IFFT */
    for (int i = 0; i < NS_FRAME_SIZE; i++)
        imag[i] = -imag[i];
    simple_fft(real, imag, NS_FRAME_SIZE);
    for (int i = 0; i < NS_FRAME_SIZE; i++)
        real[i] /= NS_FRAME_SIZE;

    /* Overlap-add */
    for (int i = 0; i < samples && i < NS_FRAME_SIZE; i++) {
        float s = real[i];
        if (s > 32767.0f) s = 32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        frame[i] = (short)s;
    }
}
```

- [ ] **Step 3: 提交**

```bash
git add src/ns.c src/ns.h
git commit -m "feat(ns): add spectral subtraction noise suppressor"
```

---

## Task 3: 集成到音频管线

**Files:**
- Modify: `src/audio_capture.c`
- Modify: `src/main.c`
- Modify: `build.bat`

- [ ] **Step 1: 在main.c添加AGC和NS实例**

```c
#include "agc.h"
#include "ns.h"

static agc_t *g_agc = NULL;
static ns_t  *g_ns = NULL;
```

- [ ] **Step 2: 初始化和销毁**

```c
// 初始化 (在audio_capture_start之后)
g_ns = ns_create(16000);
g_agc = agc_create(16000, -20);

// 销毁 (在audio_capture_stop之前)
if (g_agc) { agc_destroy(g_agc); g_agc = NULL; }
if (g_ns) { ns_destroy(g_ns); g_ns = NULL; }
```

- [ ] **Step 3: 在audio_capture.c处理**

在wavein_cb中，AEC处理之后：
```c
extern agc_t *g_agc;
extern ns_t  *g_ns;

// 在AEC处理之后
if (g_ns) ns_process(g_ns, cleaned, frames);
if (g_agc) agc_process(g_agc, cleaned, frames);
```

- [ ] **Step 4: 编译验证**

```bash
build.bat
```

- [ ] **Step 5: 提交**

```bash
git add src/audio_capture.c src/main.c build.bat
git commit -m "feat(audio): integrate AGC and NS into capture pipeline"
```

---

## 完成检查清单

- [ ] AGC模块创建成功
- [ ] NS模块创建成功
- [ ] 音频管线集成完成
- [ ] 编译成功
