// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'dao_event.dart';

// ignore_for_file: type=lint
mixin _$EventDaoMixin on DatabaseAccessor<AppDatabase> {
  $EventRecordsTable get eventRecords => attachedDatabase.eventRecords;
  EventDaoManager get managers => EventDaoManager(this);
}

class EventDaoManager {
  final _$EventDaoMixin _db;
  EventDaoManager(this._db);
  $$EventRecordsTableTableManager get eventRecords =>
      $$EventRecordsTableTableManager(_db.attachedDatabase, _db.eventRecords);
}
