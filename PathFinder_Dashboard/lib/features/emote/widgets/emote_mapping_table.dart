import 'package:flutter/material.dart';
import '../../../shared/models/emote_rules.dart';
import '../../../shared/models/emote_info.dart';
import '../../../shared/models/env_snapshot.dart';
import '../../../app/theme/app_colors.dart';

class EmoteMappingTable extends StatelessWidget {
  final EnvSnapshot? currentEnv;
  final double currentPitch;
  final double currentRoll;
  final EmoteTrigger? activeTrigger;

  const EmoteMappingTable({
    super.key,
    required this.currentEnv,
    required this.currentPitch,
    required this.currentRoll,
    this.activeTrigger,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      children: emoteRules.map((rule) {
        final isActive = activeTrigger == rule.trigger;
        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          margin: const EdgeInsets.symmetric(vertical: 2),
          decoration: BoxDecoration(
            color: isActive
                ? rule.trigger == EmoteTrigger.normal
                      ? AppColors.motionPrimary.withValues(alpha: 0.15)
                      : AppColors.warning.withValues(alpha: 0.15)
                : Colors.transparent,
            borderRadius: BorderRadius.circular(6),
            border: isActive
                ? Border.all(
                    color: isActive
                        ? AppColors.warning
                        : AppColors.motionPrimary,
                    width: 1,
                  )
                : null,
          ),
          child: Row(
            children: [
              Text(
                rule.condition,
                style: TextStyle(
                  color: isActive
                      ? AppColors.textPrimary
                      : AppColors.textSecondary,
                  fontWeight: isActive ? FontWeight.w600 : FontWeight.w400,
                  fontSize: 13,
                ),
              ),
              const Spacer(),
              Text(
                '→ ${rule.emoteName}',
                style: TextStyle(
                  color: isActive ? AppColors.warning : AppColors.textSecondary,
                  fontWeight: FontWeight.w500,
                  fontSize: 13,
                ),
              ),
            ],
          ),
        );
      }).toList(),
    );
  }
}
