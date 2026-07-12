import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../app/theme/app_colors.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../shared/providers/ble_provider.dart';
import '../../features/connection/ble_connection_sheet.dart';

/// BLE 连接状态指示芯片，放置于 AppBar actions 区域。
///
/// 根据 [BleConnectionState] 显示不同的视觉表现：
/// - disconnected → 灰色图标 + 呼吸脉冲动画
/// - scanning     → 青色搜索图标 + 旋转动画
/// - connecting   → 小号进度指示器
/// - connected    → 绿色图标 + 绿色指示点
class BleStatusChip extends ConsumerWidget {
  const BleStatusChip({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connAsync = ref.watch(connectionStateProvider);
    final state = connAsync.valueOrNull ?? BleConnectionState.disconnected;

    return GestureDetector(
      onTap: () => _showConnectionSheet(context),
      child: _ChipContent(state: state),
    );
  }

  void _showConnectionSheet(BuildContext context) {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      backgroundColor: AppColors.background,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
      ),
      builder: (_) => const BleConnectionSheet(),
    );
  }
}

/// 芯片内部内容，含状态指示动画。
class _ChipContent extends StatefulWidget {
  final BleConnectionState state;

  const _ChipContent({required this.state});

  @override
  State<_ChipContent> createState() => _ChipContentState();
}

class _ChipContentState extends State<_ChipContent>
    with TickerProviderStateMixin {
  late AnimationController _pulseController;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    );
  }

  @override
  void didUpdateWidget(covariant _ChipContent oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.state == BleConnectionState.disconnected) {
      _pulseController.repeat(reverse: true);
    } else {
      _pulseController.stop();
    }
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    // 启动/停止呼吸动画
    if (widget.state == BleConnectionState.disconnected) {
      if (!_pulseController.isAnimating) _pulseController.repeat(reverse: true);
    } else {
      _pulseController.stop();
    }

    final icon = _stateIcon(widget.state);
    final color = _stateColor(widget.state);
    final isBusy =
        widget.state == BleConnectionState.connecting ||
        widget.state == BleConnectionState.reconnecting;

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: color.withValues(alpha: 0.35), width: 1),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          // 状态点 / 忙碌指示器
          if (isBusy)
            SizedBox(
              width: 14,
              height: 14,
              child: CircularProgressIndicator(strokeWidth: 2, color: color),
            )
          else
            FadeTransition(
              opacity: _pulseController,
              child: Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: color,
                  boxShadow: widget.state == BleConnectionState.connected
                      ? [
                          BoxShadow(
                            color: color.withValues(alpha: 0.5),
                            blurRadius: 6,
                          ),
                        ]
                      : null,
                ),
              ),
            ),
          const SizedBox(width: 6),
          // 状态图标
          widget.state == BleConnectionState.scanning
              ? _SpinningIcon(icon: icon, color: color)
              : Icon(icon, size: 18, color: color),
          const SizedBox(width: 6),
          // 状态文字
          Text(
            _stateLabel(widget.state),
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: color,
            ),
          ),
        ],
      ),
    );
  }

  IconData _stateIcon(BleConnectionState state) {
    switch (state) {
      case BleConnectionState.disconnected:
        return Icons.bluetooth_disabled;
      case BleConnectionState.scanning:
        return Icons.bluetooth_searching;
      case BleConnectionState.connecting:
      case BleConnectionState.reconnecting:
        return Icons.bluetooth;
      case BleConnectionState.connected:
        return Icons.bluetooth_connected;
      case BleConnectionState.failed:
        return Icons.error_outline;
    }
  }

  Color _stateColor(BleConnectionState state) {
    switch (state) {
      case BleConnectionState.disconnected:
      case BleConnectionState.failed:
        return AppColors.textSecondary;
      case BleConnectionState.scanning:
        return AppColors.envPrimary;
      case BleConnectionState.connecting:
      case BleConnectionState.reconnecting:
        return AppColors.warning;
      case BleConnectionState.connected:
        return AppColors.motionPrimary;
    }
  }

  String _stateLabel(BleConnectionState state) {
    switch (state) {
      case BleConnectionState.disconnected:
        return '未连接';
      case BleConnectionState.scanning:
        return '搜索中';
      case BleConnectionState.connecting:
        return '连接中';
      case BleConnectionState.reconnecting:
        return '重连中';
      case BleConnectionState.connected:
        return '已连接';
      case BleConnectionState.failed:
        return '失败';
    }
  }
}

/// 旋转动画图标（用于扫描状态）
class _SpinningIcon extends StatefulWidget {
  final IconData icon;
  final Color color;

  const _SpinningIcon({required this.icon, required this.color});

  @override
  State<_SpinningIcon> createState() => _SpinningIconState();
}

class _SpinningIconState extends State<_SpinningIcon>
    with SingleTickerProviderStateMixin {
  late AnimationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    )..repeat();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return RotationTransition(
      turns: _controller,
      child: Icon(widget.icon, size: 18, color: widget.color),
    );
  }
}
