import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../shared/widgets/metric_card.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/env_line_chart.dart';

class EnvironmentScreen extends ConsumerWidget {
  const EnvironmentScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final envData = ref.watch(envStreamProvider);

    return connState.when(
      data: (state) => state == BleConnectionState.connected
          ? _buildConnected(envData)
          : const _NotConnectedPlaceholder(),
      loading: () => const _NotConnectedPlaceholder(),
      error: (_, __) => const _NotConnectedPlaceholder(),
    );
  }

  Widget _buildConnected(AsyncValue<dynamic> envData) {
    return envData.when(
      data: (env) => SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              '实时数据',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w600,
                color: AppColors.textPrimary,
              ),
            ),
            const SizedBox(height: 16),
            // ── 第一行：温度 / 湿度 ──
            Row(
              children: [
                Expanded(
                  child: MetricCard(
                    value: env.temperature,
                    unit: '°C',
                    label: '温度',
                    color: Colors.white,
                    backgroundColor: _tempColor(
                      env.temperature,
                    ).withValues(alpha: 0.15),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: MetricCard(
                    value: env.humidity,
                    unit: '%',
                    label: '湿度',
                    color: Colors.white,
                    backgroundColor: _humidityColor(
                      env.humidity,
                    ).withValues(alpha: 0.15),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            // ── 第二行：气压 / UV ──
            Row(
              children: [
                Expanded(
                  child: MetricCard(
                    value: env.pressure.toDouble(),
                    unit: 'Pa',
                    label: '气压',
                    color: Colors.white,
                    backgroundColor: _pressureColor(
                      env.pressure,
                    ).withValues(alpha: 0.15),
                    decimals: 0,
                    fontSize: 32,
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: MetricCard(
                    value: env.uvIndex,
                    unit: 'UV',
                    label: '紫外线指数',
                    color: Colors.white,
                    backgroundColor: _uvColor(
                      env.uvIndex,
                    ).withValues(alpha: 0.15),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            // ── 第三行：海拔 ──
            Row(
              children: [
                Expanded(
                  child: MetricCard(
                    value: env.altitude,
                    unit: 'm',
                    label: '海拔',
                    color: Colors.white,
                    backgroundColor: AppColors.envCapsuleBg,
                  ),
                ),
                const SizedBox(width: 10),
                const Spacer(),
              ],
            ),
            const SizedBox(height: 28),
            const Text(
              '趋势 (最近60秒)',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w600,
                color: AppColors.textPrimary,
              ),
            ),
            const SizedBox(height: 12),
            const EnvLineChart(),
          ],
        ),
      ),
      loading: () => const Center(
        child: CircularProgressIndicator(color: AppColors.envPrimary),
      ),
      error: (e, _) => Center(child: Text('数据错误: $e')),
    );
  }

  /// 温度 → EMA 表情配色
  /// ≥32°C: 叹气(琥珀)  ≤10°C: 悲伤(蓝灰)  正常: 青色
  Color _tempColor(double t) {
    if (t >= 32.0) return AppColors.warningText;
    if (t <= 10.0) return const Color(0xFF6E8BFF);
    return AppColors.envText;
  }

  /// 湿度 → EMA 表情配色
  /// ≥85%: 哭(青色)  正常: 青色
  Color _humidityColor(double h) {
    if (h >= 85.0) return const Color(0xFF00D4D4);
    return AppColors.envText;
  }

  /// 气压 → EMA 表情配色
  /// ≤1000hPa: 思考(紫)  正常: 青色
  Color _pressureColor(int p) {
    if (p > 0 && p <= 100000) return const Color(0xFF9B6EFF);
    return AppColors.envText;
  }

  /// UV → EMA 表情配色
  /// ≥8.0: 恐慌(红)  ≥6.0: 嘲讽(琥珀)  正常: 青色
  Color _uvColor(double uv) {
    if (uv >= 8.0) return AppColors.urgentText;
    if (uv >= 6.0) return AppColors.warningText;
    return AppColors.envText;
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
              color: AppColors.envPrimary.withValues(alpha: 0.7),
              fontSize: 13,
            ),
          ),
        ],
      ),
    );
  }
}
