import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../shared/models/emote_rules.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/emote_mapping_table.dart';
import 'widgets/emote_gallery.dart';

class EmoteScreen extends ConsumerWidget {
  const EmoteScreen({super.key});

  static const emoteNames = [
    'Angry',
    'Asleep',
    'Badminton',
    'Confident',
    'Cry',
    'Investigate',
    'Laugh',
    'Leisure',
    'Mock',
    'Music',
    'Mute',
    'Panic',
    'Ponder',
    'Question',
    'Sad',
    'Shocked',
    'Shy',
    'Sigh',
    'Smile',
    'Snigger',
    'Yawn',
    'Yummy',
    'Unknown1',
    'Unknown2',
  ];

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final emoteData = ref.watch(emoteStreamProvider);
    final envData = ref.watch(envStreamProvider);
    final motionData = ref.watch(motionStreamProvider);

    return connState.when(
      data: (state) => state == BleConnectionState.connected
          ? emoteData.when(
              data: (emote) {
                final env = envData.valueOrNull;
                final imu = motionData.valueOrNull;
                final rule = evaluateEmote(
                  env,
                  imu?.pitch ?? 0,
                  imu?.roll ?? 0,
                );
                return SingleChildScrollView(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      // Current emote card
                      Container(
                        width: double.infinity,
                        padding: const EdgeInsets.all(24),
                        decoration: BoxDecoration(
                          color: AppColors.surface,
                          borderRadius: BorderRadius.circular(16),
                          border: Border.all(
                            color: AppColors.warning.withValues(alpha: 0.3),
                          ),
                        ),
                        child: Column(
                          children: [
                            Icon(
                              Icons.face,
                              size: 80,
                              color: AppColors.warning,
                            ),
                            const SizedBox(height: 12),
                            Text(
                              emote.friendlyName,
                              style: const TextStyle(
                                fontSize: 28,
                                fontWeight: FontWeight.w700,
                                color: AppColors.textPrimary,
                              ),
                            ),
                            const SizedBox(height: 4),
                            Text(
                              emote.trigger.label,
                              style: const TextStyle(
                                fontSize: 14,
                                color: AppColors.warning,
                              ),
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 24),
                      // Mapping table
                      const Text(
                        '映射规则表',
                        style: TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w600,
                          color: AppColors.textPrimary,
                        ),
                      ),
                      const SizedBox(height: 12),
                      EmoteMappingTable(
                        currentEnv: env,
                        currentPitch: imu?.pitch ?? 0,
                        currentRoll: imu?.roll ?? 0,
                        activeTrigger: rule.trigger,
                      ),
                      const SizedBox(height: 24),
                      // Gallery
                      const Text(
                        '表情总览 (24种)',
                        style: TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w600,
                          color: AppColors.textPrimary,
                        ),
                      ),
                      const SizedBox(height: 12),
                      EmoteGallery(
                        currentEmoteId: emote.emoteId,
                        emoteNames: emoteNames,
                      ),
                    ],
                  ),
                );
              },
              loading: () => const Center(
                child: CircularProgressIndicator(color: AppColors.warning),
              ),
              error: (e, _) => Center(child: Text('Error: $e')),
            )
          : const _NotConnectedPlaceholder(),
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (_, __) => const Center(child: Text('Error')),
    );
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
              color: AppColors.warning.withValues(alpha: 0.7),
              fontSize: 13,
            ),
          ),
        ],
      ),
    );
  }
}
