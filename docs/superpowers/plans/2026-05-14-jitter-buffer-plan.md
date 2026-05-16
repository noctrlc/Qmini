# 方案5: 自适应Jitter Buffer 实施计划

**Goal:** 根据网络抖动动态调整缓冲深度，平衡延迟和流畅度。

**Architecture:** 统计包到达间隔抖动，动态调整target_level。

---

## Task 1: 修改Jitter Buffer添加自适应

**Files:**
- Modify: `src/jitter_buffer.c/h`

- [ ] **Step 1: 更新头文件**

在 `jitter_buffer_t` 结构体中添加：
```c
typedef struct {
    uint8_t  *packets[JB_CAPACITY];
    int       sizes[JB_CAPACITY];
    uint16_t  seq_numbers[JB_CAPACITY];
    int       count;
    int       read_cursor;
    int       write_cursor;
    int       target_level;

    /* 自适应字段 */
    uint32_t  last_arrival_time;   /* 上次包到达时间 (ms) */
    uint32_t  jitter_avg;          /* 平均抖动 (ms) */
    uint32_t  jitter_variance;     /* 抖动方差 */
    uint32_t  last_adaptation;     /* 上次调整时间 */
    int       min_target;          /* 最小缓冲深度 */
    int       max_target;          /* 最大缓冲深度 */
} jitter_buffer_t;
```

- [ ] **Step 2: 更新初始化**

```c
void jitter_buffer_init(jitter_buffer_t *jb) {
    memset(jb, 0, sizeof(*jb));
    jb->target_level = 4;        /* 初始4包 */
    jb->min_target = 2;          /* 最小2包 */
    jb->max_target = 12;         /* 最大12包 */
    jb->last_arrival_time = 0;
    jb->jitter_avg = 20;         /* 初始估计20ms */
    jb->jitter_variance = 0;
    jb->last_adaptation = 0;
}
```

- [ ] **Step 3: 修改push添加抖动统计**

```c
void jitter_buffer_push(jitter_buffer_t *jb, const uint8_t *data, int size, uint16_t seq) {
    if (jb->count >= JB_CAPACITY) return;

    /* 统计到达间隔抖动 */
    uint32_t now = GetTickCount();
    if (jb->last_arrival_time > 0) {
        uint32_t interval = now - jb->last_arrival_time;
        /* 指数移动平均 */
        int32_t diff = (int32_t)interval - (int32_t)jb->jitter_avg;
        if (diff < 0) diff = -diff;
        jb->jitter_avg = (jb->jitter_avg * 7 + interval) / 8;
        jb->jitter_variance = (jb->jitter_variance * 7 + diff * diff) / 8;
    }
    jb->last_arrival_time = now;

    /* 自适应调整target_level (每秒检查一次) */
    if (now - jb->last_adaptation > 1000) {
        uint32_t jitter_stddev = (uint32_t)sqrt((double)jb->jitter_variance);
        /* 目标延迟 = 平均抖动 + 1.5倍标准差，转换为包数 */
        int target_ms = jb->jitter_avg + jitter_stddev + jitter_stddev / 2;
        int target_packets = target_ms / 20;  /* 假设20ms一包 */

        if (target_packets < jb->min_target) target_packets = jb->min_target;
        if (target_packets > jb->max_target) target_packets = jb->max_target;

        jb->target_level = target_packets;
        jb->last_adaptation = now;
    }

    /* 原有插入逻辑 */
    int idx = (jb->read_cursor + jb->count) & 0x7F;
    uint8_t *p = (uint8_t*)malloc(size);
    if (!p) return;
    memcpy(p, data, size);
    if (jb->packets[idx]) free(jb->packets[idx]);
    jb->packets[idx]      = p;
    jb->sizes[idx]        = size;
    jb->seq_numbers[idx]  = seq;
    jb->count++;
}
```

- [ ] **Step 4: 添加头文件依赖**

```c
#include <windows.h>  /* for GetTickCount() */
#include <math.h>     /* for sqrt() */
```

- [ ] **Step 5: 编译验证**

```bash
build.bat
```

- [ ] **Step 6: 提交**

```bash
git add src/jitter_buffer.c src/jitter_buffer.h
git commit -m "feat(jitter): add adaptive buffer depth based on network jitter"
```

---

## 完成检查清单

- [ ] 抖动统计添加成功
- [ ] 自适应调整逻辑实现
- [ ] 编译成功
