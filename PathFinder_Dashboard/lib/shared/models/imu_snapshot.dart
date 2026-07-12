import 'dart:typed_data';
import 'motion_event.dart';

class ImuSnapshot {
  final DateTime timestamp;
  final double pitch; // °
  final double roll; // °
  final double accelMag; // g deviation from 1g
  final MotionEvent event;
  final int confidence; // 0~100

  const ImuSnapshot({
    required this.timestamp,
    required this.pitch,
    required this.roll,
    required this.accelMag,
    required this.event,
    required this.confidence,
  });

  /// Decode from BLE C3 characteristic (8 bytes)
  factory ImuSnapshot.fromBle(Uint8List data) {
    if (data.length != 8) {
      throw FormatException('Motion frame must be 8 bytes, got ${data.length}');
    }
    final bd = ByteData.sublistView(data);
    return ImuSnapshot(
      timestamp: DateTime.now(),
      pitch: bd.getInt16(0, Endian.little) / 100.0,
      roll: bd.getInt16(2, Endian.little) / 100.0,
      accelMag: bd.getUint16(4, Endian.little) / 1000.0,
      event: MotionEvent.fromCode(data[6]),
      confidence: data[7],
    );
  }

  factory ImuSnapshot.mock({
    double pitch = 0,
    double roll = 0,
    double accelMag = 0.01,
    MotionEvent event = MotionEvent.idle,
    int confidence = 100,
  }) {
    return ImuSnapshot(
      timestamp: DateTime.now(),
      pitch: pitch,
      roll: roll,
      accelMag: accelMag,
      event: event,
      confidence: confidence,
    );
  }
}
