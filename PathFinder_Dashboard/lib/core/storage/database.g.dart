// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'database.dart';

// ignore_for_file: type=lint
class $EnvRecordsTable extends EnvRecords
    with TableInfo<$EnvRecordsTable, EnvRecord> {
  @override
  final GeneratedDatabase attachedDatabase;
  final String? _alias;
  $EnvRecordsTable(this.attachedDatabase, [this._alias]);
  static const VerificationMeta _idMeta = const VerificationMeta('id');
  @override
  late final GeneratedColumn<int> id = GeneratedColumn<int>(
    'id',
    aliasedName,
    false,
    hasAutoIncrement: true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'PRIMARY KEY AUTOINCREMENT',
    ),
  );
  static const VerificationMeta _timestampMeta = const VerificationMeta(
    'timestamp',
  );
  @override
  late final GeneratedColumn<DateTime> timestamp = GeneratedColumn<DateTime>(
    'timestamp',
    aliasedName,
    false,
    type: DriftSqlType.dateTime,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _temperatureMeta = const VerificationMeta(
    'temperature',
  );
  @override
  late final GeneratedColumn<double> temperature = GeneratedColumn<double>(
    'temperature',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _humidityMeta = const VerificationMeta(
    'humidity',
  );
  @override
  late final GeneratedColumn<double> humidity = GeneratedColumn<double>(
    'humidity',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _pressureMeta = const VerificationMeta(
    'pressure',
  );
  @override
  late final GeneratedColumn<int> pressure = GeneratedColumn<int>(
    'pressure',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _altitudeMeta = const VerificationMeta(
    'altitude',
  );
  @override
  late final GeneratedColumn<double> altitude = GeneratedColumn<double>(
    'altitude',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _uvIndexMeta = const VerificationMeta(
    'uvIndex',
  );
  @override
  late final GeneratedColumn<double> uvIndex = GeneratedColumn<double>(
    'uv_index',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  @override
  List<GeneratedColumn> get $columns => [
    id,
    timestamp,
    temperature,
    humidity,
    pressure,
    altitude,
    uvIndex,
  ];
  @override
  String get aliasedName => _alias ?? actualTableName;
  @override
  String get actualTableName => $name;
  static const String $name = 'env_records';
  @override
  VerificationContext validateIntegrity(
    Insertable<EnvRecord> instance, {
    bool isInserting = false,
  }) {
    final context = VerificationContext();
    final data = instance.toColumns(true);
    if (data.containsKey('id')) {
      context.handle(_idMeta, id.isAcceptableOrUnknown(data['id']!, _idMeta));
    }
    if (data.containsKey('timestamp')) {
      context.handle(
        _timestampMeta,
        timestamp.isAcceptableOrUnknown(data['timestamp']!, _timestampMeta),
      );
    } else if (isInserting) {
      context.missing(_timestampMeta);
    }
    if (data.containsKey('temperature')) {
      context.handle(
        _temperatureMeta,
        temperature.isAcceptableOrUnknown(
          data['temperature']!,
          _temperatureMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_temperatureMeta);
    }
    if (data.containsKey('humidity')) {
      context.handle(
        _humidityMeta,
        humidity.isAcceptableOrUnknown(data['humidity']!, _humidityMeta),
      );
    } else if (isInserting) {
      context.missing(_humidityMeta);
    }
    if (data.containsKey('pressure')) {
      context.handle(
        _pressureMeta,
        pressure.isAcceptableOrUnknown(data['pressure']!, _pressureMeta),
      );
    } else if (isInserting) {
      context.missing(_pressureMeta);
    }
    if (data.containsKey('altitude')) {
      context.handle(
        _altitudeMeta,
        altitude.isAcceptableOrUnknown(data['altitude']!, _altitudeMeta),
      );
    } else if (isInserting) {
      context.missing(_altitudeMeta);
    }
    if (data.containsKey('uv_index')) {
      context.handle(
        _uvIndexMeta,
        uvIndex.isAcceptableOrUnknown(data['uv_index']!, _uvIndexMeta),
      );
    } else if (isInserting) {
      context.missing(_uvIndexMeta);
    }
    return context;
  }

  @override
  Set<GeneratedColumn> get $primaryKey => {id};
  @override
  EnvRecord map(Map<String, dynamic> data, {String? tablePrefix}) {
    final effectivePrefix = tablePrefix != null ? '$tablePrefix.' : '';
    return EnvRecord(
      id: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}id'],
      )!,
      timestamp: attachedDatabase.typeMapping.read(
        DriftSqlType.dateTime,
        data['${effectivePrefix}timestamp'],
      )!,
      temperature: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}temperature'],
      )!,
      humidity: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}humidity'],
      )!,
      pressure: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}pressure'],
      )!,
      altitude: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}altitude'],
      )!,
      uvIndex: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}uv_index'],
      )!,
    );
  }

  @override
  $EnvRecordsTable createAlias(String alias) {
    return $EnvRecordsTable(attachedDatabase, alias);
  }
}

class EnvRecord extends DataClass implements Insertable<EnvRecord> {
  final int id;
  final DateTime timestamp;
  final double temperature;
  final double humidity;
  final int pressure;
  final double altitude;
  final double uvIndex;
  const EnvRecord({
    required this.id,
    required this.timestamp,
    required this.temperature,
    required this.humidity,
    required this.pressure,
    required this.altitude,
    required this.uvIndex,
  });
  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    map['id'] = Variable<int>(id);
    map['timestamp'] = Variable<DateTime>(timestamp);
    map['temperature'] = Variable<double>(temperature);
    map['humidity'] = Variable<double>(humidity);
    map['pressure'] = Variable<int>(pressure);
    map['altitude'] = Variable<double>(altitude);
    map['uv_index'] = Variable<double>(uvIndex);
    return map;
  }

  EnvRecordsCompanion toCompanion(bool nullToAbsent) {
    return EnvRecordsCompanion(
      id: Value(id),
      timestamp: Value(timestamp),
      temperature: Value(temperature),
      humidity: Value(humidity),
      pressure: Value(pressure),
      altitude: Value(altitude),
      uvIndex: Value(uvIndex),
    );
  }

  factory EnvRecord.fromJson(
    Map<String, dynamic> json, {
    ValueSerializer? serializer,
  }) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return EnvRecord(
      id: serializer.fromJson<int>(json['id']),
      timestamp: serializer.fromJson<DateTime>(json['timestamp']),
      temperature: serializer.fromJson<double>(json['temperature']),
      humidity: serializer.fromJson<double>(json['humidity']),
      pressure: serializer.fromJson<int>(json['pressure']),
      altitude: serializer.fromJson<double>(json['altitude']),
      uvIndex: serializer.fromJson<double>(json['uvIndex']),
    );
  }
  @override
  Map<String, dynamic> toJson({ValueSerializer? serializer}) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return <String, dynamic>{
      'id': serializer.toJson<int>(id),
      'timestamp': serializer.toJson<DateTime>(timestamp),
      'temperature': serializer.toJson<double>(temperature),
      'humidity': serializer.toJson<double>(humidity),
      'pressure': serializer.toJson<int>(pressure),
      'altitude': serializer.toJson<double>(altitude),
      'uvIndex': serializer.toJson<double>(uvIndex),
    };
  }

  EnvRecord copyWith({
    int? id,
    DateTime? timestamp,
    double? temperature,
    double? humidity,
    int? pressure,
    double? altitude,
    double? uvIndex,
  }) => EnvRecord(
    id: id ?? this.id,
    timestamp: timestamp ?? this.timestamp,
    temperature: temperature ?? this.temperature,
    humidity: humidity ?? this.humidity,
    pressure: pressure ?? this.pressure,
    altitude: altitude ?? this.altitude,
    uvIndex: uvIndex ?? this.uvIndex,
  );
  EnvRecord copyWithCompanion(EnvRecordsCompanion data) {
    return EnvRecord(
      id: data.id.present ? data.id.value : this.id,
      timestamp: data.timestamp.present ? data.timestamp.value : this.timestamp,
      temperature: data.temperature.present
          ? data.temperature.value
          : this.temperature,
      humidity: data.humidity.present ? data.humidity.value : this.humidity,
      pressure: data.pressure.present ? data.pressure.value : this.pressure,
      altitude: data.altitude.present ? data.altitude.value : this.altitude,
      uvIndex: data.uvIndex.present ? data.uvIndex.value : this.uvIndex,
    );
  }

  @override
  String toString() {
    return (StringBuffer('EnvRecord(')
          ..write('id: $id, ')
          ..write('timestamp: $timestamp, ')
          ..write('temperature: $temperature, ')
          ..write('humidity: $humidity, ')
          ..write('pressure: $pressure, ')
          ..write('altitude: $altitude, ')
          ..write('uvIndex: $uvIndex')
          ..write(')'))
        .toString();
  }

  @override
  int get hashCode => Object.hash(
    id,
    timestamp,
    temperature,
    humidity,
    pressure,
    altitude,
    uvIndex,
  );
  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      (other is EnvRecord &&
          other.id == this.id &&
          other.timestamp == this.timestamp &&
          other.temperature == this.temperature &&
          other.humidity == this.humidity &&
          other.pressure == this.pressure &&
          other.altitude == this.altitude &&
          other.uvIndex == this.uvIndex);
}

