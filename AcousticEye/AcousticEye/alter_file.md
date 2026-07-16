## 20260522_1019

### modification
- 将主循环延时从 20ms 降到 5ms，减少每轮音频定位后的额外等待时间。
- 将最小原始 RMS 门槛从 0.0060 降到 0.0030，让较弱声音更容易进入角度计算。
- 将 GCC-PHAT 稳定输出所需有效帧数从 3 降到 2，并将角度扩散容忍度从 28 度放宽到 40 度。

### Function usage
1. 重新编译并烧录工程。
2. 打开串口监视器，观察 `GCC angle` 和 `APP angle` 输出是否更快、更连续。
3. 如果出现误判或角度跳动，再适当提高 RMS 门槛或有效帧数。

## 20260618_2111

### modification
- 为 `main/main.c` 添加中文 Doxygen 风格注释，说明应用入口、音频任务、角度映射和 LED 显示流程。
- 为 `main/ws2812.h` 与 `main/ws2812.c` 添加中文 Doxygen 风格注释，说明灯带参数、初始化、像素设置、刷新、清屏和测试接口。
- 为 `components/calc/calc_direction.h` 添加中文 Doxygen 风格注释，说明公开宏、调试结构体字段和声源定位 API。
- 为 `components/calc/calc_direction.c` 添加中文 Doxygen 风格注释，说明 ES7210/I2S 初始化、采样读取、GCC-PHAT、有效性过滤和角度稳定滤波流程。
- 本轮只补充注释，不修改算法参数和执行逻辑。

### Function usage
1. 重新编译工程，确认中文注释不会影响构建。
2. 阅读 `calc_direction.c` 中 `calcAngle()` 的三层过滤注释，定位 `APP angle` 不输出时对应的无效条件。
3. 调整灵敏度时优先参考注释中的 RMS、GCC 峰值、峰值比、方向向量和角度窗口参数说明。

## 20260618_2155

### modification
- 新增 `angle_viewer.py` Python 图形上位机，用于读取串口输出的 `angle: xxx.x deg` 数据。
- 上位机使用 `tkinter` 绘制圆形表盘、角度刻度和实时指针，默认 0 度向右、90 度向上，逆时针增加。
- 支持串口号和波特率输入、串口列表刷新、连接/断开、最近串口行显示和角度文本显示。
- 增加“反向显示”选项，便于在 LED 编号或观察方向相反时快速翻转表盘指针。

### Function usage
1. 如未安装串口库，先执行 `python -m pip install pyserial`。
2. 运行 `python angle_viewer.py` 打开图形上位机。
3. 选择或输入串口号，例如 `COM5`，确认波特率，例如 `115200`，点击“连接”。
4. ESP32 持续输出 `angle: 208.0 deg` 格式数据后，表盘指针会自动更新。

## 20260622_1849

### modification
- 新增 Arduino 库标准入口文件 `library.properties` 和 `keywords.txt`。
- 新增 `src/AcousticEye.h`、`src/AcousticEye.cpp`，将原有 C 接口封装为 Arduino 可调用的 `AcousticEye` 类。
- 将 `components/calc/calc_direction.c/.h` 复制到 `src/`，并修正 ES7210 头文件路径和 FreeRTOS 依赖包含。
- 将 ES7210 驱动源码、头文件、寄存器定义和许可证复制到 `src/third_party/es7210/`。
- 新增 `examples/basic_direction/basic_direction.ino` Arduino 示例。
- 新增 `README_Arduino.md`，说明 Arduino 库目录和基本用法。

### Function usage
1. 将当前 `AcousticEye` 文件夹放入 Arduino 的 `libraries` 目录，或在 PlatformIO 中作为本地库引用。
2. 打开 `examples/basic_direction/basic_direction.ino`。
3. 选择 ESP32-S3 开发板并编译烧录。
4. 打开串口监视器，观察 `angle`、`direction` 和 `led` 下标输出。

## 20260622_1904

### modification
- 新增 `acoustic_eye_pins_t` / `AcousticEyePins` 引脚配置结构，包含 I2C、I2S 和板级使能 GPIO。
- 新增 `AcousticEye::begin(pins, threshold)`、`AcousticEye::setPins()`、`AcousticEye::defaultPins()` 接口。
- 将 `src/calc_direction.c` 中固定的 SDA、SCL、MCLK、BCLK、WS、DIN 和使能 GPIO 改为运行时配置读取。
- 保留原默认引脚和 `eye.begin(-2.7f)` 旧用法，兼容已有示例。
- 更新 `examples/basic_direction/basic_direction.ino` 和 `README_Arduino.md`，展示在 Arduino 代码里设置引脚。

