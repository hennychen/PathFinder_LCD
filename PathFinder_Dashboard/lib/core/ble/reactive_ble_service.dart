import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'ble_service_interface.dart';
import 'ble_uuids.dart';
import '../../shared/models/env_snapshot.dart';
import '../../shared/models/imu_snapshot.dart';
import '../../shared/models/emote_info.dart';

/// 基于 flutter_reactive_ble 的真实 BLE 服务实现
class ReactiveBleService implements BleServiceInterface {
  final _ble = FlutterReactiveBle();

  // 连接状态
  BleConnectionState _currentState = BleConnectionState.disconnected;
  final _connectionController =
      StreamController<BleConnectionState>.broadcast();

  // 数据流
  final _envController = StreamController<EnvSnapshot>.broadcast();
  final _motionController = StreamController<ImuSnapshot>.broadcast();
  final _emoteController = StreamController<EmoteInfo>.broadcast();

  // 扫描结果
  final _scanResults = <DiscoveredDevice>[];
  final _devicesController =
      StreamController<List<DiscoveredDevice>>.broadcast();

  // 连接管理
  StreamSubscription? _scanSub;
  StreamSubscription<ConnectionStateUpdate>? _connSub;
  StreamSubscription<List<int>>? _envSub;
  StreamSubscription<List<int>>? _motionSub;
  StreamSubscription<List<int>>? _emoteSub;
  String? _connectedDeviceId;

  QualifiedCharacteristic _envChar(String id) => QualifiedCharacteristic(
    characteristicId: c2EnvDataUuid,
    serviceId: pfServiceUuid,
    deviceId: id,
  );

  QualifiedCharacteristic _motionChar(String id) => QualifiedCharacteristic(
    characteristicId: c3MotionDataUuid,
    serviceId: pfServiceUuid,
    deviceId: id,
  );

  QualifiedCharacteristic _emoteChar(String id) => QualifiedCharacteristic(
    characteristicId: c4EmoteStateUuid,
    serviceId: pfServiceUuid,
    deviceId: id,
  );

  @override
  Stream<BleConnectionState> get connectionState async* {
    yield _currentState;
    yield* _connectionController.stream;
  }

  @override
  Stream<EnvSnapshot> subscribeEnv() => _envController.stream;

  @override
  Stream<ImuSnapshot> subscribeMotion() => _motionController.stream;

  @override
  Stream<EmoteInfo> subscribeEmote() => _emoteController.stream;

  /// 扫描结果流
  Stream<List<DiscoveredDevice>> get devicesStream => _devicesController.stream;

  @override
  Future<void> startScan() async {
    _currentState = BleConnectionState.scanning;
    _connectionController.add(_currentState);
    _scanResults.clear();

    _scanSub?.cancel();
    debugPrint('[BLE] Starting scan...');
    _scanSub = _ble
        .scanForDevices(withServices: [], scanMode: ScanMode.lowLatency)
        .listen(
          (device) {
            debugPrint(
              '[BLE] Found: ${device.name} (${device.id}) RSSI=${device.rssi}',
            );
            if (device.name.isNotEmpty &&
                !_scanResults.any((d) => d.id == device.id)) {
              _scanResults.add(device);
              _devicesController.add(List.unmodifiable(_scanResults));
            }
          },
          onError: (e) {
            debugPrint('[BLE] Scan error: $e');
            _currentState = BleConnectionState.failed;
            _connectionController.add(_currentState);
          },
        );

    // 扫描 5 秒后自动停止
    await Future.delayed(const Duration(seconds: 5));
    await stopScan();
    if (_currentState == BleConnectionState.scanning) {
      _currentState = BleConnectionState.disconnected;
      _connectionController.add(_currentState);
    }
    debugPrint('[BLE] Scan complete. Found ${_scanResults.length} devices.');
  }

  @override
  Future<void> stopScan() async {
    await _scanSub?.cancel();
    _scanSub = null;
  }

  @override
  Future<void> connect(String deviceId) async {
    debugPrint('[BLE] Connecting to $deviceId...');
    _currentState = BleConnectionState.connecting;
    _connectionController.add(_currentState);
    _connectedDeviceId = deviceId;

    _connSub?.cancel();
    _connSub = _ble
        .connectToDevice(id: deviceId)
        .listen(
          (update) {
            debugPrint(
              '[BLE] Conn state: ${update.connectionState} for ${update.deviceId}',
            );
            final newState = _mapConnectionState(update.connectionState);
            if (newState != _currentState) {
              _currentState = newState;
              _connectionController.add(_currentState);

              if (newState == BleConnectionState.connected) {
                debugPrint(
                  '[BLE] Connected! Subscribing to characteristics...',
                );
                _subscribeCharacteristics();
              } else if (newState == BleConnectionState.disconnected) {
                _unsubscribeCharacteristics();
              }
            }
          },
          onError: (e) {
            debugPrint('[BLE] Connection error: $e');
            _currentState = BleConnectionState.failed;
            _connectionController.add(_currentState);
          },
        );
  }

  void _subscribeCharacteristics() {
    final id = _connectedDeviceId!;

    // 订阅环境数据 notify
    _envSub?.cancel();
    _envSub = _ble.subscribeToCharacteristic(_envChar(id)).listen((data) {
      try {
        _envController.add(
          EnvSnapshot.fromBle(
            data is Uint8List ? data : Uint8List.fromList(data),
          ),
        );
      } catch (_) {}
    }, onError: (_) {});

    // 订阅运动数据 notify
    _motionSub?.cancel();
    _motionSub = _ble.subscribeToCharacteristic(_motionChar(id)).listen((data) {
      try {
        _motionController.add(
          ImuSnapshot.fromBle(
            data is Uint8List ? data : Uint8List.fromList(data),
          ),
        );
      } catch (_) {}
    }, onError: (_) {});

    // 订阅表情数据 notify
    _emoteSub?.cancel();
    _emoteSub = _ble.subscribeToCharacteristic(_emoteChar(id)).listen((data) {
      try {
        _emoteController.add(
          EmoteInfo.fromBle(
            data is Uint8List ? data : Uint8List.fromList(data),
          ),
        );
      } catch (_) {}
    }, onError: (_) {});
  }

  void _unsubscribeCharacteristics() {
    _envSub?.cancel();
    _motionSub?.cancel();
    _emoteSub?.cancel();
    _envSub = null;
    _motionSub = null;
    _emoteSub = null;
  }

  @override
  Future<void> disconnect() async {
    _unsubscribeCharacteristics();
    await _connSub?.cancel();
    _connSub = null;
    _connectedDeviceId = null;
    _currentState = BleConnectionState.disconnected;
    _connectionController.add(_currentState);
  }

  BleConnectionState _mapConnectionState(DeviceConnectionState state) {
    switch (state) {
      case DeviceConnectionState.connected:
        return BleConnectionState.connected;
      case DeviceConnectionState.connecting:
        return BleConnectionState.connecting;
      case DeviceConnectionState.disconnected:
        return BleConnectionState.disconnected;
      case DeviceConnectionState.disconnecting:
        return BleConnectionState.disconnected;
    }
  }

  void dispose() {
    _unsubscribeCharacteristics();
    _connSub?.cancel();
    _scanSub?.cancel();
    _connectionController.close();
    _envController.close();
    _motionController.close();
    _emoteController.close();
    _devicesController.close();
  }
}