class EnvRecordsCompanion extends UpdateCompanion<EnvRecord> {
  final Value<int> id;
  final Value<DateTime> timestamp;
  final Value<double> temperature;
  final Value<double> humidity;
  final Value<int> pressure;
  final Value<double> altitude;
  final Value<double> uvIndex;
  const EnvRecordsCompanion({
    this.id = const Value.absent(),
    this.timestamp = const Value.absent(),
    this.temperature = const Value.absent(),
    this.humidity = const Value.absent(),
    this.pressure = const Value.absent(),
    this.altitude = const Value.absent(),
    this.uvIndex = const Value.absent(),
  });
  EnvRecordsCompanion.insert({
    this.id = const Value.absent(),
    required DateTime timestamp,
    required double temperature,
    required double humidity,
    required int pressure,
    required double altitude,
    required double uvIndex,
  }) : timestamp = Value(timestamp),
       temperature = Value(temperature),
       humidity = Value(humidity),
       pressure = Value(pressure),
       altitude = Value(altitude),
       uvIndex = Value(uvIndex);
  static Insertable<EnvRecord> custom({
    Expression<int>? id,
    Expression<DateTime>? timestamp,
    Expression<double>? temperature,
    Expression<double>? humidity,
    Expression<int>? pressure,
    Expression<double>? altitude,
    Expression<double>? uvIndex,
  }) {
    return RawValuesInsertable({
      if (id != null) 'id': id,
      if (timestamp != null) 'timestamp': timestamp,
      if (temperature != null) 'temperature': temperature,
      if (humidity != null) 'humidity': humidity,
      if (pressure != null) 'pressure': pressure,
      if (altitude != null) 'altitude': altitude,
      if (uvIndex != null) 'uv_index': uvIndex,
    });
  }

  EnvRecordsCompanion copyWith({
    Value<int>? id,
    Value<DateTime>? timestamp,
    Value<double>? temperature,
    Value<double>? humidity,
    Value<int>? pressure,
    Value<double>? altitude,
    Value<double>? uvIndex,
  }) {
    return EnvRecordsCompanion(
      id: id ?? this.id,
      timestamp: timestamp ?? this.timestamp,
      temperature: temperature ?? this.temperature,
      humidity: humidity ?? this.humidity,
      pressure: pressure ?? this.pressure,
      altitude: altitude ?? this.altitude,
      uvIndex: uvIndex ?? this.uvIndex,
    );
  }

  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    if (id.present) {
      map['id'] = Variable<int>(id.value);
    }
    if (timestamp.present) {
      map['timestamp'] = Variable<DateTime>(timestamp.value);
    }
    if (temperature.present) {
      map['temperature'] = Variable<double>(temperature.value);
    }
    if (humidity.present) {
      map['humidity'] = Variable<double>(humidity.value);
    }
    if (pressure.present) {
      map['pressure'] = Variable<int>(pressure.value);
    }
    if (altitude.present) {
      map['altitude'] = Variable<double>(altitude.value);
    }
    if (uvIndex.present) {
      map['uv_index'] = Variable<double>(uvIndex.value);
    }
    return map;
  }

  @override
  String toString() {
    return (StringBuffer('EnvRecordsCompanion(')
          ..write('id: $id, ')
          ..write('timestamp: $timestamp, ')
          ..write('temperature: $temperature, ')
          ..write('humidity: $humidity, ')
          ..write('pressure: $pressure, ')
          ..write('altitude: $altitude, ')
          ..write('uvIndex: $uvIndex')
          ..write(')'))
        .toString();
  }
}

