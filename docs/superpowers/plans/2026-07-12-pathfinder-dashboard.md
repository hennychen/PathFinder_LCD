# PathFinder Dashboard Flutter 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建 Flutter Android 应用，通过 BLE 连接 ESP32 实时采集传感器数据，以图表展示环境/运动/表情数据并支持本地历史存储。

**Architecture:** 分层架构（core/features/shared），Riverpod 状态管理 + StreamProvider 驱动实时数据流，fl_chart 图表可视化，Drift/SQLite 本地持久化，flutter_reactive_ble 通信。

**Tech Stack:** Flutter 3.32+, flutter_riverpod, flutter_reactive_ble, fl_chart, drift, go_router

**Spec:** `docs/superpowers/specs/2026-07-12-pathfinder-dashboard-flutter-design.md`

---

## M1: 项目骨架 + 主题 + 导航 + Mock 数据

### Task 1: 创建 Flutter 项目

**Files:**
- Create: `/Users/pm/PathFinder_LCD/PathFinder_Dashboard/`

- [ ] **Step 1: 初始化 Flutter 项目**

```bash
cd /Users/pm/PathFinder_LCD
flutter create --project-name pathfinder_dashboard --org com.pathfinder --platforms android PathFinder_Dashboard
```

- [ ] **Step 2: 验证项目创建成功**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter analyze
```

Expected: No errors

- [ ] **Step 3: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/
git commit -m "chore: scaffold PathFinder_Dashboard Flutter project"
```

### Task 2: 配置依赖

**Files:**
- Modify: `PathFinder_Dashboard/pubspec.yaml`

- [ ] **Step 1: 更新 pubspec.yaml**

```yaml
name: pathfinder_dashboard
description: PathFinder EMOTE companion app - BLE sensor data dashboard
publish_to: 'none'
version: 1.0.0+1

environment:
  sdk: '>=3.8.0 <4.0.0'

dependencies:
  flutter:
    sdk: flutter
  flutter_riverpod: ^2.6.1
  flutter_reactive_ble: ^5.3.1
  fl_chart: ^0.70.2
  drift: ^2.22.1
  sqlite3_flutter_libs: ^0.5.28
  go_router: ^14.8.1
  logger: ^2.5.0
  path_provider: ^2.1.5
  intl: ^0.20.2
  share_plus: ^10.1.4
  path: ^1.9.1

dev_dependencies:
  flutter_test:
    sdk: flutter
  flutter_lints: ^3.0.0
  drift_dev: ^2.22.1
  build_runner: ^2.4.14
  mockito: ^5.4.5

flutter:
  uses-material-design: true
  assets:
    - assets/emotes/
```

- [ ] **Step 2: 创建 assets 目录**

```bash
mkdir -p /Users/pm/PathFinder_LCD/PathFinder_Dashboard/assets/emotes
mkdir -p /Users/pm/PathFinder_LCD/PathFinder_Dashboard/assets/icons
```

- [ ] **Step 3: 运行 flutter pub get**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter pub get
```

- [ ] **Step 4: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/pubspec.yaml PathFinder_Dashboard/pubspec.lock
git commit -m "chore: add project dependencies"
```

### Task 3: 创建主题系统

**Files:**
- Create: `PathFinder_Dashboard/lib/app/theme/app_colors.dart`
- Create: `PathFinder_Dashboard/lib/app/theme/app_theme.dart`
- Create: `PathFinder_Dashboard/lib/app/theme/app_typography.dart`

- [ ] **Step 1: 创建 app_colors.dart**

```dart
import 'package:flutter/material.dart';

abstract class AppColors {
  static const background = Color(0xFF0A0A0F);
  static const surface = Color(0xFF1A1A24);
  static const envPrimary = Color(0xFF00B4FF);
  static const motionPrimary = Color(0xFF00FF88);
  static const warning = Color(0xFFFFB400);
  static const urgent = Color(0xFFFF5050);
  static const textPrimary = Color(0xFFE0E0E8);
  static const textSecondary = Color(0xFF888899);
  static const divider = Color(0xFF333344);
}
```

- [ ] **Step 2: 创建 app_typography.dart**

```dart
import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

abstract class AppTypography {
  static const _base = TextStyle(fontFamily: 'Roboto', color: Color(0xFFE0E0E8));

  static const displayLarge = TextStyle(fontSize: 32, fontWeight: FontWeight.w700, color: Color(0xFFE0E0E8));
  static const headlineMedium = TextStyle(fontSize: 20, fontWeight: FontWeight.w600, color: Color(0xFFE0E0E8));
  static const titleMedium = TextStyle(fontSize: 16, fontWeight: FontWeight.w500, color: Color(0xFFE0E0E8));
  static const bodyLarge = TextStyle(fontSize: 16, fontWeight: FontWeight.w400, color: Color(0xFFE0E0E8));
  static const bodyMedium = TextStyle(fontSize: 14, fontWeight: FontWeight.w400, color: Color(0xFFE0E0E8));
  static const labelLarge = TextStyle(fontSize: 14, fontWeight: FontWeight.w500, color: Color(0xFF888899));
  static const labelSmall = TextStyle(fontSize: 11, fontWeight: FontWeight.w500, color: Color(0xFF888899));
  static const metricValue = TextStyle(fontSize: 28, fontWeight: FontWeight.w700, color: Color(0xFFE0E0E8));
  static const metricUnit = TextStyle(fontSize: 14, fontWeight: FontWeight.w400, color: Color(0xFF888899));
}
```

- [ ] **Step 3: 创建 app_theme.dart**

```dart
import 'package:flutter/material.dart';
import 'app_colors.dart';

ThemeData appTheme() {
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    scaffoldBackgroundColor: AppColors.background,
    colorScheme: const ColorScheme.dark(
      primary: AppColors.envPrimary,
      secondary: AppColors.motionPrimary,
      surface: AppColors.surface,
      error: AppColors.urgent,
      onPrimary: Colors.white,
      onSecondary: Colors.black,
      onSurface: AppColors.textPrimary,
      onError: Colors.white,
    ),
    cardTheme: CardThemeData(
      color: AppColors.surface,
      elevation: 0,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
    ),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: AppColors.surface,
      indicatorColor: AppColors.envPrimary.withValues(alpha: 0.2),
      labelTextStyle: WidgetStateProperty.resolveWith((states) {
        if (states.contains(WidgetState.selected)) {
          return const TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: AppColors.envPrimary);
        }
        return const TextStyle(fontSize: 12, color: AppColors.textSecondary);
      }),
      iconTheme: WidgetStateProperty.resolveWith((states) {
        if (states.contains(WidgetState.selected)) {
          return const IconThemeData(color: AppColors.envPrimary, size: 24);
        }
        return const IconThemeData(color: AppColors.textSecondary, size: 24);
      }),
    ),
    appBarTheme: const AppBarTheme(
      backgroundColor: AppColors.background,
      foregroundColor: AppColors.textPrimary,
      elevation: 0,
      surfaceTintColor: Colors.transparent,
    ),
    dividerTheme: const DividerThemeData(color: AppColors.divider, thickness: 1, space: 1),
  );
}
```

- [ ] **Step 4: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/app/theme/
git commit -m "feat: add racing-dark theme system (colors, typography, theme)"
```

### Task 4: 创建共享数据模型

**Files:**
- Create: `PathFinder_Dashboard/lib/shared/models/env_snapshot.dart`
- Create: `PathFinder_Dashboard/lib/shared/models/imu_snapshot.dart`
- Create: `PathFinder_Dashboard/lib/shared/models/motion_event.dart`
- Create: `PathFinder_Dashboard/lib/shared/models/emote_info.dart`
- Create: `PathFinder_Dashboard/lib/shared/models/emote_rules.dart`

- [ ] **Step 1: 创建 env_snapshot.dart**

```dart
import 'dart:typed_data';

class EnvSnapshot {
  final DateTime timestamp;
  final double temperature; // °C
  final double humidity;    // %
  final int pressure;       // Pa
  final double altitude;    // m
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
```

- [ ] **Step 2: 创建 motion_event.dart**

```dart
import 'package:flutter/material.dart';

enum MotionEvent {
  idle(0, '静止', Colors.grey),
  cruise(1, '匀速行驶', Colors.teal),
  accel(2, '急加速', Colors.orange),
  brake(3, '急刹车', Colors.deepOrange),
  turnLeft(4, '急左转', Colors.amber),
  turnRight(5, '急右转', Colors.amber),
  uphill(6, '上坡', Colors.lightBlue),
  downhill(7, '下坡', Colors.lightBlue),
  tiltLeft(8, '左倾斜', Colors.yellow),
  tiltRight(9, '右倾斜', Colors.yellow),
  bumpy(10, '颠簸', Colors.purple),
  collision(11, '碰撞', Colors.red),
  highSpeed(12, '高速行驶', Colors.cyan);

