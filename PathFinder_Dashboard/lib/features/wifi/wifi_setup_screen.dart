import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../shared/providers/ble_provider.dart';

/// Wi-Fi 配网页面 — 通过 BLE 向 ESP32 发送 Wi-Fi 凭据
class WifiSetupScreen extends ConsumerStatefulWidget {
  const WifiSetupScreen({super.key});

  @override
  ConsumerState<WifiSetupScreen> createState() => _WifiSetupScreenState();
}

enum _WifiStatus { idle, sending, connecting, connected, failed }

class _WifiSetupScreenState extends ConsumerState<WifiSetupScreen> {
  final _ssidController = TextEditingController();
  final _passController = TextEditingController();
  _WifiStatus _status = _WifiStatus.idle;
  String _statusDetail = '';

  @override
  void dispose() {
    _ssidController.dispose();
    _passController.dispose();
    super.dispose();
  }

  Future<void> _sendConfig() async {
    final ssid = _ssidController.text.trim();
    final pass = _passController.text;

    if (ssid.isEmpty) {
      setState(() => _statusDetail = '请输入 WiFi 名称');
      return;
    }

    setState(() {
      _status = _WifiStatus.sending;
      _statusDetail = '正在发送配置...';
    });

    final bleService = ref.read(bleServiceProvider);
    await bleService.writeWifiConfig(ssid, pass);

    setState(() {
      _status = _WifiStatus.connecting;
      _statusDetail = 'ESP32 正在连接 WiFi...';
    });
  }

  Future<void> _resetWifi() async {
    final bleService = ref.read(bleServiceProvider);
    await bleService.resetWifiConfig();
    setState(() {
      _status = _WifiStatus.idle;
      _statusDetail = '已清除 ESP32 WiFi 配置';
    });
  }

  @override
  Widget build(BuildContext context) {
    final bleState = ref.watch(bleServiceProvider).currentState;
    final isBleConnected = bleState == BleConnectionState.connected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('WiFi 设置'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // BLE 连接状态提示
            if (!isBleConnected)
              Card(
                color: Colors.orange.shade900.withValues(alpha: 0.3),
                child: const Padding(
                  padding: EdgeInsets.all(16),
                  child: Row(
                    children: [
                      Icon(Icons.warning, color: Colors.orange),
                      SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          '请先连接 BLE 设备',
                          style: TextStyle(color: Colors.orange),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

            const SizedBox(height: 16),

            // SSID 输入
            TextField(
              controller: _ssidController,
              decoration: const InputDecoration(
                labelText: 'WiFi 名称 (SSID)',
                prefixIcon: Icon(Icons.wifi),
                border: OutlineInputBorder(),
              ),
              enabled: isBleConnected && _status != _WifiStatus.sending,
            ),

            const SizedBox(height: 16),

            // 密码输入
            TextField(
              controller: _passController,
              obscureText: true,
              decoration: const InputDecoration(
                labelText: 'WiFi 密码',
                prefixIcon: Icon(Icons.lock),
                border: OutlineInputBorder(),
              ),
              enabled: isBleConnected && _status != _WifiStatus.sending,
            ),

            const SizedBox(height: 24),

            // 发送按钮
            FilledButton.icon(
              onPressed: (isBleConnected && _status != _WifiStatus.sending)
                  ? _sendConfig
                  : null,
              icon: const Icon(Icons.send),
              label: const Text('发送配置到 ESP32'),
            ),

            const SizedBox(height: 12),

            // 重置按钮
            OutlinedButton.icon(
              onPressed: isBleConnected ? _resetWifi : null,
              icon: const Icon(Icons.refresh),
              label: const Text('清除 ESP32 WiFi 配置'),
            ),

            const SizedBox(height: 24),

            // 状态显示
            if (_statusDetail.isNotEmpty)
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Row(
                    children: [
                      Icon(
                        _status == _WifiStatus.connecting
                            ? Icons.hourglass_top
                            : _status == _WifiStatus.connected
                                ? Icons.check_circle
                                : _status == _WifiStatus.failed
                                    ? Icons.error
                                    : Icons.info,
                        color: _status == _WifiStatus.connecting
                            ? Colors.blue
                            : _status == _WifiStatus.connected
                                ? Colors.green
                                : _status == _WifiStatus.failed
                                    ? Colors.red
                                    : Colors.grey,
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          _statusDetail,
                          style: TextStyle(
                            color: _status == _WifiStatus.connecting
                                ? Colors.blue
                                : _status == _WifiStatus.connected
                                    ? Colors.green
                                    : _status == _WifiStatus.failed
                                        ? Colors.red
                                        : Colors.grey,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

            const Spacer(),

            // 说明文字
            Text(
              '通过 BLE 向 ESP32 发送 WiFi 配置。\n设备连接 WiFi 后将自动关闭配网模式。',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Colors.grey,
                  ),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
