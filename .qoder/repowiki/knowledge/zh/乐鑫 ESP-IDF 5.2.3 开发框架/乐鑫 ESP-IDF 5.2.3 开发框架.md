---
kind: external_dependency
name: 乐鑫 ESP-IDF 5.2.3 开发框架
slug: espressif-esp-idf
category: external_dependency
category_hints:
    - vendor_identity
scope:
    - '**'
---

- 通过 IDF Component Manager 从 components.espressif.com 注册表拉取依赖
- 关键外设驱动：`esp_lcd_rgb_panel`（RGB 面板）、RMT（WS2812 编码）、I2C（触摸）
- 配置通过 sdkconfig.defaults 管理，启用 Octal PSRAM + 80MHz 提升 RGB 显示带宽