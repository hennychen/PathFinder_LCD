import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../app/theme/app_colors.dart';
import '../../shared/widgets/animated_counter.dart';
import 'widgets/attitude_indicator.dart';
import 'widgets/imu_wave_chart.dart';
import 'widgets/event_timeline.dart';
import 'widgets/compass_card.dart';

class MotionScreen extends ConsumerWidget {
  const MotionScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final motionData = ref.watch(motionStreamProvider);
    final compassData = ref.watch(compassStreamProvider);

    return connState.when(
      data: (state) => state == BleConnectionState.connected
          ? motionData.when(
              data: (imu) => SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      '当前姿态',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textPrimary,
                      ),
                    ),
                    const SizedBox(height: 16),
                    // ── 姿态指示器 + 大号数据 ──
                    Row(
                      children: [
                        AttitudeIndicator(pitch: imu.pitch, roll: imu.roll),
                        const SizedBox(width: 20),
                        Expanded(
                          child: Column(
                            children: [
                              _MotionDataCard(
                                label: 'Pitch',
                                unit: '°',
                                value: imu.pitch,
                                color: _tiltColor(imu.pitch, imu.roll),
                              ),
                              const SizedBox(height: 10),
                              _MotionDataCard(
                                label: 'Roll',
                                unit: '°',
                                value: imu.roll,
                                color: _tiltColor(imu.pitch, imu.roll),
                              ),
                            ],
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 10),
                    // ── 加速度 ──
                    _MotionDataCard(
                      label: '加速度偏差',
                      unit: 'g',
                      value: imu.accelMag,
                      color: AppColors.motionText,
                      decimals: 3,
                    ),
                    const SizedBox(height: 12),
                    // ── 罗盘方位角 (C6) ──
                    compassData.when(
                      data: (compass) => CompassCard(compass: compass),
                      loading: () => const SizedBox.shrink(),
                      error: (_, __) => const SizedBox.shrink(),
                    ),
                    const SizedBox(height: 28),
                    const Text(
                      'IMU 波形 (最近10秒)',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textPrimary,
                      ),
                    ),
                    const SizedBox(height: 12),
                    const ImuWaveChart(),
                    const SizedBox(height: 24),
                    const Text(
                      '当前事件',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textPrimary,
                      ),
                    ),
                    const SizedBox(height: 12),
                    const EventTimeline(),
                  ],
                ),
              ),
              loading: () => const Center(
                child: CircularProgressIndicator(
                  color: AppColors.motionPrimary,
                ),
              ),
              error: (e, _) => Center(child: Text('Error: $e')),
            )
          : const _NotConnectedPlaceholder(),
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (_, __) => const Center(child: Text('Error')),
    );
  }

  /// 倾角 → EMA 表情配色
  /// ≥20°: 疑惑(琥珀)  ≥12°: 探究(琥珀)  正常: 绿色
  Color _tiltColor(double pitch, double roll) {
    final mag = (pitch * pitch + roll * roll);
    if (mag >= 400.0) return AppColors.warningText; // ≥20°
    if (mag >= 144.0) return AppColors.warningText; // ≥12°
    return AppColors.motionText;
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
              color: AppColors.motionPrimary.withValues(alpha: 0.7),
              fontSize: 13,
            ),
          ),
        ],
      ),
    );
  }
}

/// 运动数据胶囊卡片 — 绿色 EMA 风格，数字大且粗
class _MotionDataCard extends StatelessWidget {
  final String label;
  final String unit;
  final double value;
  final Color color;
  final int decimals;

  const _MotionDataCard({
    required this.label,
    required this.unit,
    required this.value,
    required this.color,
    this.decimals = 1,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: color.withValues(alpha: 0.4), width: 1.5),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          Text(
            label,
            style: TextStyle(
              fontSize: 13,
              fontWeight: FontWeight.w500,
              color: color.withValues(alpha: 0.7),
            ),
          ),
          const Spacer(),
          AnimatedCounter(
            value: value,
            color: color,
            decimals: decimals,
            fontSize: 36,
          ),
          const SizedBox(width: 4),
          Padding(
            padding: const EdgeInsets.only(bottom: 6),
            child: Text(
              unit,
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w600,
                color: color.withValues(alpha: 0.6),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
