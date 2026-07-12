import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'theme/app_theme.dart';
import '../shared/widgets/ble_status_chip.dart';
import '../features/environment/environment_screen.dart';
import '../features/motion/motion_screen.dart';
import '../features/emote/emote_screen.dart';
import '../features/history/history_screen.dart';
import '../features/wifi/wifi_setup_screen.dart';

class PathfinderApp extends ConsumerStatefulWidget {
  const PathfinderApp({super.key});

  @override
  ConsumerState<PathfinderApp> createState() => _PathfinderAppState();
}

class _PathfinderAppState extends ConsumerState<PathfinderApp> {
  int _currentIndex = 0;

  final _screens = const [EnvironmentScreen(), MotionScreen(), EmoteScreen()];

  final _titles = const ['环境数据', '运动数据', '表情状态'];

  final _navItems = const [
    NavigationDestination(icon: Icon(Icons.thermostat), label: '环境'),
    NavigationDestination(icon: Icon(Icons.sports_motorsports), label: '运动'),
    NavigationDestination(icon: Icon(Icons.face), label: '表情'),
  ];

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PathFinder Dashboard',
      theme: appTheme(),
      debugShowCheckedModeBanner: false,
      home: Builder(
        builder: (context) => Scaffold(
          appBar: AppBar(
            title: Text(_titles[_currentIndex]),
            actions: [
              const BleStatusChip(),
              IconButton(
                icon: const Icon(Icons.wifi, size: 22),
                onPressed: () => _navigateToWifiSetup(context),
                tooltip: 'WiFi 设置',
              ),
              if (_currentIndex == 0)
                IconButton(
                  icon: const Icon(Icons.history, size: 22),
                  onPressed: () => _navigateToHistory(context),
                  tooltip: '历史数据',
                ),
              const SizedBox(width: 4),
            ],
          ),
          body: IndexedStack(index: _currentIndex, children: _screens),
          bottomNavigationBar: NavigationBar(
            selectedIndex: _currentIndex,
            onDestinationSelected: (index) =>
                setState(() => _currentIndex = index),
            destinations: _navItems,
          ),
        ),
      ),
    );
  }

  void _navigateToHistory(BuildContext context) {
    Navigator.of(
      context,
    ).push(MaterialPageRoute(builder: (_) => const HistoryScreen()));
  }

  void _navigateToWifiSetup(BuildContext context) {
    Navigator.of(
      context,
    ).push(MaterialPageRoute(builder: (_) => const WifiSetupScreen()));
  }
}
