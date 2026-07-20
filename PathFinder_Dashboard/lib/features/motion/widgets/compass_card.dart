import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';
import '../../../shared/widgets/animated_counter.dart';
import '../../../shared/models/compass_snapshot.dart';

/// 罗盘方位角卡片 — 展示 heading 数值 + 方位字母
class CompassCard extends StatelessWidget {
  final CompassSnapshot compass;

  const CompassCard({super.key, required this.compass});

  @override
  Widget build(BuildContext context) {
    final color = compass.valid
        ? AppColors.compassText
        : AppColors.textSecondary;

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      decoration: BoxDecoration(
        color: AppColors.compassPrimary.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: AppColors.compassPrimary.withValues(alpha: 0.4),
          width: 1.5,
        ),
      ),
      child: Row(
        children: [
          // 方位字母（大号）
          Container(
            width: 56,
            height: 56,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: AppColors.compassPrimary.withValues(alpha: 0.2),
              border: Border.all(
                color: AppColors.compassPrimary.withValues(alpha: 0.5),
                width: 2,
              ),
            ),
            alignment: Alignment.center,
            child: Text(
              compass.valid ? compass.cardinal : '--',
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.w900,
                color: color,
              ),
            ),
          ),
          const SizedBox(width: 16),
          // 标签 + 数值
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  compass.valid ? '罗盘方位角' : '罗盘未就绪',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w500,
                    color: color.withValues(alpha: 0.7),
                  ),
                ),
                const SizedBox(height: 4),
                Row(
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    AnimatedCounter(
                      value: compass.valid ? compass.heading : 0,
                      color: color,
                      decimals: 1,
                      fontSize: 32,
                    ),
                    const SizedBox(width: 4),
                    Text(
                      '°',
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
          // 磁力计来源标签
          if (compass.valid)
            Text(
              _sourceLabel(compass.source),
              style: TextStyle(
                fontSize: 11,
                color: color.withValues(alpha: 0.5),
              ),
            ),
        ],
      ),
    );
  }

  String _sourceLabel(int source) {
    switch (source) {
      case 1:
        return 'HMC5883L';
      case 2:
        return 'AK8963';
      default:
        return '';
    }
  }
}
