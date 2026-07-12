import 'package:flutter/material.dart';

class AnimatedCounter extends StatelessWidget {
  final double value;
  final Color color;
  final int decimals;
  final double fontSize;
  final FontWeight fontWeight;

  const AnimatedCounter({
    super.key,
    required this.value,
    required this.color,
    this.decimals = 1,
    this.fontSize = 40,
    this.fontWeight = FontWeight.w900,
  });

  @override
  Widget build(BuildContext context) {
    return TweenAnimationBuilder<double>(
      tween: Tween(begin: value, end: value),
      duration: const Duration(milliseconds: 300),
      curve: Curves.easeOut,
      builder: (context, animatedValue, child) {
        return Text(
          animatedValue.toStringAsFixed(decimals),
          style: TextStyle(
            fontSize: fontSize,
            fontWeight: fontWeight,
            color: color,
            letterSpacing: -1,
            height: 1.0,
          ),
        );
      },
    );
  }
}
