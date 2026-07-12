import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:permission_handler/permission_handler.dart';
import '../../app/theme/app_colors.dart';
import '../../core/ble/ble_service_interface.dart';
import '../../core/ble/reactive_ble_service.dart';
import '../../shared/providers/ble_provider.dart';

/// 底部弹出的蓝牙连接面板（ModalBottomSheet）。
///
/// 包含：自动扫描、真实设备列表、连接/断开操作。
class BleConnectionSheet extends ConsumerStatefulWidget {
  const BleConnectionSheet({super.key});

  @override
  ConsumerState<BleConnectionSheet> createState() => _BleConnectionSheetState();
}

class _BleConnectionSheetState extends ConsumerState<BleConnectionSheet> {
  @override
  void initState() {
    super.initState();
    // 打开面板时自动开始扫描
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _autoScan();
    });
  }

  Future<void> _autoScan() async {
    final ble = ref.read(bleServiceProvider);
    if (ble is! ReactiveBleService) return;

    // Android 12+ 需要运行时权限
    if (Platform.isAndroid) {
      await [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.location,
      ].request();
    }
    await ble.startScan();
  }

  @override
  Widget build(BuildContext context) {
    final connAsync = ref.watch(connectionStateProvider);
    final state = connAsync.valueOrNull ?? BleConnectionState.disconnected;

    return SafeArea(
      child: Padding(
        padding: EdgeInsets.only(
          bottom: MediaQuery.of(context).viewInsets.bottom,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // 拖拽手柄
            Container(
              margin: const EdgeInsets.only(top: 12),
              width: 40,
              height: 4,
              decoration: BoxDecoration(
                color: AppColors.divider,
                borderRadius: BorderRadius.circular(2),
              ),
            ),
            const SizedBox(height: 16),
            // 标题
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(
                  state == BleConnectionState.connected
                      ? Icons.bluetooth_connected
                      : Icons.bluetooth,
                  size: 22,
                  color: state == BleConnectionState.connected
                      ? AppColors.motionPrimary
                      : AppColors.textSecondary,
                ),
                const SizedBox(width: 8),
                const Text(
                  '蓝牙连接',
                  style: TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.w700,
                    color: AppColors.textPrimary,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),
            // 状态内容
            Flexible(fit: FlexFit.loose, child: _buildContent(state)),
            const SizedBox(height: 24),
          ],
        ),
      ),
    );
  }

  Widget _buildContent(BleConnectionState state) {
    final ble = ref.read(bleServiceProvider);

    switch (state) {
      case BleConnectionState.disconnected:
      case BleConnectionState.failed:
        return _DeviceList(ble: ble, isScanning: false);
      case BleConnectionState.scanning:
        return _DeviceList(ble: ble, isScanning: true);
      case BleConnectionState.connecting:
      case BleConnectionState.reconnecting:
        return const _ConnectingView();
      case BleConnectionState.connected:
        return _ConnectedView(ble: ble);
    }
  }
}

/// 设备列表 — 扫描中实时显示已发现的设备
class _DeviceList extends StatelessWidget {
  final BleServiceInterface ble;
  final bool isScanning;

  const _DeviceList({required this.ble, required this.isScanning});