  final int code;
  final String label;
  final Color color;
  const MotionEvent(this.code, this.label, this.color);

  static MotionEvent fromCode(int code) {
    return values.firstWhere(
      (e) => e.code == code,
      orElse: () => MotionEvent.idle,
    );
  }
}
```

- [ ] **Step 3: 创建 imu_snapshot.dart**

```dart
import 'dart:typed_data';
import 'motion_event.dart';

class ImuSnapshot {
  final DateTime timestamp;
  final double pitch;       // °
  final double roll;        // °
  final double accelMag;    // g deviation from 1g
  final MotionEvent event;
  final int confidence;     // 0~100

  const ImuSnapshot({
    required this.timestamp,
    required this.pitch,
    required this.roll,
    required this.accelMag,
    required this.event,
    required this.confidence,
  });

  /// Decode from BLE C3 characteristic (8 bytes)
  factory ImuSnapshot.fromBle(Uint8List data) {
    if (data.length != 8) {
      throw FormatException('Motion frame must be 8 bytes, got ${data.length}');
    }
    final bd = ByteData.sublistView(data);
    return ImuSnapshot(
      timestamp: DateTime.now(),
      pitch: bd.getInt16(0, Endian.little) / 100.0,
      roll: bd.getInt16(2, Endian.little) / 100.0,
      accelMag: bd.getUint16(4, Endian.little) / 1000.0,
      event: MotionEvent.fromCode(data[6]),
      confidence: data[7],
    );
  }

  factory ImuSnapshot.mock({
    double pitch = 0,
    double roll = 0,
    double accelMag = 0.01,
    MotionEvent event = MotionEvent.idle,
    int confidence = 100,
  }) {
    return ImuSnapshot(
      timestamp: DateTime.now(),
      pitch: pitch,
      roll: roll,
      accelMag: accelMag,
      event: event,
      confidence: confidence,
    );
  }
}
```

- [ ] **Step 4: 创建 emote_info.dart**

```dart
import 'dart:typed_data';

enum EmoteTrigger {
  manual(0, '手动切换'),
  uvExtreme(1, '紫外线极端危险'),
  uvHigh(2, '紫外线较强'),
  hot(3, '温度过高'),
  cold(4, '温度过低'),
  humid(5, '高湿度不适'),
  tilted(6, '检测到严重倾斜'),
  slightTilt(7, '检测到倾斜'),
  lowPressure(8, '气压偏低'),
  normal(9, '一切正常');

  final int code;
  final String label;
  const EmoteTrigger(this.code, this.label);

  static EmoteTrigger fromCode(int code) {
    return values.firstWhere(
      (e) => e.code == code,
      orElse: () => EmoteTrigger.normal,
    );
  }
}

class EmoteInfo {
  final int emoteId;
  final String friendlyName;
  final EmoteTrigger trigger;
  final DateTime timestamp;

  const EmoteInfo({
    required this.emoteId,
    required this.friendlyName,
    required this.trigger,
    required this.timestamp,
  });

  /// Decode from BLE C4 characteristic (15 bytes)
  factory EmoteInfo.fromBle(Uint8List data) {
    if (data.length != 15) {
      throw FormatException('Emote frame must be 15 bytes, got ${data.length}');
    }
    final nameLen = data[1];
    final name = String.fromCharCodes(data.sublist(2, 2 + nameLen));
    return EmoteInfo(
      emoteId: data[0],
      friendlyName: name,
      trigger: EmoteTrigger.fromCode(data[14]),
      timestamp: DateTime.now(),
    );
  }

  factory EmoteInfo.mock({
    int emoteId = 0,
    String friendlyName = 'Smile',
    EmoteTrigger trigger = EmoteTrigger.normal,
  }) {
    return EmoteInfo(
      emoteId: emoteId,
      friendlyName: friendlyName,
      trigger: trigger,
      timestamp: DateTime.now(),
    );
  }
}
```

- [ ] **Step 5: 创建 emote_rules.dart**

```dart
import 'dart:math';
import 'env_snapshot.dart';
import 'emote_info.dart';

class EmoteRule {
  final String emoteName;
  final int priority;
  final EmoteTrigger trigger;
  final String condition;
  final bool Function(EnvSnapshot? env, double pitch, double roll) matches;

  const EmoteRule({
    required this.emoteName,
    required this.priority,
    required this.trigger,
    required this.condition,
    required this.matches,
  });
}

/// 9 rules matching ESP32 evaluate_sensors() priority order
const emoteRules = <EmoteRule>[
  EmoteRule(
    emoteName: 'panic', priority: 90, trigger: EmoteTrigger.uvExtreme,
    condition: 'UV ≥ 8.0',
    matches: (env, p, r) => env != null && env.uvIndex >= 8.0,
  ),
  EmoteRule(
    emoteName: 'mock', priority: 80, trigger: EmoteTrigger.uvHigh,
    condition: 'UV ≥ 6.0',
    matches: (env, p, r) => env != null && env.uvIndex >= 6.0,
  ),
  EmoteRule(
    emoteName: 'sigh', priority: 70, trigger: EmoteTrigger.hot,
    condition: '温度 ≥ 32°C',
    matches: (env, p, r) => env != null && env.temperature >= 32.0,
  ),
  EmoteRule(
    emoteName: 'sad', priority: 65, trigger: EmoteTrigger.cold,
    condition: '温度 ≤ 10°C',
    matches: (env, p, r) => env != null && env.temperature <= 10.0,
  ),
  EmoteRule(
    emoteName: 'cry', priority: 60, trigger: EmoteTrigger.humid,
    condition: '湿度 ≥ 85%',
    matches: (env, p, r) => env != null && env.humidity >= 85.0,
  ),
  EmoteRule(
    emoteName: 'question', priority: 50, trigger: EmoteTrigger.tilted,
    condition: '倾角 ≥ 20°',
    matches: (env, p, r) => sqrt(p * p + r * r) >= 20.0,
  ),
  EmoteRule(
    emoteName: 'investigate', priority: 40, trigger: EmoteTrigger.slightTilt,
    condition: '倾角 ≥ 12°',
    matches: (env, p, r) => sqrt(p * p + r * r) >= 12.0,
  ),
  EmoteRule(
    emoteName: 'ponder', priority: 30, trigger: EmoteTrigger.lowPressure,
    condition: '气压 ≤ 1000hPa',
    matches: (env, p, r) => env != null && env.pressure > 0 && env.pressure <= 100000,
  ),
  EmoteRule(
    emoteName: 'leisure', priority: 10, trigger: EmoteTrigger.normal,
    condition: '默认正常状态',
    matches: (env, p, r) => true,
  ),
];

/// Evaluate sensor data against rules, return highest-priority match
EmoteRule evaluateEmote(EnvSnapshot? env, double pitch, double roll) {
  for (final rule in emoteRules) {
    if (rule.matches(env, pitch, roll)) return rule;
  }
  return emoteRules.last; // leisure (always matches)
}
```

- [ ] **Step 6: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/shared/models/
git commit -m "feat: add shared data models (EnvSnapshot, ImuSnapshot, MotionEvent, EmoteInfo, EmoteRules)"
```

### Task 5: 编写模型单元测试

**Files:**
- Create: `PathFinder_Dashboard/test/shared/models/env_snapshot_test.dart`
- Create: `PathFinder_Dashboard/test/shared/models/emote_rules_test.dart`

- [ ] **Step 1: 创建 env_snapshot_test.dart**

```dart
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:pathfinder_dashboard/shared/models/env_snapshot.dart';

void main() {
  group('EnvSnapshot.fromBle', () {
    test('correctly decodes 20-byte environment frame', () {
      final bytes = Uint8List(20);
      final bd = ByteData.sublistView(bytes);
      bd.setInt16(4, Endian.little, 2635);   // 26.35°C
      bd.setUint16(6, Endian.little, 5800);  // 58.00%
      bd.setUint32(8, Endian.little, 101325); // 101325 Pa
      bd.setInt16(12, Endian.little, 1560);  // 156.0 m
      bd.setUint16(14, Endian.little, 320);  // UV 3.20

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
```

- [ ] **Step 2: 创建 emote_rules_test.dart**

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:pathfinder_dashboard/shared/models/env_snapshot.dart';
import 'package:pathfinder_dashboard/shared/models/emote_info.dart';
import 'package:pathfinder_dashboard/shared/models/emote_rules.dart';

void main() {
  group('evaluateEmote', () {
    test('UV >= 8.0 → panic (highest priority)', () {
      final env = EnvSnapshot.mock(uvIndex: 8.1, temperature: 25.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'panic');
      expect(rule.trigger, EmoteTrigger.uvExtreme);
    });

    test('UV >= 6.0 → mock', () {
      final env = EnvSnapshot.mock(uvIndex: 6.5, temperature: 25.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'mock');
    });

    test('temperature >= 32 → sigh', () {
      final env = EnvSnapshot.mock(temperature: 35.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'sigh');
    });

    test('temperature <= 10 → sad', () {
      final env = EnvSnapshot.mock(temperature: 5.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'sad');
    });

    test('humidity >= 85 → cry', () {
      final env = EnvSnapshot.mock(temperature: 25.0, humidity: 90.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'cry');
    });

    test('tilt >= 20° → question', () {
      final env = EnvSnapshot.mock(temperature: 25.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 20.0, 0);
      expect(rule.emoteName, 'question');
    });

    test('tilt >= 12° → investigate', () {
      final env = EnvSnapshot.mock(temperature: 25.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 12.0, 0);
      expect(rule.emoteName, 'investigate');
    });

    test('low pressure → ponder', () {
      final env = EnvSnapshot.mock(temperature: 25.0, uvIndex: 2.0, pressure: 99000);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'ponder');
    });

    test('normal → leisure', () {
      final env = EnvSnapshot.mock(temperature: 25.0, uvIndex: 2.0, pressure: 101325);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'leisure');
    });

    test('multiple conditions → highest priority wins', () {
      final env = EnvSnapshot.mock(uvIndex: 8.5, temperature: 35.0, humidity: 90.0);
      final rule = evaluateEmote(env, 25.0, 0);
      // UV extreme (priority 90) beats hot (70), humid (60), tilt (50)
      expect(rule.emoteName, 'panic');
      expect(rule.priority, 90);
    });
  });
}
```

- [ ] **Step 3: 运行测试**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter test test/shared/models/
```

