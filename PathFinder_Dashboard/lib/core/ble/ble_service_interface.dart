import '../../shared/models/env_snapshot.dart';
import '../../shared/models/imu_snapshot.dart';
import '../../shared/models/emote_info.dart';
import '../../shared/models/compass_snapshot.dart';
import '../../shared/models/tracker_snapshot.dart';

enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  reconnecting,
  failed,
}

abstract class BleServiceInterface {
  BleConnectionState get currentState;
  Stream<BleConnectionState> get connectionState;
  Stream<EnvSnapshot> subscribeEnv();
  Stream<ImuSnapshot> subscribeMotion();
  Stream<EmoteInfo> subscribeEmote();
  Stream<CompassSnapshot> subscribeCompass();
  Stream<TrackerSnapshot> subscribeTracker();
  Future<void> startScan();
  Future<void> stopScan();
  Future<void> connect(String deviceId);
  Future<void> disconnect();

  // Wi-Fi 配网
  Future<void> writeWifiConfig(String ssid, String password);
  Future<void> queryWifiStatus();
  Future<void> resetWifiConfig();
}
