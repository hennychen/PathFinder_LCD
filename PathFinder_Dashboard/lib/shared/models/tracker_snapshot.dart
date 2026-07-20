import 'dart:typed_data';

/// 追踪聚合快照（声源 + 人脸 + 状态），对应 BLE C7 特征值 (15 bytes)。
class TrackerSnapshot {
  final DateTime timestamp;
  final double soundAngle; // 0~360°
  final int soundConfidence; // 0-100
  final bool soundValid;
  final int faceCount;
  final bool faceFound;
  final int faceCx;
  final int faceCy;
  final int faceW;
  final int faceH;
  final int trackState;

  const TrackerSnapshot({
    required this.timestamp,
    required this.soundAngle,
    required this.soundConfidence,
    required this.soundValid,
    required this.faceCount,
    required this.faceFound,
    required this.faceCx,
    required this.faceCy,
    required this.faceW,
    required this.faceH,
    required this.trackState,
  });

  /// Decode from BLE C7 characteristic (15 bytes)
  /// [0-1]   sound_angle_u16 LE (×10)
  /// [2]     sound_confidence_u8
  /// [3]     sound_valid_u8
  /// [4]     face_count_u8
  /// [5]     face_found_u8
  /// [6-7]   face_cx_i16 LE
  /// [8-9]   face_cy_i16 LE
  /// [10-11] face_w_u16 LE
  /// [12-13] face_h_u16 LE
  /// [14]    track_state_u8
  factory TrackerSnapshot.fromBle(Uint8List data) {
    if (data.length != 15) {
      throw FormatException(
        'Tracker frame must be 15 bytes, got ${data.length}',
      );
    }
    final bd = ByteData.sublistView(data);
    return TrackerSnapshot(
      timestamp: DateTime.now(),
      soundAngle: bd.getUint16(0, Endian.little) / 10.0,
      soundConfidence: data[2],
      soundValid: data[3] != 0,
      faceCount: data[4],
      faceFound: data[5] != 0,
      faceCx: bd.getInt16(6, Endian.little),
      faceCy: bd.getInt16(8, Endian.little),
      faceW: bd.getUint16(10, Endian.little),
      faceH: bd.getUint16(12, Endian.little),
      trackState: data[14],
    );
  }

  factory TrackerSnapshot.mock({
    double soundAngle = 90.0,
    int soundConfidence = 80,
    bool soundValid = true,
    int faceCount = 1,
    bool faceFound = true,
    int faceCx = 160,
    int faceCy = 120,
    int faceW = 80,
    int faceH = 100,
    int trackState = 1,
  }) {
    return TrackerSnapshot(
      timestamp: DateTime.now(),
      soundAngle: soundAngle,
      soundConfidence: soundConfidence,
      soundValid: soundValid,
      faceCount: faceCount,
      faceFound: faceFound,
      faceCx: faceCx,
      faceCy: faceCy,
      faceW: faceW,
      faceH: faceH,
      trackState: trackState,
    );
  }
}
