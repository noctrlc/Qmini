# QminiDoctor HUD 面板 — Direct2D 渲染方案

日期: 2026-05-16
状态: 设计稿 (待实施)

## 背景

Win32 标准控件 (BUTTON/STATIC/EDIT/LISTBOX) 无法实现暗色主题的自定义外观。控件背景色、字体颜色、边框样式均由系统主题控制，WM_CTLCOLOR 系列消息对 BUTTON 和 EDIT 控件无效。要达到设计稿中的暗色 HUD 效果，需要自行渲染所有 UI 元素。

## 技术选型：Direct2D + DirectWrite

| 方案 | 内存 | 性能 | 开发量 | 效果 |
|------|------|------|--------|------|
| **Direct2D + DirectWrite** | +15 KB | 硬件加速, <1% GPU | ~500行 | 像素级控制, 完美暗色 |
| GDI+ | +10 KB | CPU 渲染, ~2% CPU | ~600行 | 可暗色, 无动画 |
| WebView2 | +50 MB | 独立进程 | ~300行 | HTML/CSS 任意设计 |
| raw GDI | +5 KB | CPU 渲染 | ~800行 | 可暗色, 代码量大 |

**选择 Direct2D**：内存增加极少 (~15 KB)，硬件加速零 CPU 开销，完美支持透明度/圆角/渐变。

## 架构

```
panel.c → 保留 API (panel_create / panel_set_* 接口不变)
       → 内部使用 Direct2D 渲染所有控件
       → 自行处理鼠标点击/悬停命中测试
       → 通过 WM_PAINT 触发全量重绘

依赖:
  d2d1.h       Direct2D 1.0 (Windows 7+)
  dwrite.h     DirectWrite (文字渲染)
  dwmapi.h     DWM (毛玻璃/阴影效果, 可选)
```

## 内存预算

| 项目 | 大小 |
|------|------|
| ID2D1Factory (单例) | ~1 KB |
| ID2D1HwndRenderTarget | ~2 KB |
| IDWriteFactory + text format ×3 | ~2 KB |
| 画刷缓存 (6个纯色) | ~1 KB |
| 渲染代码 (~400行) | ~8 KB |
| **合计** | **~14 KB** |

不分配任何位图缓存 — Direct2D 直接渲染到窗口表面。

## 控件渲染

所有"控件"都是 panel.c 在 WM_PAINT 中绘制的矩形区域：

### 按钮

```
常态:   圆角矩形 + 文字居中
悬停:   背景微微变亮 (叠加白色 10% alpha)
按下:   背景略微变暗
选中:   绿色高亮 (说话模式) / 红色 (静音) / 橙色 (自由发言)
```

- 使用 `ID2D1RenderTarget::FillRoundedRectangle` 绘制圆角矩形
- 使用 `IDWriteTextLayout` 计算文字尺寸，居中放置
- 命中测试：在 WM_LBUTTONDOWN 中检查 (x,y) 是否在按钮矩形内

### 音量条

```
背景:   深灰矩形 (RGB 50,50,50)
前景:   绿色矩形 (RGB 0,255,0)，宽度 = peak_pct%
边界:   圆角 2px
```

### 成员列表

```
整体:   圆角矩形背景 (RGB 30,30,50)
每行:   说话状态圆点 (●/○ 6px直径) + 名字 + 音量指示
说话行: 背景微亮 (RGB 50,50,70)
```

### 展开/收拢动画

展开时窗口高度从 40 → 250 的简单线性过渡 (10ms × 8 步 = 80ms)。使用 `SetTimer` 触发每一步。

## 窗口管理

```
窗口样式:   WS_POPUP | WS_THICKFRAME
拖拽:       WM_NCHITTEST → HTCAPTION
置顶:       SetWindowPos(HWND_TOPMOST / HWND_NOTOPMOST)
关闭:       点击 ✕ 按钮 → WM_CLOSE
最小高度:   40px (折叠态)
最大高度:   250px (加入展开态)
```

## 输入处理

- **WM_LBUTTONDOWN**: 获取 (x,y)，遍历所有控件矩形，命中则触发对应 action
- **WM_MOUSEMOVE**: 更新悬停状态，`InvalidateRect` 重绘悬停变化的控件
- **WM_LBUTTONUP**: 清除按下状态
- **键盘输入**: 加入表单的 EDIT 控件使用系统 EDIT (唯一的标准控件)，其余文字用 DirectWrite 渲染

