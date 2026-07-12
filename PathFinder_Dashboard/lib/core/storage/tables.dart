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
