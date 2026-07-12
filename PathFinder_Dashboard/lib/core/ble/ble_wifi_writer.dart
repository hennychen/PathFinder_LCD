import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'ble_uuids.dart';

/// BLE C5 (0xFE05) Write 封装 — 向 ESP32 发送 Wi-Fi 配网命令
class BleWifiWriter {
  final FlutterReactiveBle _ble;

  BleWifiWriter(this._ble);

  /// 发送 Wi-Fi 凭据到 ESP32
  Future<void> sendWifiConfig(String deviceId, String ssid, String password) async {
    final json = jsonEncode({
      'cmd': 'set_wifi',
      'ssid': ssid,
      'pass': password,
    });
    await _writeJson(deviceId, json);
  }

  /// 查询当前配网状态
  Future<void> queryStatus(String deviceId) async {
    await _writeJson(deviceId, jsonEncode({'cmd': 'get_status'}));
  }

  /// 清除 ESP32 的 Wi-Fi 凭据 (重新配网)
  Future<void> resetWifi(String deviceId) async {
    await _writeJson(deviceId, jsonEncode({'cmd': 'reset_wifi'}));
  }

  /// 内部: 发送 JSON, 支持 BLE MTU 分包
  Future<void> _writeJson(String deviceId, String json) async {
    final char = QualifiedCharacteristic(
      characteristicId: c5WifiUuid,
      serviceId: pfServiceUuid,
      deviceId: deviceId,
    );

    final bytes = utf8.encode(json);
    const mtu = 180; // BLE ATT MTU 安全上限

    if (bytes.length <= mtu - 1) {
      // 单帧: 前缀 0x00 + JSON
      final frame = Uint8List(bytes.length + 1);
      frame[0] = 0x00;
      frame.setRange(1, 1 + bytes.length, bytes);
      await _ble.writeCharacteristicWithoutResponse(char, value: frame.toList());
      debugPrint('[BLE WiFi] Sent single frame: $json');
    } else {
      // 分包: 首帧 0x00 + 后续帧 0x01
      int offset = 0;
      bool isFirst = true;

      while (offset < bytes.length) {
        final chunkSize = mtu - 1;
        final remaining = bytes.length - offset;
        final copyLen = remaining < chunkSize ? remaining : chunkSize;

        final frame = Uint8List(copyLen + 1);
        frame[0] = isFirst ? 0x00 : 0x01;
        frame.setRange(1, 1 + copyLen, bytes.sublist(offset, offset + copyLen));

        await _ble.writeCharacteristicWithoutResponse(char, value: frame.toList());
        debugPrint('[BLE WiFi] Sent ${isFirst ? "first" : "continuation"} frame: $copyLen bytes');

        offset += copyLen;
        isFirst = false;

        // 帧间小延迟
        await Future.delayed(const Duration(milliseconds: 20));
      }
    }
  }
}
