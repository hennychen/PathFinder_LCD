import 'package:flutter/material.dart';

enum MotionEvent {
  idle(0, '静止', Colors.grey),
  cruise(1, '匀速行驶', Colors.teal),
  accel(2, '急加速', Colors.orange),
  brake(3, '急刹车', Colors.deepOrange),
  turnLeft(4, '急左转', Colors.amber),
  turnRight(5, '急右转', Colors.amber),
  uphill(6, '上坡', Colors.lightBlue),
  downhill(7, '下坡', Colors.lightBlue),
  tiltLeft(8, '左倾斜', Colors.yellow),
  tiltRight(9, '右倾斜', Colors.yellow),
  bumpy(10, '颠簸', Colors.purple),
  collision(11, '碰撞', Colors.red),
  highSpeed(12, '高速行驶', Colors.cyan);

  final int code;
  final String label;
  final Color color;
  const MotionEvent(this.code, this.label, this.color);

  static MotionEvent fromCode(int code) {
    return values.firstWhere(
      (e) => e.code == code,
      orElse: () => MotionEvent.idle,
    );
  }
}
