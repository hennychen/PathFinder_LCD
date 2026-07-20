import 'dart:async';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// 实时摄像头预览组件。
///
/// 通过 HTTP 轮询 B板 HTTP Server (port 8080) 获取 JPEG 图片。
/// B板必须已连接 WiFi 路由器并启动 camera_http_server。
///
/// IP 地址可通过右上角设置按钮手动配置，存储在 SharedPreferences。
class CameraPreview extends StatefulWidget {
  final double aspectRatio;

  const CameraPreview({super.key, this.aspectRatio = 4 / 3});

  @override
  State<CameraPreview> createState() => _CameraPreviewState();
}

class _CameraPreviewState extends State<CameraPreview> {
  static const String _prefKey = 'tracker_cam_ip';
  static const String _defaultIp = '192.168.10.184';
  static const int _defaultPort = 8080;

  Timer? _timer;
  int _tick = 0;
  bool _error = false;
  bool _loading = true;
  int _fpsCount = 0;
  DateTime _fpsStart = DateTime.now();
  double _currentFps = 0;

  String _ip = _defaultIp;
  final int _port = _defaultPort;
  late String _baseUrl;

  @override
  void initState() {
    super.initState();
    _loadSavedIp();
  }

  Future<void> _loadSavedIp() async {
    final prefs = await SharedPreferences.getInstance();
    final savedIp = prefs.getString(_prefKey);
    if (savedIp != null && savedIp.isNotEmpty) {
      _ip = savedIp;
    }
    _updateBaseUrl();
    _startTimer();
  }

  void _updateBaseUrl() {
    _baseUrl = 'http://$_ip:$_port';
  }

  Future<void> _saveIp(String ip) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefKey, ip);
    setState(() {
      _ip = ip;
      _updateBaseUrl();
      _tick = 0;
      _error = false;
      _loading = true;
    });
  }

  void _startTimer() {
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(milliseconds: 300), (_) {
      if (mounted) {
        setState(() {
          _tick++;
          _fpsCount++;
          final elapsed = DateTime.now().difference(_fpsStart).inMilliseconds;
          if (elapsed >= 1000) {
            _currentFps = (_fpsCount * 1000.0 / elapsed).roundToDouble();
            _fpsCount = 0;
            _fpsStart = DateTime.now();
          }
        });
      }
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  void _showIpEditDialog() {
    final controller = TextEditingController(text: _ip);
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('设置 B板 IP 地址'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextField(
              controller: controller,
              decoration: const InputDecoration(
                labelText: 'IP 地址',
                hintText: '192.168.10.184',
                border: OutlineInputBorder(),
              ),
              keyboardType: TextInputType.number,
            ),
            const SizedBox(height: 12),
            Text(
              '端口: $_defaultPort (固定)\n'
              'B板需已连同一 WiFi 路由器\n'
              '运行 xiaozhi-esp32 固件',
              style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () {
              final newIp = controller.text.trim();
              if (newIp.isNotEmpty) {
                _saveIp(newIp);
                Navigator.pop(ctx);
              }
            },
            child: const Text('保存'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.black,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white.withValues(alpha: 0.1)),
      ),
      child: ClipRRect(
        borderRadius: BorderRadius.circular(12),
        child: AspectRatio(
          aspectRatio: widget.aspectRatio,
          child: Stack(
            fit: StackFit.expand,
            children: [
              if (_tick > 0)
                Image.network(
                  '$_baseUrl/cam?t=$_tick',
                  fit: BoxFit.contain,
                  gaplessPlayback: true,
                  loadingBuilder: (context, child, loadingProgress) {
                    if (loadingProgress == null) {
                      if (_error || _loading) {
                        WidgetsBinding.instance.addPostFrameCallback((_) {
                          if (mounted) {
                            setState(() {
                              _error = false;
                              _loading = false;
                            });
                          }
                        });
                      }
                      return child;
                    }
                    return const ColoredBox(
                      color: Colors.black,
                      child: Center(
                        child: SizedBox(
                          width: 24,
                          height: 24,
                          child: CircularProgressIndicator(
                            strokeWidth: 2,
                            color: Colors.white54,
                          ),
                        ),
                      ),
                    );
                  },
                  errorBuilder: (context, error, stackTrace) {
                    if (!_error) {
                      WidgetsBinding.instance.addPostFrameCallback((_) {
                        if (mounted) {
                          setState(() => _error = true);
                        }
                      });
                    }
                    return _buildErrorView();
                  },
                )
              else
                const ColoredBox(
                  color: Colors.black,
                  child: Center(
                    child: SizedBox(
                      width: 24,
                      height: 24,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: Colors.white54,
                      ),
                    ),
                  ),
                ),
              // FPS / 状态标签
              Positioned(
                top: 6,
                right: 8,
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 6,
                    vertical: 2,
                  ),
                  decoration: BoxDecoration(
                    color: Colors.black.withValues(alpha: 0.6),
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: Text(
                    _error
                        ? 'OFFLINE'
                        : _loading
                        ? '...'
                        : '${_currentFps.toStringAsFixed(0)} FPS',
                    style: TextStyle(
                      color: _error
                          ? Colors.redAccent
                          : _loading
                          ? Colors.white54
                          : Colors.greenAccent,
                      fontSize: 10,
                      fontFamily: 'monospace',
                    ),
                  ),
                ),
              ),
              // IP 编辑按钮
              Positioned(
                top: 6,
                left: 8,
                child: GestureDetector(
                  onTap: _showIpEditDialog,
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 6,
                      vertical: 2,
                    ),
                    decoration: BoxDecoration(
                      color: Colors.black.withValues(alpha: 0.6),
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        const Icon(
                          Icons.settings,
                          size: 10,
                          color: Colors.white70,
                        ),
                        const SizedBox(width: 3),
                        Text(
                          '$_ip:$_port',
                          style: const TextStyle(
                            color: Colors.white70,
                            fontSize: 9,
                            fontFamily: 'monospace',
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildErrorView() {
    return Container(
      color: Colors.black87,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.wifi_off, size: 32, color: Colors.white38),
            const SizedBox(height: 8),
            const Text(
              '无法连接摄像头',
              style: TextStyle(color: Colors.white54, fontSize: 13),
            ),
            const SizedBox(height: 4),
            Text(
              '$_ip:$_port\n'
              '点击左上角设置修改 IP',
              textAlign: TextAlign.center,
              style: const TextStyle(color: Colors.white30, fontSize: 10),
            ),
          ],
        ),
      ),
    );
  }
}
