import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:pathfinder_dashboard/shared/models/env_snapshot.dart';

void main() {
  group('EnvSnapshot.fromBle', () {
    test('correctly decodes 20-byte environment frame', () {
      final bytes = Uint8List(20);
      final bd = ByteData.sublistView(bytes);
      bd.setInt16(4, 2635, Endian.little); // 26.35°C
      bd.setUint16(6, 5800, Endian.little); // 58.00%
      bd.setUint32(8, 101325, Endian.little); // 101325 Pa
      bd.setInt16(12, 1560, Endian.little); // 156.0 m
      bd.setUint16(14, 320, Endian.little); // UV 3.20

      final env = EnvSnapshot.fromBle(bytes);

      expect(env.temperature, closeTo(26.35, 0.01));
      expect(env.humidity, closeTo(58.0, 0.01));
      expect(env.pressure, 101325);
      expect(env.altitude, closeTo(156.0, 0.1));
      expect(env.uvIndex, closeTo(3.2, 0.01));
    });

    test('throws on wrong frame length', () {
      expect(() => EnvSnapshot.fromBle(Uint8List(19)), throwsFormatException);
      expect(() => EnvSnapshot.fromBle(Uint8List(21)), throwsFormatException);
    });

    test('mock factory creates valid instance', () {
      final env = EnvSnapshot.mock(temperature: 30.0, uvIndex: 5.0);
      expect(env.temperature, 30.0);
      expect(env.uvIndex, 5.0);
    });
  });
}
