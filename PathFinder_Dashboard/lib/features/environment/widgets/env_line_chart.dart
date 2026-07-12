import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../shared/providers/sensor_provider.dart';
import '../../../shared/models/env_snapshot.dart';
import '../../../app/theme/app_colors.dart';

class EnvLineChart extends ConsumerStatefulWidget {
  const EnvLineChart({super.key});

  @override
  ConsumerState<EnvLineChart> createState() => _EnvLineChartState();
}

class _EnvLineChartState extends ConsumerState<EnvLineChart> {
  final List<EnvSnapshot> _buffer = [];
  static const int maxPoints = 60;

  @override
  Widget build(BuildContext context) {
    final envData = ref.watch(envStreamProvider);
    envData.whenData((env) {
      _buffer.add(env);
      if (_buffer.length > maxPoints) _buffer.removeAt(0);
    });

    return SizedBox(
      height: 200,
      child: _buffer.isEmpty
          ? const Center(
              child: Text(
                '等待数据...',
                style: TextStyle(color: AppColors.textSecondary),
              ),
            )
          : LineChart(
              LineChartData(
                gridData: FlGridData(
                  show: true,
                  drawVerticalLine: false,
                  getDrawingHorizontalLine: (v) =>
                      FlLine(color: AppColors.divider, strokeWidth: 0.5),
                ),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 40,
                      getTitlesWidget: (v, _) => Text(
                        v.toStringAsFixed(0),
                        style: const TextStyle(
                          color: AppColors.textSecondary,
                          fontSize: 10,
                        ),
                      ),
                    ),
                  ),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 20,
                      getTitlesWidget: (v, _) => Text(
                        '${-maxPoints + v.toInt()}s',
                        style: const TextStyle(
                          color: AppColors.textSecondary,
                          fontSize: 10,
                        ),
                      ),
                    ),
                  ),
                  topTitles: const AxisTitles(
                    sideTitles: SideTitles(showTitles: false),
                  ),
                  rightTitles: const AxisTitles(
                    sideTitles: SideTitles(showTitles: false),
                  ),
                ),
                borderData: FlBorderData(show: false),
                lineBarsData: [
                  LineChartBarData(
                    spots: _buffer
                        .asMap()
                        .entries
                        .map(
                          (e) => FlSpot(
                            e.key.toDouble(),
                            _buffer[e.key].temperature,
                          ),
                        )
                        .toList(),
                    isCurved: true,
                    color: AppColors.envPrimary,
                    barWidth: 2,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(
                      show: true,
                      color: AppColors.envPrimary.withValues(alpha: 0.1),
                    ),
                  ),
                ],
                lineTouchData: LineTouchData(enabled: true),
              ),
            ),
    );
  }
}
