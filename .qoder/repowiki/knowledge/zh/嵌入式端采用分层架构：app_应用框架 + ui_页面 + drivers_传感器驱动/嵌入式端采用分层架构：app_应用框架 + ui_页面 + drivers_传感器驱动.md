---
kind: design
name: 嵌入式端采用分层架构：app/应用框架 + ui/页面 + drivers/传感器驱动
source: session
category: adr
---

# 嵌入式端采用分层架构：app/应用框架 + ui/页面 + drivers/传感器驱动

_来源：b97f679 → a60a546 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

## 背景
PathFinder_LCD 原为单文件 ColorPalette_LED 项目，需扩展为多传感器（AHT20/BMP280/MPU6050/UV）+ 多 UI 页面的嵌入式设备，原有扁平结构无法支撑。

## 决策驱动
- 硬件抽象隔离（驱动与业务解耦）
- UI 可插拔（新页面无需改动框架）
- 传感器数据集中采集避免总线冲突

## 备选方案
- **按功能模块划分（dashboard/motion/emote 各自包含驱动）** _（已否决）_ — 优点：每个功能自包含，独立编译；缺点：I2C 总线初始化重复、sensor_data_t 结构体多处定义、LED 规则引擎耦合到各页面
- **分层目录（app/ui/drivers），共享 sensor_hub 通过 FreeRTOS Queue 分发** — 优点：单一传感器源、页面只消费数据不碰硬件、LED 规则集中管理优先级；缺点：新增 app_sensor_hub 中间层，数据结构变更需跨层同步

## 决策
将 main.c 精简为启动器，新建 app/（app_ui 导航 + app_led_manager 规则引擎 + app_sensor_hub 采集聚合）、ui/（各 lv_tileview 页面）、drivers/（每传感器一个 drv_*.c），所有传感器共用 GPIO13/20 I2C 总线并通过 FreeRTOS Queue 以 200ms 周期推送 sensor_data_t。

## 影响
新增页面只需实现 create/update 回调并注册到 app_ui；新增传感器只需在 app_sensor_hub 任务中增加一次读取并填充 sensor_data_t；LED 行为规则集中在 app_led_manager 中按告警>倾斜>UV>温度>气压>默认优先级评估，避免各页面各自写 LED。