Expected: All tests pass

- [ ] **Step 4: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/test/
git commit -m "test: add model unit tests (EnvSnapshot decode, emote rules)"
```

### Task 6: 创建 Mock BLE 服务

**Files:**
- Create: `PathFinder_Dashboard/lib/core/ble/ble_service_interface.dart`
- Create: `PathFinder_Dashboard/lib/core/ble/mock_ble_service.dart`

- [ ] **Step 1: 创建 ble_service_interface.dart**

```dart
import '../../shared/models/env_snapshot.dart';
import '../../shared/models/imu_snapshot.dart';
import '../../shared/models/emote_info.dart';

enum ConnectionState { disconnected, scanning, connecting, connected, reconnecting, failed }

abstract class BleServiceInterface {
  Stream<ConnectionState> get connectionState;
  Stream<EnvSnapshot> subscribeEnv();
  Stream<ImuSnapshot> subscribeMotion();
  Stream<EmoteInfo> subscribeEmote();
  Future<void> startScan();
  Future<void> stopScan();
  Future<void> connect(String deviceId);
  Future<void> disconnect();
}
```

- [ ] **Step 2: 创建 mock_ble_service.dart**

```dart
import 'dart:async';
import 'dart:math';
import 'ble_service_interface.dart';
import '../../shared/models/env_snapshot.dart';
import '../../shared/models/imu_snapshot.dart';
import '../../shared/models/emote_info.dart';
import '../../shared/models/motion_event.dart';
import '../../shared/models/emote_info.dart';

class MockBleService implements BleServiceInterface {
  final _connectionController = StreamController<ConnectionState>.broadcast();
  final _envController = StreamController<EnvSnapshot>.broadcast();
  final _motionController = StreamController<ImuSnapshot>.broadcast();
  final _emoteController = StreamController<EmoteInfo>.broadcast();
  Timer? _envTimer;
  Timer? _motionTimer;
  Timer? _emoteTimer;
  final _random = Random();
  int _tick = 0;

  @override
  Stream<ConnectionState> get connectionState => _connectionController.stream;
  @override
  Stream<EnvSnapshot> subscribeEnv() => _envController.stream;
  @override
  Stream<ImuSnapshot> subscribeMotion() => _motionController.stream;
  @override
  Stream<EmoteInfo> subscribeEmote() => _emoteController.stream;

  @override
  Future<void> startScan() async {
    _connectionController.add(ConnectionState.scanning);
    await Future.delayed(const Duration(seconds: 2));
    _connectionController.add(ConnectionState.disconnected);
  }

  @override
  Future<void> stopScan() async {}

  @override
  Future<void> connect(String deviceId) async {
    _connectionController.add(ConnectionState.connecting);
    await Future.delayed(const Duration(seconds: 1));
    _connectionController.add(ConnectionState.connected);
    _startMockData();
  }

  @override
  Future<void> disconnect() async {
    _stopMockData();
    _connectionController.add(ConnectionState.disconnected);
  }

  void _startMockData() {
    // Environment data @1Hz
    _envTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      _tick++;
      _envController.add(EnvSnapshot(
        timestamp: DateTime.now(),
        temperature: 25.0 + sin(_tick / 10.0) * 5.0,
        humidity: 55.0 + sin(_tick / 7.0) * 15.0,
        pressure: 101325 + (sin(_tick / 20.0) * 500).round(),
        altitude: 156.0 + sin(_tick / 15.0) * 10.0,
        uvIndex: max(0, 3.0 + sin(_tick / 5.0) * 4.0),
      ));
    });

    // Motion data @25Hz
    _motionTimer = Timer.periodic(const Duration(milliseconds: 40), (_) {
      _motionController.add(ImuSnapshot(
        timestamp: DateTime.now(),
        pitch: sin(_tick / 3.0) * 15.0,
        roll: cos(_tick / 4.0) * 10.0,
        accelMag: 0.05 + _random.nextDouble() * 0.1,
        event: _tick % 50 == 0 ? MotionEvent.brake : MotionEvent.cruise,
        confidence: 90 + _random.nextInt(10),
      ));
    });

    // Emote state (changes every 10 seconds based on mock env)
    _emoteTimer = Timer.periodic(const Duration(seconds: 10), (_) {
      final emotes = ['Leisure', 'Panic', 'Sigh', 'Sad', 'Question', 'Mock'];
      final triggers = [EmoteTrigger.normal, EmoteTrigger.uvExtreme,
                        EmoteTrigger.hot, EmoteTrigger.cold,
                        EmoteTrigger.tilted, EmoteTrigger.uvHigh];
      final idx = _random.nextInt(emotes.length);
      _emoteController.add(EmoteInfo(
        emoteId: idx,
        friendlyName: emotes[idx],
        trigger: triggers[idx],
        timestamp: DateTime.now(),
      ));
    });
  }

  void _stopMockData() {
    _envTimer?.cancel();
    _motionTimer?.cancel();
    _emoteTimer?.cancel();
    _envTimer = null;
    _motionTimer = null;
    _emoteTimer = null;
  }

  void dispose() {
    _stopMockData();
    _connectionController.close();
    _envController.close();
    _motionController.close();
    _emoteController.close();
  }
}
```

- [ ] **Step 3: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/core/ble/
git commit -m "feat: add Mock BLE service with simulated sensor data streams"
```

### Task 7: 创建 Riverpod Providers

**Files:**
- Create: `PathFinder_Dashboard/lib/shared/providers/ble_provider.dart`
- Create: `PathFinder_Dashboard/lib/shared/providers/sensor_provider.dart`

- [ ] **Step 1: 创建 ble_provider.dart**

```dart
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../core/ble/mock_ble_service.dart';

final bleServiceProvider = Provider<BleServiceInterface>((ref) {
  final service = MockBleService();
  ref.onDispose(() => service.dispose());
  return service;
});

final connectionStateProvider = StreamProvider<ConnectionState>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.connectionState;
});
```

- [ ] **Step 2: 创建 sensor_provider.dart**

```dart
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'ble_provider.dart';
import '../../models/env_snapshot.dart';
import '../../models/imu_snapshot.dart';
import '../../models/emote_info.dart';

final envStreamProvider = StreamProvider<EnvSnapshot>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.subscribeEnv();
});

final motionStreamProvider = StreamProvider<ImuSnapshot>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.subscribeMotion();
});

final emoteStreamProvider = StreamProvider<EmoteInfo>((ref) {
  final ble = ref.watch(bleServiceProvider);
  return ble.subscribeEmote();
});
```

- [ ] **Step 3: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/shared/providers/
git commit -m "feat: add Riverpod providers for BLE and sensor streams"
```

### Task 8: 创建共享 Widget 组件

**Files:**
- Create: `PathFinder_Dashboard/lib/shared/widgets/metric_card.dart`
- Create: `PathFinder_Dashboard/lib/shared/widgets/animated_counter.dart`
- Create: `PathFinder_Dashboard/lib/shared/widgets/status_indicator.dart`

- [ ] **Step 1: 创建 metric_card.dart**

```dart
import 'package:flutter/material.dart';
import '../../app/theme/app_colors.dart';
import 'animated_counter.dart';

class MetricCard extends StatelessWidget {
  final double value;
  final String unit;
  final String label;
  final Color color;
  final VoidCallback? onTap;

