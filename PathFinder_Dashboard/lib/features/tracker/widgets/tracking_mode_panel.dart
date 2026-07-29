import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

/// 声源角度 → 中文方向描述
///
/// 匹配固件 sound_localizer.c → sound_localizer_get_direction()
/// 角度定义（4-mic atan2）：
///   0° = 右, 90° = 前, 180° = 左, 270° = 后
class SoundDirection {
  static String fromAngle(double angle) {
    if (angle < 0) return '未知方向';
    if (angle < 22.5 || angle >= 337.5) return '右方 →';
    if (angle < 67.5) return '右前方 ↗';
    if (angle < 112.5) return '正前方 ↑';
    if (angle < 157.5) return '左前方 ↖';
    if (angle < 202.5) return '左方 ←';
    if (angle < 247.5) return '左后方 ↙';
    if (angle < 292.5) return '正后方 ↓';
    return '右后方 ↘';
  }
}

/// 追踪模式说明面板
///
/// 对应固件 tracking_coordinator.h → track_mode_t 四种模式：
///   IDLE / AUTO / FACE / MANUAL
/// 高亮显示当前激活的模式。
class TrackingModePanel extends StatelessWidget {
  final int currentMode;

  const TrackingModePanel({super.key, required this.currentMode});

  static const _modes = [
    _ModeInfo(
      id: 0,
      name: 'IDLE',
      cnName: '空闲',
      icon: Icons.pause_circle_outline,
      desc: '保持当前位置不动',
    ),
    _ModeInfo(
      id: 1,
      name: 'AUTO',
      cnName: '声源追踪',
      icon: Icons.graphic_eq,
      desc: 'GCC-PHAT 四麦克声源定位\n驱动 Pan 舵机自动转向声源',
    ),
    _ModeInfo(
      id: 2,
      name: 'FACE',
      cnName: '人脸追踪',
      icon: Icons.face,
      desc: 'ESP-DL MSRMNP 神经网络检测\nPan+Tilt 双轴 PID 跟随',
    ),
    _ModeInfo(
      id: 3,
      name: 'MANUAL',
      cnName: '手动控制',
      icon: Icons.pan_tool,
      desc: 'AI 通过 MCP 工具手动指令\nPan/Tilt 角度由 LLM 决定',
    ),
  ];

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '追踪模式',
          style: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimary,
          ),
        ),
        const SizedBox(height: 4),
        const Text(
          '上电默认 AUTO · 可通过语音/MCP 切换',
          style: TextStyle(fontSize: 11, color: AppColors.textSecondary),
        ),
        const SizedBox(height: 12),
        ..._modes.map((m) => _ModeCard(mode: m, active: m.id == currentMode)),
      ],
    );
  }
}

class _ModeInfo {
  final int id;
  final String name;
  final String cnName;
  final IconData icon;
  final String desc;

  const _ModeInfo({
    required this.id,
    required this.name,
    required this.cnName,
    required this.icon,
    required this.desc,
  });
}

class _ModeCard extends StatelessWidget {
  final _ModeInfo mode;
  final bool active;

  const _ModeCard({required this.mode, required this.active});

  @override
  Widget build(BuildContext context) {
    final color = active ? AppColors.trackerText : AppColors.textSecondary;
    final bgColor = active
        ? AppColors.trackerPrimary.withValues(alpha: 0.15)
        : AppColors.surface;

    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 300),
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
        decoration: BoxDecoration(
          color: bgColor,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(
            color: active
                ? AppColors.trackerPrimary.withValues(alpha: 0.5)
                : AppColors.divider,
            width: active ? 1.5 : 1,
          ),
        ),
        child: Row(
          children: [
            Icon(mode.icon, size: 22, color: color),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Text(
                        mode.name,
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          color: color,
                          fontFamily: 'monospace',
                        ),
                      ),
                      const SizedBox(width: 8),
                      Text(
                        mode.cnName,
                        style: TextStyle(
                          fontSize: 13,
                          color: color.withValues(alpha: 0.8),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 2),
                  Text(
                    mode.desc,
                    style: TextStyle(
                      fontSize: 11,
                      height: 1.4,
                      color: color.withValues(alpha: 0.6),
                    ),
                  ),
                ],
              ),
            ),
            if (active)
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: AppColors.trackerPrimary.withValues(alpha: 0.3),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: const Text(
                  '当前',
                  style: TextStyle(
                    fontSize: 10,
                    fontWeight: FontWeight.w600,
                    color: AppColors.trackerText,
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
