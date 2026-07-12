import 'dart:typed_data';

enum EmoteTrigger {
  manual(0, '手动切换'),
  uvExtreme(1, '紫外线极端危险'),
  uvHigh(2, '紫外线较强'),
  hot(3, '温度过高'),
  cold(4, '温度过低'),
  humid(5, '高湿度不适'),
  tilted(6, '检测到严重倾斜'),
  slightTilt(7, '检测到倾斜'),
  lowPressure(8, '气压偏低'),
  normal(9, '一切正常');

  final int code;
  final String label;
  const EmoteTrigger(this.code, this.label);

  static EmoteTrigger fromCode(int code) {
    return values.firstWhere(
      (e) => e.code == code,
      orElse: () => EmoteTrigger.normal,
    );
  }
}

class EmoteInfo {
  final int emoteId;
  final String friendlyName;
  final EmoteTrigger trigger;
  final DateTime timestamp;

  const EmoteInfo({
    required this.emoteId,
    required this.friendlyName,
    required this.trigger,
    required this.timestamp,
  });

  /// Decode from BLE C4 characteristic (15 bytes)
  factory EmoteInfo.fromBle(Uint8List data) {
    if (data.length != 15) {
      throw FormatException('Emote frame must be 15 bytes, got ${data.length}');
    }
    final nameLen = data[1];
    final name = String.fromCharCodes(data.sublist(2, 2 + nameLen));
    return EmoteInfo(
      emoteId: data[0],
      friendlyName: name,
      trigger: EmoteTrigger.fromCode(data[14]),
      timestamp: DateTime.now(),
    );
  }

  factory EmoteInfo.mock({
    int emoteId = 0,
    String friendlyName = 'Smile',
    EmoteTrigger trigger = EmoteTrigger.normal,
  }) {
    return EmoteInfo(
      emoteId: emoteId,
      friendlyName: friendlyName,
      trigger: trigger,
      timestamp: DateTime.now(),
    );
  }
}
