import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'database.dart';
import 'dao_env.dart';
import 'dao_event.dart';
import 'dao_emote.dart';

final databaseProvider = Provider<AppDatabase>((ref) {
  final db = AppDatabase();
  ref.onDispose(() => db.close());
  return db;
});

final envDaoProvider = Provider<EnvDao>((ref) {
  return EnvDao(ref.watch(databaseProvider));
});

final eventDaoProvider = Provider<EventDao>((ref) {
  return EventDao(ref.watch(databaseProvider));
});

final emoteDaoProvider = Provider<EmoteDao>((ref) {
  return EmoteDao(ref.watch(databaseProvider));
});
