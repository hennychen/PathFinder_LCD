import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:pathfinder_dashboard/shared/models/env_snapshot.dart';
import 'package:pathfinder_dashboard/shared/models/imu_snapshot.dart';
import 'package:pathfinder_dashboard/shared/models/motion_event.dart';
import 'package:pathfinder_dashboard/shared/models/emote_info.dart';

void main() {
  group('BLE Codec', () {
    group('EnvSnapshot (C2, 20 bytes)', () {
      test('decodes temperature correctly', () {
        final bytes = Uint8List(20);
        ByteData.sublistView(bytes).setInt16(4, -500, Endian.little); // -5.00°C
        final env = EnvSnapshot.fromBle(bytes);
        expect(env.temperature, closeTo(-5.0, 0.01));
      });

      test('decodes max temperature', () {
        final bytes = Uint8List(20);
        ByteData.sublistView(
          bytes,
        ).setInt16(4, 32767, Endian.little); // 327.67°C
        final env = EnvSnapshot.fromBle(bytes);
        expect(env.temperature, closeTo(327.67, 0.01));
      });

      test('decodes all fields', () {
        final bytes = Uint8List(20);
        final bd = ByteData.sublistView(bytes);
        bd.setInt16(4, 2635, Endian.little);
        bd.setUint16(6, 5800, Endian.little);
        bd.setUint32(8, 101325, Endian.little);
        bd.setInt16(12, -150, Endian.little); // -15.0m
        bd.setUint16(14, 800, Endian.little); // UV 8.00
        final env = EnvSnapshot.fromBle(bytes);
        expect(env.temperature, closeTo(26.35, 0.01));
        expect(env.humidity, closeTo(58.0, 0.01));
        expect(env.pressure, 101325);
        expect(env.altitude, closeTo(-15.0, 0.1));
        expect(env.uvIndex, closeTo(8.0, 0.01));
      });
    });

    group('ImuSnapshot (C3, 8 bytes)', () {
      test('decodes pitch and roll', () {
        final bytes = Uint8List(8);
        final bd = ByteData.sublistView(bytes);
        bd.setInt16(0, 1234, Endian.little); // 12.34°
        bd.setInt16(2, -567, Endian.little); // -5.67°
        bd.setUint16(4, 150, Endian.little); // 0.150g
        bytes[6] = 3; // brake
        bytes[7] = 85; // 85%
        final imu = ImuSnapshot.fromBle(bytes);
        expect(imu.pitch, closeTo(12.34, 0.01));
        expect(imu.roll, closeTo(-5.67, 0.01));
        expect(imu.accelMag, closeTo(0.15, 0.001));
        expect(imu.event, MotionEvent.brake);
        expect(imu.confidence, 85);
      });
    });

    group('EmoteInfo (C4, 15 bytes)', () {
      test('decodes emote state', () {
        final bytes = Uint8List(15);
        bytes[0] = 5; // emoteId
        bytes[1] = 5; // name length
        bytes.setAll(2, 'Panic'.codeUnits);
        bytes[14] = 1; // UV Extreme trigger
        final emote = EmoteInfo.fromBle(bytes);
        expect(emote.emoteId, 5);
        expect(emote.friendlyName, 'Panic');
        expect(emote.trigger, EmoteTrigger.uvExtreme);
      });
    });
  });
}