  const MetricCard({
    super.key,
    required this.value,
    required this.unit,
    required this.label,
    required this.color,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: AppColors.surface,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: color.withValues(alpha: 0.3), width: 1),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(label, style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w500, color: AppColors.textSecondary)),
            const SizedBox(height: 4),
            Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                AnimatedCounter(value: value, color: color),
                const SizedBox(width: 4),
                Text(unit, style: const TextStyle(fontSize: 14, color: AppColors.textSecondary)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
```

- [ ] **Step 2: 创建 animated_counter.dart**

```dart
import 'package:flutter/material.dart';

class AnimatedCounter extends StatelessWidget {
  final double value;
  final Color color;
  final int decimals;

  const AnimatedCounter({
    super.key,
    required this.value,
    required this.color,
    this.decimals = 1,
  });

  @override
  Widget build(BuildContext context) {
    return TweenAnimationBuilder<double>(
      tween: Tween(begin: value, end: value),
      duration: const Duration(milliseconds: 300),
      curve: Curves.easeOut,
      builder: (context, animatedValue, child) {
        return Text(
          animatedValue.toStringAsFixed(decimals),
          style: TextStyle(fontSize: 28, fontWeight: FontWeight.w700, color: color),
        );
      },
    );
  }
}
```

- [ ] **Step 3: 创建 status_indicator.dart**

```dart
import 'package:flutter/material.dart';
import '../../app/theme/app_colors.dart';

class StatusIndicator extends StatelessWidget {
  final String label;
  final bool active;
  final Color? activeColor;

  const StatusIndicator({
    super.key,
    required this.label,
    this.active = false,
    this.activeColor,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 8, height: 8,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: active ? (activeColor ?? AppColors.motionPrimary) : AppColors.divider,
          ),
        ),
        const SizedBox(width: 6),
        Text(label, style: TextStyle(
          fontSize: 12,
          color: active ? AppColors.textPrimary : AppColors.textSecondary,
        )),
      ],
    );
  }
}
```

- [ ] **Step 4: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/shared/widgets/
git commit -m "feat: add shared widgets (MetricCard, AnimatedCounter, StatusIndicator)"
```

### Task 9: 创建占位 Screen 和导航

**Files:**
- Create: `PathFinder_Dashboard/lib/features/connection/connection_screen.dart`
- Create: `PathFinder_Dashboard/lib/features/environment/environment_screen.dart`
- Create: `PathFinder_Dashboard/lib/features/motion/motion_screen.dart`
- Create: `PathFinder_Dashboard/lib/features/emote/emote_screen.dart`
- Create: `PathFinder_Dashboard/lib/app/app.dart`

- [ ] **Step 1: 创建 4 个占位 Screen**

每个 Screen 暂时显示一个居中的占位文本，后续 M3/M4 替换。

```dart
// connection_screen.dart
import 'package:flutter/material.dart';

class ConnectionScreen extends StatelessWidget {
  const ConnectionScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return const Center(child: Text('Connection Screen', style: TextStyle(color: Colors.white)));
  }
}
```

```dart
// environment_screen.dart
import 'package:flutter/material.dart';

class EnvironmentScreen extends StatelessWidget {
  const EnvironmentScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return const Center(child: Text('Environment Screen', style: TextStyle(color: Colors.white)));
  }
}
```

```dart
// motion_screen.dart
import 'package:flutter/material.dart';

class MotionScreen extends StatelessWidget {
  const MotionScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return const Center(child: Text('Motion Screen', style: TextStyle(color: Colors.white)));
  }
}
```

```dart
// emote_screen.dart
import 'package:flutter/material.dart';

class EmoteScreen extends StatelessWidget {
  const EmoteScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return const Center(child: Text('Emote Screen', style: TextStyle(color: Colors.white)));
  }
}
```

- [ ] **Step 2: 创建 app.dart（主框架 + BottomNavigationBar）**

```dart
import 'package:flutter/material.dart';
import 'theme/app_theme.dart';
import 'theme/app_colors.dart';
import '../features/connection/connection_screen.dart';
import '../features/environment/environment_screen.dart';
import '../features/motion/motion_screen.dart';
import '../features/emote/emote_screen.dart';

class PathfinderApp extends StatefulWidget {
  const PathfinderApp({super.key});

  @override
  State<PathfinderApp> createState() => _PathfinderAppState();
}

class _PathfinderAppState extends State<PathfinderApp> {
  int _currentIndex = 0;

  final _screens = const [
    ConnectionScreen(),
    EnvironmentScreen(),
    MotionScreen(),
    EmoteScreen(),
  ];

  final _navItems = const [
    NavigationDestination(icon: Icon(Icons.bluetooth), label: '设备'),
    NavigationDestination(icon: Icon(Icons.thermostat), label: '环境'),
    NavigationDestination(icon: Icon(Icons.sports_motorsports), label: '运动'),
    NavigationDestination(icon: Icon(Icons.face), label: '表情'),
  ];

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PathFinder Dashboard',
      theme: appTheme(),
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        body: IndexedStack(index: _currentIndex, children: _screens),
        bottomNavigationBar: NavigationBar(
          selectedIndex: _currentIndex,
          onDestinationSelected: (index) => setState(() => _currentIndex = index),
          destinations: _navItems,
        ),
      ),
    );
  }
}
```

- [ ] **Step 3: 更新 main.dart**

```dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'app/app.dart';

void main() {
  runApp(const ProviderScope(child: PathfinderApp()));
}
```

- [ ] **Step 4: 运行验证**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter analyze
flutter run  # 在 Android 模拟器/设备上验证 4 Tab 可切换
```

Expected: App 启动，4 个 Tab 可切换，各自显示占位文本

- [ ] **Step 5: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/
git commit -m "feat: M1 complete - 4-tab navigation with placeholder screens and theme"
```

**M1 完成。** 验证标准：App 启动，4 Tab 可切换，主题生效，Mock 数据流可用。

---

## M2: BLE 通信层 + 协议编解码

### Task 10: BLE UUID 常量

**Files:**
- Create: `PathFinder_Dashboard/lib/core/ble/ble_uuids.dart`

- [ ] **Step 1: 创建 ble_uuids.dart**

```dart
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

// Service UUID
final pfServiceUuid = Uuid.parse('0000fe00-0000-1000-8000-00805f9b34fb');

// Characteristic UUIDs
final c1DeviceInfoUuid = Uuid.parse('0000fe01-0000-1000-8000-00805f9b34fb');
final c2EnvDataUuid = Uuid.parse('0000fe02-0000-1000-8000-00805f9b34fb');
final c3MotionDataUuid = Uuid.parse('0000fe03-0000-1000-8000-00805f9b34fb');
final c4EmoteStateUuid = Uuid.parse('0000fe04-0000-1000-8000-00805f9b34fb');
final c5RawImuUuid = Uuid.parse('0000fe05-0000-1000-8000-00805f9b34fb');
```

- [ ] **Step 2: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/core/ble/ble_uuids.dart
git commit -m "feat: add BLE GATT UUID constants"
```

### Task 11: BLE 编解码测试

**Files:**
- Create: `PathFinder_Dashboard/test/core/ble/ble_codec_test.dart`

- [ ] **Step 1: 创建 ble_codec_test.dart**

```dart
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
        ByteData.sublistView(bytes).setInt16(4, Endian.little, -500); // -5.00°C
        final env = EnvSnapshot.fromBle(bytes);
        expect(env.temperature, closeTo(-5.0, 0.01));
      });

      test('decodes max temperature', () {
        final bytes = Uint8List(20);
        ByteData.sublistView(bytes).setInt16(4, Endian.little, 32767); // 327.67°C
        final env = EnvSnapshot.fromBle(bytes);
        expect(env.temperature, closeTo(327.67, 0.01));
      });

      test('decodes all fields', () {
        final bytes = Uint8List(20);
        final bd = ByteData.sublistView(bytes);
        bd.setInt16(4, Endian.little, 2635);
        bd.setUint16(6, Endian.little, 5800);
        bd.setUint32(8, Endian.little, 101325);
        bd.setInt16(12, Endian.little, -150); // -15.0m
        bd.setUint16(14, Endian.little, 800); // UV 8.00
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
        bd.setInt16(0, Endian.little, 1234);  // 12.34°
        bd.setInt16(2, Endian.little, -567);  // -5.67°
        bd.setUint16(4, Endian.little, 150);  // 0.150g
        bytes[6] = 3;  // brake
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
        bytes[0] = 5;   // emoteId
        bytes[1] = 5;   // name length
        bytes.setAll(2, 'Panic'.codeUnits);
        bytes[14] = 1;  // UV Extreme trigger
        final emote = EmoteInfo.fromBle(bytes);
        expect(emote.emoteId, 5);
        expect(emote.friendlyName, 'Panic');
        expect(emote.trigger, EmoteTrigger.uvExtreme);
      });
    });
  });
}
```

- [ ] **Step 2: 运行测试**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter test test/core/ble/ble_codec_test.dart
```

