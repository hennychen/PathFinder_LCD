import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

// Service UUID
final pfServiceUuid = Uuid.parse('0000fe00-0000-1000-8000-00805f9b34fb');

// Characteristic UUIDs
final c1DeviceInfoUuid = Uuid.parse('0000fe01-0000-1000-8000-00805f9b34fb');
final c2EnvDataUuid = Uuid.parse('0000fe02-0000-1000-8000-00805f9b34fb');
final c3MotionDataUuid = Uuid.parse('0000fe03-0000-1000-8000-00805f9b34fb');
final c4EmoteStateUuid = Uuid.parse('0000fe04-0000-1000-8000-00805f9b34fb');
final c5RawImuUuid = Uuid.parse('0000fe05-0000-1000-8000-00805f9b34fb');
