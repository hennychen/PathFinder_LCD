import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';
import '../../../shared/models/tracker_snapshot.dart';

/// 人脸检测信息卡片
///
/// 展示人脸数量、坐标、尺寸以及追踪状态。
class FaceInfoCard extends StatelessWidget {
  final TrackerSnapshot tracker;

  const FaceInfoCard({super.key, required this.tracker});

  @override
  Widget build(BuildContext context) {
    final found = tracker.faceFound;
    final color = found ? AppColors.trackerText : AppColors.textSecondary;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.trackerPrimary.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: AppColors.trackerPrimary.withValues(alpha: 0.3),
          width: 1.5,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 标题行
          Row(
            children: [
              Icon(Icons.face, size: 20, color: color),
              const SizedBox(width: 8),
              Text(
                '人脸检测',
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                  color: color,
                ),
              ),
              const Spacer(),
              _StatusBadge(found: found),
            ],
          ),
          const SizedBox(height: 16),
          // 数据网格
          if (found)
            _buildDataGrid(color)
          else
            const Center(
              child: Padding(
                padding: EdgeInsets.symmetric(vertical: 20),
                child: Text(
                  '未检测到人脸',
                  style: TextStyle(
                    fontSize: 14,
                    color: AppColors.textSecondary,
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }

  Widget _buildDataGrid(Color color) {
    return GridView.count(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      crossAxisCount: 2,
      childAspectRatio: 3.2,
      mainAxisSpacing: 8,
      crossAxisSpacing: 12,
      children: [
        _DataCell(label: '中心 X', value: '${tracker.faceCx}', color: color),
        _DataCell(label: '中心 Y', value: '${tracker.faceCy}', color: color),
        _DataCell(label: '宽度', value: '${tracker.faceW}px', color: color),
        _DataCell(label: '高度', value: '${tracker.faceH}px', color: color),
      ],
    );
  }
}

class _StatusBadge extends StatelessWidget {
  final bool found;
  const _StatusBadge({required this.found});

  @override
  Widget build(BuildContext context) {
    final color = found ? AppColors.motionPrimary : AppColors.textSecondary;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: color.withValues(alpha: 0.4)),
      ),
      child: Text(
        found ? '追踪中' : '待机',
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.w600,
          color: color,
        ),
      ),
    );
  }
}

class _DataCell extends StatelessWidget {
  final String label;
  final String value;
  final Color color;

  const _DataCell({
    required this.label,
    required this.value,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          label,
          style: TextStyle(fontSize: 11, color: color.withValues(alpha: 0.6)),
        ),
        const SizedBox(height: 2),
        Text(
          value,
          style: TextStyle(
            fontSize: 18,
            fontWeight: FontWeight.w700,
            color: color,
          ),
        ),
      ],
    );
  }
}
