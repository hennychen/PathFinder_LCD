---
kind: design
name: Dashboard 从 4-Tab 底部导航重构为 AppBar+BleStatusChip 的 3-Tab 布局
source: session
category: adr
---

# Dashboard 从 4-Tab 底部导航重构为 AppBar+BleStatusChip 的 3-Tab 布局

_来源：b97f679 → a60a546 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

## 背景
原 4-Tab（设备/环境/运动/表情）中「设备」Tab 使用频率最低却占据首位置，BLE 连接操作被埋藏在独立全屏页面，用户路径过长。

## 决策驱动
- 高频操作就近原则（BLE 连接入口提升）
- 减少 Tab 数量降低认知负担
- 保持现有 Provider 状态管理不变

## 备选方案
- **保留 4-Tab，仅调整顺序把设备放最后** _（已否决）_ — 优点：改动最小；缺点：设备仍占一整个 Tab，连接流程仍需跳转全屏页
- **移除设备 Tab，BLE 入口迁移至 AppBar 右上角 BleStatusChip，点击弹出 ModalBottomSheet** — 优点：连接操作始终可见且一步可达；3-Tab 更聚焦核心内容；BleConnectionSheet 复用 connection_screen 逻辑；缺点：AppBar actions 区域空间有限，芯片需紧凑设计；环境/运动/表情三屏需去 Scaffold 化由外壳统一提供

## 决策
将 lib/app/app.dart 改为 ConsumerStatefulWidget 作为共享外壳，底部导航缩减为环境/运动/表情三项；新增 shared/widgets/ble_status_chip.dart 监听 connectionStateProvider 并以呼吸/旋转动画提示连接状态；点击弹出 features/connection/ble_connection_sheet.dart（从原 connection_screen 提取），环境/运动/表情三个 Screen 移除外层 Scaffold 和 AppBar。

## 影响
连接操作从全屏页下沉为面板，缩短交互路径；三个业务屏不再持有自己的 AppBar/Scaffold，由外壳统一管理标题和 BLE 入口；connection_screen.dart 暂时保留作为全屏连接备选方案。