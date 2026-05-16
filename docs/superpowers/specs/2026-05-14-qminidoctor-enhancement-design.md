# QminiDoctor 架构增强设计方案

日期: 2026-05-14

## 概述

QminiDoctor 是一个轻量级 P2P 语音通话应用，当前已完成基础功能。本文档描述6项架构增强方案的设计细节，旨在提升安全性、音频质量、网络适应性和扩展性。

## 当前架构状态

```
模块组成:
- main.c: 主控状态机 (IDLE → CONNECTING → IN_ROOM)
- signaling.c: WebSocket 信令 (房间管理/Peer交换)
- network.c: UDP P2P 通信 (多Peer/Keepalive/Relay)
- codec.c: Opus 编解码封装
- jitter_buffer.c: 抖动缓冲 (固定目标延迟)
- audio_capture.c: Windows 麦克风采集 (16kHz mono)
- audio_playback.c: 扬声器播放 + 多路混音
- panel.c: 主面板 UI (音量条/成员列表)
- tray.c: 系统托盘 + 右键菜单
- hotkey.c: 全局热键 (PTT/静音)
- config.c: 注册表配置
- dialog.c: 加入房间对话框

已知短板:
- 无传输加密 (明文UDP)
- 无回声消除 (AEC)
- 无自动增益 (AGC)
- 无噪声抑制 (NS)
- 固定码率 (无拥塞控制)
- 固定Jitter Buffer延迟
- Mesh P2P扩展性差 (>6人带宽压力大)
```

---

## 方案1: AES-128-CTR 传输加密

### 目标
防止语音包被网络嗅探，提供基础保密性。

### 设计

#### 密钥派生
```
房间密码 (用户输入)
    ↓
SHA-256
    ↓
AES-128 密钥 (16字节)
```

#### 加密包格式
```
┌──────────┬──────────┬─────────────────┬──────────┐
│ type(2B) │nonce(8B) │ 密文 (N bytes)  │ HMAC(4B) │
└──────────┴──────────┴─────────────────┴──────────┘

- type: 包类型 (明文，用于路由)
- nonce: 8字节随机数 + 序列号，确保唯一性
- 密文: AES-128-CTR 加密后的数据
- HMAC: HMAC-SHA256 截断4字节，完整性校验
```

#### 加密范围
- **加密**: 语音数据包 (type=1/2)
- **不加密**: keepalive (type=3/4)、信令

#### 实现改动
```
新增文件:
- src/crypto.c/h: AES-CTR加密/解密 + HMAC

修改文件:
- src/network.c: 发送前加密，接收后解密
- src/signaling.c: 从房间密码派生密钥
- src/dialog.c: 添加密码输入框

依赖:
- tiny-aes-c: 单文件AES实现 (tiny-aes.c + tiny-aes.h)
```

#### 代码结构
```c
// crypto.h
typedef struct {
    uint8_t key[16];        // AES-128密钥
    uint8_t nonce[8];       // 基础nonce
} crypto_ctx_t;

void crypto_init(crypto_ctx_t *ctx, const char *password);
int  crypto_encrypt(crypto_ctx_t *ctx, uint16_t seq,
                    const uint8_t *in, uint8_t *out, int len);
int  crypto_decrypt(crypto_ctx_t *ctx, uint16_t seq,
                    const uint8_t *in, uint8_t *out, int len);
```

---

## 方案2: AEC 回声消除 (WebRTC AEC3 精简版)

### 目标
消除扬声器声音被麦克风重新采集导致的回声。

### 设计

#### 架构
```
麦克风输入 ──┐
             ▼
         ┌───────┐     ┌──────────┐
         │ AEC3  │────▶│ Opus编码 │──▶ 发送
         │ 消除  │     └──────────┘
         └───┬───┘
             ▲
扬声器参考 ──┘ (从audio_playback获取)
```

#### 算法核心
```
1. 延迟估计: 扬声器→麦克风的系统延迟
2. 自适应滤波器: 模拟回声路径
3. 回声消除: y_clean = y_mic - H * x_ref
4. 残余回声抑制: 频域处理消除残余

参数:
- 采样率: 16kHz
- 帧长: 10ms (160 samples)
- 滤波器长度: 64ms (1024 taps)
- 延迟估计范围: 0-500ms
```

