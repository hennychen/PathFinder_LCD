import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../shared/providers/sensor_provider.dart';
import '../../../shared/models/imu_snapshot.dart';
import '../../../app/theme/app_colors.dart';

class ImuWaveChart extends ConsumerStatefulWidget {
  const ImuWaveChart({super.key});

  @override
  ConsumerState<ImuWaveChart> createState() => _ImuWaveChartState();
}

class _ImuWaveChartState extends ConsumerState<ImuWaveChart> {
  final List<ImuSnapshot> _buffer = [];
  static const int maxPoints = 250; // 10 seconds @ 25Hz

  @override
  Widget build(BuildContext context) {
    final motionData = ref.watch(motionStreamProvider);
    motionData.whenData((imu) {
      _buffer.add(imu);
      if (_buffer.length > maxPoints) _buffer.removeAt(0);
    });

    if (_buffer.isEmpty) {
      return const SizedBox(
        height: 150,
        child: Center(
          child: Text(
            '等待数据...',
            style: TextStyle(color: AppColors.textSecondary),
          ),
        ),
      );
    }

    return RepaintBoundary(
      child: SizedBox(
        height: 150,
        child: LineChart(
          LineChartData(
            gridData: FlGridData(show: false),
            titlesData: FlTitlesData(show: false),
            borderData: FlBorderData(show: false),
            lineBarsData: [
              // Pitch — 运动胶囊绿色
              _makeLine(AppColors.motionPrimary, (imu) => imu.pitch),
              // Roll — 环境胶囊青色
              _makeLine(AppColors.envPrimary, (imu) => imu.roll),
              // Accel — 警告胶囊琥珀
              _makeLine(
                AppColors.warning,
                (imu) => imu.accelMag * 100,
              ), // scale up for visibility
            ],
          ),
        ),
      ),
    );
  }

  LineChartBarData _makeLine(Color color, double Function(ImuSnapshot) getter) {
    return LineChartBarData(
      spots: _buffer
          .asMap()
          .entries
          .map((e) => FlSpot(e.key.toDouble(), getter(_buffer[e.key])))
          .toList(),
      isCurved: true,
      color: color,
      barWidth: 2,
      dotData: const FlDotData(show: false),
      belowBarData: BarAreaData(
        show: true,
        color: color.withValues(alpha: 0.08),
      ),
    );
  }
}