class $EventRecordsTable extends EventRecords
    with TableInfo<$EventRecordsTable, EventRecord> {
  @override
  final GeneratedDatabase attachedDatabase;
  final String? _alias;
  $EventRecordsTable(this.attachedDatabase, [this._alias]);
  static const VerificationMeta _idMeta = const VerificationMeta('id');
  @override
  late final GeneratedColumn<int> id = GeneratedColumn<int>(
    'id',
    aliasedName,
    false,
    hasAutoIncrement: true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'PRIMARY KEY AUTOINCREMENT',
    ),
  );
  static const VerificationMeta _timestampMeta = const VerificationMeta(
    'timestamp',
  );
  @override
  late final GeneratedColumn<DateTime> timestamp = GeneratedColumn<DateTime>(
    'timestamp',
    aliasedName,
    false,
    type: DriftSqlType.dateTime,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _eventTypeMeta = const VerificationMeta(
    'eventType',
  );
  @override
  late final GeneratedColumn<int> eventType = GeneratedColumn<int>(
    'event_type',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _pitchMeta = const VerificationMeta('pitch');
  @override
  late final GeneratedColumn<double> pitch = GeneratedColumn<double>(
    'pitch',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _rollMeta = const VerificationMeta('roll');
  @override
  late final GeneratedColumn<double> roll = GeneratedColumn<double>(
    'roll',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _accelMagMeta = const VerificationMeta(
    'accelMag',
  );
  @override
  late final GeneratedColumn<double> accelMag = GeneratedColumn<double>(
    'accel_mag',
    aliasedName,
    false,
    type: DriftSqlType.double,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _durationMeta = const VerificationMeta(
    'duration',
  );
  @override
  late final GeneratedColumn<int> duration = GeneratedColumn<int>(
    'duration',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  @override
  List<GeneratedColumn> get $columns => [
    id,
    timestamp,
    eventType,
    pitch,
    roll,
    accelMag,
    duration,
  ];
  @override
  String get aliasedName => _alias ?? actualTableName;
  @override
  String get actualTableName => $name;
  static const String $name = 'event_records';
  @override
  VerificationContext validateIntegrity(
    Insertable<EventRecord> instance, {
    bool isInserting = false,
  }) {
    final context = VerificationContext();
    final data = instance.toColumns(true);
    if (data.containsKey('id')) {
      context.handle(_idMeta, id.isAcceptableOrUnknown(data['id']!, _idMeta));
    }
    if (data.containsKey('timestamp')) {
      context.handle(
        _timestampMeta,
        timestamp.isAcceptableOrUnknown(data['timestamp']!, _timestampMeta),
      );
    } else if (isInserting) {
      context.missing(_timestampMeta);
    }
    if (data.containsKey('event_type')) {
      context.handle(
        _eventTypeMeta,
        eventType.isAcceptableOrUnknown(data['event_type']!, _eventTypeMeta),
      );
    } else if (isInserting) {
      context.missing(_eventTypeMeta);
    }
    if (data.containsKey('pitch')) {
      context.handle(
        _pitchMeta,
        pitch.isAcceptableOrUnknown(data['pitch']!, _pitchMeta),
      );
    } else if (isInserting) {
      context.missing(_pitchMeta);
    }
    if (data.containsKey('roll')) {
      context.handle(
        _rollMeta,
        roll.isAcceptableOrUnknown(data['roll']!, _rollMeta),
      );
    } else if (isInserting) {
      context.missing(_rollMeta);
    }
    if (data.containsKey('accel_mag')) {
      context.handle(
        _accelMagMeta,
        accelMag.isAcceptableOrUnknown(data['accel_mag']!, _accelMagMeta),
      );
    } else if (isInserting) {
      context.missing(_accelMagMeta);
    }
    if (data.containsKey('duration')) {
      context.handle(
        _durationMeta,
        duration.isAcceptableOrUnknown(data['duration']!, _durationMeta),
      );
    }
    return context;
  }

  @override
  Set<GeneratedColumn> get $primaryKey => {id};
  @override
  EventRecord map(Map<String, dynamic> data, {String? tablePrefix}) {
    final effectivePrefix = tablePrefix != null ? '$tablePrefix.' : '';
    return EventRecord(
      id: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}id'],
      )!,
      timestamp: attachedDatabase.typeMapping.read(
        DriftSqlType.dateTime,
        data['${effectivePrefix}timestamp'],
      )!,
      eventType: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}event_type'],
      )!,
      pitch: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}pitch'],
      )!,
      roll: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}roll'],
      )!,
      accelMag: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}accel_mag'],
      )!,
      duration: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}duration'],
      ),
    );
  }

  @override
  $EventRecordsTable createAlias(String alias) {
    return $EventRecordsTable(attachedDatabase, alias);
  }
}

class EventRecord extends DataClass implements Insertable<EventRecord> {
  final int id;
  final DateTime timestamp;
  final int eventType;
  final double pitch;
  final double roll;
  final double accelMag;
  final int? duration;
  const EventRecord({
    required this.id,
    required this.timestamp,
    required this.eventType,
    required this.pitch,
    required this.roll,
    required this.accelMag,
    this.duration,
  });
  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    map['id'] = Variable<int>(id);
    map['timestamp'] = Variable<DateTime>(timestamp);
    map['event_type'] = Variable<int>(eventType);
    map['pitch'] = Variable<double>(pitch);
    map['roll'] = Variable<double>(roll);
    map['accel_mag'] = Variable<double>(accelMag);
    if (!nullToAbsent || duration != null) {
      map['duration'] = Variable<int>(duration);
    }
    return map;
  }

  EventRecordsCompanion toCompanion(bool nullToAbsent) {
    return EventRecordsCompanion(
      id: Value(id),
      timestamp: Value(timestamp),
      eventType: Value(eventType),
      pitch: Value(pitch),
      roll: Value(roll),
      accelMag: Value(accelMag),
      duration: duration == null && nullToAbsent
          ? const Value.absent()
          : Value(duration),
    );
  }

  factory EventRecord.fromJson(
    Map<String, dynamic> json, {
    ValueSerializer? serializer,
  }) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return EventRecord(
      id: serializer.fromJson<int>(json['id']),
      timestamp: serializer.fromJson<DateTime>(json['timestamp']),
      eventType: serializer.fromJson<int>(json['eventType']),
      pitch: serializer.fromJson<double>(json['pitch']),
      roll: serializer.fromJson<double>(json['roll']),
      accelMag: serializer.fromJson<double>(json['accelMag']),
      duration: serializer.fromJson<int?>(json['duration']),
    );
  }
  @override
  Map<String, dynamic> toJson({ValueSerializer? serializer}) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return <String, dynamic>{
      'id': serializer.toJson<int>(id),
      'timestamp': serializer.toJson<DateTime>(timestamp),
      'eventType': serializer.toJson<int>(eventType),
      'pitch': serializer.toJson<double>(pitch),
      'roll': serializer.toJson<double>(roll),
      'accelMag': serializer.toJson<double>(accelMag),
      'duration': serializer.toJson<int?>(duration),
    };
  }

  EventRecord copyWith({
    int? id,
    DateTime? timestamp,
    int? eventType,
    double? pitch,
    double? roll,
    double? accelMag,
    Value<int?> duration = const Value.absent(),
  }) => EventRecord(
    id: id ?? this.id,
    timestamp: timestamp ?? this.timestamp,
    eventType: eventType ?? this.eventType,
    pitch: pitch ?? this.pitch,
    roll: roll ?? this.roll,
    accelMag: accelMag ?? this.accelMag,
    duration: duration.present ? duration.value : this.duration,
  );
  EventRecord copyWithCompanion(EventRecordsCompanion data) {
    return EventRecord(
      id: data.id.present ? data.id.value : this.id,
      timestamp: data.timestamp.present ? data.timestamp.value : this.timestamp,
      eventType: data.eventType.present ? data.eventType.value : this.eventType,
      pitch: data.pitch.present ? data.pitch.value : this.pitch,
      roll: data.roll.present ? data.roll.value : this.roll,
      accelMag: data.accelMag.present ? data.accelMag.value : this.accelMag,
      duration: data.duration.present ? data.duration.value : this.duration,
    );
  }

  @override
  String toString() {
    return (StringBuffer('EventRecord(')
          ..write('id: $id, ')
          ..write('timestamp: $timestamp, ')
          ..write('eventType: $eventType, ')
          ..write('pitch: $pitch, ')
          ..write('roll: $roll, ')
          ..write('accelMag: $accelMag, ')
          ..write('duration: $duration')
          ..write(')'))
        .toString();
  }

  @override
  int get hashCode =>
      Object.hash(id, timestamp, eventType, pitch, roll, accelMag, duration);
  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      (other is EventRecord &&
          other.id == this.id &&
          other.timestamp == this.timestamp &&
          other.eventType == this.eventType &&
          other.pitch == this.pitch &&
          other.roll == this.roll &&
          other.accelMag == this.accelMag &&
          other.duration == this.duration);
}

