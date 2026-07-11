---
kind: external_dependency
name: LVGL 8.3.11 图形库
slug: lvgl
category: external_dependency
category_hints:
    - vendor_identity
scope:
    - '**'
---

- 已集成到 managed_components 目录，包含完整源码、示例和文档
- 项目使用环形菜单 UI、GIF 动画、字体渲染等高级功能
- 需要自定义 LCD 驱动适配 ST7701S 控制器，帧缓冲位于 PSRAM
- 支持事件处理、动画、多语言字体（阿里巴巴普惠体）