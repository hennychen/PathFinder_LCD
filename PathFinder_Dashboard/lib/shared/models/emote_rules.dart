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
final emoteRules = <EmoteRule>[
  EmoteRule(
    emoteName: 'panic',
    priority: 90,
    trigger: EmoteTrigger.uvExtreme,
    condition: 'UV ≥ 8.0',
    matches: (env, p, r) => env != null && env.uvIndex >= 8.0,
  ),
  EmoteRule(
    emoteName: 'mock',
    priority: 80,
    trigger: EmoteTrigger.uvHigh,
    condition: 'UV ≥ 6.0',
    matches: (env, p, r) => env != null && env.uvIndex >= 6.0,
  ),
  EmoteRule(
    emoteName: 'sigh',
    priority: 70,
    trigger: EmoteTrigger.hot,
    condition: '温度 ≥ 32°C',
    matches: (env, p, r) => env != null && env.temperature >= 32.0,
  ),
  EmoteRule(
    emoteName: 'sad',
    priority: 65,
    trigger: EmoteTrigger.cold,
    condition: '温度 ≤ 10°C',
    matches: (env, p, r) => env != null && env.temperature <= 10.0,
  ),
  EmoteRule(
    emoteName: 'cry',
    priority: 60,
    trigger: EmoteTrigger.humid,
    condition: '湿度 ≥ 85%',
    matches: (env, p, r) => env != null && env.humidity >= 85.0,
  ),
  EmoteRule(
    emoteName: 'question',
    priority: 50,
    trigger: EmoteTrigger.tilted,
    condition: '倾角 ≥ 20°',
    matches: (env, p, r) => sqrt(p * p + r * r) >= 20.0,
  ),
  EmoteRule(
    emoteName: 'investigate',
    priority: 40,
    trigger: EmoteTrigger.slightTilt,
    condition: '倾角 ≥ 12°',
    matches: (env, p, r) => sqrt(p * p + r * r) >= 12.0,
  ),
  EmoteRule(
    emoteName: 'ponder',
    priority: 30,
    trigger: EmoteTrigger.lowPressure,
    condition: '气压 ≤ 1000hPa',
    matches: (env, p, r) =>
        env != null && env.pressure > 0 && env.pressure <= 100000,
  ),
  EmoteRule(
    emoteName: 'leisure',
    priority: 10,
    trigger: EmoteTrigger.normal,
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