## 加入表单

服务器/房间/昵称/密码四个输入框仍使用 Win32 EDIT 控件 (CreateWindowEx "EDIT")。原因：输入法、复制粘贴、光标移动等需要系统支持，自行实现编辑框极其复杂。

EDIT 控件外观通过子类化 (SetWindowSubclass) 调整：
- 背景色设为暗色 (处理 WM_CTLCOLOREDIT)
- 边框通过 WS_EX_CLIENTEDGE 或自绘

## 实施步骤

### Step 1: 初始化 Direct2D

```c
// panel.c 新增全局
static ID2D1Factory        *g_d2d_factory = NULL;
static ID2D1HwndRenderTarget *g_rt = NULL;
static IDWriteFactory      *g_dwrite_factory = NULL;
static IDWriteTextFormat   *g_text_fmt = NULL;      // 正文 11px
static IDWriteTextFormat   *g_text_fmt_small = NULL; // 小字 9px
static ID2D1SolidColorBrush *g_brushes[8];           // 预缓存画刷

int d2d_init(HWND hwnd);    // CreateFactory + CreateHwndRenderTarget
void d2d_resize(HWND hwnd); // Resize render target
void d2d_cleanup();          // Release all COM objects
```

### Step 2: 定义控件矩形

```c
typedef struct { RECT rc; const wchar_t *text; int id; } button_t;
static button_t g_buttons[12];  // 所有"按钮"预计算位置
static int g_nbuttons = 0;

void layout_buttons(int panel_w, int mode); // 根据面板状态计算所有按钮位置
```

### Step 3: WM_PAINT 渲染

```c
case WM_PAINT:
    BeginPaint → ID2D1HwndRenderTarget::BeginDraw
    → rt->Clear(背景色)
    → 绘制顶栏 (品牌 + 状态 + Mlaiou)
    → 绘制按钮 (遍历 g_buttons)
    → 绘制成员列表 (展开态)
    → 绘制音量条
    → rt->EndDraw → EndPaint
```

### Step 4: 命中测试

```c
case WM_LBUTTONDOWN:
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    for (int i = 0; i < g_nbuttons; i++) {
        if (PtInRect(&g_buttons[i].rc, pt)) {
            // 触发 g_buttons[i].id 对应的操作
            break;
        }
    }
```

### Step 5: 扩展态动画

```c
case WM_TIMER:
    if (动画进行中) {
        高度 += step;
        SetWindowPos(...);
        layout_buttons(...);
        InvalidateRect(...);
        if (到达目标高度) KillTimer;
    }
```

## 与现有代码的兼容

- `panel.h` 完全不变 — main.c 无需任何修改
- `panel_create` → 额外调用 `d2d_init`
- `panel_destroy` → 额外调用 `d2d_cleanup`
- `panel_set_*` 函数设置状态 + `InvalidateRect` 触发重绘
- 唯一的 Win32 子控件：加入表单的 4 个 EDIT (通过 CreateWindowEx 创建)

## 性能评估

| 操作 | CPU | 备注 |
|------|-----|------|
| 常态渲染 (40px高) | < 0.1ms | 6 个按钮 + 文字 |
| 展开渲染 (250px高) | < 0.3ms | + 表单/成员列表 |
| 重绘频率 | 仅在状态变化时 | 无持续动画 |
| GPU 占用 | < 0.1% | 2D 填充/文字 |

## 风险与缓解

| 风险 | 缓解 |
|------|------|
| Direct2D 初始化失败 (罕见) | 回退到标准 Win32 面板 |
| EDIT 控件与自绘背景不协调 | EDIT 使用暗色背景子类化 |
| 中文字体渲染 | DirectWrite 原生支持雅黑 |
| 高 DPI 缩放 | D2D 使用 DIP (设备无关像素) |

## 文件变更

- **修改**: `src/panel.c` (~500行重写)
- **不修改**: `src/panel.h`, `src/main.c`, build.bat
- **不引入新文件**
- **新增链接库**: `d2d1.lib`, `dwrite.lib` (Windows SDK 自带)
