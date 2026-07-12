import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:pathfinder_dashboard/app/app.dart';

void main() {
  testWidgets('App smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const ProviderScope(child: PathfinderApp()));
    await tester.pump();
    // The default tab is Environment with title '环境数据'
    expect(find.text('环境数据'), findsWidgets);
    // BLE status chip should be visible in AppBar
    expect(find.text('未连接'), findsOneWidget);
  });
}
