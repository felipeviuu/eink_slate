import SwiftUI

@main
struct ESlateApp: App {
    @StateObject private var bluetoothManager = BluetoothManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(bluetoothManager)
                .preferredColorScheme(.dark)
        }
    }
}