Expected: All tests pass

- [ ] **Step 3: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/test/
git commit -m "test: add BLE codec tests for all 3 data characteristics"
```

### Task 12: 连接页 UI

**Files:**
- Modify: `PathFinder_Dashboard/lib/features/connection/connection_screen.dart`
- Create: `PathFinder_Dashboard/lib/features/connection/widgets/device_tile.dart`

- [ ] **Step 1: 创建 device_tile.dart**

```dart
import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

class DeviceTile extends StatelessWidget {
  final String name;
  final int rssi;
  final VoidCallback onTap;

  const DeviceTile({super.key, required this.name, required this.rssi, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      child: ListTile(
        onTap: onTap,
        leading: Icon(Icons.bluetooth, color: AppColors.envPrimary),
        title: Text(name, style: const TextStyle(color: AppColors.textPrimary, fontWeight: FontWeight.w600)),
        subtitle: Text('RSSI: ${rssi}dBm', style: const TextStyle(color: AppColors.textSecondary)),
        trailing: const Icon(Icons.chevron_right, color: AppColors.textSecondary),
      ),
    );
  }
}
```

- [ ] **Step 2: 重写 connection_screen.dart**

```dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/ble_provider.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/device_tile.dart';

class ConnectionScreen extends ConsumerWidget {
  const ConnectionScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('PathFinder Dashboard')),
      body: connState.when(
        data: (state) => _buildBody(state, ref),
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => Center(child: Text('Error: $e')),
      ),
    );
  }

  Widget _buildBody(dynamic state, WidgetRef ref) {
    final ble = ref.read(bleServiceProvider);

    switch (state) {
      case ConnectionState.disconnected:
        return Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.bluetooth_searching, size: 64, color: AppColors.envPrimary),
            const SizedBox(height: 16),
            const Text('点击扫描附近设备', style: TextStyle(color: AppColors.textSecondary)),
            const SizedBox(height: 24),
            ElevatedButton.icon(
              onPressed: () => ble.startScan(),
              icon: const Icon(Icons.refresh),
              label: const Text('扫描设备'),
              style: ElevatedButton.styleFrom(backgroundColor: AppColors.envPrimary),
            ),
            const SizedBox(height: 32),
            // Mock device for development
            DeviceTile(
              name: 'PathFinder-EMOTE (Mock)',
              rssi: -52,
              onTap: () => ble.connect('mock-device-001'),
            ),
          ],
        );
      case ConnectionState.connecting:
        return const Center(child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CircularProgressIndicator(color: AppColors.envPrimary),
            SizedBox(height: 16),
            Text('连接中...', style: TextStyle(color: AppColors.textPrimary)),
          ],
        ));
      case ConnectionState.connected:
        return Center(child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.bluetooth_connected, size: 64, color: AppColors.motionPrimary),
            const SizedBox(height: 16),
            const Text('已连接 PathFinder-EMOTE', style: TextStyle(color: AppColors.textPrimary, fontSize: 18)),
            const SizedBox(height: 8),
            Row(mainAxisAlignment: MainAxisAlignment.center, children: const [
              Text('✓AHT20', style: TextStyle(color: AppColors.motionPrimary, fontSize: 12)),
              SizedBox(width: 8),
              Text('✓BMP280', style: TextStyle(color: AppColors.motionPrimary, fontSize: 12)),
              SizedBox(width: 8),
              Text('✓MPU6050', style: TextStyle(color: AppColors.motionPrimary, fontSize: 12)),
              SizedBox(width: 8),
              Text('✓UV', style: TextStyle(color: AppColors.motionPrimary, fontSize: 12)),
            ]),
            const SizedBox(height: 24),
            OutlinedButton.icon(
              onPressed: () => ble.disconnect(),
              icon: const Icon(Icons.bluetooth_disabled),
              label: const Text('断开连接'),
              style: OutlinedButton.styleFrom(foregroundColor: AppColors.urgent),
            ),
          ],
        ));
      case ConnectionState.scanning:
        return const Center(child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CircularProgressIndicator(color: AppColors.envPrimary),
            SizedBox(height: 16),
            Text('扫描中...', style: TextStyle(color: AppColors.textPrimary)),
          ],
        ));
      default:
        return const Center(child: Text('Unknown state'));
    }
  }
}
```

- [ ] **Step 3: 运行验证**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter run
```

Expected: 连接页显示扫描按钮，点击后显示 Mock 设备，点击连接后显示已连接状态

- [ ] **Step 4: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/features/connection/
git commit -m "feat: M2 - connection screen with BLE scan/connect/disconnect UI"
```

**M2 完成。** 验证标准：Mock BLE 服务可扫描→连接→断开，连接页 UI 状态切换正常。

---

## M3: 环境 Tab + 运动 Tab 完整 UI

### Task 13: 环境数据页 — 指标卡片

**Files:**
- Modify: `PathFinder_Dashboard/lib/features/environment/environment_screen.dart`

- [ ] **Step 1: 实现 environment_screen.dart**

```dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../shared/widgets/metric_card.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/env_line_chart.dart';

class EnvironmentScreen extends ConsumerWidget {
  const EnvironmentScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final envData = ref.watch(envStreamProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('环境数据'),
        actions: [
          TextButton(
            onPressed: () {}, // TODO: navigate to history
            child: const Text('历史', style: TextStyle(color: AppColors.envPrimary)),
          ),
        ],
      ),
      body: connState.when(
        data: (state) => state == ConnectionState.connected
            ? _buildConnected(envData)
            : const _NotConnectedPlaceholder(),
        loading: () => const _NotConnectedPlaceholder(),
        error: (_, __) => const _NotConnectedPlaceholder(),
      ),
    );
  }

  Widget _buildConnected(AsyncValue<dynamic> envData) {
    return envData.when(
      data: (env) => SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Metric cards row
            const Text('实时数据', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
            const SizedBox(height: 12),
            Row(children: [
              Expanded(child: MetricCard(value: env.temperature, unit: '°C', label: '温度', color: AppColors.envPrimary)),
              const SizedBox(width: 8),
              Expanded(child: MetricCard(value: env.humidity, unit: '%', label: '湿度', color: AppColors.envPrimary)),
              const SizedBox(width: 8),
              Expanded(child: MetricCard(value: env.pressure.toDouble(), unit: 'Pa', label: '气压', color: AppColors.envPrimary, decimals: 0)),
            ]),
            const SizedBox(height: 8),
            Row(children: [
              Expanded(child: MetricCard(value: env.altitude, unit: 'm', label: '海拔', color: AppColors.envPrimary)),
              const SizedBox(width: 8),
              Expanded(child: MetricCard(value: env.uvIndex, unit: 'UV', label: '紫外线指数', color: env.uvIndex >= 6 ? AppColors.warning : AppColors.envPrimary)),
              const Spacer(),
            ]),
            const SizedBox(height: 24),
            // Line chart
            const Text('趋势 (最近60秒)', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
            const SizedBox(height: 12),
            const EnvLineChart(),
          ],
        ),
      ),
      loading: () => const Center(child: CircularProgressIndicator(color: AppColors.envPrimary)),
      error: (e, _) => Center(child: Text('数据错误: $e')),
    );
  }
}

class _NotConnectedPlaceholder extends StatelessWidget {
  const _NotConnectedPlaceholder();
  @override
  Widget build(BuildContext context) {
    return const Center(
      child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
        Icon(Icons.bluetooth_disabled, size: 48, color: AppColors.textSecondary),
        SizedBox(height: 16),
        Text('等待设备连接...', style: TextStyle(color: AppColors.textSecondary)),
      ]),
    );
  }
}
```

- [ ] **Step 2: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/features/environment/
git commit -m "feat: M3 - environment screen with metric cards"
```

### Task 14: 环境折线图

**Files:**
- Create: `PathFinder_Dashboard/lib/features/environment/widgets/env_line_chart.dart`

- [ ] **Step 1: 创建 env_line_chart.dart**

