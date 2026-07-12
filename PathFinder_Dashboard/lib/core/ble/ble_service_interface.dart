import '../../shared/models/env_snapshot.dart';
import '../../shared/models/imu_snapshot.dart';
import '../../shared/models/emote_info.dart';

enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  reconnecting,
  failed,
}

abstract class BleServiceInterface {
  Stream<BleConnectionState> get connectionState;
  Stream<EnvSnapshot> subscribeEnv();
  Stream<ImuSnapshot> subscribeMotion();
  Stream<EmoteInfo> subscribeEmote();
  Future<void> startScan();
  Future<void> stopScan();
  Future<void> connect(String deviceId);
  Future<void> disconnect();
}
