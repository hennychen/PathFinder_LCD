import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/storage/storage_providers.dart';
import 'ble_provider.dart';
import '../models/env_snapshot.dart';
import '../models/imu_snapshot.dart';
import '../models/emote_info.dart';
import '../models/compass_snapshot.dart';
import '../models/tracker_snapshot.dart';

final envStreamProvider = StreamProvider<EnvSnapshot>((ref) {
  final ble = ref.watch(bleServiceProvider);
  final envDao = ref.watch(envDaoProvider);
  return ble.subscribeEnv().map((snapshot) {
    envDao.insertEnv(
      DateTime.now(),
      snapshot.temperature,
      snapshot.humidity,
      snapshot.pressure,
      snapshot.altitude,
      snapshot.uvIndex,
    );
    return snapshot;
  });
});

final motionStreamProvider = StreamProvider<ImuSnapshot>((ref) {
  final ble = ref.watch(bleServiceProvider);
  final eventDao = ref.watch(eventDaoProvider);
  return ble.subscribeMotion().map((snapshot) {
    eventDao.insertEvent(
      DateTime.now(),
      snapshot.event.index,
      snapshot.pitch,
      snapshot.roll,
      snapshot.accelMag,
    );
    return snapshot;
  });
});

final emoteStreamProvider = StreamProvider<EmoteInfo>((ref) {
  final ble = ref.watch(bleServiceProvider);
  final emoteDao = ref.watch(emoteDaoProvider);
  return ble.subscribeEmote().map((info) {
    emoteDao.insertEmote(
      DateTime.now(),
      info.emoteId,
      info.friendlyName,
      info.trigger.index,
      info.trigger.name,
    );
    return info;
  });
});

/// 罗盘方位角流 (C6) — 纯实时展示，不接入持久化
final compassStreamProvider = StreamProvider<CompassSnapshot>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.subscribeCompass();
});

/// 追踪聚合流 (C7) — 声源角度 + 人脸检测，纯实时展示
final trackerStreamProvider = StreamProvider<TrackerSnapshot>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.subscribeTracker();
});