class EventRecordsCompanion extends UpdateCompanion<EventRecord> {
  final Value<int> id;
  final Value<DateTime> timestamp;
  final Value<int> eventType;
  final Value<double> pitch;
  final Value<double> roll;
  final Value<double> accelMag;
  final Value<int?> duration;
  const EventRecordsCompanion({
    this.id = const Value.absent(),
    this.timestamp = const Value.absent(),
    this.eventType = const Value.absent(),
    this.pitch = const Value.absent(),
    this.roll = const Value.absent(),
    this.accelMag = const Value.absent(),
    this.duration = const Value.absent(),
  });
  EventRecordsCompanion.insert({
    this.id = const Value.absent(),
    required DateTime timestamp,
    required int eventType,
    required double pitch,
    required double roll,
    required double accelMag,
    this.duration = const Value.absent(),
  }) : timestamp = Value(timestamp),
       eventType = Value(eventType),
       pitch = Value(pitch),
       roll = Value(roll),
       accelMag = Value(accelMag);
  static Insertable<EventRecord> custom({
    Expression<int>? id,
    Expression<DateTime>? timestamp,
    Expression<int>? eventType,
    Expression<double>? pitch,
    Expression<double>? roll,
    Expression<double>? accelMag,
    Expression<int>? duration,
  }) {
    return RawValuesInsertable({
      if (id != null) 'id': id,
      if (timestamp != null) 'timestamp': timestamp,
      if (eventType != null) 'event_type': eventType,
      if (pitch != null) 'pitch': pitch,
      if (roll != null) 'roll': roll,
      if (accelMag != null) 'accel_mag': accelMag,
      if (duration != null) 'duration': duration,
    });
  }

  EventRecordsCompanion copyWith({
    Value<int>? id,
    Value<DateTime>? timestamp,
    Value<int>? eventType,
    Value<double>? pitch,
    Value<double>? roll,
    Value<double>? accelMag,
    Value<int?>? duration,
  }) {
    return EventRecordsCompanion(
      id: id ?? this.id,
      timestamp: timestamp ?? this.timestamp,
      eventType: eventType ?? this.eventType,
      pitch: pitch ?? this.pitch,
      roll: roll ?? this.roll,
      accelMag: accelMag ?? this.accelMag,
      duration: duration ?? this.duration,
    );
  }

  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    if (id.present) {
      map['id'] = Variable<int>(id.value);
    }
    if (timestamp.present) {
      map['timestamp'] = Variable<DateTime>(timestamp.value);
    }
    if (eventType.present) {
      map['event_type'] = Variable<int>(eventType.value);
    }
    if (pitch.present) {
      map['pitch'] = Variable<double>(pitch.value);
    }
    if (roll.present) {
      map['roll'] = Variable<double>(roll.value);
    }
    if (accelMag.present) {
      map['accel_mag'] = Variable<double>(accelMag.value);
    }
    if (duration.present) {
      map['duration'] = Variable<int>(duration.value);
    }
    return map;
  }

  @override
  String toString() {
    return (StringBuffer('EventRecordsCompanion(')
          ..write('id: $id, ')
          ..write('timestamp: $timestamp, ')
          ..write('eventType: $eventType, ')
          ..write('pitch: $pitch, ')
          ..write('roll: $roll, ')
          ..write('accelMag: $accelMag, ')
          ..write('duration: $duration')
          ..write(')'))
        .toString();
  }
}

class $EmoteRecordsTable extends EmoteRecords
    with TableInfo<$EmoteRecordsTable, EmoteRecord> {
  @override
  final GeneratedDatabase attachedDatabase;
  final String? _alias;
  $EmoteRecordsTable(this.attachedDatabase, [this._alias]);
  static const VerificationMeta _idMeta = const VerificationMeta('id');
  @override
  late final GeneratedColumn<int> id = GeneratedColumn<int>(
    'id',
    aliasedName,
    false,
    hasAutoIncrement: true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'PRIMARY KEY AUTOINCREMENT',
    ),
  );
  static const VerificationMeta _timestampMeta = const VerificationMeta(
    'timestamp',
  );
  @override
  late final GeneratedColumn<DateTime> timestamp = GeneratedColumn<DateTime>(
    'timestamp',
    aliasedName,
    false,
    type: DriftSqlType.dateTime,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _emoteIdMeta = const VerificationMeta(
    'emoteId',
  );
  @override
  late final GeneratedColumn<int> emoteId = GeneratedColumn<int>(
    'emote_id',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _friendlyNameMeta = const VerificationMeta(
    'friendlyName',
  );
  @override
  late final GeneratedColumn<String> friendlyName = GeneratedColumn<String>(
    'friendly_name',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _triggerCodeMeta = const VerificationMeta(
    'triggerCode',
  );
  @override
  late final GeneratedColumn<int> triggerCode = GeneratedColumn<int>(
    'trigger_code',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _triggerLabelMeta = const VerificationMeta(
    'triggerLabel',
  );
  @override
  late final GeneratedColumn<String> triggerLabel = GeneratedColumn<String>(
    'trigger_label',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _sensorValueMeta = const VerificationMeta(
    'sensorValue',
  );
  @override
  late final GeneratedColumn<double> sensorValue = GeneratedColumn<double>(
    'sensor_value',
    aliasedName,
    true,
    type: DriftSqlType.double,
    requiredDuringInsert: false,
  );
  @override
  List<GeneratedColumn> get $columns => [
    id,
    timestamp,
    emoteId,
    friendlyName,
    triggerCode,
    triggerLabel,
    sensorValue,
  ];
  @override
  String get aliasedName => _alias ?? actualTableName;
  @override
  String get actualTableName => $name;
  static const String $name = 'emote_records';
  @override
  VerificationContext validateIntegrity(
    Insertable<EmoteRecord> instance, {
    bool isInserting = false,
  }) {
    final context = VerificationContext();
    final data = instance.toColumns(true);
    if (data.containsKey('id')) {
      context.handle(_idMeta, id.isAcceptableOrUnknown(data['id']!, _idMeta));
    }
    if (data.containsKey('timestamp')) {
      context.handle(
        _timestampMeta,
        timestamp.isAcceptableOrUnknown(data['timestamp']!, _timestampMeta),
      );
    } else if (isInserting) {
      context.missing(_timestampMeta);
    }
    if (data.containsKey('emote_id')) {
      context.handle(
        _emoteIdMeta,
        emoteId.isAcceptableOrUnknown(data['emote_id']!, _emoteIdMeta),
      );
    } else if (isInserting) {
      context.missing(_emoteIdMeta);
    }
    if (data.containsKey('friendly_name')) {
      context.handle(
        _friendlyNameMeta,
        friendlyName.isAcceptableOrUnknown(
          data['friendly_name']!,
          _friendlyNameMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_friendlyNameMeta);
    }
    if (data.containsKey('trigger_code')) {
      context.handle(
        _triggerCodeMeta,
        triggerCode.isAcceptableOrUnknown(
          data['trigger_code']!,
          _triggerCodeMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_triggerCodeMeta);
    }
    if (data.containsKey('trigger_label')) {
      context.handle(
        _triggerLabelMeta,
        triggerLabel.isAcceptableOrUnknown(
          data['trigger_label']!,
          _triggerLabelMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_triggerLabelMeta);
    }
    if (data.containsKey('sensor_value')) {
      context.handle(
        _sensorValueMeta,
        sensorValue.isAcceptableOrUnknown(
          data['sensor_value']!,
          _sensorValueMeta,
        ),
      );
    }
    return context;
  }

  @override
  Set<GeneratedColumn> get $primaryKey => {id};
  @override
  EmoteRecord map(Map<String, dynamic> data, {String? tablePrefix}) {
    final effectivePrefix = tablePrefix != null ? '$tablePrefix.' : '';
    return EmoteRecord(
      id: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}id'],
      )!,
      timestamp: attachedDatabase.typeMapping.read(
        DriftSqlType.dateTime,
        data['${effectivePrefix}timestamp'],
      )!,
      emoteId: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}emote_id'],
      )!,
      friendlyName: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}friendly_name'],
      )!,
      triggerCode: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}trigger_code'],
      )!,
      triggerLabel: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}trigger_label'],
      )!,
      sensorValue: attachedDatabase.typeMapping.read(
        DriftSqlType.double,
        data['${effectivePrefix}sensor_value'],
      ),
    );
  }

  @override
  $EmoteRecordsTable createAlias(String alias) {
    return $EmoteRecordsTable(attachedDatabase, alias);
  }
}

