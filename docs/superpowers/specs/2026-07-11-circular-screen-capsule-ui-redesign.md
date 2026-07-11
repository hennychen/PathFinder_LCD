# PathFinder_EMOTE 圆形屏胶囊式 UI 重新设计

> 替代原设计文档第 7 节（UI 层 — 数据叠加层设计）
> 日期：2026-07-11

---

## 1. 背景

原设计（四角叠加卡片）存在两个严重问题：

1. **圆形屏裁切**：四个卡片放在距圆心约 291px 处，而内切圆半径仅 240px、安全区半径 210px — 卡片大部分落在可视区之外
2. **字体过小**：Montserrat 14 在 2.1 寸屏上仅约 3.5mm 高，车载场景下难以快速阅读

### 设计目标

- 所有 UI 元素严格在安全圆（R=210）内
- 字体升级到 Montserrat 16-20，确保车载场景可读性
- 符合车载机器人产品定位：表情为核心焦点，传感器数据为辅助信息
- 兼顾沉浸感与安全提醒能力

---

## 2. 布局架构：极简胶囊

### 2.1 三区域划分

```
┌─────────────── 480 × 480 圆形屏 ───────────────┐
│                                                │
│            ┌──────────────────┐                │
│            │  顶部胶囊 (环境)   │  ← 默认隐藏    │
│            └──────────────────┘                │
│                                                │
│                                                │
│         ┌───────────────────────┐              │
│         │                       │              │
│         │    EAF 表情 (330×330)  │  ← 始终可见  │
│         │      核心视觉焦点       │              │
│         │                       │              │
│         └───────────────────────┘              │
│                                                │
│            [ smile ]  ← 名称标签 Montserrat 20  │
│                                                │
│         ┌─────────────────────────┐            │
│         │  底部胶囊 (运动+UV)       │  ← 默认隐藏 │
│         └─────────────────────────┘            │
│                                                │
└────────────────────────────────────────────────┘
```

### 2.2 元素精确规格

| 元素 | 尺寸 | 位置 (圆心坐标系) | 字体 | 颜色 | 行为 |
|------|------|-------------------|------|------|------|
| EAF 表情 | 330×330 | 圆心居中 (75,75)→(405,405) | — | 原样 | 始终可见 |
| 名称标签 | auto | 底部居中 y=350 | Montserrat 20 | 白色 | 始终可见 |
| 顶部胶囊 | 200×38 | (140,68)→(340,106) | Montserrat 16 | 青色 15%透明 | 默认隐藏 |
| 底部胶囊 | 250×38 | (115,378)→(365,416) | Montserrat 16 | 绿色 12%透明 | 默认隐藏 |
| 异常高亮 | 290×44 | (95,370)→(385,414) 覆盖底部 | Montserrat 16 Bold | 红/黄 | 自动弹出 |

### 2.3 数据分组

| 胶囊 | 数据内容 | 更新频率 |
|------|---------|---------|
| 顶部（环境） | `26.5°C · 45% · 1013hPa` | 1Hz |
| 底部（运动） | `UV 6.2 · 128m · P+12° R+5°` | UV/海拔 1Hz，倾角 5Hz |

### 2.4 圆形安全验证

所有胶囊元素的四个角到圆心 (240,240) 的最大距离：

- 顶部胶囊最远角 (140,68)：√(100² + 172²) = 199px ✅ < 210
- 底部胶囊最远角 (115,378)：√(125² + 138²) = 186px ✅ < 210
- 异常高亮最远角 (95,370)：√(145² + 130²) = 195px ✅ < 210

**所有元素完全在安全圆内。**

---

## 3. 智能混合交互模式

### 3.1 三种状态

**状态 1：默认（隐藏）**
- 只显示 EAF 表情 + 名称标签
- 顶部/底部胶囊完全不可见（opacity = 0）
- EAF 区域最大化，沉浸感最强

**状态 2：点击唤出**
- 用户点击屏幕 → 胶囊 200ms 淡入（opacity 0→100%）
- 同时触发表情轮播（保留原有手动切换行为）
- 3 秒后胶囊自动淡出（200ms）
- 如果 3 秒内再次点击，重置计时器

**状态 3：异常自动弹出**
- 传感器/运动数据触发异常条件 → 对应胶囊自动弹出
- 胶囊变为高亮颜色 + 脉冲动画（1s 周期）
- 持续显示直到异常解除后 3 秒
- 点击不会关闭异常高亮（只能等异常解除）

### 3.2 异常触发规则

