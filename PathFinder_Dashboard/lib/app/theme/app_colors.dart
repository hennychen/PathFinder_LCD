import 'package:flutter/material.dart';

/// EMA 表情胶囊配色体系
/// 对应 ESP32 固件 main.c 中 capsule_set_normal_style / capsule_set_alert_style
abstract class AppColors {
  static const background = Color(0xFF0A0A0F);
  static const surface = Color(0xFF1A1A24);

  // ---- 基础强调色 ----
  static const envPrimary = Color(0xFF00B4FF); // 青色 — 环境胶囊
  static const motionPrimary = Color(0xFF00FF88); // 绿色 — 运动胶囊
  static const warning = Color(0xFFFFB400); // 琥珀 — 警告胶囊
  static const urgent = Color(0xFFFF5050); // 红色 — 紧急胶囊

  // ---- 文字颜色（高亮态）----
  static const envText = Color(0xFF00C8FF); // 环境胶囊文字色
  static const motionText = Color(0xFF00FF88); // 运动胶囊文字色
  static const warningText = Color(0xFFFFC850); // 警告胶囊文字色
  static const urgentText = Color(0xFFFF7878); // 紧急胶囊文字色

  // ---- EMA 胶囊风格半透明背景 ----
  static const envCapsuleBg = Color(0x2E00B4FF); // ~18% opacity
  static const motionCapsuleBg = Color(0x1F00FF88); // ~12% opacity
  static const warningCapsuleBg = Color(0x33FFB400); // ~20% opacity
  static const urgentCapsuleBg = Color(0x33FF5050); // ~20% opacity

  // ---- 罗盘 / 追踪配色 ----
  static const compassPrimary = Color(0xFFFF8800); // 橙色 — 罗盘
  static const compassText = Color(0xFFFFAA44);
  static const trackerPrimary = Color(0xFFB468FF); // 紫色 — 追踪
  static const trackerText = Color(0xFFC88AFF);

  // ---- 通用文字 ----
  static const textPrimary = Color(0xFFE0E0E8);
  static const textSecondary = Color(0xFF888899);
  static const divider = Color(0xFF333344);
}
