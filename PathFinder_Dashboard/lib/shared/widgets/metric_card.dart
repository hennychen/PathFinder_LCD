import 'package:flutter/material.dart';
import 'animated_counter.dart';

/// EMA 胶囊风格数据卡片
///
/// 背景使用强调色的半透明叠加（对应 ESP32 capsule 胶囊样式），
/// 数据数字超大超粗（w900 / 40px+），边框使用强调色高透明度。
class MetricCard extends StatelessWidget {
  final double value;
  final String unit;
  final String label;
  final Color color;
  final Color? backgroundColor;
  final VoidCallback? onTap;
  final int decimals;
  final double fontSize;

  const MetricCard({
    super.key,
    required this.value,
    required this.unit,
    required this.label,
    required this.color,
    this.backgroundColor,
    this.onTap,
    this.decimals = 1,
    this.fontSize = 40,
  });

  @override
  Widget build(BuildContext context) {
    final bg = backgroundColor ?? color.withValues(alpha: 0.15);

    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 16),
        decoration: BoxDecoration(
          color: bg,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: color.withValues(alpha: 0.4), width: 1.5),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              label,
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w500,
                color: color.withValues(alpha: 0.7),
              ),
            ),
            const SizedBox(height: 8),
            Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                AnimatedCounter(
                  value: value,
                  color: color,
                  decimals: decimals,
                  fontSize: fontSize,
                ),
                const SizedBox(width: 4),
                Text(
                  unit,
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                    color: color.withValues(alpha: 0.6),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