### Function usage
1. 在 `.ino` 中创建 `AcousticEyePins pins = AcousticEye::defaultPins();`。
2. 按实际硬件修改 `pins.i2c_sda`、`pins.i2c_scl`、`pins.i2s_mclk`、`pins.i2s_bclk`、`pins.i2s_ws`、`pins.i2s_din`。
3. 如果没有板级使能脚，将 `pins.enable_gpio` 设为 `ACOUSTIC_EYE_GPIO_DISABLED`。
4. 调用 `eye.begin(pins, -2.7f)` 初始化。

## 20260622_1923

### modification
- 将 Arduino 库中的 ES7210 I2C 控制链路从旧 `driver/i2c.h` 驱动切换为 Arduino-ESP32 3.x / ESP-IDF 5 的 `driver/i2c_master.h` 新驱动。
- 删除 `i2c_param_config()`、`i2c_driver_install()`、`i2c_cmd_link_create()`、`i2c_master_cmd_begin()` 等旧 I2C API 使用。
- 在 `src/calc_direction.c` 中使用 `i2c_new_master_bus()` 和 `i2c_master_bus_add_device()` 创建 ES7210 I2C 设备。
- 在 `src/third_party/es7210/es7210.c` 中使用 `i2c_master_transmit()` 写 ES7210 寄存器。
- 更新 `README_Arduino.md`，说明库已适配 Arduino-ESP32 3.x / ESP-IDF 5 新 I2C 驱动。

### Function usage
1. 重新复制更新后的 `library.properties`、`src/`、`examples/` 到 Arduino 库目录。
2. 使用 Arduino-ESP32 3.x 内核重新编译烧录。
3. 如果使用过 `Wire.begin()`，先不要在示例中主动调用它，让 AcousticEye 自己创建 I2C master bus。
4. 打开串口监视器，确认不再出现 `driver_ng is not allowed to be used with this old driver`。

## 20260622_2011

### modification
- 同步用户在 IDF 源码中调整的 ES7210/I2S 默认引脚到 Arduino 库：I2C SDA=10、SCL=6，I2S MCLK=9、BCLK=5、WS=4、DIN=8，使能 GPIO=13。
- 同步声源定位通道映射和 Y 轴方向：`SSL_AXIS_Y_SIGN=1.0f`，左右/上下麦克风通道映射与 `components/calc/calc_direction.c` 保持一致。
- 在 `src/AcousticEye.h`、`src/AcousticEye.cpp`、`src/calc_direction.h` 和 ES7210 写寄存器入口补充规范中文 Doxygen 注释。
- 将板级音频使能 GPIO 提前到 ES7210 I2C 寄存器访问之前，避免芯片未上电时写 `ES7210_RESET_REG00` 失败。
- 增加 ES7210 I2C 初始化和寄存器写入失败日志，便于继续定位 `ESP_ERR_INVALID_STATE`、无 ACK 或引脚错误问题。
- 更新 `examples/basic_direction/basic_direction.ino` 和 `README_Arduino.md` 中的 Arduino 示例引脚。

### Function usage
1. 将当前工程中的 `library.properties`、`keywords.txt`、`src/`、`examples/`、`README_Arduino.md` 复制到 Arduino 的 `AcousticEye` 库目录。
2. Arduino 示例中使用 `AcousticEyePins pins = AcousticEye::defaultPins();`，按硬件确认 SDA/SCL/MCLK/BCLK/WS/DIN/enable GPIO 后调用 `eye.begin(pins, -2.7f)`。
3. 使用 Arduino-ESP32 3.x 内核重新编译烧录。
4. 如果仍报 ES7210 初始化失败，查看串口中新增的 `init ES7210 I2C` 和 `write reg failed` 日志，优先核对 I2C 引脚、ES7210 地址、供电/使能脚和板上上拉。

## 20260622_2026

### modification
- 调整 `src/calc_direction.c` 中 ES7210 I2C bus 初始化顺序，正常启动时直接调用 `i2c_new_master_bus()` 创建 bus。
- 仅当 `i2c_new_master_bus()` 返回 `ESP_ERR_INVALID_STATE` 时，才调用 `i2c_master_get_bus_handle()` 尝试复用已经存在的 I2C bus。
- 去掉正常启动路径中 `i2c_master_get_bus_handle()` 造成的误导性日志：`this port has not been initialized, please initialize it first`。
- 保留已有 I2C bus 被其他库初始化时的兼容兜底逻辑。