```dart
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../shared/providers/sensor_provider.dart';
import '../../../shared/models/env_snapshot.dart';
import '../../../app/theme/app_colors.dart';

class EnvLineChart extends ConsumerStatefulWidget {
  const EnvLineChart({super.key});

  @override
  ConsumerState<EnvLineChart> createState() => _EnvLineChartState();
}

class _EnvLineChartState extends ConsumerState<EnvLineChart> {
  final List<EnvSnapshot> _buffer = [];
  static const int maxPoints = 60;

  @override
  Widget build(BuildContext context) {
    final envData = ref.watch(envStreamProvider);
    envData.whenData((env) {
      _buffer.add(env);
      if (_buffer.length > maxPoints) _buffer.removeAt(0);
    });

    return SizedBox(
      height: 200,
      child: _buffer.isEmpty
          ? const Center(child: Text('等待数据...', style: TextStyle(color: AppColors.textSecondary)))
          : LineChart(
              LineChartData(
                gridData: FlGridData(show: true, drawVerticalLine: false,
                  getDrawingHorizontalLine: (v) => FlLine(color: AppColors.divider, strokeWidth: 0.5)),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(sideTitles: SideTitles(
                    showTitles: true, reservedSize: 40,
                    getTitlesWidget: (v, _) => Text(v.toStringAsFixed(0),
                      style: const TextStyle(color: AppColors.textSecondary, fontSize: 10)))),
                  bottomTitles: AxisTitles(sideTitles: SideTitles(
                    showTitles: true, reservedSize: 20,
                    getTitlesWidget: (v, _) => Text('${-maxPoints + v.toInt()}s',
                      style: const TextStyle(color: AppColors.textSecondary, fontSize: 10)))),
                  topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                  rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                ),
                borderData: FlBorderData(show: false),
                lineBarsData: [
                  // Temperature line (cyan)
                  LineChartBarData(
                    spots: _buffer.asMap().entries.map((e) =>
                      FlSpot(e.key.toDouble(), _buffer[e.key].temperature)).toList(),
                    isCurved: true,
                    color: AppColors.envPrimary,
                    barWidth: 2,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(show: true, color: AppColors.envPrimary.withValues(alpha: 0.1)),
                  ),
                ],
                lineTouchData: LineTouchData(enabled: true),
              ),
            ),
    );
  }
}
```

- [ ] **Step 2: 运行验证**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter run
```

Expected: 连接后环境 Tab 显示 5 个指标卡片 + 实时折线图

- [ ] **Step 3: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/features/environment/
git commit -m "feat: M3 - environment line chart with 60s sliding window"
```

### Task 15: 运动数据页

**Files:**
- Modify: `PathFinder_Dashboard/lib/features/motion/motion_screen.dart`
- Create: `PathFinder_Dashboard/lib/features/motion/widgets/attitude_indicator.dart`
- Create: `PathFinder_Dashboard/lib/features/motion/widgets/imu_wave_chart.dart`
- Create: `PathFinder_Dashboard/lib/features/motion/widgets/event_timeline.dart`

- [ ] **Step 1: 创建 attitude_indicator.dart**

```dart
import 'dart:math';
import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

class AttitudeIndicator extends StatelessWidget {
  final double pitch; // degrees
  final double roll;  // degrees

  const AttitudeIndicator({super.key, required this.pitch, required this.roll});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 120, height: 120,
      child: CustomPaint(
        painter: _AttitudePainter(pitch: pitch, roll: roll),
        child: Center(
          child: Text('${pitch.toStringAsFixed(1)}°',
            style: const TextStyle(color: Colors.white, fontSize: 14, fontWeight: FontWeight.w600)),
        ),
      ),
    );
  }
}

class _AttitudePainter extends CustomPainter {
  final double pitch, roll;
  _AttitudePainter({required this.pitch, required this.roll});

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = size.width / 2 - 4;

    // Background circle
    canvas.drawCircle(center, radius, Paint()..color = AppColors.surface);
    canvas.drawCircle(center, radius, Paint()..color = AppColors.divider..style = PaintingStyle.stroke..strokeWidth = 1);

    // Crosshair
    canvas.drawLine(Offset(center.dx - radius * 0.3, center.dy), Offset(center.dx + radius * 0.3, center.dy),
      Paint()..color = AppColors.divider..strokeWidth = 0.5);
    canvas.drawLine(Offset(center.dx, center.dy - radius * 0.3), Offset(center.dx, center.dy + radius * 0.3),
      Paint()..color = AppColors.divider..strokeWidth = 0.5);

    // Bubble (roll/pitch mapped to position)
    final bubbleX = center.dx + (roll / 45.0) * radius * 0.7;
    final bubbleY = center.dy - (pitch / 45.0) * radius * 0.7;
    canvas.drawCircle(Offset(bubbleX.clamp(center.dx - radius * 0.8, center.dx + radius * 0.8),
                             bubbleY.clamp(center.dy - radius * 0.8, center.dy + radius * 0.8)),
      8, Paint()..color = AppColors.motionPrimary);
  }

  @override
  bool shouldRepaint(covariant _AttitudePainter old) => old.pitch != pitch || old.roll != roll;
}
```

- [ ] **Step 2: 创建 imu_wave_chart.dart**

```dart
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../shared/providers/sensor_provider.dart';
import '../../../shared/models/imu_snapshot.dart';
import '../../../app/theme/app_colors.dart';

class ImuWaveChart extends ConsumerStatefulWidget {
  const ImuWaveChart({super.key});

  @override
  ConsumerState<ImuWaveChart> createState() => _ImuWaveChartState();
}

class _ImuWaveChartState extends ConsumerState<ImuWaveChart> {
  final List<ImuSnapshot> _buffer = [];
  static const int maxPoints = 250; // 10 seconds @ 25Hz

  @override
  Widget build(BuildContext context) {
    final motionData = ref.watch(motionStreamProvider);
    motionData.whenData((imu) {
      _buffer.add(imu);
      if (_buffer.length > maxPoints) _buffer.removeAt(0);
    });

    if (_buffer.isEmpty) {
      return const SizedBox(height: 150, child: Center(child: Text('等待数据...', style: TextStyle(color: AppColors.textSecondary))));
    }

    return RepaintBoundary(
      child: SizedBox(
        height: 150,
        child: LineChart(
          LineChartData(
            gridData: FlGridData(show: false),
            titlesData: FlTitlesData(show: false),
            borderData: FlBorderData(show: false),
            lineBarsData: [
              _makeLine(Colors.red, (imu) => imu.pitch),
              _makeLine(Colors.green, (imu) => imu.roll),
              _makeLine(Colors.blue, (imu) => imu.accelMag * 100), // scale up for visibility
            ],
          ),
        ),
      ),
    );
  }

  LineChartBarData _makeLine(Color color, double Function(ImuSnapshot) getter) {
    return LineChartBarData(
      spots: _buffer.asMap().entries.map((e) => FlSpot(e.key.toDouble(), getter(_buffer[e.key]))).toList(),
      isCurved: true,
      color: color,
      barWidth: 1.5,
      dotData: const FlDotData(show: false),
    );
  }
}
```

- [ ] **Step 3: 创建 event_timeline.dart**

```dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../shared/providers/sensor_provider.dart';
import '../../../shared/models/motion_event.dart';
import '../../../app/theme/app_colors.dart';

class EventTimeline extends ConsumerWidget {
  const EventTimeline({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final motionData = ref.watch(motionStreamProvider);
    return motionData.when(
      data: (imu) => Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: AppColors.surface,
          borderRadius: BorderRadius.circular(8),
        ),
        child: Row(
          children: [
            Container(width: 10, height: 10, decoration: BoxDecoration(
              shape: BoxShape.circle, color: imu.event.color)),
            const SizedBox(width: 8),
            Text(imu.event.label, style: TextStyle(color: imu.event.color, fontWeight: FontWeight.w600)),
            const Spacer(),
            Text('置信度 ${imu.confidence}%', style: const TextStyle(color: AppColors.textSecondary, fontSize: 12)),
          ],
        ),
      ),
      loading: () => const SizedBox.shrink(),
      error: (_, __) => const SizedBox.shrink(),
    );
  }
}
```

- [ ] **Step 4: 重写 motion_screen.dart**

```dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/attitude_indicator.dart';
import 'widgets/imu_wave_chart.dart';
import 'widgets/event_timeline.dart';

class MotionScreen extends ConsumerWidget {
  const MotionScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final motionData = ref.watch(motionStreamProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('运动数据')),
      body: connState.when(
        data: (state) => state == ConnectionState.connected
            ? motionData.when(
                data: (imu) => SingleChildScrollView(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text('当前姿态', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
                      const SizedBox(height: 12),
                      Row(children: [
                        AttitudeIndicator(pitch: imu.pitch, roll: imu.roll),
                        const SizedBox(width: 16),
                        Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                          Text('Pitch: ${imu.pitch.toStringAsFixed(1)}°',
                            style: TextStyle(color: AppColors.motionPrimary, fontSize: 16, fontWeight: FontWeight.w600)),
                          const SizedBox(height: 4),
                          Text('Roll: ${imu.roll.toStringAsFixed(1)}°',
                            style: TextStyle(color: AppColors.motionPrimary, fontSize: 16, fontWeight: FontWeight.w600)),
                          const SizedBox(height: 4),
                          Text('加速度偏差: ${imu.accelMag.toStringAsFixed(3)}g',
                            style: const TextStyle(color: AppColors.textSecondary, fontSize: 14)),
                        ]),
                      ]),
                      const SizedBox(height: 24),
                      const Text('IMU 波形 (最近10秒)', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
                      const SizedBox(height: 12),
                      const ImuWaveChart(),
                      const SizedBox(height: 24),
                      const Text('当前事件', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
                      const SizedBox(height: 12),
                      const EventTimeline(),
                    ],
                  ),
                ),
                loading: () => const Center(child: CircularProgressIndicator(color: AppColors.motionPrimary)),
                error: (e, _) => Center(child: Text('Error: $e')),
              )
            : const Center(child: Text('等待设备连接...', style: TextStyle(color: AppColors.textSecondary))),
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (_, __) => const Center(child: Text('Error')),
      ),
    );
  }
}
```

