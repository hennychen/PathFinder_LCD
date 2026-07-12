import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

class AttitudeIndicator extends StatelessWidget {
  final double pitch; // degrees
  final double roll; // degrees

  const AttitudeIndicator({super.key, required this.pitch, required this.roll});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 120,
      height: 120,
      child: CustomPaint(
        painter: _AttitudePainter(pitch: pitch, roll: roll),
        child: Center(
          child: Text(
            '${pitch.toStringAsFixed(1)}°',
            style: const TextStyle(
              color: AppColors.motionText,
              fontSize: 16,
              fontWeight: FontWeight.w800,
            ),
          ),
        ),
      ),
    );
  }
}

class _AttitudePainter extends CustomPainter {
  final double pitch, roll;
  _AttitudePainter({required this.pitch, required this.roll});

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = size.width / 2 - 4;

    // Background circle — EMA motion capsule style
    canvas.drawCircle(
      center,
      radius,
      Paint()..color = AppColors.motionCapsuleBg,
    );
    canvas.drawCircle(
      center,
      radius,
      Paint()
        ..color = AppColors.motionPrimary.withValues(alpha: 0.4)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5,
    );

    // Crosshair — motion green tinted
    canvas.drawLine(
      Offset(center.dx - radius * 0.3, center.dy),
      Offset(center.dx + radius * 0.3, center.dy),
      Paint()
        ..color = AppColors.motionPrimary.withValues(alpha: 0.2)
        ..strokeWidth = 0.5,
    );
    canvas.drawLine(
      Offset(center.dx, center.dy - radius * 0.3),
      Offset(center.dx, center.dy + radius * 0.3),
      Paint()
        ..color = AppColors.motionPrimary.withValues(alpha: 0.2)
        ..strokeWidth = 0.5,
    );

    // Bubble (roll/pitch mapped to position)
    final bubbleX = center.dx + (roll / 45.0) * radius * 0.7;
    final bubbleY = center.dy - (pitch / 45.0) * radius * 0.7;
    canvas.drawCircle(
      Offset(
        bubbleX.clamp(center.dx - radius * 0.8, center.dx + radius * 0.8),
        bubbleY.clamp(center.dy - radius * 0.8, center.dy + radius * 0.8),
      ),
      8,
      Paint()..color = AppColors.motionPrimary,
    );
  }

  @override
  bool shouldRepaint(covariant _AttitudePainter old) =>
      old.pitch != pitch || old.roll != roll;
}