#### 实现改动
```
新增文件:
- src/aec.c/h: AEC3核心算法 (从WebRTC提取，约3000行)

修改文件:
- src/audio_capture.c: 采集后送AEC处理
- src/audio_playback.c: 提供参考信号给AEC

提取来源:
- webrtc/modules/audio_processing/aec3/
- 精简: 移除复杂度估计、多通道等非核心功能
```

#### 代码结构
```c
// aec.h
typedef struct aec3_t aec3_t;

aec3_t* aec3_create(int sample_rate);
void    aec3_destroy(aec3_t *aec);

// 处理一帧 (10ms = 160 samples @ 16kHz)
// near: 麦克风信号 (输入/输出，原地处理)
// far:  扬声器参考信号
void    aec3_process(aec3_t *aec, float *near, const float *far);
```

---

## 方案3: 基于丢包率的自适应码率

### 目标
根据网络状况动态调整Opus码率，平衡音质和流畅度。

### 设计

#### 算法
```
每秒统计:
- 发送包数
- 确认包数 (通过seq回绕)
- 丢包率 = (发送-确认)/发送

码率决策:
┌─────────────┬─────────────┐
│ 丢包率      │ 目标码率    │
├─────────────┼─────────────┤
│ < 2%        │ 64 kbps     │
│ 2% - 5%    │ 48 kbps     │
│ 5% - 10%   │ 32 kbps     │
│ > 10%      │ 16 kbps     │
└─────────────┴─────────────┘

保护机制:
- 最小调整间隔: 2秒
- 码率变化时渐进: 每次调整8kbps
```

#### 实现改动
```
新增文件:
- src/congestion.c/h: 丢包统计 + 码率决策

修改文件:
- src/codec.c: 添加 opus_encoder_set_bitrate() 接口
- src/network.c: 统计丢包率
- src/main.c: 定时调用拥塞检测
```

#### 代码结构
```c
// congestion.h
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_acked;
    uint32_t last_loss_check;
    int      current_bitrate;
    uint32_t last_bitrate_change;
} congestion_ctrl_t;

void congestion_init(congestion_ctrl_t *cc);
void congestion_update_sent(congestion_ctrl_t *cc);
void congestion_update_acked(congestion_ctrl_t *cc, uint16_t seq);
int  congestion_get_bitrate(congestion_ctrl_t *cc);
```

---

## 方案4: WebRTC AGC + NS 噪声抑制

### 目标
自动调整麦克风音量，抑制背景噪声。

### 设计

#### 音频处理管线
```
麦克风 ──▶ [NS噪声抑制] ──▶ [AGC自动增益] ──▶ [AEC] ──▶ Opus编码

处理顺序: NS → AGC → AEC (AEC在最后，保留参考信号对齐)
```

#### AGC 参数
```
- 目标电平: -20 dBFS
- 增益范围: 0 - 30 dB
- 压缩阈值: -30 dBFS
- 响应时间: 10ms
- 限制器: 启用 (防削波)
```

#### NS 参数
```
- 模型: WebRTC NS (轻量级，频域处理)
- 降噪强度: 中等 (保守，避免语音失真)
- 处理延迟: 10ms
- 采样率: 16kHz
```

#### 实现改动
```
新增文件:
- src/agc.c/h: WebRTC AGC提取 (约800行)
- src/ns.c/h: WebRTC NS提取 (约700行)

修改文件:
- src/audio_capture.c: 添加NS和AGC处理

提取来源:
- webrtc/modules/audio_processing/gain_control/
- webrtc/modules/audio_processing/ns/
```

#### 代码结构
```c
// agc.h
typedef struct agc_t agc_t;
agc_t* agc_create(int sample_rate, int target_level_dbfs);
void   agc_destroy(agc_t *agc);
void   agc_process(agc_t *agc, float *frame, int samples);

// ns.h
typedef struct ns_t ns_t;
ns_t* ns_create(int sample_rate);
void  ns_destroy(ns_t *ns);
void  ns_process(ns_t *ns, float *frame, int samples);
```

---

## 方案5: 自适应 Jitter Buffer

### 目标
根据网络抖动动态调整缓冲深度，平衡延迟和流畅度。

### 设计

#### 算法
```
持续统计:
- 包到达间隔 (inter-arrival jitter)
- 平均抖动 (moving average)
- 抖动标准差

目标延迟计算:
target_delay = avg_jitter + 1.5 * jitter_stddev

约束:
- 最小: 20ms (单帧)
- 最大: 200ms (防卡顿)
- 初始: 60ms

自适应策略:
┌─────────────────┬───────────────────┐
│ 缓冲状态        │ 处理方式          │
├─────────────────┼───────────────────┤
│ 欠缓冲 (< min)  │ PLC丢包补偿       │
│ 正常            │ 平滑播放          │
│ 过缓冲 (> max)  │ 跳帧加速          │
└─────────────────┴───────────────────┘

调整间隔: 1秒 (避免频繁抖动)
```

