import 'dart:typed_data';

/// 罗盘方位角快照，对应 BLE C6 特征值 (4 bytes)。
class CompassSnapshot {
  final DateTime timestamp;
  final double heading; // 0~360°
  final bool valid;
  final int source; // 0=NONE, 1=HMC5883L, 2=AK8963

  const CompassSnapshot({
    required this.timestamp,
    required this.heading,
    required this.valid,
    required this.source,
  });

  /// Decode from BLE C6 characteristic (4 bytes)
  /// [0-1] heading_u16 LE (×100)
  /// [2]   valid_u8
  /// [3]   source_u8
  factory CompassSnapshot.fromBle(Uint8List data) {
    if (data.length != 4) {
      throw FormatException(
        'Compass frame must be 4 bytes, got ${data.length}',
      );
    }
    final bd = ByteData.sublistView(data);
    return CompassSnapshot(
      timestamp: DateTime.now(),
      heading: bd.getUint16(0, Endian.little) / 100.0,
      valid: data[2] != 0,
      source: data[3],
    );
  }

  factory CompassSnapshot.mock({
    double heading = 180.0,
    bool valid = true,
    int source = 1,
  }) {
    return CompassSnapshot(
      timestamp: DateTime.now(),
      heading: heading,
      valid: valid,
      source: source,
    );
  }

  /// 返回方位字母 (N/NE/E/SE/S/SW/W/NW)
  String get cardinal {
    const names = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
    final idx = ((heading + 22.5) % 360 / 45).floor() % 8;
    return names[idx];
  }
}
