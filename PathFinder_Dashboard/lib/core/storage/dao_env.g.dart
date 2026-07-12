// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'dao_env.dart';

// ignore_for_file: type=lint
mixin _$EnvDaoMixin on DatabaseAccessor<AppDatabase> {
  $EnvRecordsTable get envRecords => attachedDatabase.envRecords;
  EnvDaoManager get managers => EnvDaoManager(this);
}

class EnvDaoManager {
  final _$EnvDaoMixin _db;
  EnvDaoManager(this._db);
  $$EnvRecordsTableTableManager get envRecords =>
      $$EnvRecordsTableTableManager(_db.attachedDatabase, _db.envRecords);
}
