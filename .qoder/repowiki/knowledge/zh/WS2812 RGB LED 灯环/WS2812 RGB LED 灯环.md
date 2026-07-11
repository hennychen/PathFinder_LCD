---
kind: external_dependency
name: WS2812 RGB LED 灯环
slug: ws2812
category: external_dependency
category_hints:
    - vendor_identity
scope:
    - '**'
---

### WS2812 可编程 RGB LED 灯环
- 12 颗 WS2812 LED 组成环形灯带，通过 GPIO38 控制
- 使用 ESP32 RMT 外设进行单总线时序编码
- 提供完整的 API：初始化、反初始化、单个 LED 写入
- 演示效果包括红绿蓝跑马灯循环动画
- 属于通用 LED 驱动芯片，非特定厂商专有协议