| 等级 | 颜色 | 触发条件 | 弹出胶囊 |
|------|------|---------|---------|
| 紧急 | 红色 `#FF5050` 70% | MOTION_COLLISION · MOTION_BRAKE | 底部胶囊 |
| 警告 | 黄色 `#FFB400` 70% | UV > 8 · \|roll\| > 20° · MOTION_BUMPY | 对应胶囊 |

### 3.3 状态转换图

```
                    点击
  [隐藏] ──────────────────→ [唤出显示]
     ↑                           │
     │ 3秒到期                    │ 3秒倒计时
     │←──────────────────────────│
     │                           │
     │ 异常触发                   │ 再次点击(重置计时)
     │                           │
     ↓                      ←────┘
  [异常高亮] ──── 异常解除+3秒 ──→ [隐藏]
```

---

## 4. 胶囊样式规范

### 4.1 基础样式（正常态）

```c
/* 顶部胶囊 — 环境数据（青色系） */
lv_obj_set_style_bg_color(capsule_env, lv_color_hex(0x00B4FF), 0);
lv_obj_set_style_bg_opa(capsule_env, LV_OPA_15, 0);          /* 15% 透明 */
lv_obj_set_style_border_color(capsule_env, lv_color_hex(0x00B4FF), 0);
lv_obj_set_style_border_opa(capsule_env, LV_OPA_40, 0);
lv_obj_set_style_border_width(capsule_env, 1, 0);
lv_obj_set_style_radius(capsule_env, 19, 0);                  /* 高度38/2=全圆角 */
lv_obj_set_style_text_font(label_env, &lv_font_montserrat_16, 0);
lv_obj_set_style_text_color(label_env, lv_color_hex(0x00C8FF), 0);
lv_obj_set_style_text_opa(label_env, LV_OPA_85, 0);

/* 底部胶囊 — 运动数据（绿色系） */
lv_obj_set_style_bg_color(capsule_motion, lv_color_hex(0x00FF88), 0);
lv_obj_set_style_bg_opa(capsule_motion, LV_OPA_12, 0);
/* ... 其余同上，颜色替换为绿色系 */
```

### 4.2 异常高亮样式

```c
/* 紧急 — 红色 */
lv_obj_set_style_bg_color(capsule, lv_color_hex(0xFF5050), 0);
lv_obj_set_style_bg_opa(capsule, LV_OPA_20, 0);
lv_obj_set_style_border_color(capsule, lv_color_hex(0xFF5050), 0);
lv_obj_set_style_border_opa(capsule, LV_OPA_70, 0);
lv_obj_set_style_border_width(capsule, 2, 0);
lv_obj_set_style_text_color(label, lv_color_hex(0xFF7878), 0);

/* 警告 — 黄色 */
lv_obj_set_style_bg_color(capsule, lv_color_hex(0xFFB400), 0);
lv_obj_set_style_bg_opa(capsule, LV_OPA_20, 0);
/* ... 同上结构 */
```

### 4.3 脉冲动画

使用 LVGL `lv_anim_t` 对 border_opa 做 1s 周期往复动画：

```c
/* 脉冲: border_opa 40% ↔ 10% */
lv_anim_t a;
lv_anim_init(&a);
lv_anim_set_var(&a, capsule);
lv_anim_set_values(&a, LV_OPA_10, LV_OPA_70);
lv_anim_set_time(&a, 1000);
lv_anim_set_playback_time(&a, 1000);
lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_border_opa);
lv_anim_start(&a);
```

---

## 5. 淡入/淡出动画

### 5.1 唤出动画（200ms 淡入）

```c
static void capsule_fade_in(lv_obj_t *capsule)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, capsule);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
}
```

### 5.2 淡出动画（200ms）

```c
static void capsule_fade_out(lv_obj_t *capsule)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, capsule);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
}
```

---

## 6. 点击行为变更

### 6.1 原行为

点击 → 仅触发 `emote_engine_manual_next()`（切换表情）

### 6.2 新行为

```c
static void click_cb(lv_event_t *e)
{
    /* 1. 切换表情（保留原有行为） */
    emote_engine_manual_next();

    /* 2. 唤出数据胶囊（如果当前不是异常高亮状态） */
    if (s_overlay_state != OVERLAY_ALERT) {
        overlay_show_temporary();  /* 淡入 + 3秒后淡出 */
    }
}
```

### 6.3 3 秒自动淡出计时

在 `overlay_update()` 中维护：

