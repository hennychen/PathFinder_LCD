import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

class DeviceTile extends StatelessWidget {
  final String name;
  final int rssi;
  final VoidCallback onTap;

  const DeviceTile({
    super.key,
    required this.name,
    required this.rssi,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      child: ListTile(
        onTap: onTap,
        leading: const Icon(Icons.bluetooth, color: AppColors.envPrimary),
        title: Text(
          name,
          style: const TextStyle(
            color: AppColors.textPrimary,
            fontWeight: FontWeight.w600,
          ),
        ),
        subtitle: Text(
          'RSSI: ${rssi}dBm',
          style: const TextStyle(color: AppColors.textSecondary),
        ),
        trailing: const Icon(
          Icons.chevron_right,
          color: AppColors.textSecondary,
        ),
      ),
    );
  }
}
