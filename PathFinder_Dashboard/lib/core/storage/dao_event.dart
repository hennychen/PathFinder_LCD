import 'package:drift/drift.dart';
import 'database.dart';
import 'tables.dart';

part 'dao_event.g.dart';

@DriftAccessor(tables: [EventRecords])
class EventDao extends DatabaseAccessor<AppDatabase> with _$EventDaoMixin {
  EventDao(AppDatabase db) : super(db);

  Future<void> insertEvent(
    DateTime timestamp,
    int eventType,
    double pitch,
    double roll,
    double accelMag, {
    int? duration,
  }) {
    return into(eventRecords).insert(
      EventRecordsCompanion.insert(
        timestamp: timestamp,
        eventType: eventType,
        pitch: pitch,
        roll: roll,
        accelMag: accelMag,
        duration: Value(duration),
      ),
    );
  }

  Stream<List<EventRecord>> watchRecentEvents({int limit = 50}) {
    return (select(eventRecords)
          ..orderBy([(t) => OrderingTerm.desc(t.timestamp)])
          ..limit(limit))
        .watch();
  }
}