class EmoteRecord extends DataClass implements Insertable<EmoteRecord> {
  final int id;
  final DateTime timestamp;
  final int emoteId;
  final String friendlyName;
  final int triggerCode;
  final String triggerLabel;
  final double? sensorValue;
  const EmoteRecord({
    required this.id,
    required this.timestamp,
    required this.emoteId,
    required this.friendlyName,
    required this.triggerCode,
    required this.triggerLabel,
    this.sensorValue,
  });
  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    map['id'] = Variable<int>(id);
    map['timestamp'] = Variable<DateTime>(timestamp);
    map['emote_id'] = Variable<int>(emoteId);
    map['friendly_name'] = Variable<String>(friendlyName);
    map['trigger_code'] = Variable<int>(triggerCode);
    map['trigger_label'] = Variable<String>(triggerLabel);
    if (!nullToAbsent || sensorValue != null) {
      map['sensor_value'] = Variable<double>(sensorValue);
    }
    return map;
  }

  EmoteRecordsCompanion toCompanion(bool nullToAbsent) {
    return EmoteRecordsCompanion(
      id: Value(id),
      timestamp: Value(timestamp),
      emoteId: Value(emoteId),
      friendlyName: Value(friendlyName),
      triggerCode: Value(triggerCode),
      triggerLabel: Value(triggerLabel),
      sensorValue: sensorValue == null && nullToAbsent
          ? const Value.absent()
          : Value(sensorValue),
    );
  }

  factory EmoteRecord.fromJson(
    Map<String, dynamic> json, {
    ValueSerializer? serializer,
  }) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return EmoteRecord(
      id: serializer.fromJson<int>(json['id']),
      timestamp: serializer.fromJson<DateTime>(json['timestamp']),
      emoteId: serializer.fromJson<int>(json['emoteId']),
      friendlyName: serializer.fromJson<String>(json['friendlyName']),
      triggerCode: serializer.fromJson<int>(json['triggerCode']),
      triggerLabel: serializer.fromJson<String>(json['triggerLabel']),
      sensorValue: serializer.fromJson<double?>(json['sensorValue']),
    );
  }
  @override
  Map<String, dynamic> toJson({ValueSerializer? serializer}) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return <String, dynamic>{
      'id': serializer.toJson<int>(id),
      'timestamp': serializer.toJson<DateTime>(timestamp),
      'emoteId': serializer.toJson<int>(emoteId),
      'friendlyName': serializer.toJson<String>(friendlyName),
      'triggerCode': serializer.toJson<int>(triggerCode),
      'triggerLabel': serializer.toJson<String>(triggerLabel),
      'sensorValue': serializer.toJson<double?>(sensorValue),
    };
  }

  EmoteRecord copyWith({
    int? id,
    DateTime? timestamp,
    int? emoteId,
    String? friendlyName,
    int? triggerCode,
    String? triggerLabel,
    Value<double?> sensorValue = const Value.absent(),
  }) => EmoteRecord(
    id: id ?? this.id,
    timestamp: timestamp ?? this.timestamp,
    emoteId: emoteId ?? this.emoteId,
    friendlyName: friendlyName ?? this.friendlyName,
    triggerCode: triggerCode ?? this.triggerCode,
    triggerLabel: triggerLabel ?? this.triggerLabel,
    sensorValue: sensorValue.present ? sensorValue.value : this.sensorValue,
  );
  EmoteRecord copyWithCompanion(EmoteRecordsCompanion data) {
    return EmoteRecord(
      id: data.id.present ? data.id.value : this.id,
      timestamp: data.timestamp.present ? data.timestamp.value : this.timestamp,
      emoteId: data.emoteId.present ? data.emoteId.value : this.emoteId,
      friendlyName: data.friendlyName.present
          ? data.friendlyName.value
          : this.friendlyName,
      triggerCode: data.triggerCode.present
          ? data.triggerCode.value
          : this.triggerCode,
      triggerLabel: data.triggerLabel.present
          ? data.triggerLabel.value
          : this.triggerLabel,
      sensorValue: data.sensorValue.present
          ? data.sensorValue.value
          : this.sensorValue,
    );
  }

  @override
  String toString() {
    return (StringBuffer('EmoteRecord(')
          ..write('id: $id, ')
          ..write('timestamp: $timestamp, ')
          ..write('emoteId: $emoteId, ')
          ..write('friendlyName: $friendlyName, ')
          ..write('triggerCode: $triggerCode, ')
          ..write('triggerLabel: $triggerLabel, ')
          ..write('sensorValue: $sensorValue')
          ..write(')'))
        .toString();
  }

  @override
  int get hashCode => Object.hash(
    id,
    timestamp,
    emoteId,
    friendlyName,
    triggerCode,
    triggerLabel,
    sensorValue,
  );
  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      (other is EmoteRecord &&
          other.id == this.id &&
          other.timestamp == this.timestamp &&
          other.emoteId == this.emoteId &&
          other.friendlyName == this.friendlyName &&
          other.triggerCode == this.triggerCode &&
          other.triggerLabel == this.triggerLabel &&
          other.sensorValue == this.sensorValue);
}

class EmoteRecordsCompanion extends UpdateCompanion<EmoteRecord> {
  final Value<int> id;
  final Value<DateTime> timestamp;
  final Value<int> emoteId;
  final Value<String> friendlyName;
  final Value<int> triggerCode;
  final Value<String> triggerLabel;
  final Value<double?> sensorValue;
  const EmoteRecordsCompanion({
    this.id = const Value.absent(),
    this.timestamp = const Value.absent(),
    this.emoteId = const Value.absent(),
    this.friendlyName = const Value.absent(),
    this.triggerCode = const Value.absent(),
    this.triggerLabel = const Value.absent(),
    this.sensorValue = const Value.absent(),
  });
  EmoteRecordsCompanion.insert({
    this.id = const Value.absent(),
    required DateTime timestamp,
    required int emoteId,
    required String friendlyName,
    required int triggerCode,
    required String triggerLabel,
    this.sensorValue = const Value.absent(),
  }) : timestamp = Value(timestamp),
       emoteId = Value(emoteId),
       friendlyName = Value(friendlyName),
       triggerCode = Value(triggerCode),
       triggerLabel = Value(triggerLabel);
  static Insertable<EmoteRecord> custom({
    Expression<int>? id,
    Expression<DateTime>? timestamp,
    Expression<int>? emoteId,
    Expression<String>? friendlyName,
    Expression<int>? triggerCode,
    Expression<String>? triggerLabel,
    Expression<double>? sensorValue,
  }) {
    return RawValuesInsertable({
      if (id != null) 'id': id,
      if (timestamp != null) 'timestamp': timestamp,
      if (emoteId != null) 'emote_id': emoteId,
      if (friendlyName != null) 'friendly_name': friendlyName,
      if (triggerCode != null) 'trigger_code': triggerCode,
      if (triggerLabel != null) 'trigger_label': triggerLabel,
      if (sensorValue != null) 'sensor_value': sensorValue,
    });
  }

