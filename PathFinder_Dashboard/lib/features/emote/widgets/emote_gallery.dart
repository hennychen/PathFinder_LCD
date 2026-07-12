import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

class EmoteGallery extends StatelessWidget {
  final int currentEmoteId;
  final List<String> emoteNames;

  const EmoteGallery({
    super.key,
    required this.currentEmoteId,
    required this.emoteNames,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 200,
      child: GridView.builder(
        scrollDirection: Axis.horizontal,
        gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
          crossAxisCount: 3,
          childAspectRatio: 1.0,
          mainAxisSpacing: 4,
          crossAxisSpacing: 4,
        ),
        itemCount: emoteNames.length,
        itemBuilder: (context, index) {
          final isActive = index == currentEmoteId;
          return Container(
            decoration: BoxDecoration(
              color: AppColors.surface,
              borderRadius: BorderRadius.circular(8),
              border: Border.all(
                color: isActive ? AppColors.warning : AppColors.divider,
                width: isActive ? 2 : 1,
              ),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(
                  Icons.face,
                  size: 32,
                  color: isActive ? AppColors.warning : AppColors.textSecondary,
                ),
                const SizedBox(height: 4),
                Text(
                  emoteNames[index],
                  style: TextStyle(
                    color: isActive
                        ? AppColors.warning
                        : AppColors.textSecondary,
                    fontSize: 10,
                    fontWeight: isActive ? FontWeight.w600 : FontWeight.w400,
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}
