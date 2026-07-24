import 'dart:typed_data';

class EnvSnapshot {
  final DateTime timestamp;
  final double temperature; // °C
  final double humidity; // %
  final int pressure; // Pa
  final double altitude; // m
  final double uvIndex;
  final double dustDensity; // mg/m³ (GP2Y1010AU0F)
  final int aqiLevel; // 0~4 (优/良/轻度/中度/重度)

  const EnvSnapshot({
    required this.timestamp,
    required this.temperature,
    required this.humidity,
    required this.pressure,
    required this.altitude,
    required this.uvIndex,
    required this.dustDensity,
    required this.aqiLevel,
  });

  /// Decode from BLE C2 characteristic (20 bytes)
  /// Layout:
  ///   [0-3]   magic header "ENV\0"
  ///   [4-5]   temperature × 100 (int16 LE)
  ///   [6-7]   humidity × 100 (uint16 LE)
  ///   [8-11]  pressure (uint32 LE)
  ///   [12-13] altitude × 10 (int16 LE)
  ///   [14-15] uv × 100 (uint16 LE)
  ///   [16-17] dust density × 100 (uint16 LE)
  ///   [18]    aqi_level (uint8) 0~4
  ///   [19]    reserved
  factory EnvSnapshot.fromBle(Uint8List data) {
    if (data.length != 20) {
      throw FormatException('Env frame must be 20 bytes, got ${data.length}');
    }
    final bd = ByteData.sublistView(data);
    return EnvSnapshot(
      timestamp: DateTime.now(),
      temperature: bd.getInt16(4, Endian.little) / 100.0,
      humidity: bd.getUint16(6, Endian.little) / 100.0,
      pressure: bd.getUint32(8, Endian.little),
      altitude: bd.getInt16(12, Endian.little) / 10.0,
      uvIndex: bd.getUint16(14, Endian.little) / 100.0,
      dustDensity: bd.getUint16(16, Endian.little) / 100.0,
      aqiLevel: data[18],
    );
  }

  /// AQI level → 中文标签
  String get aqiLabel {
    const labels = ['优', '良', '轻度污染', '中度污染', '重度污染'];
    if (aqiLevel >= 0 && aqiLevel < labels.length) return labels[aqiLevel];
    return '未知';
  }

  /// Create mock data for development
  factory EnvSnapshot.mock({
    double temperature = 25.0,
    double humidity = 55.0,
    int pressure = 101325,
    double altitude = 156.0,
    double uvIndex = 3.0,
    double dustDensity = 0.05,
    int aqiLevel = 0,
  }) {
    return EnvSnapshot(
      timestamp: DateTime.now(),
      temperature: temperature,
      humidity: humidity,
      pressure: pressure,
      altitude: altitude,
      uvIndex: uvIndex,
      dustDensity: dustDensity,
      aqiLevel: aqiLevel,
    );
  }
}
