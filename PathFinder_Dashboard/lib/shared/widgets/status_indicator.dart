import 'package:flutter/material.dart';
import '../../app/theme/app_colors.dart';

class StatusIndicator extends StatelessWidget {
  final String label;
  final bool active;
  final Color? activeColor;

  const StatusIndicator({
    super.key,
    required this.label,
    this.active = false,
    this.activeColor,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 8,
          height: 8,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: active
                ? (activeColor ?? AppColors.motionPrimary)
                : AppColors.divider,
          ),
        ),
        const SizedBox(width: 6),
        Text(
          label,
          style: TextStyle(
            fontSize: 12,
            color: active ? AppColors.textPrimary : AppColors.textSecondary,
          ),
        ),
      ],
    );
  }
}
