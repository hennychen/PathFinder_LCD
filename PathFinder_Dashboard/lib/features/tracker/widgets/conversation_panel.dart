import 'package:flutter/material.dart';
import '../../../app/theme/app_colors.dart';

/// B板对话同步能力面板
///
/// B板运行 xiaozhi-esp32 固件，具备 LLM 语音对话能力。
/// 通过 ESP-NOW/Mesh 向 A板同步三类对话数据：
///
///   MSG_DIALOG_STATE (0x21) — 对话状态机
///     0=idle 空闲, 1=listening 听取, 2=speaking 播报, 3=connecting 连接中
///
///   MSG_EMOTION (0x22) — LLM 情感标签
///     ASCII string, 如 "happy", "neutral", "sad"
///     A板 emote_engine 据此覆盖表情动画
///
///   MSG_CHAT_TEXT (0x23) — 字幕文本
///     UTF-8 中英文混合, ≤246B/帧
///     A板 subtitle_view 在 LCD 底部显示
///
/// 注：以上数据在 A板 LCD 上实时显示。
///    Dashboard 通过 BLE C4 (表情) 间接收到的情感变化。
class ConversationPanel extends StatelessWidget {
  const ConversationPanel({super.key});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.envPrimary.withValues(alpha: 0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 标题
          Row(
            children: [
              Icon(Icons.record_voice_over, size: 20, color: AppColors.envText),
              const SizedBox(width: 8),
              const Text(
                'AI 对话同步',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textPrimary,
                ),
              ),
              const Spacer(),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: AppColors.envPrimary.withValues(alpha: 0.2),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  'B板 xiaozhi',
                  style: TextStyle(
                    fontSize: 10,
                    fontWeight: FontWeight.w600,
                    color: AppColors.envText,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          const Text(
            'B板运行小智 AI 固件，支持语音唤醒、LLM 对话、TTS 播报。'
            '对话状态和情感通过 Mesh 同步到 A板 LCD 显示。',
            style: TextStyle(
              fontSize: 12,
              height: 1.5,
              color: AppColors.textSecondary,
            ),
          ),
          const SizedBox(height: 16),
          // 对话状态机
          const Text(
            '对话状态机 (MSG_DIALOG_STATE)',
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: AppColors.envText,
            ),
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: const [
              _DialogStateChip(
                code: '0',
                label: 'idle',
                cnLabel: '空闲',
                color: AppColors.textSecondary,
              ),
              _DialogStateChip(
                code: '1',
                label: 'listening',
                cnLabel: '听取',
                color: AppColors.envText,
              ),
              _DialogStateChip(
                code: '2',
                label: 'speaking',
                cnLabel: '播报',
                color: AppColors.motionText,
              ),
              _DialogStateChip(
                code: '3',
                label: 'connecting',
                cnLabel: '连接中',
                color: AppColors.warningText,
              ),
            ],
          ),
          const SizedBox(height: 16),
          // 数据流图
          _buildDataFlow(),
          const SizedBox(height: 12),
          // MCP 工具能力
          const Text(
            'MCP 工具集 (AI 可调用)',
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: AppColors.trackerText,
            ),
          ),
          const SizedBox(height: 8),
          const _McpToolItem(
            name: 'self.servo.set_mode',
            desc: '切换追踪模式 (auto/face/manual/idle)',
          ),
          const _McpToolItem(
            name: 'self.servo.get_status',
            desc: '查询 Pan/Tilt 角度、模式、声源',
          ),
          const _McpToolItem(name: 'self.servo.look_at_sound', desc: '转向声源方向'),
          const _McpToolItem(
            name: 'self.servo.center',
            desc: '云台回正 (Pan=90 Tilt=90)',
          ),
          const _McpToolItem(
            name: 'self.face.start / stop',
            desc: '启停 ESP-DL 人脸追踪',
          ),
          const _McpToolItem(
            name: 'self.sound.locate / status',
            desc: '声源定位查询',
          ),
        ],
      ),
    );
  }

  Widget _buildDataFlow() {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppColors.background,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.divider),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // B板行
          _FlowNode(
            icon: Icons.mic,
            name: 'B板 xiaozhi',
            subtitle: 'ES7210 四麦克 → WakeNet → LLM → TTS',
            color: AppColors.envText,
          ),
          const _FlowArrow(label: 'ESP-NOW / Mesh'),
          // A板行
          _FlowNode(
            icon: Icons.tv,
            name: 'A板 EMOTE',
            subtitle: '字幕显示 · 表情联动 · BLE 推送',
            color: AppColors.trackerText,
          ),
          const _FlowArrow(label: 'BLE GATT'),
          // 手机行
          _FlowNode(
            icon: Icons.phone_android,
            name: 'Dashboard',
            subtitle: 'C4 表情 · C7 追踪数据',
            color: AppColors.motionText,
          ),
        ],
      ),
    );
  }
}

class _DialogStateChip extends StatelessWidget {
  final String code;
  final String label;
  final String cnLabel;
  final Color color;

  const _DialogStateChip({
    required this.code,
    required this.label,
    required this.cnLabel,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            code,
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              color: color,
              fontFamily: 'monospace',
            ),
          ),
          const SizedBox(width: 6),
          Text(
            label,
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w600,
              color: color,
              fontFamily: 'monospace',
            ),
          ),
          const SizedBox(width: 4),
          Text(
            cnLabel,
            style: TextStyle(fontSize: 11, color: color.withValues(alpha: 0.7)),
          ),
        ],
      ),
    );
  }
}

class _FlowNode extends StatelessWidget {
  final IconData icon;
  final String name;
  final String subtitle;
  final Color color;

  const _FlowNode({
    required this.icon,
    required this.name,
    required this.subtitle,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 18, color: color),
        const SizedBox(width: 8),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                name,
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w600,
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
    );
  }
}

class _FlowArrow extends StatelessWidget {
  final String label;
  const _FlowArrow({required this.label});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4, horizontal: 8),
      child: Row(
        children: [
          const Icon(Icons.arrow_downward, size: 12, color: AppColors.divider),
          const SizedBox(width: 4),
          Text(
            label,
            style: const TextStyle(
              fontSize: 10,
              color: AppColors.textSecondary,
              fontFamily: 'monospace',
            ),
          ),
        ],
      ),
    );
  }
}

class _McpToolItem extends StatelessWidget {
  final String name;
  final String desc;

  const _McpToolItem({required this.name, required this.desc});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            margin: const EdgeInsets.only(top: 2),
            width: 4,
            height: 4,
            decoration: const BoxDecoration(
              color: AppColors.trackerPrimary,
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  name,
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    color: AppColors.trackerText,
                    fontFamily: 'monospace',
                  ),
                ),
                Text(
                  desc,
                  style: const TextStyle(
                    fontSize: 11,
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