- [ ] **Step 5: 运行验证**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter run
```

Expected: 连接后运动 Tab 显示姿态指示器 + 波形图 + 事件状态

- [ ] **Step 6: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/features/motion/
git commit -m "feat: M3 - motion screen with attitude indicator, IMU waveform, event timeline"
```

**M3 完成。** 验证标准：环境 Tab 有 5 个指标卡片 + 折线图，运动 Tab 有姿态指示器 + 波形图 + 事件显示。

---

## M4: 表情 Tab + 历史页

### Task 16: 表情状态页

**Files:**
- Modify: `PathFinder_Dashboard/lib/features/emote/emote_screen.dart`
- Create: `PathFinder_Dashboard/lib/features/emote/widgets/emote_mapping_table.dart`
- Create: `PathFinder_Dashboard/lib/features/emote/widgets/emote_gallery.dart`

- [ ] **Step 1: 创建 emote_mapping_table.dart**

```dart
import 'package:flutter/material.dart';
import '../../../shared/models/emote_rules.dart';
import '../../../shared/models/emote_info.dart';
import '../../../shared/models/env_snapshot.dart';
import '../../../app/theme/app_colors.dart';

class EmoteMappingTable extends StatelessWidget {
  final EnvSnapshot? currentEnv;
  final double currentPitch;
  final double currentRoll;
  final EmoteTrigger? activeTrigger;

  const EmoteMappingTable({
    super.key,
    required this.currentEnv,
    required this.currentPitch,
    required this.currentRoll,
    this.activeTrigger,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      children: emoteRules.map((rule) {
        final isActive = activeTrigger == rule.trigger;
        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          margin: const EdgeInsets.symmetric(vertical: 2),
          decoration: BoxDecoration(
            color: isActive ? rule.trigger == EmoteTrigger.normal
                ? AppColors.motionPrimary.withValues(alpha: 0.15)
                : AppColors.warning.withValues(alpha: 0.15)
                : Colors.transparent,
            borderRadius: BorderRadius.circular(6),
            border: isActive ? Border.all(
              color: isActive ? AppColors.warning : AppColors.motionPrimary, width: 1) : null,
          ),
          child: Row(children: [
            Text(rule.condition, style: TextStyle(
              color: isActive ? AppColors.textPrimary : AppColors.textSecondary,
              fontWeight: isActive ? FontWeight.w600 : FontWeight.w400, fontSize: 13)),
            const Spacer(),
            Text('→ ${rule.emoteName}', style: TextStyle(
              color: isActive ? AppColors.warning : AppColors.textSecondary,
              fontWeight: FontWeight.w500, fontSize: 13)),
          ]),
        );
      }).toList(),
    );
  }
}
```

- [ ] **Step 2: 创建 emote_gallery.dart**

```dart
import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

class EmoteGallery extends StatelessWidget {
  final int currentEmoteId;
  final List<String> emoteNames;

  const EmoteGallery({super.key, required this.currentEmoteId, required this.emoteNames});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 200,
      child: GridView.builder(
        scrollDirection: Axis.horizontal,
        gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
          crossAxisCount: 3, childAspectRatio: 1.0, mainAxisSpacing: 4, crossAxisSpacing: 4),
        itemCount: emoteNames.length,
        itemBuilder: (context, index) {
          final isActive = index == currentEmoteId;
          return Container(
            decoration: BoxDecoration(
              color: AppColors.surface,
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: isActive ? AppColors.warning : AppColors.divider,
                width: isActive ? 2 : 1),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.face, size: 32, color: isActive ? AppColors.warning : AppColors.textSecondary),
                const SizedBox(height: 4),
                Text(emoteNames[index], style: TextStyle(
                  color: isActive ? AppColors.warning : AppColors.textSecondary,
                  fontSize: 10, fontWeight: isActive ? FontWeight.w600 : FontWeight.w400)),
              ],
            ),
          );
        },
      ),
    );
  }
}
```

- [ ] **Step 3: 重写 emote_screen.dart**

```dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../shared/providers/sensor_provider.dart';
import '../../shared/providers/ble_provider.dart';
import '../../shared/models/emote_rules.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/emote_mapping_table.dart';
import 'widgets/emote_gallery.dart';

class EmoteScreen extends ConsumerWidget {
  const EmoteScreen({super.key});

  static const emoteNames = [
    'Angry', 'Asleep', 'Badminton', 'Confident', 'Cry',
    'Investigate', 'Laugh', 'Leisure', 'Mock', 'Music',
    'Mute', 'Panic', 'Ponder', 'Question', 'Sad',
    'Shocked', 'Shy', 'Sigh', 'Smile', 'Snigger',
    'Yawn', 'Yummy', 'Unknown1', 'Unknown2',
  ];

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(connectionStateProvider);
    final emoteData = ref.watch(emoteStreamProvider);
    final envData = ref.watch(envStreamProvider);
    final motionData = ref.watch(motionStreamProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('表情状态')),
      body: connState.when(
        data: (state) => state == ConnectionState.connected
            ? emoteData.when(
                data: (emote) {
                  final env = envData.valueOrNull;
                  final imu = motionData.valueOrNull;
                  final rule = evaluateEmote(env, imu?.pitch ?? 0, imu?.roll ?? 0);
                  return SingleChildScrollView(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        // Current emote card
                        Container(
                          width: double.infinity,
                          padding: const EdgeInsets.all(24),
                          decoration: BoxDecoration(
                            color: AppColors.surface,
                            borderRadius: BorderRadius.circular(16),
                            border: Border.all(color: AppColors.warning.withValues(alpha: 0.3)),
                          ),
                          child: Column(children: [
                            Icon(Icons.face, size: 80, color: AppColors.warning),
                            const SizedBox(height: 12),
                            Text(emote.friendlyName, style: const TextStyle(
                              fontSize: 28, fontWeight: FontWeight.w700, color: AppColors.textPrimary)),
                            const SizedBox(height: 4),
                            Text(emote.trigger.label, style: const TextStyle(
                              fontSize: 14, color: AppColors.warning)),
                          ]),
                        ),
                        const SizedBox(height: 24),
                        // Mapping table
                        const Text('映射规则表', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
                        const SizedBox(height: 12),
                        EmoteMappingTable(
                          currentEnv: env,
                          currentPitch: imu?.pitch ?? 0,
                          currentRoll: imu?.roll ?? 0,
                          activeTrigger: rule.trigger,
                        ),
                        const SizedBox(height: 24),
                        // Gallery
                        const Text('表情总览 (24种)', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
                        const SizedBox(height: 12),
                        EmoteGallery(currentEmoteId: emote.emoteId, emoteNames: emoteNames),
                      ],
                    ),
                  );
                },
                loading: () => const Center(child: CircularProgressIndicator(color: AppColors.warning)),
                error: (e, _) => Center(child: Text('Error: $e')),
              )
            : const Center(child: Text('等待设备连接...', style: TextStyle(color: AppColors.textSecondary))),
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (_, __) => const Center(child: Text('Error')),
      ),
    );
  }
}
```

- [ ] **Step 4: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/features/emote/
git commit -m "feat: M4 - emote screen with current emote, mapping table, gallery"
```

### Task 17: 历史页

**Files:**
- Create: `PathFinder_Dashboard/lib/features/history/history_screen.dart`

- [ ] **Step 1: 创建 history_screen.dart（占位实现，M5 接入 Drift 后完善）**

```dart
import 'package:flutter/material.dart';
import '../../app/theme/app_colors.dart';

class HistoryScreen extends StatelessWidget {
  const HistoryScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('历史数据')),
      body: const Center(
        child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
          Icon(Icons.history, size: 48, color: AppColors.textSecondary),
          SizedBox(height: 16),
          Text('历史数据将在 M5 接入 Drift 存储后启用', style: TextStyle(color: AppColors.textSecondary)),
        ]),
      ),
    );
  }
}
```

- [ ] **Step 2: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/features/history/
git commit -m "feat: M4 - history screen placeholder (to be wired to Drift in M5)"
```

**M4 完成。** 验证标准：表情 Tab 显示当前表情 + 映射表高亮 + 24 种表情网格。

---

## M5: 存储 + 导出 + 优化