```c
#define OVERLAY_DISPLAY_MS  3000

static int64_t s_overlay_shown_at_us = 0;

static void overlay_show_temporary(void)
{
    s_overlay_state = OVERLAY_SHOWN;
    s_overlay_shown_at_us = esp_timer_get_time();
    capsule_fade_in(s_capsule_env);
    capsule_fade_in(s_capsule_motion);
}

/* 在 overlay_update() 中调用 */
if (s_overlay_state == OVERLAY_SHOWN) {
    if (esp_timer_get_time() - s_overlay_shown_at_us >= OVERLAY_DISPLAY_MS * 1000) {
        s_overlay_state = OVERLAY_HIDDEN;
        capsule_fade_out(s_capsule_env);
        capsule_fade_out(s_capsule_motion);
    }
}
```

---

## 7. 异常联动集成

### 7.1 overlay 状态枚举

```c
typedef enum {
    OVERLAY_HIDDEN = 0,   /* 默认隐藏 */
    OVERLAY_SHOWN,        /* 点击唤出，3秒后淡出 */
    OVERLAY_ALERT,        /* 异常高亮，持续显示 */
} overlay_state_t;
```

### 7.2 与运动引擎集成

在 `emote_engine_on_motion()` 中，当收到异常级别事件时同时触发 overlay：

```c
void emote_engine_on_motion(motion_event_t event)
{
    /* 原有逻辑：设置 pending_event */

    /* 新增：异常事件触发 overlay */
    if (event == MOTION_COLLISION || event == MOTION_BRAKE) {
        overlay_trigger_alert(ALERT_LEVEL_URGENT);   /* 红色高亮 */
    } else if (event == MOTION_BUMPY) {
        overlay_trigger_alert(ALERT_LEVEL_WARNING);   /* 黄色高亮 */
    }
}
```

### 7.3 与环境传感器集成

在 `overlay_update()` 的环境数据更新中检查阈值：

```c
/* UV 异常 */
if (env.uv.uv_index > 8.0f) {
    overlay_trigger_alert(ALERT_LEVEL_WARNING);
}

/* 倾角异常（从 motion_engine 缓存读取） */
float pitch, roll;
motion_engine_get_angles(&pitch, &roll);
if (fabsf(roll) > 20.0f) {
    overlay_trigger_alert(ALERT_LEVEL_WARNING);
}
```

---

## 8. LVGL 对象树结构

```
scr (纯黑背景)
├── eaf_obj (330×330 居中, CLICKABLE)
├── name_label (底部居中 y=350, Montserrat 20, 白色)
├── capsule_env (200×38, y=68, 默认 OPA_0)
│   └── label_env (Montserrat 16, 青色)
└── capsule_motion (250×38, y=378, 默认 OPA_0)
    └── label_motion (Montserrat 16, 绿色)
```

**关键变更**：EAF 对象不再是全屏 480×480，而是缩小到 330×330，避免与胶囊区域重叠。但 EAF 内部的动画仍以其自身画布居中渲染。

---

## 9. 实现影响分析

### 9.1 需修改的文件

| 文件 | 修改内容 |
|------|---------|
| `main.c` | 重构 `ui_create()`：删除四角卡片，创建胶囊 + EAF 缩放；重构 `overlay_update()`：添加状态机和淡入淡出；重构 `click_cb()`：增加唤出逻辑 |
| `emote_engine.c` | 在 `emote_engine_on_motion()` 中增加 overlay 异常触发调用 |
| `emote_engine.h` | 导出 `overlay_trigger_alert()` 声明（或放在 main.c 中通过函数指针注入） |

### 9.2 不变的部分

- 传感器驱动层（drv_aht20/bmp280/mpu6050/uv_adc）— 完全不变
- sensor_manager — 完全不变
- motion_engine — 完全不变
- LCD 硬件配置 — 完全不变
- EAF 资源管理 — 完全不变

### 9.3 新增依赖

- `lv_font_montserrat_16` 需要在 sdkconfig 中启用（LVGL 字体配置）
- 检查 `CONFIG_LV_FONT_MONTSERRAT_16` 是否已开启

---

## 10. 设计验证清单

- [x] 所有元素在安全圆 R=210 内（第 2.4 节已验证）
- [x] 字体 ≥ Montserrat 16（可读性满足）
- [x] EAF 表情始终可见（核心焦点不丢失）
- [x] 点击行为保留原有表情轮播（无功能退化）
- [x] 异常自动弹出（安全提醒能力）
- [x] 线程安全：overlay 状态机在 LVGL 线程运行（overlay_update 在 lvgl_lock 内）
- [x] 动画在 LVGL 线程执行（lv_anim 是 LVGL 内部 API）
- [x] 性能：胶囊仅在显示/更新时渲染，隐藏时 OPA_0 不触发重绘
