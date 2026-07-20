import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

/// 声源极坐标雷达图
///
/// 圆形雷达，显示声源角度方向。扇形指针指向 [angle]，
/// 外圈标注 N/E/S/W 方位刻度。
class SoundRadarChart extends StatefulWidget {
  final double angle; // 0~360°
  final bool valid;
  final int confidence; // 0-100

  const SoundRadarChart({
    super.key,
    required this.angle,
    required this.valid,
    required this.confidence,
  });

  @override
  State<SoundRadarChart> createState() => _SoundRadarChartState();
}

class _SoundRadarChartState extends State<SoundRadarChart>
    with SingleTickerProviderStateMixin {
  late AnimationController _sweepController;

  @override
  void initState() {
    super.initState();
    _sweepController = AnimationController(
      duration: const Duration(milliseconds: 3000),
      vsync: this,
    )..repeat();
  }

  @override
  void dispose() {
    _sweepController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: CustomPaint(
        size: const Size(double.infinity, 240),
        painter: _RadarPainter(
          angle: widget.angle,
          valid: widget.valid,
          confidence: widget.confidence,
          sweep: _sweepController,
        ),
      ),
    );
  }
}

class _RadarPainter extends CustomPainter {
  final double angle;
  final bool valid;
  final int confidence;
  final Animation<double> sweep;

  _RadarPainter({
    required this.angle,
    required this.valid,
    required this.confidence,
    required this.sweep,
  }) : super(repaint: sweep);

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    final radius = math.min(cx, cy) - 20.0;
    final center = Offset(cx, cy);

    // 背景同心圆
    final gridPaint = Paint()
      ..color = AppColors.trackerPrimary.withValues(alpha: 0.15)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1;
    for (var i = 1; i <= 3; i++) {
      canvas.drawCircle(center, radius * i / 3, gridPaint);
    }

    // 十字交叉线
    final linePaint = Paint()
      ..color = AppColors.trackerPrimary.withValues(alpha: 0.1)
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(cx - radius, cy),
      Offset(cx + radius, cy),
      linePaint,
    );
    canvas.drawLine(
      Offset(cx, cy - radius),
      Offset(cx, cy + radius),
      linePaint,
    );

    // 扫描线（旋转扇形）
    final sweepAngle = sweep.value * 2 * math.pi;
    final sweepPaint = Paint()
      ..shader = SweepGradient(
        startAngle: sweepAngle - math.pi / 3,
        endAngle: sweepAngle,
        colors: [
          AppColors.trackerPrimary.withValues(alpha: 0.0),
          AppColors.trackerPrimary.withValues(alpha: 0.15),
        ],
      ).createShader(Rect.fromCircle(center: center, radius: radius));
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      sweepAngle - math.pi / 3,
      math.pi / 3,
      true,
      sweepPaint,
    );

    // 方位刻度 N/E/S/W
    _drawDirectionLabel(canvas, center, radius + 8, 0, 'N');
    _drawDirectionLabel(canvas, center, radius + 8, 90, 'E');
    _drawDirectionLabel(canvas, center, radius + 8, 180, 'S');
    _drawDirectionLabel(canvas, center, radius + 8, 270, 'W');

    if (!valid) {
      // 无效信号 — 显示灰色虚线圆
      final dashedPaint = Paint()
        ..color = AppColors.textSecondary.withValues(alpha: 0.3)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2;
      canvas.drawCircle(center, radius * 0.5, dashedPaint);
      return;
    }

    // 声源角度指针 (0° = 北/顶部, 顺时针)
    final rad = (angle - 90) * math.pi / 180.0;
    final tipX = cx + radius * 0.85 * math.cos(rad);
    final tipY = cy + radius * 0.85 * math.sin(rad);

    // 指针线
    final pointerPaint = Paint()
      ..color = AppColors.trackerPrimary
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(center, Offset(tipX, tipY), pointerPaint);

    // 声源点（带光晕）
    final glowPaint = Paint()
      ..color = AppColors.trackerPrimary.withValues(alpha: 0.3)
      ..style = PaintingStyle.fill;
    canvas.drawCircle(Offset(tipX, tipY), 16, glowPaint);
    final dotPaint = Paint()
      ..color = AppColors.trackerText
      ..style = PaintingStyle.fill;
    canvas.drawCircle(Offset(tipX, tipY), 8, dotPaint);

    // 中心点
    canvas.drawCircle(center, 4, Paint()..color = AppColors.trackerPrimary);
  }

  void _drawDirectionLabel(
    Canvas canvas,
    Offset center,
    double r,
    double angleDeg,
    String label,
  ) {
    final rad = (angleDeg - 90) * math.pi / 180.0;
    final x = center.dx + r * math.cos(rad);
    final y = center.dy + r * math.sin(rad);

    final tp = TextPainter(
      text: TextSpan(
        text: label,
        style: const TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w600,
          color: AppColors.textSecondary,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(x - tp.width / 2, y - tp.height / 2));
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) {
    if (oldDelegate is! _RadarPainter) return true;
    return oldDelegate.angle != angle ||
        oldDelegate.valid != valid ||
        oldDelegate.confidence != confidence;
  }
}
