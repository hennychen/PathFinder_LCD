import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:wifi_scan/wifi_scan.dart';
import 'package:permission_handler/permission_handler.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../shared/providers/ble_provider.dart';

/// Wi-Fi 配网页面 — 扫描可用 WiFi 列表，选中后通过 BLE 发送凭据到 ESP32
class WifiSetupScreen extends ConsumerStatefulWidget {
  const WifiSetupScreen({super.key});

  @override
  ConsumerState<WifiSetupScreen> createState() => _WifiSetupScreenState();
}

enum _WifiStatus { idle, scanning, sending, connecting, connected, failed }

class _WifiSetupScreenState extends ConsumerState<WifiSetupScreen> {
  _WifiStatus _status = _WifiStatus.idle;
  String _statusDetail = '';

  List<WiFiAccessPoint> _apList = [];
  bool _scanning = false;
  String? _selectedSsid;
  bool _showManualInput = false;

  final _passController = TextEditingController();
  final _manualSsidController = TextEditingController();
  final _scrollController = ScrollController();
  StreamSubscription<Map<String, dynamic>>? _wifiStatusSub;

  @override
  void initState() {
    super.initState();
    // 延迟执行扫描，等页面构建完成
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _startScan();
      _listenWifiStatus();
    });
  }

  /// 监听 B板 WiFi 配网状态回报（通过 BLE C5 Notify）
  void _listenWifiStatus() {
    final bleService = ref.read(bleServiceProvider);
    _wifiStatusSub = bleService.wifiStatusStream.listen((json) {
      if (!mounted) return;
      final status = json['status'] as String?;
      final ip = json['ip'] as String?;

      if (status == 'connecting') {
        setState(() {
          _status = _WifiStatus.connecting;
          _statusDetail = 'B板正在连接 WiFi...';
        });
      } else if (status == 'connected' && ip != null) {
        setState(() {
          _status = _WifiStatus.connected;
          _statusDetail = 'B板已连接! IP: $ip';
        });
        _showSuccessDialog(ip);
      } else if (status == 'failed') {
        setState(() {
          _status = _WifiStatus.failed;
          _statusDetail = 'B板 WiFi 连接失败';
        });
      }
    });
  }

  @override
  void dispose() {
    _wifiStatusSub?.cancel();
    _passController.dispose();
    _manualSsidController.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  /// 请求定位权限（Android WiFi 扫描必需）
  Future<bool> _requestLocationPermission() async {
    final status = await Permission.locationWhenInUse.request();
    return status.isGranted;
  }

  /// 扫描 WiFi 网络
  Future<void> _startScan() async {
    final hasPermission = await _requestLocationPermission();
    if (!hasPermission) {
      setState(() {
        _statusDetail = '需要定位权限才能扫描 WiFi';
        _status = _WifiStatus.failed;
      });
      return;
    }

    setState(() {
      _scanning = true;
      _status = _WifiStatus.idle;
      _statusDetail = '';
    });

    // 检查 WiFi 扫描能力
    final canScan = await WiFiScan.instance.canStartScan();
    if (canScan != CanStartScan.yes) {
      setState(() {
        _scanning = false;
        _statusDetail = '无法扫描: $canScan';
      });
      return;
    }

    // 启动扫描
    await WiFiScan.instance.startScan();

    // 等待扫描结果（Android 扫描是异步的，延迟获取结果）
    await Future.delayed(const Duration(milliseconds: 1500));

    await _loadScanResults();
  }

  /// 获取扫描结果
  Future<void> _loadScanResults() async {
    final canGet = await WiFiScan.instance.canGetScannedResults();
    if (canGet != CanGetScannedResults.yes) {
      setState(() {
        _scanning = false;
        _statusDetail = '无法获取扫描结果: $canGet';
      });
      return;
    }

    final results = await WiFiScan.instance.getScannedResults();

    // 去重（同一 SSID 可能出现多次），按信号强度排序
    final seen = <String>{};
    final unique = <WiFiAccessPoint>[];
    for (final ap in results) {
      if (!seen.contains(ap.ssid) && ap.ssid.isNotEmpty) {
        seen.add(ap.ssid);
        unique.add(ap);
      }
    }
    unique.sort((a, b) => b.level.compareTo(a.level));

    setState(() {
      _apList = unique;
      _scanning = false;
      if (unique.isEmpty) {
        _statusDetail = '未找到 WiFi 网络';
      } else {
        _statusDetail = '';
      }
    });
  }

  /// 选中某个 WiFi
  void _selectWifi(String ssid) {
    setState(() {
      _selectedSsid = ssid;
      _status = _WifiStatus.idle;
      _statusDetail = '';
      _passController.clear();
    });
  }

  /// 发送配网信息到 ESP32
  Future<void> _sendConfig() async {
    final ssid = _selectedSsid ?? _manualSsidController.text.trim();
    final pass = _passController.text;

    if (ssid.isEmpty) {
      setState(() => _statusDetail = '请选择或输入 WiFi 名称');
      return;
    }

    setState(() {
      _status = _WifiStatus.sending;
      _statusDetail = '正在发送配置到 ESP32...';
    });

    final bleService = ref.read(bleServiceProvider);
    await bleService.writeWifiConfig(ssid, pass);

    setState(() {
      _status = _WifiStatus.connecting;
      _statusDetail = 'ESP32 正在连接 WiFi: $ssid';
    });
  }

  /// 重置 ESP32 WiFi 配置
  Future<void> _resetWifi() async {
    final bleService = ref.read(bleServiceProvider);
    await bleService.resetWifiConfig();
    setState(() {
      _status = _WifiStatus.idle;
      _statusDetail = '已清除 ESP32 WiFi 配置';
    });
  }

  /// 连接成功弹窗 + 自动返回
  void _showSuccessDialog(String ip) {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        icon: const Icon(Icons.wifi_off, size: 0),
        title: Row(
          children: [
            Icon(Icons.check_circle, color: Colors.green.shade600, size: 32),
            const SizedBox(width: 12),
            const Text('WiFi 连接成功'),
          ],
        ),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('B板已连接到 WiFi', style: TextStyle(fontSize: 15)),
            const SizedBox(height: 8),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: Colors.green.withValues(alpha: 0.1),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Row(
                children: [
                  const Icon(Icons.router, size: 18, color: Colors.green),
                  const SizedBox(width: 8),
                  Text(
                    'IP: $ip',
                    style: const TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      fontFamily: 'monospace',
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 8),
            Text(
              '摄像头预览 IP 已自动配置\n人脸追踪功能已就绪',
              style: TextStyle(fontSize: 13, color: Colors.grey.shade600),
            ),
          ],
        ),
        actions: [
          FilledButton(
            onPressed: () {
              Navigator.of(ctx).pop();
              Navigator.of(context).pop(); // 返回上一页
            },
            child: const Text('完成'),
          ),
        ],
      ),
    );
  }

  /// 信号强度 → 信号条数图标
  IconData _signalIcon(int level) {
    if (level >= -50) return Icons.signal_wifi_4_bar;
    if (level >= -60) return Icons.network_wifi_3_bar;
    if (level >= -70) return Icons.network_wifi_2_bar;
    return Icons.network_wifi_1_bar;
  }

  /// 信号强度 → 颜色
  Color _signalColor(int level) {
    if (level >= -60) return Colors.green;
    if (level >= -75) return Colors.orange;
    return Colors.red;
  }

  /// 加密类型判断
  bool _isSecured(WiFiAccessPoint ap) {
    final cap = ap.capabilities.toUpperCase();
    return cap.contains('WPA') || cap.contains('WEP') || cap.contains('WPS');
  }

  @override
  Widget build(BuildContext context) {
    final bleState = ref.watch(bleServiceProvider).currentState;
    final isBleConnected = bleState == BleConnectionState.connected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('WiFi 设置'),
        actions: [
          IconButton(
            icon: _scanning
                ? const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(
                      strokeWidth: 2,
                      color: Colors.white,
                    ),
                  )
                : const Icon(Icons.refresh),
            onPressed: _scanning ? null : _startScan,
            tooltip: '重新扫描',
          ),
        ],
      ),
      body: !isBleConnected
          ? _buildBleWarning()
          : Column(
              children: [
                // ── 扫描列表区域 ──
                Expanded(
                  flex: _selectedSsid != null ? 2 : 3,
                  child: _buildScanList(),
                ),
                // ── 密码输入与发送区域 ──
                if (_selectedSsid != null || _showManualInput)
                  Container(
                    decoration: BoxDecoration(
                      color: Theme.of(context).scaffoldBackgroundColor,
                      border: Border(
                        top: BorderSide(
                          color: Colors.white.withValues(alpha: 0.1),
                        ),
                      ),
                      boxShadow: [
                        BoxShadow(
                          color: Colors.black.withValues(alpha: 0.3),
                          blurRadius: 8,
                          offset: const Offset(0, -2),
                        ),
                      ],
                    ),
                    padding: const EdgeInsets.all(20),
                    child: _buildPasswordInput(),
                  ),
                // ── 状态栏 ──
                if (_statusDetail.isNotEmpty) _buildStatusBar(),
              ],
            ),
    );
  }

  /// BLE 未连接警告
  Widget _buildBleWarning() {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(
              Icons.bluetooth_disabled,
              size: 64,
              color: Colors.orange,
            ),
            const SizedBox(height: 16),
            const Text(
              '请先连接 BLE 设备',
              style: TextStyle(fontSize: 18, color: Colors.orange),
            ),
            const SizedBox(height: 8),
            Text(
              'WiFi 配网需要通过 BLE 向 ESP32 发送凭据',
              style: TextStyle(fontSize: 13, color: Colors.grey.shade500),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }

  /// WiFi 扫描列表
  Widget _buildScanList() {
    if (_scanning && _apList.isEmpty) {
      return const Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            CircularProgressIndicator(),
            SizedBox(height: 16),
            Text('正在扫描 WiFi 网络...', style: TextStyle(color: Colors.grey)),
          ],
        ),
      );
    }

    if (_apList.isEmpty) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.wifi_off, size: 48, color: Colors.grey),
            const SizedBox(height: 16),
            Text(
              _statusDetail.isNotEmpty ? _statusDetail : '未找到 WiFi 网络',
              style: const TextStyle(color: Colors.grey),
            ),
            const SizedBox(height: 16),
            FilledButton.icon(
              onPressed: _startScan,
              icon: const Icon(Icons.refresh),
              label: const Text('重新扫描'),
            ),
            const SizedBox(height: 8),
            TextButton.icon(
              onPressed: () {
                setState(() => _showManualInput = true);
              },
              icon: const Icon(Icons.edit, size: 18),
              label: const Text('手动输入 WiFi 名称'),
            ),
          ],
        ),
      );
    }

    return Column(
      children: [
        // 列表头
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Row(
            children: [
              const Icon(Icons.wifi_find, size: 16, color: Colors.grey),
              const SizedBox(width: 8),
              Text(
                '${_apList.length} 个可用网络',
                style: const TextStyle(
                  fontSize: 13,
                  color: Colors.grey,
                  fontWeight: FontWeight.w500,
                ),
              ),
              const Spacer(),
              if (_scanning)
                const SizedBox(
                  width: 14,
                  height: 14,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
            ],
          ),
        ),
        // 列表
        Expanded(
          child: ListView.builder(
            controller: _scrollController,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            itemCount: _apList.length + 1, // +1 手动输入项
            itemBuilder: (context, index) {
              if (index == _apList.length) {
                return _buildManualEntryTile();
              }
              final ap = _apList[index];
              return _buildWifiTile(ap);
            },
          ),
        ),
      ],
    );
  }

  /// 单个 WiFi 列表项
  Widget _buildWifiTile(WiFiAccessPoint ap) {
    final isSelected = _selectedSsid == ap.ssid;
    final secured = _isSecured(ap);

    return Card(
      color: isSelected ? Colors.blue.withValues(alpha: 0.15) : null,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(10),
        side: isSelected
            ? BorderSide(color: Colors.blue.withValues(alpha: 0.5), width: 1.5)
            : BorderSide.none,
      ),
      margin: const EdgeInsets.symmetric(vertical: 3),
      child: ListTile(
        leading: Icon(
          _signalIcon(ap.level),
          color: _signalColor(ap.level),
          size: 28,
        ),
        title: Text(
          ap.ssid,
          style: TextStyle(
            fontWeight: isSelected ? FontWeight.w600 : FontWeight.normal,
          ),
        ),
        subtitle: Text(
          '${ap.level} dBm'
          '${secured ? "  ·  加密" : "  ·  开放"}'
          '${ap.frequency > 0 ? "  ·  ${ap.frequency}MHz" : ""}',
          style: TextStyle(fontSize: 11, color: Colors.grey.shade500),
        ),
        trailing: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (secured) const Icon(Icons.lock, size: 16, color: Colors.grey),
            if (isSelected)
              const Icon(Icons.check_circle, color: Colors.blue, size: 20),
          ],
        ),
        onTap: () => _selectWifi(ap.ssid),
      ),
    );
  }

  /// 手动输入 WiFi 名称选项
  Widget _buildManualEntryTile() {
    return Card(
      color: _showManualInput ? Colors.blue.withValues(alpha: 0.15) : null,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(10),
        side: _showManualInput
            ? BorderSide(color: Colors.blue.withValues(alpha: 0.5), width: 1.5)
            : BorderSide.none,
      ),
      margin: const EdgeInsets.symmetric(vertical: 3),
      child: ListTile(
        leading: const Icon(Icons.edit, color: Colors.grey, size: 28),
        title: const Text('手动输入 WiFi 名称'),
        subtitle: const Text('如果列表中没有你要找的网络', style: TextStyle(fontSize: 11)),
        trailing: _showManualInput
            ? const Icon(Icons.check_circle, color: Colors.blue, size: 20)
            : null,
        onTap: () {
          setState(() {
            _showManualInput = !_showManualInput;
            if (!_showManualInput) {
              _selectedSsid = null;
            } else {
              _selectedSsid = null;
            }
          });
        },
      ),
    );
  }

  /// 密码输入区域
  Widget _buildPasswordInput() {
    return SafeArea(
      top: false,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // 选中网络标题
          Row(
            children: [
              const Icon(Icons.wifi, color: Colors.blue, size: 20),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  _showManualInput ? '手动输入 WiFi' : '已选择: $_selectedSsid',
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
              IconButton(
                icon: const Icon(Icons.close, size: 20),
                onPressed: () {
                  setState(() {
                    _selectedSsid = null;
                    _showManualInput = false;
                    _passController.clear();
                  });
                },
              ),
            ],
          ),
          const SizedBox(height: 8),
          // 手动 SSID 输入
          if (_showManualInput) ...[
            TextField(
              controller: _manualSsidController,
              decoration: const InputDecoration(
                labelText: 'WiFi 名称 (SSID)',
                prefixIcon: Icon(Icons.wifi),
                border: OutlineInputBorder(),
                isDense: true,
              ),
            ),
            const SizedBox(height: 12),
          ],
          // 密码输入
          TextField(
            controller: _passController,
            obscureText: true,
            decoration: const InputDecoration(
              labelText: 'WiFi 密码',
              prefixIcon: Icon(Icons.lock),
              border: OutlineInputBorder(),
              isDense: true,
              hintText: '开放网络可留空',
            ),
            onSubmitted: (_) => _sendConfig(),
          ),
          const SizedBox(height: 16),
          // 发送按钮
          Row(
            children: [
              Expanded(
                child: FilledButton.icon(
                  onPressed: _status == _WifiStatus.sending
                      ? null
                      : _sendConfig,
                  icon: _status == _WifiStatus.sending
                      ? const SizedBox(
                          width: 18,
                          height: 18,
                          child: CircularProgressIndicator(
                            strokeWidth: 2,
                            color: Colors.white,
                          ),
                        )
                      : const Icon(Icons.send),
                  label: const Text('发送配置到 ESP32'),
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          // 重置按钮
          OutlinedButton.icon(
            onPressed: _resetWifi,
            icon: const Icon(Icons.refresh, size: 18),
            label: const Text('清除 ESP32 WiFi 配置'),
            style: OutlinedButton.styleFrom(
              minimumSize: const Size.fromHeight(40),
            ),
          ),
        ],
      ),
    );
  }

  /// 状态显示栏
  Widget _buildStatusBar() {
    final isError = _status == _WifiStatus.failed;
    final isConnecting = _status == _WifiStatus.connecting;
    final isSending = _status == _WifiStatus.sending;
    final isConnected = _status == _WifiStatus.connected;

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      color: isError
          ? Colors.red.withValues(alpha: 0.1)
          : isConnected
          ? Colors.green.withValues(alpha: 0.1)
          : isConnecting
          ? Colors.blue.withValues(alpha: 0.1)
          : isSending
          ? Colors.orange.withValues(alpha: 0.1)
          : Colors.grey.withValues(alpha: 0.05),
      child: Row(
        children: [
          Icon(
            isError
                ? Icons.error_outline
                : isConnected
                ? Icons.check_circle
                : isConnecting
                ? Icons.hourglass_top
                : isSending
                ? Icons.upload
                : Icons.info_outline,
            size: 18,
            color: isError
                ? Colors.red
                : isConnected
                ? Colors.green
                : isConnecting
                ? Colors.blue
                : isSending
                ? Colors.orange
                : Colors.grey,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              _statusDetail,
              style: TextStyle(
                fontSize: 13,
                color: isError
                    ? Colors.red
                    : isConnected
                    ? Colors.green
                    : isConnecting
                    ? Colors.blue
                    : isSending
                    ? Colors.orange
                    : Colors.grey,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