  EmoteRecordsCompanion copyWith({
    Value<int>? id,
    Value<DateTime>? timestamp,
    Value<int>? emoteId,
    Value<String>? friendlyName,
    Value<int>? triggerCode,
    Value<String>? triggerLabel,
    Value<double?>? sensorValue,
  }) {
    return EmoteRecordsCompanion(
      id: id ?? this.id,
      timestamp: timestamp ?? this.timestamp,
      emoteId: emoteId ?? this.emoteId,
      friendlyName: friendlyName ?? this.friendlyName,
      triggerCode: triggerCode ?? this.triggerCode,
      triggerLabel: triggerLabel ?? this.triggerLabel,
      sensorValue: sensorValue ?? this.sensorValue,
    );
  }

  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    if (id.present) {
      map['id'] = Variable<int>(id.value);
    }
    if (timestamp.present) {
      map['timestamp'] = Variable<DateTime>(timestamp.value);
    }
    if (emoteId.present) {
      map['emote_id'] = Variable<int>(emoteId.value);
    }
    if (friendlyName.present) {
      map['friendly_name'] = Variable<String>(friendlyName.value);
    }
    if (triggerCode.present) {
      map['trigger_code'] = Variable<int>(triggerCode.value);
    }
    if (triggerLabel.present) {
      map['trigger_label'] = Variable<String>(triggerLabel.value);
    }
    if (sensorValue.present) {
      map['sensor_value'] = Variable<double>(sensorValue.value);
    }
    return map;
  }

  @override
  String toString() {
    return (StringBuffer('EmoteRecordsCompanion(')
          ..write('id: $id, ')
          ..write('timestamp: $timestamp, ')
          ..write('emoteId: $emoteId, ')
          ..write('friendlyName: $friendlyName, ')
          ..write('triggerCode: $triggerCode, ')
          ..write('triggerLabel: $triggerLabel, ')
          ..write('sensorValue: $sensorValue')
          ..write(')'))
        .toString();
  }
}

abstract class _$AppDatabase extends GeneratedDatabase {
  _$AppDatabase(QueryExecutor e) : super(e);
  $AppDatabaseManager get managers => $AppDatabaseManager(this);
  late final $EnvRecordsTable envRecords = $EnvRecordsTable(this);
  late final $EventRecordsTable eventRecords = $EventRecordsTable(this);
  late final $EmoteRecordsTable emoteRecords = $EmoteRecordsTable(this);
  @override
  Iterable<TableInfo<Table, Object?>> get allTables =>
      allSchemaEntities.whereType<TableInfo<Table, Object?>>();
  @override
  List<DatabaseSchemaEntity> get allSchemaEntities => [
    envRecords,
    eventRecords,
    emoteRecords,
  ];
}

typedef $$EnvRecordsTableCreateCompanionBuilder =
    EnvRecordsCompanion Function({
      Value<int> id,
      required DateTime timestamp,
      required double temperature,
      required double humidity,
      required int pressure,
      required double altitude,
      required double uvIndex,
    });
typedef $$EnvRecordsTableUpdateCompanionBuilder =
    EnvRecordsCompanion Function({
      Value<int> id,
      Value<DateTime> timestamp,
      Value<double> temperature,
      Value<double> humidity,
      Value<int> pressure,
      Value<double> altitude,
      Value<double> uvIndex,
    });