### Function usage
1. 重新复制更新后的 `src/` 到 Arduino `AcousticEye` 库目录。
2. Arduino 代码中继续使用 `eye.begin(pins, -2.7f)` 传入实际引脚配置。
3. 重新编译烧录后，正常启动时应不再出现 `this port has not been initialized` 这条误导性日志。

## 20260622_2036

### modification
- 扩展 `AcousticEye::ledIndex()`，新增 `reverse` 和 `ledOffset` 参数，用于校准 WS2812 灯环安装方向和 0 度对应灯珠位置。
- 更新 `examples/basic_direction/basic_direction.ino`，强制使用 `eye.begin(pins, -2.7f)`，并在初始化失败时停止运行。
- 示例增加 200 ms 间隔的调试输出，打印 `valid`、稳定角度、原始角度、最终角度、LED 下标、四通道 RMS 和无效原因标志。
- 示例去掉固定 `delay(5)`，减少 Arduino loop 对连续音频采样的额外等待。
- 更新 `README_Arduino.md`，说明 LED 反向和偏移校准方式，以及如何区分算法角度错误和 LED 映射错误。

### Function usage
1. 重新复制更新后的 `src/`、`examples/`、`README_Arduino.md` 到 Arduino `AcousticEye` 库目录。
2. 使用 `eye.begin(pins, -2.7f)` 初始化，确认 `pins.enable_gpio` 与实际硬件一致。
3. 灯环方向相反时调用 `eye.ledIndex(36, true, 0)`；若 0 度不对齐，通过第三个参数调整灯珠偏移。
4. 串口观察 `raw`、`final` 和 `rms`：若 `raw` 已经明显错误，优先检查麦克风物理方向和通道；若 `raw/final` 正常但灯错，调整 LED 映射参数。

## 20260622_2046

### modification
- 修复 `src/calc_direction.c` 中调试信息未刷新 `raw_levels[]` 的问题，Arduino 日志中的四通道 RMS 现在会显示真实值。
- 在 `calcAngle()` 开头重置本帧调试字段，避免弱声或提前返回时沿用上一帧 `raw`、`final` 和无效标志。
- 为角度稳定滤波增加大跳变抑制：已有稳定角度时，单帧超过 90 度的跳变先作为离群点处理，不写入稳定窗口。
- 连续 3 次出现一致的大幅新方向后，滤波器会接受为真实声源移动并重新建立稳定角度。
- Arduino 示例提高 RMS 打印精度到 7 位小数，并新增 `jump` 字段显示角度跳变或窗口离散被抑制。
- 更新 `README_Arduino.md` 和 `calc_direction.h` 中关于 `jump_unreliable` 的说明。

### Function usage
1. 重新复制更新后的 `src/`、`examples/`、`README_Arduino.md` 到 Arduino `AcousticEye` 库目录。
2. 重新烧录后观察串口 `rms` 是否不再全部显示 `0.0000`。
3. 若 `jump=1` 且 `final` 仍稳定，说明离群角度已被抑制；若 `jump=1` 连续出现且最终切换角度，说明声源发生了真实大幅移动。
4. 若 `rms` 某一路明显小很多或接近 0，优先检查该麦克风通道、焊接或 ES7210 槽位映射。

## 20260622_2058

### modification
- 重构 `src/calc_direction.c` 中角度稳定滤波流程：未产生稳定角前使用窗口建立初值，产生稳定角后直接围绕稳定角跟踪。
- 已稳定状态下不再把新 `raw` 角度写入旧窗口，避免旧窗口中的 90/180/270 度离群值继续污染正常角度。
- 将单帧大跳变待确认阈值收紧为 45 度，超过阈值的新方向需要连续 3 次一致才会被接受。
- 初始窗口离散过大时清理旧窗口，只保留当前帧重新开始，避免一直卡在 `final=-1`。

### Function usage
1. 重新复制更新后的 `src/` 到 Arduino `AcousticEye` 库目录。
2. 重新烧录后同一声源位置下，接近稳定角的 `raw` 应持续更新 `final`，不应再因为旧离群窗口出现 `raw` 接近但 `final=-1`。
3. 若声源确实移动很大，`jump=1` 会先保持旧角度，连续确认后再切换到新方向。