### Task 18: Drift 数据库

**Files:**
- Create: `PathFinder_Dashboard/lib/core/storage/database.dart`
- Create: `PathFinder_Dashboard/lib/core/storage/tables.dart`
- Create: `PathFinder_Dashboard/lib/core/storage/dao_env.dart`
- Create: `PathFinder_Dashboard/lib/core/storage/dao_event.dart`
- Create: `PathFinder_Dashboard/lib/core/storage/dao_emote.dart`

- [ ] **Step 1: 创建 tables.dart**

```dart
import 'package:drift/drift.dart';

class EnvRecords extends Table {
  IntColumn get id => integer().autoIncrement()();
  DateTimeColumn get timestamp => dateTime()();
  RealColumn get temperature => real()();
  RealColumn get humidity => real()();
  IntColumn get pressure => integer()();
  RealColumn get altitude => real()();
  RealColumn get uvIndex => real()();
}

class EventRecords extends Table {
  IntColumn get id => integer().autoIncrement()();
  DateTimeColumn get timestamp => dateTime()();
  IntColumn get eventType => integer()();
  RealColumn get pitch => real()();
  RealColumn get roll => real()();
  RealColumn get accelMag => real()();
  IntColumn get duration => integer().nullable()();
}

class EmoteRecords extends Table {
  IntColumn get id => integer().autoIncrement()();
  DateTimeColumn get timestamp => dateTime()();
  IntColumn get emoteId => integer()();
  TextColumn get friendlyName => text()();
  IntColumn get triggerCode => integer()();
  TextColumn get triggerLabel => text()();
  RealColumn get sensorValue => real().nullable()();
}
```

- [ ] **Step 2: 创建 database.dart**

```dart
import 'dart:io';
import 'package:drift/drift.dart';
import 'package:drift/native.dart';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
import 'tables.dart';
import 'dao_env.dart';
import 'dao_event.dart';
import 'dao_emote.dart';

part 'database.g.dart';

@DriftDatabase(tables: [EnvRecords, EventRecords, EmoteRecords], include: {'dao_env.dart', 'dao_event.dart', 'dao_emote.dart'})
class AppDatabase extends _$AppDatabase {
  AppDatabase() : super(_openConnection());

  @override
  int get schemaVersion => 1;

  @override
  MigrationStrategy get migration => MigrationStrategy(
    onCreate: (m) async => m.createAll(),
    beforeOpen: (details) async => customStatement('PRAGMA journal_mode=WAL'),
  );
}

LazyDatabase _openConnection() {
  return LazyDatabase(() async {
    final dir = await getApplicationDocumentsDirectory();
    final file = File(p.join(dir.path, 'pathfinder_dashboard.sqlite'));
    return NativeDatabase.createInBackground(file);
  });
}
```

- [ ] **Step 3: 创建 DAO 文件**

```dart
// dao_env.dart
import 'package:drift/drift.dart';
import 'database.dart';
import 'tables.dart';

part 'dao_env.g.dart';

@DriftAccessor(tables: [EnvRecords])
class EnvDao extends DatabaseAccessor<AppDatabase> with _$EnvDaoMixin {
  EnvDao(AppDatabase db) : super(db);

  Future<void> insertEnv(DateTime timestamp, double temperature, double humidity,
      int pressure, double altitude, double uvIndex) {
    return into(envRecords).insert(EnvRecordsCompanion.insert(
      timestamp: timestamp, temperature: temperature, humidity: humidity,
      pressure: pressure, altitude: altitude, uvIndex: uvIndex));
  }

  Stream<List<EnvRecord>> watchRecent({int limit = 60}) {
    return (select(envRecords)..orderBy([(t) => OrderingTerm.desc(t.timestamp)])..limit(limit))
        .watch();
  }

  Future<List<EnvRecord>> queryRange(DateTime start, DateTime end) {
    return (select(envRecords)
      ..where((t) => t.timestamp.isBetweenValues(start, end))
      ..orderBy([(t) => OrderingTerm.asc(t.timestamp)]))
        .get();
  }
}
```

```dart
// dao_event.dart
import 'package:drift/drift.dart';
import 'database.dart';
import 'tables.dart';

part 'dao_event.g.dart';

@DriftAccessor(tables: [EventRecords])
class EventDao extends DatabaseAccessor<AppDatabase> with _$EventDaoMixin {
  EventDao(AppDatabase db) : super(db);

  Future<void> insertEvent(DateTime timestamp, int eventType, double pitch,
      double roll, double accelMag, {int? duration}) {
    return into(eventRecords).insert(EventRecordsCompanion.insert(
      timestamp: timestamp, eventType: eventType, pitch: pitch,
      roll: roll, accelMag: accelMag, duration: Value(duration)));
  }

  Stream<List<EventRecord>> watchRecentEvents({int limit = 50}) {
    return (select(eventRecords)..orderBy([(t) => OrderingTerm.desc(t.timestamp)])..limit(limit))
        .watch();
  }
}
```

```dart
// dao_emote.dart
import 'package:drift/drift.dart';
import 'database.dart';
import 'tables.dart';

part 'dao_emote.g.dart';

@DriftAccessor(tables: [EmoteRecords])
class EmoteDao extends DatabaseAccessor<AppDatabase> with _$EmoteDaoMixin {
  EmoteDao(AppDatabase db) : super(db);

  Future<void> insertEmote(DateTime timestamp, int emoteId, String friendlyName,
      int triggerCode, String triggerLabel, {double? sensorValue}) {
    return into(emoteRecords).insert(EmoteRecordsCompanion.insert(
      timestamp: timestamp, emoteId: emoteId, friendlyName: friendlyName,
      triggerCode: triggerCode, triggerLabel: triggerLabel,
      sensorValue: Value(sensorValue)));
  }

  Stream<List<EmoteRecord>> watchRecentEmotes({int limit = 50}) {
    return (select(emoteRecords)..orderBy([(t) => OrderingTerm.desc(t.timestamp)])..limit(limit))
        .watch();
  }
}
```

- [ ] **Step 4: 运行 build_runner 生成代码**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
dart run build_runner build --delete-conflicting-outputs
```

- [ ] **Step 5: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/core/storage/
git commit -m "feat: M5 - Drift database with 3 tables (env, event, emote)"
```

### Task 19: 接入存储到数据流

**Files:**
- Modify: `PathFinder_Dashboard/lib/shared/providers/sensor_provider.dart`

- [ ] **Step 1: 修改 sensor_provider.dart 添加入库逻辑**

在 envStreamProvider 和 emoteStreamProvider 的 stream 中添加 `.map()` 调用 DAO 写入。具体代码在实现时根据 Drift 生成的类型调整。

- [ ] **Step 2: 运行验证**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter analyze
flutter test
```

- [ ] **Step 3: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/shared/providers/
git commit -m "feat: M5 - wire up Drift storage to sensor data streams"
```

### Task 20: CSV 导出

**Files:**
- Create: `PathFinder_Dashboard/lib/core/storage/csv_export.dart`

- [ ] **Step 1: 创建 csv_export.dart**

```dart
import 'dart:io';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
import 'database.dart';

Future<String> exportEnvToCsv(AppDatabase db) async {
  final records = await (db.select(db.envRecords)
    ..orderBy([(t) => OrderingTerm.asc(t.timestamp)]))
      .get();

  final buffer = StringBuffer();
  buffer.writeln('timestamp,temperature,humidity,pressure,altitude,uvIndex');
  for (final r in records) {
    buffer.writeln('${r.timestamp.toIso8601String()},${r.temperature},${r.humidity},${r.pressure},${r.altitude},${r.uvIndex}');
  }

  final dir = await getApplicationDocumentsDirectory();
  final file = File(p.join(dir.path, 'pathfinder_env_${DateTime.now().millisecondsSinceEpoch}.csv'));
  await file.writeAsString(buffer.toString());
  return file.path;
}
```

- [ ] **Step 2: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Dashboard/lib/core/storage/csv_export.dart
git commit -m "feat: M5 - CSV export for environment data"
```

### Task 21: 最终验证与清理

- [ ] **Step 1: 全量测试**

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Dashboard
flutter analyze
flutter test
flutter build apk --debug
```

Expected: 无错误，所有测试通过，APK 构建成功

- [ ] **Step 2: 最终提交**

```bash
cd /Users/pm/PathFinder_LCD
git add -A
git commit -m "feat: M5 complete - storage, export, optimization. All milestones delivered."
```

**M5 完成。** 全部 5 个里程碑交付。

---

## 执行交接

计划已保存到 `docs/superpowers/plans/2026-07-12-pathfinder-dashboard.md`。两种执行方式：

**1. Subagent 驱动（推荐）** — 每个 Task 分派独立 subagent，任务间审查，快速迭代

**2. 内联执行** — 在当前会话中逐步执行，批量执行 + 检查点

选择哪种方式？
