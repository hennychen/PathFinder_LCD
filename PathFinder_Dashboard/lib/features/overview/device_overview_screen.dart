import 'package:flutter/material.dart';
import '../../app/theme/app_colors.dart';

/// 设备能力概览页面
///
/// 整合 PathFinder 双板架构 (A板 EMOTE + B板 Tracker) 的完整功能矩阵，
/// 展示硬件规格、传感器配置、通信协议和 BLE 特征值映射。
class DeviceOverviewScreen extends StatelessWidget {
  const DeviceOverviewScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('设备能力概览')),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _buildArchitectureDiagram(),
            const SizedBox(height: 24),
            _buildBoardCard(
              title: 'B板 — PathFinder Tracker',
              subtitle: 'xiaozhi-esp32 · ESP32-S3-N16R8',
              color: AppColors.envText,
              icon: Icons.track_changes,
              sections: const [
                _BoardSection(
                  title: '音频架构 (AcousticEye V1.0)',
                  items: [
                    _SpecItem('ES7210', '4-ch TDM ADC @24kHz · 声源定位'),
                    _SpecItem('ES8311', 'DAC @24kHz → NS4150B 功放'),
                    _SpecItem('I2S', 'MCLK=42 BCLK=41 WS=40 DIN=21 DOUT=3'),
                    _SpecItem('GCC-PHAT', '256-FFT · 50mm 麦克间距 · 自适应底噪'),
                  ],
                ),
                _BoardSection(
                  title: '视觉追踪',
                  items: [
                    _SpecItem('OV2640', 'DVP 16MHz XCLK · 320x240'),
                    _SpecItem('ESP-DL', 'MSRMNP 人脸检测 · PID 双轴'),
                    _SpecItem('流水线', '采集→推理→舵机 跨核 pipeline'),
                    _SpecItem('帧率', '目标 14-18 FPS'),
                  ],
                ),
                _BoardSection(
                  title: '云台舵机',
                  items: [
                    _SpecItem('MG90S ×2', 'Pan=GPIO47 Tilt=GPIO14'),
                    _SpecItem('LEDC', '50Hz 14-bit · 600-2400μs 脉宽'),
                    _SpecItem('安全范围', 'Pan 30-150° · Tilt 30-120°'),
                    _SpecItem('平滑', '100Hz 后台任务 · 死区 8°'),
                  ],
                ),
                _BoardSection(
                  title: 'AI 对话 (xiaozhi)',
                  items: [
                    _SpecItem('WakeNet', '语音唤醒 · 24k→16k 重采样'),
                    _SpecItem('LLM', '云端大模型对话'),
                    _SpecItem('TTS', 'ES8311 播报 → NS4150B'),
                    _SpecItem('MCP', '6 servo + 2 face + 2 sound 工具'),
                  ],
                ),
              ],
            ),
            const SizedBox(height: 20),
            _buildBoardCard(
              title: 'A板 — PathFinder EMOTE',
              subtitle: 'ESP32-S3 · 2.1" 圆形 LCD (ST7701S)',
              color: AppColors.warning,
              icon: Icons.dashboard_customize,
              sections: const [
                _BoardSection(
                  title: '传感器矩阵',
                  items: [
                    _SpecItem('AHT20', '温度 ±0.3°C · 湿度 ±2%'),
                    _SpecItem('BMP280', '气压 · 海拔 · P0 校准'),
                    _SpecItem('MPU9250', '6轴 IMU · 互补滤波 pitch/roll'),
                    _SpecItem('QMC5883L', '罗盘方位角 · 3轴磁力计'),
                    _SpecItem('GUVA-S12SD', 'UV 指数 · ADC'),
                    _SpecItem('GP2Y1010AU0F', '粉尘 · AQI 5级'),
                  ],
                ),
                _BoardSection(
                  title: '显示与 UI',
                  items: [
                    _SpecItem('LCD', '480×480 圆形 ST7701S · PCLK 10MHz'),
                    _SpecItem('飞行仪表', '姿态指引仪 · 指南针 · 海拔'),
                    _SpecItem('EMA 胶囊', '环境/运动数据浮层 · 异常告警'),
                    _SpecItem('字幕', 'B板对话文本实时显示'),
                    _SpecItem('表情', '24种 EAF 动画 · LLM 情感联动'),
                  ],
                ),
                _BoardSection(
                  title: '通信枢纽',
                  items: [
                    _SpecItem('BLE', 'NimBLE GATT · C2-C7 特征值'),
                    _SpecItem('ESP-NOW', 'B板 ← 声源/人脸/对话数据'),
                    _SpecItem('WiFi', 'APSTA · Captive Portal 配网'),
                    _SpecItem('Mesh', 'ROOT 节点 · 转发传感器数据'),
                  ],
                ),
              ],
            ),
            const SizedBox(height: 24),
            _buildBleReference(),
            const SizedBox(height: 24),
            _buildMeshProtocolRef(),
          ],
        ),
      ),
    );
  }

  Widget _buildArchitectureDiagram() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.divider),
      ),
      child: const Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            '系统架构',
            style: TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          SizedBox(height: 16),
          // 架构流程图
          _ArchNode(
            icon: Icons.mic,
            title: 'B板 Tracker',
            subtitle: '声源定位 · 人脸追踪 · AI对话 · 云台',
            color: AppColors.envText,
          ),
          _ArchConnector(label: 'ESP-NOW / ESP-WIFI-MESH'),
          _ArchNode(
            icon: Icons.tv,
            title: 'A板 EMOTE',
            subtitle: '传感器 · LCD显示 · 表情 · BLE',
            color: AppColors.warning,
          ),
          _ArchConnector(label: 'BLE GATT (C2-C7)'),
          _ArchNode(
            icon: Icons.phone_android,
            title: 'Dashboard App',
            subtitle: '实时监控 · 历史记录 · WiFi配网',
            color: AppColors.motionText,
          ),
        ],
      ),
    );
  }

  Widget _buildBoardCard({
    required String title,
    required String subtitle,
    required Color color,
    required IconData icon,
    required List<_BoardSection> sections,
  }) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 24, color: color),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: color,
                      ),
                    ),
                    Text(
                      subtitle,
                      style: const TextStyle(
                        fontSize: 11,
                        color: AppColors.textSecondary,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          ...sections.expand(
            (s) => [
              Padding(
                padding: const EdgeInsets.only(top: 12, bottom: 6),
                child: Text(
                  s.title,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: color.withValues(alpha: 0.8),
                  ),
                ),
              ),
              ...s.items.map((item) => _SpecRow(item: item)),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildBleReference() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.divider),
      ),
      child: const Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.bluetooth, size: 20, color: AppColors.envText),
              SizedBox(width: 8),
              Text(
                'BLE GATT 特征值',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textPrimary,
                ),
              ),
            ],
          ),
          SizedBox(height: 12),
          _BleCharRow(
            uuid: 'C2 (fe02)',
            name: '环境数据',
            spec: '20B · temp/humi/press/alt/uv/dust/aqi · @1Hz',
          ),
          _BleCharRow(
            uuid: 'C3 (fe03)',
            name: '运动数据',
            spec: '8B · pitch/roll/accel/event/conf · @10Hz',
          ),
          _BleCharRow(
            uuid: 'C4 (fe04)',
            name: '表情状态',
            spec: '15B · emote_id/name/trigger · on-change',
          ),
          _BleCharRow(
            uuid: 'C5 (fe05)',
            name: 'WiFi 配网',
            spec: 'Write+Notify · JSON 配网指令/状态',
          ),
          _BleCharRow(
            uuid: 'C6 (fe06)',
            name: '罗盘方位',
            spec: 'heading/valid/source · @10Hz',
          ),
          _BleCharRow(
            uuid: 'C7 (fe07)',
            name: '追踪聚合',
            spec: '15B · sound_angle/face_info/track_state · @5Hz',
          ),
        ],
      ),
    );
  }

  Widget _buildMeshProtocolRef() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.divider),
      ),
      child: const Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                Icons.wifi_tethering,
                size: 20,
                color: AppColors.trackerText,
              ),
              SizedBox(width: 8),
              Text(
                'ESP-NOW Mesh 协议',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textPrimary,
                ),
              ),
            ],
          ),
          SizedBox(height: 12),
          Text(
            '帧格式: [TYPE(1)][SEQ(1)][LEN(1)][PAYLOAD(0-246)][CRC8(1)]',
            style: TextStyle(
              fontSize: 11,
              fontFamily: 'monospace',
              color: AppColors.textSecondary,
            ),
          ),
          SizedBox(height: 12),
          _MeshMsgRow(
            code: '0x01',
            name: 'MSG_ANGLE_DATA',
            dir: 'B→A',
            desc: '声源角度 u16×10 + valid',
          ),
          _MeshMsgRow(
            code: '0x02',
            name: 'MSG_TRACK_STATE',
            dir: 'B→A',
            desc: '追踪状态机',
          ),
          _MeshMsgRow(
            code: '0x03',
            name: 'MSG_FACE_INFO',
            dir: 'B→A',
            desc: '人脸 found/cx/cy/w/h',
          ),
          _MeshMsgRow(
            code: '0x04',
            name: 'MSG_SERVO_CTRL',
            dir: 'A→B',
            desc: 'Pan/Tilt 舵机角度',
          ),
          _MeshMsgRow(
            code: '0x10',
            name: 'MSG_HEARTBEAT',
            dir: 'B→A',
            desc: '心跳 @500ms',
          ),
          _MeshMsgRow(
            code: '0x20',
            name: 'MSG_SENSOR_DATA',
            dir: 'A→B',
            desc: '传感器聚合包 ~72B',
          ),
          _MeshMsgRow(
            code: '0x21',
            name: 'MSG_DIALOG_STATE',
            dir: 'B→A',
            desc: '对话状态 idle/listen/speak',
          ),
          _MeshMsgRow(
            code: '0x22',
            name: 'MSG_EMOTION',
            dir: 'B→A',
            desc: 'LLM 情感标签',
          ),
          _MeshMsgRow(
            code: '0x23',
            name: 'MSG_CHAT_TEXT',
            dir: 'B→A',
            desc: '字幕文本 UTF-8',
          ),
          _MeshMsgRow(
            code: '0x30',
            name: 'MSG_WIFI_CONFIG',
            dir: 'A→B',
            desc: 'WiFi 配网 JSON',
          ),
          _MeshMsgRow(
            code: '0x31',
            name: 'MSG_WIFI_STATUS',
            dir: 'B→A',
            desc: 'WiFi 连接状态 + IP',
          ),
        ],
      ),
    );
  }
}

