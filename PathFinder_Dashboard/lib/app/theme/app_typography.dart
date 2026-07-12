import 'package:flutter/material.dart';

abstract class AppTypography {
  static const displayLarge = TextStyle(
    fontSize: 32,
    fontWeight: FontWeight.w700,
    color: Color(0xFFE0E0E8),
  );
  static const headlineMedium = TextStyle(
    fontSize: 20,
    fontWeight: FontWeight.w600,
    color: Color(0xFFE0E0E8),
  );
  static const titleMedium = TextStyle(
    fontSize: 16,
    fontWeight: FontWeight.w500,
    color: Color(0xFFE0E0E8),
  );
  static const bodyLarge = TextStyle(
    fontSize: 16,
    fontWeight: FontWeight.w400,
    color: Color(0xFFE0E0E8),
  );
  static const bodyMedium = TextStyle(
    fontSize: 14,
    fontWeight: FontWeight.w400,
    color: Color(0xFFE0E0E8),
  );
  static const labelLarge = TextStyle(
    fontSize: 14,
    fontWeight: FontWeight.w500,
    color: Color(0xFF888899),
  );
  static const labelSmall = TextStyle(
    fontSize: 11,
    fontWeight: FontWeight.w500,
    color: Color(0xFF888899),
  );
  static const metricValue = TextStyle(
    fontSize: 28,
    fontWeight: FontWeight.w700,
    color: Color(0xFFE0E0E8),
  );
  static const metricUnit = TextStyle(
    fontSize: 14,
    fontWeight: FontWeight.w400,
    color: Color(0xFF888899),
  );
}
