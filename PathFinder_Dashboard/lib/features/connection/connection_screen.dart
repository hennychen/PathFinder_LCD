import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:permission_handler/permission_handler.dart';
import '../../shared/providers/ble_provider.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../core/ble/reactive_ble_service.dart';
import '../../app/theme/app_colors.dart';
import 'widgets/device_tile.dart';

class ConnectionScreen extends ConsumerStatefulWidget {
  const ConnectionScreen({super.key});

  @override
  ConsumerState<ConnectionScreen> createState() => _ConnectionScreenState();
}

class _ConnectionScreenState extends ConsumerState<ConnectionScreen> {
  List<DiscoveredDevice> _devices = [];

  @override
  Widget build(BuildContext context) {
    final connState = ref.watch(connectionStateProvider);
    final ble = ref.read(bleServiceProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('PathFinder Dashboard')),
      body: connState.when(
        data: (state) => _buildBody(state, ble),
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (e, _) => Center(child: Text('Error: $e')),
      ),
    );
  }

  Widget _buildBody(BleConnectionState state, BleServiceInterface ble) {
    switch (state) {
      case BleConnectionState.disconnected:
      case BleConnectionState.scanning:
        return _buildScanView(state, ble);
      case BleConnectionState.connecting:
      case BleConnectionState.reconnecting:
        return const Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              CircularProgressIndicator(color: AppColors.envPrimary),
              SizedBox(height: 16),
              Text('连接中...', style: TextStyle(color: AppColors.textPrimary)),
            ],
          ),
        );
      case BleConnectionState.connected:
        return _buildConnectedView(ble);
      case BleConnectionState.failed:
        return Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const Icon(
                Icons.error_outline,
                size: 64,
                color: AppColors.urgent,
              ),
              const SizedBox(height: 16),
              const Text('连接失败', style: TextStyle(color: AppColors.urgent)),
              const SizedBox(height: 24),
              ElevatedButton.icon(
                onPressed: () => _startScan(ble),
                icon: const Icon(Icons.refresh),
                label: const Text('重试'),
              ),
            ],
          ),
        );
      default:
        return const Center(child: Text('Unknown state'));
    }
  }

  Widget _buildScanView(BleConnectionState state, BleServiceInterface ble) {
    final isScanning = state == BleConnectionState.scanning;

    // 监听扫描结果
    if (ble is ReactiveBleService) {
      return StreamBuilder<List<DiscoveredDevice>>(
        stream: ble.devicesStream,
        builder: (context, snapshot) {
          _devices = snapshot.data ?? [];
          return Column(
            children: [
              // 扫描按钮区
              Padding(
                padding: const EdgeInsets.all(16),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(
                      isScanning ? Icons.bluetooth_searching : Icons.bluetooth,
                      size: 48,
                      color: AppColors.envPrimary,
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Text(
                        isScanning
                            ? '正在扫描 PathFinder 设备...'
                            : _devices.isEmpty
                            ? '点击下方按钮扫描设备'
                            : '发现 ${_devices.length} 台设备',
                        style: const TextStyle(color: AppColors.textSecondary),
                      ),
                    ),
                  ],
                ),
              ),
              // 设备列表
              Expanded(
                child: _devices.isEmpty
                    ? Center(
                        child: isScanning
                            ? const CircularProgressIndicator(
                                color: AppColors.envPrimary,
                              )
                            : const Text(
                                '暂无设备',
                                style: TextStyle(
                                  color: AppColors.textSecondary,
                                ),
                              ),
                      )
                    : ListView.builder(
                        itemCount: _devices.length,
                        itemBuilder: (context, index) {
                          final dev = _devices[index];
                          return DeviceTile(
                            name: dev.name.isNotEmpty
                                ? dev.name
                                : 'Unknown Device',
                            rssi: dev.rssi,
                            onTap: () => ble.connect(dev.id),
                          );
                        },
                      ),
              ),
              // 扫描按钮
              Padding(
                padding: const EdgeInsets.all(16),
                child: ElevatedButton.icon(
                  onPressed: isScanning ? null : () => _startScan(ble),
                  icon: const Icon(Icons.search),
                  label: Text(isScanning ? '扫描中...' : '扫描设备'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: AppColors.envPrimary,
                    minimumSize: const Size(double.infinity, 48),
                  ),
                ),
              ),
            ],
          );
        },
      );
    }

    // Fallback: Mock 服务（不显示扫描结果）
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(
            Icons.bluetooth_searching,
            size: 64,
            color: AppColors.envPrimary,
          ),
          const SizedBox(height: 16),
          const Text(
            '点击扫描附近设备',
            style: TextStyle(color: AppColors.textSecondary),
          ),
          const SizedBox(height: 24),
          ElevatedButton.icon(
            onPressed: () => _startScan(ble),
            icon: const Icon(Icons.refresh),
            label: const Text('扫描设备'),
            style: ElevatedButton.styleFrom(
              backgroundColor: AppColors.envPrimary,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildConnectedView(BleServiceInterface ble) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(
            Icons.bluetooth_connected,
            size: 64,
            color: AppColors.motionPrimary,
          ),
          const SizedBox(height: 16),
          const Text(
            '已连接 PathFinder-EMOTE',
            style: TextStyle(color: AppColors.textPrimary, fontSize: 18),
          ),
          const SizedBox(height: 8),
          const Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text(
                '✓AHT20',
                style: TextStyle(color: AppColors.motionPrimary, fontSize: 12),
              ),
              SizedBox(width: 8),
              Text(
                '✓BMP280',
                style: TextStyle(color: AppColors.motionPrimary, fontSize: 12),
              ),
              SizedBox(width: 8),
              Text(
                '✓MPU6050',
                style: TextStyle(color: AppColors.motionPrimary, fontSize: 12),
              ),
              SizedBox(width: 8),
              Text(
                '✓UV',
                style: TextStyle(color: AppColors.motionPrimary, fontSize: 12),
              ),
            ],
          ),
          const SizedBox(height: 24),
          OutlinedButton.icon(
            onPressed: () => ble.disconnect(),
            icon: const Icon(Icons.bluetooth_disabled),
            label: const Text('断开连接'),
            style: OutlinedButton.styleFrom(foregroundColor: AppColors.urgent),
          ),
        ],
      ),
    );
  }

  void _startScan(BleServiceInterface ble) async {
    // 请求 BLE 权限
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.location,
    ].request();

    setState(() => _devices.clear());
    ble.startScan();
  }
}
