---
kind: frontend_style
name: 嵌入式 LVGL UI 风格体系（无 Web CSS）
category: frontend_style
scope:
    - '**'
source_files:
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/ui/lvgl_demo_ui.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/ui/lvgl_gif_demo.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/fonts/ui_font_Alibaba_PuHuiTi_Font_14.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/fonts/ui_font_Alibaba_PuHuiTi_Font_20.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/fonts/ui_font_Alibaba_PuHuiTi_Font_32.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/fonts/ui_font_Alibaba_PuHuiTi_Font_48.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/images/ui_img_534919753.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/main/gif/minions_5.c
    - ESP32开发板/TK021F2699_ESP32_LVGL_GIF_LED/TK021F2699_ESP32_LVGL_GIF_LED/managed_components/lvgl__lvgl/lv_conf_template.h
---

本仓库不包含任何 Web 前端样式系统（CSS/SCSS/Tailwind 等），其“前端”界面完全由 ESP32 上的嵌入式 GUI 框架 LVGL v8.3 驱动，通过 C 代码在 RGB LCD 上绘制。因此 frontend_style 在此仓库中的含义是 LVGL 主题与视觉风格约定。

1. 使用的系统与工具链
- 渲染引擎：LVGL v8.3（以 managed_components 组件形式引入）。
- 构建环境：ESP-IDF + CMake；UI 资源通过 LVGL 的 lv_img_*、lv_font_* 宏声明为 C 数组。
- 字体：Alibaba PuHuiTi，经脚本生成多字号位图字体文件（14/20/32/48pt），位于 main/fonts/ui_font_Alibaba_PuHuiTi_Font_*.c。
- 图片/GIF：PNG 图标与 GIF 动画经 LVGL 工具链转成 C 数组，存放于 main/images/ 与 main/gif/。

2. 核心样式文件与位置
- 主 UI 布局与样式：main/ui/lvgl_demo_ui.c（环形菜单、面板、标签、GIF 播放器的创建与 lv_obj_set_style_* 调用集中于此）。
- 辅助 UI 入口：main/ui/lvgl_gif_demo.c。
- 字体定义：main/fonts/ui_font_Alibaba_PuHuiTi_Font_{14,20,32,48}.c。
- 图片/GIF 资源：main/images/ui_img_*.c、main/gif/*.c。
- LVGL 全局配置模板：managed_components/lvgl__lvgl/lv_conf_template.h（项目未直接修改，使用默认或 IDF Kconfig 覆盖）。

3. 架构与视觉约定
- 样式 API：全部使用 LVGL 运行时样式 API（lv_obj_set_style_bg_color、lv_obj_set_style_radius、lv_obj_set_style_text_font 等），而非外部样式语言。
- 颜色策略：采用硬编码十六进制色值（如 0x000000、0xFFFFFF、0x46DB46、0xFFA500 等），每个菜单项使用不同背景色区分功能模块。
- 字体层级：标题用 48pt，次级信息用 20pt，提示文本用 14pt，统一来自 Alibaba PuHuiTi 家族。
- 布局模式：基于绝对坐标 + LV_ALIGN_CENTER 的组合，配合自定义环形布局数学计算（半径 180、中心 185,185）实现旋转菜单效果。
- 动效：通过 lv_anim_t 与 lv_timer_create 实现菜单旋转动画与 Wi-Fi 信号轮询刷新。

4. 开发者应遵循的规则
- 新增控件时优先复用现有 lv_obj_set_style_* 调用模式，避免散落魔法数字；将尺寸、颜色、圆角等提取为 #define 常量。
- 所有颜色、字体、图片必须通过 LVGL 声明宏引用（lv_color_hex、&ui_font_*、LV_IMG_DECLARE），禁止在渲染路径中动态解析外部资源。
- 字体仅使用已生成的 14/20/32/48pt 四档，如需新字号需先通过 LVGL 字体生成器产出对应 .c 文件再引入。
- 图片与 GIF 资源统一放入 main/images/ 与 main/gif/，并通过 LVGL 工具链转换为 C 数组后由 LV_IMG_DECLARE 引用。
- 交互逻辑与样式设置分离：UI 结构在 lvgl_demo_ui.c 中创建，状态切换与业务逻辑放在独立回调函数中，保持单文件可读性。