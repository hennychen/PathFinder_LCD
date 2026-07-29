import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../app/theme/app_colors.dart';
import '../../shared/models/tracker_snapshot.dart';
import 'widgets/sound_radar_chart.dart';
import 'widgets/face_info_card.dart';
import 'widgets/camera_preview.dart';
import 'widgets/tracking_mode_panel.dart';
import 'widgets/conversation_panel.dart';

class TrackerScreen extends ConsumerWidget {
  const TrackerScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final trackerData = ref.watch(trackerStreamProvider);

    return connState.when(
      data: (state) => state == BleConnectionState.connected
          ? trackerData.when(
              data: (tracker) => _buildContent(context, tracker),
              loading: () => const Center(
                child: CircularProgressIndicator(
                  color: AppColors.trackerPrimary,
                ),
              ),
              error: (e, _) => Center(
                child: Text(
                  '追踪数据加载失败\n$e',
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: AppColors.textSecondary),
                ),
              ),
            )
          : const _NotConnectedPlaceholder(),
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (_, __) => const Center(child: Text('Error')),
    );
  }

  Widget _buildContent(BuildContext context, TrackerSnapshot tracker) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ── 摄像头预览 ──
          const Text(
            '摄像头预览',
            style: TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 4),
          const Text(
            'B板需与手机在同一局域网，点击预览左上角设置可修改 IP',
            style: TextStyle(fontSize: 11, color: AppColors.textSecondary),
          ),
          const SizedBox(height: 8),
          const CameraPreview(),
          const SizedBox(height: 24),
          // ── 声源雷达图 ──
          const Text(
            '声源定位',
            style: TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 4),
          Text(
            tracker.soundValid
                ? '角度 ${tracker.soundAngle.toStringAsFixed(1)}°  置信度 ${tracker.soundConfidence}%'
                : '信号丢失 — B板可能离线',
            style: TextStyle(
              fontSize: 13,
              color: tracker.soundValid
                  ? AppColors.trackerText
                  : AppColors.textSecondary,
            ),
          ),
          const SizedBox(height: 12),
          SoundRadarChart(
            angle: tracker.soundAngle,
            valid: tracker.soundValid,
            confidence: tracker.soundConfidence,
          ),
          if (tracker.soundValid)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Center(
                child: Text(
                  SoundDirection.fromAngle(tracker.soundAngle),
                  style: TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                    color: AppColors.trackerText,
                  ),
                ),
              ),
            ),
          const SizedBox(height: 28),
          // ── 人脸检测 ──
          const Text(
            '视觉追踪',
            style: TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 4),
          const Text(
            'ESP-DL MSRMNP 神经网络 · PID 双轴云台 · ~6.7Hz',
            style: TextStyle(fontSize: 11, color: AppColors.textSecondary),
          ),
          const SizedBox(height: 12),
          FaceInfoCard(tracker: tracker),
          const SizedBox(height: 28),
          // ── 追踪模式 ──
          TrackingModePanel(currentMode: tracker.trackState),
          const SizedBox(height: 28),
          // ── 对话同步 ──
          const ConversationPanel(),
          const SizedBox(height: 16),
          // ── 追踪状态 ──
          _TrackStateBadge(state: tracker.trackState),
        ],
      ),
    );
  }
}

class _TrackStateBadge extends StatelessWidget {
  final int state;
  const _TrackStateBadge({required this.state});

  @override
  Widget build(BuildContext context) {
    final label = _stateLabel(state);
    final color = _stateColor(state);
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Row(
        children: [
          Icon(Icons.radar, size: 20, color: color),
          const SizedBox(width: 10),
          Text(
            '追踪状态: $label',
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w600,
              color: color,
            ),
          ),
        ],
      ),
    );
  }

  /// 对应固件 tracking_coordinator.h → track_mode_t 枚举:
  ///   0 = IDLE   空闲保持当前位置
  ///   1 = AUTO   自动追踪声源 (GCC-PHAT 驱动 Pan 舵机)
  ///   2 = FACE   人脸追踪 (ESP-DL 驱动 Pan+Tilt 双轴)
  ///   3 = MANUAL 手动控制 (AI/MCP 指令驱动)
  String _stateLabel(int s) {
    switch (s) {
      case 0:
        return '空闲待机';
      case 1:
        return '声源自动追踪';
      case 2:
        return '人脸追踪';
      case 3:
        return '手动控制';
      default:
        return '未知($s)';
    }
  }

  Color _stateColor(int s) {
    switch (s) {
      case 0:
        return AppColors.textSecondary;
      case 1:
        return AppColors.envText; // 青色 — 声源模式
      case 2:
        return AppColors.trackerText; // 紫色 — 人脸模式
      case 3:
        return AppColors.warningText; // 琥珀 — 手动模式
      default:
        return AppColors.textSecondary;
    }
  }
}

class _NotConnectedPlaceholder extends StatelessWidget {
  const _NotConnectedPlaceholder();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(
            Icons.bluetooth_disabled,
            size: 48,
            color: AppColors.textSecondary,
          ),
          const SizedBox(height: 16),
          const Text(
            '等待设备连接...',
            style: TextStyle(color: AppColors.textSecondary),
          ),
          const SizedBox(height: 8),
          Text(
            '点击右上角连接设备',
            style: TextStyle(
              color: AppColors.trackerPrimary.withValues(alpha: 0.7),
              fontSize: 13,
            ),
          ),
        ],
      ),
    );
  }
}