class _BoardSection {
  final String title;
  final List<_SpecItem> items;

  const _BoardSection({required this.title, required this.items});
}

class _SpecItem {
  final String name;
  final String desc;

  const _SpecItem(this.name, this.desc);
}

class _SpecRow extends StatelessWidget {
  final _SpecItem item;

  const _SpecRow({required this.item});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 100,
            child: Text(
              item.name,
              style: const TextStyle(
                fontSize: 11,
                fontWeight: FontWeight.w600,
                color: AppColors.textPrimary,
              ),
            ),
          ),
          Expanded(
            child: Text(
              item.desc,
              style: const TextStyle(
                fontSize: 11,
                color: AppColors.textSecondary,
                height: 1.4,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _ArchNode extends StatelessWidget {
  final IconData icon;
  final String title;
  final String subtitle;
  final Color color;

  const _ArchNode({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 20, color: color),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w700,
                    color: color,
                  ),
                ),
                Text(
                  subtitle,
                  style: const TextStyle(
                    fontSize: 10,
                    color: AppColors.textSecondary,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _ArchConnector extends StatelessWidget {
  final String label;
  const _ArchConnector({required this.label});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(Icons.arrow_downward, size: 14, color: AppColors.divider),
          const SizedBox(width: 4),
          Text(
            label,
            style: const TextStyle(
              fontSize: 10,
              fontFamily: 'monospace',
              color: AppColors.textSecondary,
            ),
          ),
        ],
      ),
    );
  }
}

class _BleCharRow extends StatelessWidget {
  final String uuid;
  final String name;
  final String spec;

  const _BleCharRow({
    required this.uuid,
    required this.name,
    required this.spec,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
            decoration: BoxDecoration(
              color: AppColors.envPrimary.withValues(alpha: 0.15),
              borderRadius: BorderRadius.circular(6),
            ),
            child: Text(
              uuid,
              style: const TextStyle(
                fontSize: 10,
                fontWeight: FontWeight.w600,
                fontFamily: 'monospace',
                color: AppColors.envText,
              ),
            ),
          ),
          const SizedBox(width: 10),
          Text(
            name,
            style: const TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              spec,
              style: const TextStyle(
                fontSize: 10,
                color: AppColors.textSecondary,
                fontFamily: 'monospace',
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _MeshMsgRow extends StatelessWidget {
  final String code;
  final String name;
  final String dir;
  final String desc;

  const _MeshMsgRow({
    required this.code,
    required this.name,
    required this.dir,
    required this.desc,
  });

  @override
  Widget build(BuildContext context) {
    final isB2A = dir == 'B→A';
    final color = isB2A ? AppColors.envText : AppColors.trackerText;

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          SizedBox(
            width: 40,
            child: Text(
              code,
              style: TextStyle(
                fontSize: 10,
                fontWeight: FontWeight.w600,
                fontFamily: 'monospace',
                color: color,
              ),
            ),
          ),
          SizedBox(
            width: 28,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
              decoration: BoxDecoration(
                color: color.withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(4),
              ),
              child: Text(
                dir,
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 9,
                  fontWeight: FontWeight.w600,
                  color: color,
                ),
              ),
            ),
          ),
          const SizedBox(width: 8),
          SizedBox(
            width: 140,
            child: Text(
              name,
              style: const TextStyle(
                fontSize: 10,
                fontFamily: 'monospace',
                color: AppColors.textPrimary,
              ),
            ),
          ),
          Expanded(
            child: Text(
              desc,
              style: const TextStyle(
                fontSize: 10,
                color: AppColors.textSecondary,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
