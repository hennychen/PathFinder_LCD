import 'dart:async';
import 'dart:math';
import 'ble_service_interface.dart';
import '../../shared/models/env_snapshot.dart';
import '../../shared/models/imu_snapshot.dart';
import '../../shared/models/emote_info.dart';
import '../../shared/models/compass_snapshot.dart';
import '../../shared/models/tracker_snapshot.dart';
import '../../shared/models/motion_event.dart';

class MockBleService implements BleServiceInterface {
  final _connectionController =
      StreamController<BleConnectionState>.broadcast();
  final _envController = StreamController<EnvSnapshot>.broadcast();
  final _motionController = StreamController<ImuSnapshot>.broadcast();
  final _emoteController = StreamController<EmoteInfo>.broadcast();
  final _compassController = StreamController<CompassSnapshot>.broadcast();
  final _trackerController = StreamController<TrackerSnapshot>.broadcast();
  Timer? _envTimer;
  Timer? _motionTimer;
  Timer? _emoteTimer;
  Timer? _compassTimer;
  Timer? _trackerTimer;
  final _random = Random();
  int _tick = 0;
  BleConnectionState _currentState = BleConnectionState.disconnected;

  @override
  Stream<BleConnectionState> get connectionState async* {
    // 每次订阅时先 yield 当前状态，避免 StreamProvider 卡在 loading
    yield _currentState;
    yield* _connectionController.stream;
  }

  @override
  BleConnectionState get currentState => _currentState;

  @override
  Stream<EnvSnapshot> subscribeEnv() => _envController.stream;
  @override
  Stream<ImuSnapshot> subscribeMotion() => _motionController.stream;
  @override
  Stream<EmoteInfo> subscribeEmote() => _emoteController.stream;

  @override
  Stream<CompassSnapshot> subscribeCompass() => _compassController.stream;

  @override
  Stream<TrackerSnapshot> subscribeTracker() => _trackerController.stream;

  @override
  Future<void> startScan() async {
    _currentState = BleConnectionState.scanning;
    _connectionController.add(_currentState);
    await Future.delayed(const Duration(seconds: 2));
    _currentState = BleConnectionState.disconnected;
    _connectionController.add(_currentState);
  }

  @override
  Future<void> stopScan() async {}

  @override
  Future<void> connect(String deviceId) async {
    _currentState = BleConnectionState.connecting;
    _connectionController.add(_currentState);
    await Future.delayed(const Duration(seconds: 1));
    _currentState = BleConnectionState.connected;
    _connectionController.add(_currentState);
    _startMockData();
  }

  @override
  Future<void> disconnect() async {
    _stopMockData();
    _currentState = BleConnectionState.disconnected;
    _connectionController.add(_currentState);
  }

  @override
  Future<void> writeWifiConfig(String ssid, String password) async {}

  @override
  Future<void> queryWifiStatus() async {}

  @override
  Future<void> resetWifiConfig() async {}

  void _startMockData() {
    // Environment data @1Hz
    _envTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      _tick++;
      _envController.add(
        EnvSnapshot(
          timestamp: DateTime.now(),
          temperature: 25.0 + sin(_tick / 10.0) * 5.0,
          humidity: 55.0 + sin(_tick / 7.0) * 15.0,
          pressure: 101325 + (sin(_tick / 20.0) * 500).round(),
          altitude: 156.0 + sin(_tick / 15.0) * 10.0,
          uvIndex: max(0, 3.0 + sin(_tick / 5.0) * 4.0),
        ),
      );
    });

    // Motion data @25Hz
    _motionTimer = Timer.periodic(const Duration(milliseconds: 40), (_) {
      _motionController.add(
        ImuSnapshot(
          timestamp: DateTime.now(),
          pitch: sin(_tick / 3.0) * 15.0,
          roll: cos(_tick / 4.0) * 10.0,
          accelMag: 0.05 + _random.nextDouble() * 0.1,
          event: _tick % 50 == 0 ? MotionEvent.brake : MotionEvent.cruise,
          confidence: 90 + _random.nextInt(10),
        ),
      );
    });

    // Emote state (changes every 10 seconds)
    _emoteTimer = Timer.periodic(const Duration(seconds: 10), (_) {
      final emotes = ['Leisure', 'Panic', 'Sigh', 'Sad', 'Question', 'Mock'];
      final triggers = [
        EmoteTrigger.normal,
        EmoteTrigger.uvExtreme,
        EmoteTrigger.hot,
        EmoteTrigger.cold,
        EmoteTrigger.tilted,
        EmoteTrigger.uvHigh,
      ];
      final idx = _random.nextInt(emotes.length);
      _emoteController.add(
        EmoteInfo(
          emoteId: idx,
          friendlyName: emotes[idx],
          trigger: triggers[idx],
          timestamp: DateTime.now(),
        ),
      );
    });

    // Compass heading @2Hz (缓慢旋转)
    _compassTimer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      final heading = (_tick * 5.0) % 360.0;
      _compassController.add(
        CompassSnapshot(
          timestamp: DateTime.now(),
          heading: heading,
          valid: true,
          source: 1,
        ),
      );
    });

    // Tracker @5Hz (声源角度 + 人脸随机出现)
    bool faceVisible = false;
    _trackerTimer = Timer.periodic(const Duration(milliseconds: 200), (_) {
      if (_random.nextInt(10) < 3) faceVisible = !faceVisible;
      _trackerController.add(
        TrackerSnapshot(
          timestamp: DateTime.now(),
          soundAngle: (sin(_tick / 8.0) * 180.0 + 180.0),
          soundConfidence: 70 + _random.nextInt(30),
          soundValid: true,
          faceCount: faceVisible ? 1 : 0,
          faceFound: faceVisible,
          faceCx: 160 + _random.nextInt(40) - 20,
          faceCy: 120 + _random.nextInt(30) - 15,
          faceW: 60 + _random.nextInt(40),
          faceH: 80 + _random.nextInt(40),
          trackState: faceVisible ? 2 : 0,
        ),
      );
    });
  }

  void _stopMockData() {
    _envTimer?.cancel();
    _motionTimer?.cancel();
    _emoteTimer?.cancel();
    _compassTimer?.cancel();
    _trackerTimer?.cancel();
    _envTimer = null;
    _motionTimer = null;
    _emoteTimer = null;
    _compassTimer = null;
    _trackerTimer = null;
  }

  void dispose() {
    _stopMockData();
    _connectionController.close();
    _envController.close();
    _motionController.close();
    _emoteController.close();
    _compassController.close();
    _trackerController.close();
  }
}
