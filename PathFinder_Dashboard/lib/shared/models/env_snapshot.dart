import 'dart:typed_data';

class EnvSnapshot {
  final DateTime timestamp;
  final double temperature; // °C
  final double humidity; // %
  final int pressure; // Pa
  final double altitude; // m
  final double uvIndex;

  const EnvSnapshot({
    required this.timestamp,
    required this.temperature,
    required this.humidity,
    required this.pressure,
    required this.altitude,
    required this.uvIndex,
  });

  /// Decode from BLE C2 characteristic (20 bytes)
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
    );
  }

  /// Create mock data for development
  factory EnvSnapshot.mock({
    double temperature = 25.0,
    double humidity = 55.0,
    int pressure = 101325,
    double altitude = 156.0,
    double uvIndex = 3.0,
  }) {
    return EnvSnapshot(
      timestamp: DateTime.now(),
      temperature: temperature,
      humidity: humidity,
      pressure: pressure,
      altitude: altitude,
      uvIndex: uvIndex,
    );
  }
}