class $$EnvRecordsTableFilterComposer
    extends Composer<_$AppDatabase, $EnvRecordsTable> {
  $$EnvRecordsTableFilterComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnFilters<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<DateTime> get timestamp => $composableBuilder(
    column: $table.timestamp,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get temperature => $composableBuilder(
    column: $table.temperature,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get humidity => $composableBuilder(
    column: $table.humidity,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get pressure => $composableBuilder(
    column: $table.pressure,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get altitude => $composableBuilder(
    column: $table.altitude,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get uvIndex => $composableBuilder(
    column: $table.uvIndex,
    builder: (column) => ColumnFilters(column),
  );
}

class $$EnvRecordsTableOrderingComposer
    extends Composer<_$AppDatabase, $EnvRecordsTable> {
  $$EnvRecordsTableOrderingComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnOrderings<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<DateTime> get timestamp => $composableBuilder(
    column: $table.timestamp,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get temperature => $composableBuilder(
    column: $table.temperature,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get humidity => $composableBuilder(
    column: $table.humidity,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get pressure => $composableBuilder(
    column: $table.pressure,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get altitude => $composableBuilder(
    column: $table.altitude,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get uvIndex => $composableBuilder(
    column: $table.uvIndex,
    builder: (column) => ColumnOrderings(column),
  );
}

class $$EnvRecordsTableAnnotationComposer
    extends Composer<_$AppDatabase, $EnvRecordsTable> {
  $$EnvRecordsTableAnnotationComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  GeneratedColumn<int> get id =>
      $composableBuilder(column: $table.id, builder: (column) => column);

  GeneratedColumn<DateTime> get timestamp =>
      $composableBuilder(column: $table.timestamp, builder: (column) => column);

  GeneratedColumn<double> get temperature => $composableBuilder(
    column: $table.temperature,
    builder: (column) => column,
  );

  GeneratedColumn<double> get humidity =>
      $composableBuilder(column: $table.humidity, builder: (column) => column);

  GeneratedColumn<int> get pressure =>
      $composableBuilder(column: $table.pressure, builder: (column) => column);

  GeneratedColumn<double> get altitude =>
      $composableBuilder(column: $table.altitude, builder: (column) => column);

  GeneratedColumn<double> get uvIndex =>
      $composableBuilder(column: $table.uvIndex, builder: (column) => column);
}

class $$EnvRecordsTableTableManager
    extends
        RootTableManager<
          _$AppDatabase,
          $EnvRecordsTable,
          EnvRecord,
          $$EnvRecordsTableFilterComposer,
          $$EnvRecordsTableOrderingComposer,
          $$EnvRecordsTableAnnotationComposer,
          $$EnvRecordsTableCreateCompanionBuilder,
          $$EnvRecordsTableUpdateCompanionBuilder,
          (
            EnvRecord,
            BaseReferences<_$AppDatabase, $EnvRecordsTable, EnvRecord>,
          ),
          EnvRecord,
          PrefetchHooks Function()
        > {
  $$EnvRecordsTableTableManager(_$AppDatabase db, $EnvRecordsTable table)
    : super(
        TableManagerState(
          db: db,
          table: table,
          createFilteringComposer: () =>
              $$EnvRecordsTableFilterComposer($db: db, $table: table),
          createOrderingComposer: () =>
              $$EnvRecordsTableOrderingComposer($db: db, $table: table),
          createComputedFieldComposer: () =>
              $$EnvRecordsTableAnnotationComposer($db: db, $table: table),
          updateCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                Value<DateTime> timestamp = const Value.absent(),
                Value<double> temperature = const Value.absent(),
                Value<double> humidity = const Value.absent(),
                Value<int> pressure = const Value.absent(),
                Value<double> altitude = const Value.absent(),
                Value<double> uvIndex = const Value.absent(),
              }) => EnvRecordsCompanion(
                id: id,
                timestamp: timestamp,
                temperature: temperature,
                humidity: humidity,
                pressure: pressure,
                altitude: altitude,
                uvIndex: uvIndex,
              ),
          createCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                required DateTime timestamp,
                required double temperature,
                required double humidity,
                required int pressure,
                required double altitude,
                required double uvIndex,
              }) => EnvRecordsCompanion.insert(
                id: id,
                timestamp: timestamp,
                temperature: temperature,
                humidity: humidity,
                pressure: pressure,
                altitude: altitude,
                uvIndex: uvIndex,
              ),
          withReferenceMapper: (p0) => p0
              .map((e) => (e.readTable(table), BaseReferences(db, table, e)))
              .toList(),
          prefetchHooksCallback: null,
        ),
      );
}

typedef $$EnvRecordsTableProcessedTableManager =
    ProcessedTableManager<
      _$AppDatabase,
      $EnvRecordsTable,
      EnvRecord,
      $$EnvRecordsTableFilterComposer,
      $$EnvRecordsTableOrderingComposer,
      $$EnvRecordsTableAnnotationComposer,
      $$EnvRecordsTableCreateCompanionBuilder,
      $$EnvRecordsTableUpdateCompanionBuilder,
      (EnvRecord, BaseReferences<_$AppDatabase, $EnvRecordsTable, EnvRecord>),
      EnvRecord,
      PrefetchHooks Function()
    >;
typedef $$EventRecordsTableCreateCompanionBuilder =
    EventRecordsCompanion Function({
      Value<int> id,
      required DateTime timestamp,
      required int eventType,
      required double pitch,
      required double roll,
      required double accelMag,
      Value<int?> duration,
    });
typedef $$EventRecordsTableUpdateCompanionBuilder =
    EventRecordsCompanion Function({
      Value<int> id,
      Value<DateTime> timestamp,
      Value<int> eventType,
      Value<double> pitch,
      Value<double> roll,
      Value<double> accelMag,
      Value<int?> duration,
    });

class $$EventRecordsTableFilterComposer
    extends Composer<_$AppDatabase, $EventRecordsTable> {
  $$EventRecordsTableFilterComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnFilters<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<DateTime> get timestamp => $composableBuilder(
    column: $table.timestamp,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get eventType => $composableBuilder(
    column: $table.eventType,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get pitch => $composableBuilder(
    column: $table.pitch,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get roll => $composableBuilder(
    column: $table.roll,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get accelMag => $composableBuilder(
    column: $table.accelMag,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get duration => $composableBuilder(
    column: $table.duration,
    builder: (column) => ColumnFilters(column),
  );
}

class $$EventRecordsTableOrderingComposer
    extends Composer<_$AppDatabase, $EventRecordsTable> {
  $$EventRecordsTableOrderingComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnOrderings<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<DateTime> get timestamp => $composableBuilder(
    column: $table.timestamp,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get eventType => $composableBuilder(
    column: $table.eventType,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get pitch => $composableBuilder(
    column: $table.pitch,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get roll => $composableBuilder(
    column: $table.roll,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get accelMag => $composableBuilder(
    column: $table.accelMag,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get duration => $composableBuilder(
    column: $table.duration,
    builder: (column) => ColumnOrderings(column),
  );
}

class $$EventRecordsTableAnnotationComposer
    extends Composer<_$AppDatabase, $EventRecordsTable> {
  $$EventRecordsTableAnnotationComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  GeneratedColumn<int> get id =>
      $composableBuilder(column: $table.id, builder: (column) => column);

  GeneratedColumn<DateTime> get timestamp =>
      $composableBuilder(column: $table.timestamp, builder: (column) => column);

  GeneratedColumn<int> get eventType =>
      $composableBuilder(column: $table.eventType, builder: (column) => column);

  GeneratedColumn<double> get pitch =>
      $composableBuilder(column: $table.pitch, builder: (column) => column);

  GeneratedColumn<double> get roll =>
      $composableBuilder(column: $table.roll, builder: (column) => column);

  GeneratedColumn<double> get accelMag =>
      $composableBuilder(column: $table.accelMag, builder: (column) => column);

  GeneratedColumn<int> get duration =>
      $composableBuilder(column: $table.duration, builder: (column) => column);
}

class $$EventRecordsTableTableManager
    extends
        RootTableManager<
          _$AppDatabase,
          $EventRecordsTable,
          EventRecord,
          $$EventRecordsTableFilterComposer,
          $$EventRecordsTableOrderingComposer,
          $$EventRecordsTableAnnotationComposer,
          $$EventRecordsTableCreateCompanionBuilder,
          $$EventRecordsTableUpdateCompanionBuilder,
          (
            EventRecord,
            BaseReferences<_$AppDatabase, $EventRecordsTable, EventRecord>,
          ),
          EventRecord,
          PrefetchHooks Function()
        > {
  $$EventRecordsTableTableManager(_$AppDatabase db, $EventRecordsTable table)
    : super(
        TableManagerState(
          db: db,
          table: table,
          createFilteringComposer: () =>
              $$EventRecordsTableFilterComposer($db: db, $table: table),
          createOrderingComposer: () =>
              $$EventRecordsTableOrderingComposer($db: db, $table: table),
          createComputedFieldComposer: () =>
              $$EventRecordsTableAnnotationComposer($db: db, $table: table),
          updateCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                Value<DateTime> timestamp = const Value.absent(),
                Value<int> eventType = const Value.absent(),
                Value<double> pitch = const Value.absent(),
                Value<double> roll = const Value.absent(),
                Value<double> accelMag = const Value.absent(),
                Value<int?> duration = const Value.absent(),
              }) => EventRecordsCompanion(
                id: id,
                timestamp: timestamp,
                eventType: eventType,
                pitch: pitch,
                roll: roll,
                accelMag: accelMag,
                duration: duration,
              ),
          createCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                required DateTime timestamp,
                required int eventType,
                required double pitch,
                required double roll,
                required double accelMag,
                Value<int?> duration = const Value.absent(),
              }) => EventRecordsCompanion.insert(
                id: id,
                timestamp: timestamp,
                eventType: eventType,
                pitch: pitch,
                roll: roll,
                accelMag: accelMag,
                duration: duration,
              ),
          withReferenceMapper: (p0) => p0
              .map((e) => (e.readTable(table), BaseReferences(db, table, e)))
              .toList(),
          prefetchHooksCallback: null,
        ),
      );
}

typedef $$EventRecordsTableProcessedTableManager =
    ProcessedTableManager<
      _$AppDatabase,
      $EventRecordsTable,
      EventRecord,
      $$EventRecordsTableFilterComposer,
      $$EventRecordsTableOrderingComposer,
      $$EventRecordsTableAnnotationComposer,
      $$EventRecordsTableCreateCompanionBuilder,
      $$EventRecordsTableUpdateCompanionBuilder,
      (
        EventRecord,
        BaseReferences<_$AppDatabase, $EventRecordsTable, EventRecord>,
      ),
      EventRecord,
      PrefetchHooks Function()
    >;
typedef $$EmoteRecordsTableCreateCompanionBuilder =
    EmoteRecordsCompanion Function({
      Value<int> id,
      required DateTime timestamp,
      required int emoteId,
      required String friendlyName,
      required int triggerCode,
      required String triggerLabel,
      Value<double?> sensorValue,
    });
typedef $$EmoteRecordsTableUpdateCompanionBuilder =
    EmoteRecordsCompanion Function({
      Value<int> id,
      Value<DateTime> timestamp,
      Value<int> emoteId,
      Value<String> friendlyName,
      Value<int> triggerCode,
      Value<String> triggerLabel,
      Value<double?> sensorValue,
    });

class $$EmoteRecordsTableFilterComposer
    extends Composer<_$AppDatabase, $EmoteRecordsTable> {
  $$EmoteRecordsTableFilterComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnFilters<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<DateTime> get timestamp => $composableBuilder(
    column: $table.timestamp,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get emoteId => $composableBuilder(
    column: $table.emoteId,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get friendlyName => $composableBuilder(
    column: $table.friendlyName,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get triggerCode => $composableBuilder(
    column: $table.triggerCode,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get triggerLabel => $composableBuilder(
    column: $table.triggerLabel,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<double> get sensorValue => $composableBuilder(
    column: $table.sensorValue,
    builder: (column) => ColumnFilters(column),
  );
}

class $$EmoteRecordsTableOrderingComposer
    extends Composer<_$AppDatabase, $EmoteRecordsTable> {
  $$EmoteRecordsTableOrderingComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnOrderings<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<DateTime> get timestamp => $composableBuilder(
    column: $table.timestamp,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get emoteId => $composableBuilder(
    column: $table.emoteId,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get friendlyName => $composableBuilder(
    column: $table.friendlyName,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get triggerCode => $composableBuilder(
    column: $table.triggerCode,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get triggerLabel => $composableBuilder(
    column: $table.triggerLabel,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<double> get sensorValue => $composableBuilder(
    column: $table.sensorValue,
    builder: (column) => ColumnOrderings(column),
  );
}

class $$EmoteRecordsTableAnnotationComposer
    extends Composer<_$AppDatabase, $EmoteRecordsTable> {
  $$EmoteRecordsTableAnnotationComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  GeneratedColumn<int> get id =>
      $composableBuilder(column: $table.id, builder: (column) => column);

  GeneratedColumn<DateTime> get timestamp =>
      $composableBuilder(column: $table.timestamp, builder: (column) => column);

  GeneratedColumn<int> get emoteId =>
      $composableBuilder(column: $table.emoteId, builder: (column) => column);

  GeneratedColumn<String> get friendlyName => $composableBuilder(
    column: $table.friendlyName,
    builder: (column) => column,
  );

  GeneratedColumn<int> get triggerCode => $composableBuilder(
    column: $table.triggerCode,
    builder: (column) => column,
  );

  GeneratedColumn<String> get triggerLabel => $composableBuilder(
    column: $table.triggerLabel,
    builder: (column) => column,
  );

  GeneratedColumn<double> get sensorValue => $composableBuilder(
    column: $table.sensorValue,
    builder: (column) => column,
  );
}

class $$EmoteRecordsTableTableManager
    extends
        RootTableManager<
          _$AppDatabase,
          $EmoteRecordsTable,
          EmoteRecord,
          $$EmoteRecordsTableFilterComposer,
          $$EmoteRecordsTableOrderingComposer,
          $$EmoteRecordsTableAnnotationComposer,
          $$EmoteRecordsTableCreateCompanionBuilder,
          $$EmoteRecordsTableUpdateCompanionBuilder,
          (
            EmoteRecord,
            BaseReferences<_$AppDatabase, $EmoteRecordsTable, EmoteRecord>,
          ),
          EmoteRecord,
          PrefetchHooks Function()
        > {
  $$EmoteRecordsTableTableManager(_$AppDatabase db, $EmoteRecordsTable table)
    : super(
        TableManagerState(
          db: db,
          table: table,
          createFilteringComposer: () =>
              $$EmoteRecordsTableFilterComposer($db: db, $table: table),
          createOrderingComposer: () =>
              $$EmoteRecordsTableOrderingComposer($db: db, $table: table),
          createComputedFieldComposer: () =>
              $$EmoteRecordsTableAnnotationComposer($db: db, $table: table),
          updateCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                Value<DateTime> timestamp = const Value.absent(),
                Value<int> emoteId = const Value.absent(),
                Value<String> friendlyName = const Value.absent(),
                Value<int> triggerCode = const Value.absent(),
                Value<String> triggerLabel = const Value.absent(),
                Value<double?> sensorValue = const Value.absent(),
              }) => EmoteRecordsCompanion(
                id: id,
                timestamp: timestamp,
                emoteId: emoteId,
                friendlyName: friendlyName,
                triggerCode: triggerCode,
                triggerLabel: triggerLabel,
                sensorValue: sensorValue,
              ),
          createCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                required DateTime timestamp,
                required int emoteId,
                required String friendlyName,
                required int triggerCode,
                required String triggerLabel,
                Value<double?> sensorValue = const Value.absent(),
              }) => EmoteRecordsCompanion.insert(
                id: id,
                timestamp: timestamp,
                emoteId: emoteId,
                friendlyName: friendlyName,
                triggerCode: triggerCode,
                triggerLabel: triggerLabel,
                sensorValue: sensorValue,
              ),
          withReferenceMapper: (p0) => p0
              .map((e) => (e.readTable(table), BaseReferences(db, table, e)))
              .toList(),
          prefetchHooksCallback: null,
        ),
      );
}

typedef $$EmoteRecordsTableProcessedTableManager =
    ProcessedTableManager<
      _$AppDatabase,
      $EmoteRecordsTable,
      EmoteRecord,
      $$EmoteRecordsTableFilterComposer,
      $$EmoteRecordsTableOrderingComposer,
      $$EmoteRecordsTableAnnotationComposer,
      $$EmoteRecordsTableCreateCompanionBuilder,
      $$EmoteRecordsTableUpdateCompanionBuilder,
      (
        EmoteRecord,
        BaseReferences<_$AppDatabase, $EmoteRecordsTable, EmoteRecord>,
      ),
      EmoteRecord,
      PrefetchHooks Function()
    >;

class $AppDatabaseManager {
  final _$AppDatabase _db;
  $AppDatabaseManager(this._db);
  $$EnvRecordsTableTableManager get envRecords =>
      $$EnvRecordsTableTableManager(_db, _db.envRecords);
  $$EventRecordsTableTableManager get eventRecords =>
      $$EventRecordsTableTableManager(_db, _db.eventRecords);
  $$EmoteRecordsTableTableManager get emoteRecords =>
      $$EmoteRecordsTableTableManager(_db, _db.emoteRecords);
}