#### 实现改动
```
修改文件:
- src/jitter_buffer.c/h: 添加自适应逻辑
- 添加PLC (利用Opus内置 opus_decode 的PLC功能)
- 添加跳帧逻辑

关键指标统计:
- 包到达时间戳
- 播放时间戳
- 缓冲区水位
```

#### 代码结构
```c
// jitter_buffer.h 新增
typedef struct {
    // 原有字段...
    int       target_level;      // 目标缓冲深度 (动态)
    uint32_t  last_adjustment;   // 上次调整时间
    uint32_t  jitter_avg;        // 平均抖动 (us)
    uint32_t  jitter_stddev;     // 抖动标准差
    int       buffer_level;      // 当前缓冲区水位
} jitter_buffer_t;

// 新增自适应函数
void jitter_buffer_update_stats(jitter_buffer_t *jb, uint32_t arrival_time);
void jitter_buffer_adapt(jitter_buffer_t *jb);
```

---

## 方案6: SFU 媒体转发架构

### 目标
支持10-20人房间，超过6人自动切换到SFU模式。

### 设计

#### 架构
```
Mesh P2P (≤6人):          SFU模式 (>6人):
A ◄──► B                 A ──▶ ┌──────┐ ──▶ B
▲ ╲  ╱ ▲                 B ──▶ │ SFU  │ ──▶ C
│  ╲╱  │                 C ──▶ │ 服务器│ ──▶ A
│  ╱╲  │                 D ──▶ └──────┘ ──▶ D
▼ ╱  ╲ ▼
C ◄──► D
```

#### 自动切换逻辑
```c
if (room_members <= 6) {
    mode = MODE_MESH_P2P;
} else {
    mode = MODE_SFU;
    // 断开所有P2P连接
    // 连接到SFU服务器
}
```

#### SFU 信令扩展
```
新增信令消息:
- sfu_publish: 客户端发布音频流到SFU
- sfu_subscribe: 客户端订阅其他用户流
- sfu_unsubscribe: 取消订阅
- sfu_stream_list: SFU告知可用流列表

SFU数据流:
Client A ──UDP──▶ SFU Server ──UDP──▶ Client B
Client A ──UDP──▶ SFU Server ──UDP──▶ Client C
```

#### 服务器端改动
```
修改文件:
- signaling_server/signaling_server.c: 添加SFU转发逻辑
- signaling_server/signaling_server_linux.c: 同步改动

SFU转发逻辑:
- 维护 room -> {stream_id -> client_addr} 映射
- 收到音频包 → 查找同房间其他成员 → 转发
- 支持订阅/取消订阅 (选择性转发)
```

#### 客户端改动
```
新增文件:
- src/sfu_client.c/h: SFU客户端逻辑

修改文件:
- src/signaling.c: 处理SFU信令
- src/main.c: 自动切换逻辑
- src/network.c: 支持SFU单连接模式
```

---

## 实施顺序

### 阶段1: 安全基础
1. 方案1: 传输加密 ← 最高优先级

### 阶段2: 音频质量
2. 方案2: AEC回声消除
3. 方案4: AGC+NS噪声抑制

### 阶段3: 网络优化
4. 方案3: 自适应码率
5. 方案5: 自适应Jitter Buffer

### 阶段4: 扩展性
6. 方案6: SFU架构

---

## 依赖清单

| 方案 | 外部依赖 | 来源 | 集成方式 |
|------|----------|------|----------|
| 1. 加密 | tiny-aes-c | GitHub | 单文件添加 |
| 2. AEC | WebRTC AEC3 | WebRTC源码 | 代码提取 |
| 3. 拥塞 | 无 | - | - |
| 4. AGC+NS | WebRTC AGC/NS | WebRTC源码 | 代码提取 |
| 5. JB | 无 | - | - |
| 6. SFU | 无 | - | - |

## 风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| AEC延迟估计不准 | 回声残留 | 添加延迟校准机制 |
| 加密增加延迟 | 语音卡顿 | 硬件AES加速 |
| SFU服务器负载 | 性能下降 | 限制单房间人数 |
| WebRTC代码提取 | 编译问题 | 精简非必要模块 |