## 20260623_1020

### modification
- 将 Arduino 封装改为 IDF 风格后台音频任务模型：`begin()` 内部创建 FreeRTOS 任务连续执行 `i2s_read_mics()` 和 `calcAngle()`。
- 新增 `AcousticEye::available()`，用于在 Arduino `loop()` 中查询后台任务最近一次是否得到有效稳定角度。
- 将 `AcousticEye::update()` 改为兼容旧代码的轮询接口，不再直接读取 I2S 或执行定位计算。
- 使用 FreeRTOS 互斥锁保护角度、RMS 和调试信息缓存，避免后台任务和 Arduino `loop()` 同时读写结果。
- `rawLevel()`、`rawLevels()`、`debugInfo()` 改为读取后台任务缓存，减少显示层对音频采样的干扰。
- 更新 `examples/basic_direction/basic_direction.ino` 和 `README_Arduino.md`，示例改为 `eye.available()` 读取结果。
- 更新 `keywords.txt`，增加 `available` 关键字。

### Function usage
1. 重新复制更新后的 `src/`、`examples/`、`README_Arduino.md`、`keywords.txt` 到 Arduino `AcousticEye` 库目录。
2. Arduino `setup()` 中调用 `eye.begin(pins, -2.7f)` 后，库会自动启动后台音频任务。
3. Arduino `loop()` 中使用 `eye.available()` 判断是否有有效角度，再用 `eye.angle()`、`eye.debugInfo()` 或 `eye.ledIndex()` 读取缓存结果。
4. 不要在 `loop()` 中直接调用底层 `i2s_read_mics()` 或 `calcAngle()`，避免破坏后台任务的连续采样节奏。

## 20260623_1034

### modification
- 将 `src/calc_direction.c` 的角度稳定滤波恢复为 IDF 原工程逻辑，去掉 Arduino 版新增的 `ANGLE_JUMP_*` 跳变确认阈值。
- 删除 `gcc_angle_filter_t` 中的 `pending_jump_angle` 和 `pending_jump_count` 状态，避免滤波器把 0/180 度附近的错误跳变锁定为稳定方向。
- 保留 Arduino 库已有的后台采样任务、I2C/I2S 引脚配置接口和调试输出接口，本轮不改硬件初始化流程。

### Function usage
1. 重新复制更新后的 `src/` 到 Arduino `AcousticEye` 库目录。
2. 重新烧录后观察同一声源位置的 `raw` 和 `final`：如果 `raw` 仍在 0/180 度之间大幅跳变，下一步应检查 I2S 槽位/麦克风通道映射，而不是继续调滤波。
3. 此版本中 `jump` 调试字段不再参与算法决策，主要以 `raw`、`final`、`rms`、`cd/lc/wp/wa` 判断定位问题。

## 20260623_1106

### modification
- 在 `src/calc_direction.c` 中新增 24 种四路 TDM 槽位映射表，`setChannelMapId()` 现在会真实改变逻辑麦克风通道读取的槽位顺序。
- 切换通道映射后通过音频任务重置角度滤波器，避免旧映射下的稳定角度污染新映射结果。
- 在 `debugInfo` 中填充当前 `channel_map_id`、`channel_map_count`、`channel_map[]`、轴对信息，便于串口判断当前测试的是哪一种映射。
- 在 Arduino 封装中新增 `setChannelMapId()`、`channelMapId()`、`channelMapCount()` 接口，并同步更新 `keywords.txt`。
- 更新 `examples/basic_direction/basic_direction.ino`，增加 `CHANNEL_MAP_ID`，并打印 `map`、`delay`、`axis`、`peak` 诊断字段。
- 更新 `README_Arduino.md`，说明固定声源时如何扫描 0~23 的 TDM 槽位映射。

### Function usage
1. 重新复制更新后的 `src/`、`examples/`、`README_Arduino.md`、`keywords.txt` 到 Arduino `AcousticEye` 库目录。
2. 打开 `examples/basic_direction/basic_direction.ino`，固定音源不动，只修改 `CHANNEL_MAP_ID` 从 0 逐个试到 23。
3. 每个映射至少观察 5~10 秒，优先选择 `raw`、`delay`、`axis` 都稳定且 `cd/lc/wa/wp` 较少出现的映射。
4. 找到稳定映射后，把该编号固定到你的正式 Arduino 程序中：`eye.setChannelMapId(编号);`。
