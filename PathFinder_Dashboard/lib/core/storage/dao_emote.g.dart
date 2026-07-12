// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'dao_emote.dart';

// ignore_for_file: type=lint
mixin _$EmoteDaoMixin on DatabaseAccessor<AppDatabase> {
  $EmoteRecordsTable get emoteRecords => attachedDatabase.emoteRecords;
  EmoteDaoManager get managers => EmoteDaoManager(this);
}

class EmoteDaoManager {
  final _$EmoteDaoMixin _db;
  EmoteDaoManager(this._db);
  $$EmoteRecordsTableTableManager get emoteRecords =>
      $$EmoteRecordsTableTableManager(_db.attachedDatabase, _db.emoteRecords);
}
