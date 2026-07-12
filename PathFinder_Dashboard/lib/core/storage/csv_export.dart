import 'dart:io';
import 'package:drift/drift.dart';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
import 'database.dart';

Future<String> exportEnvToCsv(AppDatabase db) async {
  final records = await (db.select(
    db.envRecords,
  )..orderBy([(t) => OrderingTerm.asc(t.timestamp)])).get();

  final buffer = StringBuffer();
  buffer.writeln('timestamp,temperature,humidity,pressure,altitude,uvIndex');
  for (final r in records) {
    buffer.writeln(
      '${r.timestamp.toIso8601String()},${r.temperature},${r.humidity},${r.pressure},${r.altitude},${r.uvIndex}',
    );
  }

  final dir = await getApplicationDocumentsDirectory();
  final file = File(
    p.join(
      dir.path,
      'pathfinder_env_${DateTime.now().millisecondsSinceEpoch}.csv',
    ),
  );
  await file.writeAsString(buffer.toString());
  return file.path;
}

Future<String> exportEventsToCsv(AppDatabase db) async {
  final records = await (db.select(
    db.eventRecords,
  )..orderBy([(t) => OrderingTerm.asc(t.timestamp)])).get();

  final buffer = StringBuffer();
  buffer.writeln('timestamp,eventType,pitch,roll,accelMag,duration');
  for (final r in records) {
    buffer.writeln(
      '${r.timestamp.toIso8601String()},${r.eventType},${r.pitch},${r.roll},${r.accelMag},${r.duration ?? ''}',
    );
  }

  final dir = await getApplicationDocumentsDirectory();
  final file = File(
    p.join(
      dir.path,
      'pathfinder_events_${DateTime.now().millisecondsSinceEpoch}.csv',
    ),
  );
  await file.writeAsString(buffer.toString());
  return file.path;
}

Future<String> exportEmotesToCsv(AppDatabase db) async {
  final records = await (db.select(
    db.emoteRecords,
  )..orderBy([(t) => OrderingTerm.asc(t.timestamp)])).get();

  final buffer = StringBuffer();
  buffer.writeln(
    'timestamp,emoteId,friendlyName,triggerCode,triggerLabel,sensorValue',
  );
  for (final r in records) {
    buffer.writeln(
      '${r.timestamp.toIso8601String()},${r.emoteId},${r.friendlyName},${r.triggerCode},${r.triggerLabel},${r.sensorValue ?? ''}',
    );
  }

  final dir = await getApplicationDocumentsDirectory();
  final file = File(
    p.join(
      dir.path,
      'pathfinder_emotes_${DateTime.now().millisecondsSinceEpoch}.csv',
    ),
  );
  await file.writeAsString(buffer.toString());
  return file.path;
}
