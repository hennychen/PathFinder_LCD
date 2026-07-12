import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../shared/providers/sensor_provider.dart';
import '../../../app/theme/app_colors.dart';

class EventTimeline extends ConsumerWidget {
  const EventTimeline({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final motionData = ref.watch(motionStreamProvider);
    return motionData.when(
      data: (imu) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
        decoration: BoxDecoration(
          color: AppColors.motionCapsuleBg,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(
            color: AppColors.motionPrimary.withValues(alpha: 0.4),
            width: 1.5,
          ),
        ),
        child: Row(
          children: [
            Container(
              width: 10,
              height: 10,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: imu.event.color,
              ),
            ),
            const SizedBox(width: 8),
            Text(
              imu.event.label,
              style: TextStyle(
                color: imu.event.color,
                fontWeight: FontWeight.w800,
                fontSize: 16,
              ),
            ),
            const Spacer(),
            Text(
              '${imu.confidence}%',
              style: TextStyle(
                color: AppColors.motionText.withValues(alpha: 0.7),
                fontSize: 20,
                fontWeight: FontWeight.w900,
              ),
            ),
          ],
        ),
      ),
      loading: () => const SizedBox.shrink(),
      error: (_, __) => const SizedBox.shrink(),
    );
  }
}
