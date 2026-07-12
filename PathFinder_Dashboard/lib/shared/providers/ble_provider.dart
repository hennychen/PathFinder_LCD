import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../core/ble/reactive_ble_service.dart';

final bleServiceProvider = Provider<BleServiceInterface>((ref) {
  final service = ReactiveBleService();
  ref.onDispose(() => service.dispose());
  return service;
});

final connectionStateProvider = StreamProvider<BleConnectionState>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.connectionState;
});
