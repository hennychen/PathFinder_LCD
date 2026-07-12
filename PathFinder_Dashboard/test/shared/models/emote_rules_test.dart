import 'package:flutter_test/flutter_test.dart';
import 'package:pathfinder_dashboard/shared/models/env_snapshot.dart';
import 'package:pathfinder_dashboard/shared/models/emote_info.dart';
import 'package:pathfinder_dashboard/shared/models/emote_rules.dart';

void main() {
  group('evaluateEmote', () {
    test('UV >= 8.0 -> panic (highest priority)', () {
      final env = EnvSnapshot.mock(uvIndex: 8.1, temperature: 25.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'panic');
      expect(rule.trigger, EmoteTrigger.uvExtreme);
    });

    test('UV >= 6.0 -> mock', () {
      final env = EnvSnapshot.mock(uvIndex: 6.5, temperature: 25.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'mock');
    });

    test('temperature >= 32 -> sigh', () {
      final env = EnvSnapshot.mock(temperature: 35.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'sigh');
    });

    test('temperature <= 10 -> sad', () {
      final env = EnvSnapshot.mock(temperature: 5.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'sad');
    });

    test('humidity >= 85 -> cry', () {
      final env = EnvSnapshot.mock(
        temperature: 25.0,
        humidity: 90.0,
        uvIndex: 2.0,
      );
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'cry');
    });

    test('tilt >= 20 -> question', () {
      final env = EnvSnapshot.mock(temperature: 25.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 20.0, 0);
      expect(rule.emoteName, 'question');
    });

    test('tilt >= 12 -> investigate', () {
      final env = EnvSnapshot.mock(temperature: 25.0, uvIndex: 2.0);
      final rule = evaluateEmote(env, 12.0, 0);
      expect(rule.emoteName, 'investigate');
    });

    test('low pressure -> ponder', () {
      final env = EnvSnapshot.mock(
        temperature: 25.0,
        uvIndex: 2.0,
        pressure: 99000,
      );
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'ponder');
    });

    test('normal -> leisure', () {
      final env = EnvSnapshot.mock(
        temperature: 25.0,
        uvIndex: 2.0,
        pressure: 101325,
      );
      final rule = evaluateEmote(env, 0, 0);
      expect(rule.emoteName, 'leisure');
    });

    test('multiple conditions -> highest priority wins', () {
      final env = EnvSnapshot.mock(
        uvIndex: 8.5,
        temperature: 35.0,
        humidity: 90.0,
      );
      final rule = evaluateEmote(env, 25.0, 0);
      expect(rule.emoteName, 'panic');
      expect(rule.priority, 90);
    });
  });
}
