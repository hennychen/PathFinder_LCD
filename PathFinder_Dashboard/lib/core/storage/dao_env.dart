import 'package:drift/drift.dart';
import 'database.dart';
import 'tables.dart';

part 'dao_env.g.dart';

@DriftAccessor(tables: [EnvRecords])
class EnvDao extends DatabaseAccessor<AppDatabase> with _$EnvDaoMixin {
  EnvDao(super.db);

  Future<void> insertEnv(
    DateTime timestamp,
    double temperature,
    double humidity,
    int pressure,
    double altitude,
    double uvIndex,
  ) {
    return into(envRecords).insert(
      EnvRecordsCompanion.insert(
        timestamp: timestamp,
        temperature: temperature,
        humidity: humidity,
        pressure: pressure,
        altitude: altitude,
        uvIndex: uvIndex,
      ),
    );
  }

  Stream<List<EnvRecord>> watchRecent({int limit = 60}) {
    return (select(envRecords)
          ..orderBy([(t) => OrderingTerm.desc(t.timestamp)])
          ..limit(limit))
        .watch();
  }

  Future<List<EnvRecord>> queryRange(DateTime start, DateTime end) {
    return (select(envRecords)
          ..where((t) => t.timestamp.isBetweenValues(start, end))
          ..orderBy([(t) => OrderingTerm.asc(t.timestamp)]))
        .get();
  }
}
