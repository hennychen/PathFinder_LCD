import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:http/http.dart' as http;
import 'package:google_mlkit_face_detection/google_mlkit_face_detection.dart';

/// 手机端人脸检测服务。
///
/// 双循环解耦架构：
/// - **拉帧循环**（200ms / 5 FPS）：GET /cam → 广播 JPEG bytes 给显示
/// - **检测循环**（400ms / 2.5 Hz）：取最新帧 → ML Kit 检测 → POST /face（非阻塞）
///
/// 显示帧率不受检测耗时影响。
class FaceDetectionService {
  final String baseUrl;
  late final FaceDetector _detector;

  Timer? _fetchTimer;
  Timer? _detectTimer;
  bool _running = false;

  /// 最新帧缓冲（拉帧循环写入，检测循环读取）
  Uint8List? _latestFrame;

  /// 复用同一临时文件
  late final File _tempFile;

  /// 帧数据流：拉到的 JPEG bytes 广播给显示订阅者
  final _frameController = StreamController<Uint8List>.broadcast();
  Stream<Uint8List> get frameStream => _frameController.stream;

  FaceDetectionService({required this.baseUrl}) {
    _detector = FaceDetector(
      options: FaceDetectorOptions(
        performanceMode: FaceDetectorMode.fast,
        minFaceSize: 0.15,
      ),
    );
    _tempFile = File('${Directory.systemTemp.path}/esp32_cam_frame.jpg');
  }

  void start() {
    if (_running) return;
    _running = true;

    // 拉帧循环：200ms 间隔，只负责下载 + 广播显示
    _fetchTimer = Timer.periodic(
      const Duration(milliseconds: 200),
      (_) => _fetchFrame(),
    );
    _fetchFrame(); // 立即拉第一帧

    // 检测循环：400ms 间隔，独立运行不阻塞显示
    _detectTimer = Timer.periodic(
      const Duration(milliseconds: 400),
      (_) => _detectFrame(),
    );
  }

  void stop() {
    _running = false;
    _fetchTimer?.cancel();
    _detectTimer?.cancel();
    _fetchTimer = null;
    _detectTimer = null;
  }

  void dispose() {
    stop();
    _detector.close();
    _frameController.close();
  }

  /// 拉帧循环：快速下载 JPEG → 广播给显示（不做检测，不阻塞）
  Future<void> _fetchFrame() async {
    if (!_running) return;
    try {
      final response = await http.get(Uri.parse('$baseUrl/cam'));
      if (response.statusCode == 200) {
        _latestFrame = response.bodyBytes;
        _frameController.add(response.bodyBytes);
      }
    } catch (_) {}
  }

  /// 检测循环：取最新帧 → ML Kit → POST /face（fire-and-forget）
  Future<void> _detectFrame() async {
    if (!_running) return;
    final frame = _latestFrame;
    if (frame == null) return;

    try {
      // 写入临时文件
      await _tempFile.writeAsBytes(frame, flush: true);
      final inputImage = InputImage.fromFile(_tempFile);

      // ML Kit 检测
      final faces = await _detector.processImage(inputImage);

      // 构造 JSON
      String jsonBody;
      if (faces.isEmpty) {
        jsonBody = jsonEncode({'found': false});
      } else {
        Face largest = faces.first;
        double maxArea = 0;
        for (final face in faces) {
          final area = face.boundingBox.width * face.boundingBox.height;
          if (area > maxArea) {
            maxArea = area;
            largest = face;
          }
        }
        final imgW = inputImage.metadata?.size.width ?? 320;
        final imgH = inputImage.metadata?.size.height ?? 240;
        jsonBody = jsonEncode({
          'x': (largest.boundingBox.center.dx / imgW).clamp(0.0, 1.0),
          'y': (largest.boundingBox.center.dy / imgH).clamp(0.0, 1.0),
          'w': (largest.boundingBox.width / imgW).clamp(0.0, 1.0),
          'h': (largest.boundingBox.height / imgH).clamp(0.0, 1.0),
          'score': 0.9,
        });
      }

      // fire-and-forget：不 await，不阻塞下一个检测周期
      http.post(
        Uri.parse('$baseUrl/face'),
        headers: {'Content-Type': 'application/json'},
        body: jsonBody,
      );
    } catch (_) {}
  }
}
