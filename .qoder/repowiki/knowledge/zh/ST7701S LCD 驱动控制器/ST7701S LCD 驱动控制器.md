---
kind: external_dependency
name: ST7701S LCD 驱动控制器
slug: st7701s
category: external_dependency
category_hints:
    - vendor_identity
scope:
    - '**'
---

### ST7701S RGB LCD 驱动 IC
- TK021F2699 屏幕的驱动控制器，支持 480×480 分辨率
- 通过软件 SPI（GPIO 1/13/20）进行初始化配置
- 关键寄存器：0x3A（色深设置）、0xFF（Bank 切换）、Gamma 和电源时序配置
- 支持 16bit RGB565 和 24bit RGB888 并行接口
- 像素时钟 16MHz，HSYNC/VSYNC 脉宽2、后沿9、前沿4