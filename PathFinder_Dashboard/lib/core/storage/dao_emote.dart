import 'package:drift/drift.dart';
import 'database.dart';
import 'tables.dart';

part 'dao_emote.g.dart';

@DriftAccessor(tables: [EmoteRecords])
class EmoteDao extends DatabaseAccessor<AppDatabase> with _$EmoteDaoMixin {
  EmoteDao(super.db);

  Future<void> insertEmote(DateTime timestamp, int emoteId,
      String friendlyName, int triggerCode, String triggerLabel,
      {double? sensorValue}) {
    return into(emoteRecords).insert(EmoteRecordsCompanion.insert(
      timestamp: timestamp,
      emoteId: emoteId,
      friendlyName: friendlyName,
      triggerCode: triggerCode,
      triggerLabel: triggerLabel,
      sensorValue: Value(sensorValue),
    ));
  }

  Stream<List<EmoteRecord>> watchRecentEmotes({int limit = 50}) {
    return (select(emoteRecords)
          ..orderBy([(t) => OrderingTerm.desc(t.timestamp)])
          ..limit(limit))
        .watch();
  }
}