  @override
  Widget build(BuildContext context) {
    if (ble is! ReactiveBleService) {
      return const Padding(
        padding: EdgeInsets.all(24),
        child: Center(
          child: Text(
            'BLE 服务不可用',
            style: TextStyle(color: AppColors.textSecondary),
          ),
        ),
      );
    }
    final realBle = ble as ReactiveBleService;

    return StreamBuilder(
      stream: realBle.devicesStream,
      builder: (context, snapshot) {
        final devices = snapshot.data ?? [];

        return Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // 扫描进度条
            if (isScanning)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 8),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(
                        strokeWidth: 2.5,
                        color: AppColors.envPrimary,
                      ),
                    ),
                    const SizedBox(width: 10),
                    Text(
                      devices.isEmpty
                          ? '正在搜索附近设备...'
                          : '已发现 ${devices.length} 个设备',
                      style: const TextStyle(
                        color: AppColors.envPrimary,
                        fontSize: 13,
                      ),
                    ),
                  ],
                ),
              )
            else
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 8),
                child: OutlinedButton.icon(
                  onPressed: () => ble.startScan(),
                  icon: const Icon(Icons.refresh, size: 18),
                  label: const Text('重新扫描'),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: AppColors.envPrimary,
                    side: const BorderSide(color: AppColors.envPrimary),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(20),
                    ),
                  ),
                ),
              ),
            // 设备列表
            if (devices.isEmpty && !isScanning)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 24),
                child: Column(
                  children: [
                    Icon(
                      Icons.bluetooth_searching,
                      size: 48,
                      color: AppColors.envPrimary,
                    ),
                    SizedBox(height: 12),
                    Text(
                      '点击重新扫描',
                      style: TextStyle(
                        color: AppColors.textSecondary,
                        fontSize: 14,
                      ),
                    ),
                  ],
                ),
              )
            else if (devices.isNotEmpty)
              ConstrainedBox(
                constraints: BoxConstraints(
                  maxHeight: MediaQuery.of(context).size.height * 0.35,
                ),
                child: ListView.builder(
                  shrinkWrap: true,
                  itemCount: devices.length,
                  itemBuilder: (context, index) {
                    final device = devices[index];
                    final isPF = device.name.contains('PathFinder');
                    return ListTile(
                      leading: Icon(
                        isPF ? Icons.devices_other : Icons.bluetooth,
                        color: isPF
                            ? AppColors.envPrimary
                            : AppColors.textSecondary,
                        size: 28,
                      ),
                      title: Text(
                        device.name.isNotEmpty ? device.name : '未知设备',
                        style: TextStyle(
                          fontWeight: isPF
                              ? FontWeight.w600
                              : FontWeight.normal,
                          color: isPF
                              ? AppColors.envPrimary
                              : AppColors.textPrimary,
                        ),
                      ),
                      subtitle: Text(
                        '${device.id}  •  RSSI: ${device.rssi}dBm',
                        style: const TextStyle(
                          fontSize: 11,
                          color: AppColors.textSecondary,
                        ),
                      ),
                      trailing: isPF
                          ? ElevatedButton(
                              onPressed: () {
                                ble.stopScan();
                                ble.connect(device.id);
                              },
                              style: ElevatedButton.styleFrom(
                                backgroundColor: AppColors.envPrimary,
                                foregroundColor: Colors.white,
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 16,
                                  vertical: 6,
                                ),
                                shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(16),
                                ),
                              ),
                              child: const Text('连接'),
                            )
                          : null,
                      onTap: isPF
                          ? () {
                              ble.stopScan();
                              ble.connect(device.id);
                            }
                          : null,
                    );
                  },
                ),
              ),
          ],
        );
      },
    );
  }
}

/// 连接中状态视图
class _ConnectingView extends StatelessWidget {
  const _ConnectingView();

  @override
  Widget build(BuildContext context) {
    return const Padding(
      padding: EdgeInsets.symmetric(vertical: 32),
      child: Column(
        children: [
          SizedBox(
            width: 32,
            height: 32,
            child: CircularProgressIndicator(
              strokeWidth: 3,
              color: AppColors.warning,
            ),
          ),
          SizedBox(height: 16),
          Text(
            '正在连接设备...',
            style: TextStyle(color: AppColors.warning, fontSize: 14),
          ),
        ],
      ),
    );
  }
}

/// 已连接状态视图
class _ConnectedView extends StatelessWidget {
  final BleServiceInterface ble;

  const _ConnectedView({required this.ble});

  @override
  Widget build(BuildContext context) {
    final sensors = [
      ('AHT20', AppColors.motionPrimary),
      ('BMP280', AppColors.motionPrimary),
      ('MPU6050', AppColors.motionPrimary),
      ('UV', AppColors.motionPrimary),
    ];

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(
            Icons.bluetooth_connected,
            size: 56,
            color: AppColors.motionPrimary,
          ),
          const SizedBox(height: 16),
          const Text(
            '已连接 PathFinder-EMOTE',
            style: TextStyle(
              color: AppColors.textPrimary,
              fontSize: 16,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            alignment: WrapAlignment.center,
            children: sensors
                .map(
                  (s) => Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 10,
                      vertical: 4,
                    ),
                    decoration: BoxDecoration(
                      color: s.$2.withValues(alpha: 0.12),
                      borderRadius: BorderRadius.circular(12),
                      border: Border.all(color: s.$2.withValues(alpha: 0.3)),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(Icons.check_circle, size: 14, color: s.$2),
                        const SizedBox(width: 4),
                        Text(
                          s.$1,
                          style: TextStyle(
                            color: s.$2,
                            fontSize: 12,
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                )
                .toList(),
          ),
          const SizedBox(height: 24),
          OutlinedButton.icon(
            onPressed: () => ble.disconnect(),
            icon: const Icon(Icons.bluetooth_disabled, size: 20),
            label: const Text('断开连接'),
            style: OutlinedButton.styleFrom(
              foregroundColor: AppColors.urgent,
              side: const BorderSide(color: AppColors.urgent, width: 1.5),
              padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 12),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(24),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
