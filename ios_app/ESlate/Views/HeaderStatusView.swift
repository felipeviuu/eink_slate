import SwiftUI

public struct HeaderStatusView: View {
    @ObservedObject var bt: BluetoothManager
    @Binding var showSettings: Bool

    public init(bt: BluetoothManager, showSettings: Binding<Bool>) {
        self.bt = bt
        self._showSettings = showSettings
    }

    public var body: some View {
        HStack(spacing: 12) {
            // Connection Status Pill
            Button(action: {
                if !bt.isConnected {
                    bt.startScanning()
                }
            }) {
                HStack(spacing: 6) {
                    Circle()
                        .fill(statusColor)
                        .frame(width: 8, height: 8)

                    Text(statusText)
                        .font(.system(size: 13, weight: .bold, design: .monospaced))
                        .foregroundColor(.white)

                    if bt.isAuthenticated {
                        Image(systemName: "lock.shield.fill")
                            .font(.system(size: 11))
                            .foregroundColor(Color(red: 0.3, green: 0.85, blue: 0.4))
                    }
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(Color(red: 0.12, green: 0.12, blue: 0.15))
                .cornerRadius(20)
                .overlay(
                    RoundedRectangle(cornerRadius: 20)
                        .stroke(Color(red: 0.2, green: 0.2, blue: 0.25), lineWidth: 1)
                )
            }

            Spacer()

            // Battery Telemetry Badge
            if bt.isConnected {
                HStack(spacing: 5) {
                    Image(systemName: batteryIconName)
                        .font(.system(size: 13))
                        .foregroundColor(batteryColor)

                    Text("\(bt.battery.percentage)% (\(bt.battery.voltageString))")
                        .font(.system(size: 12, weight: .medium, design: .monospaced))
                        .foregroundColor(.white)
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 5)
                .background(Color(red: 0.12, green: 0.12, blue: 0.15))
                .cornerRadius(12)
            }

            // Settings Button
            Button(action: {
                showSettings = true
            }) {
                Image(systemName: "gearshape.fill")
                    .font(.system(size: 16))
                    .foregroundColor(Color(red: 1.0, green: 0.7, blue: 0.0))
                    .padding(8)
                    .background(Color(red: 0.12, green: 0.12, blue: 0.15))
                    .clipShape(Circle())
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
    }

    private var statusColor: Color {
        if bt.isAuthenticated {
            return Color(red: 0.2, green: 0.85, blue: 0.3)
        } else if bt.isConnected {
            return Color.orange
        } else if bt.isScanning {
            return Color.blue
        } else {
            return Color.red
        }
    }

    private var statusText: String {
        if bt.isAuthenticated {
            return "🟢 Conectado: \(bt.connectedPeripheralName)"
        } else if bt.isConnected {
            return "🟡 Autenticando..."
        } else if bt.isScanning {
            return "🔵 Buscando..."
        } else {
            return "🔴 Desconectado"
        }
    }

    private var batteryIconName: String {
        if bt.battery.isChargingOrFull {
            return "battery.100.bolt"
        } else if bt.battery.isLowBattery {
            return "battery.25"
        } else if bt.battery.percentage > 70 {
            return "battery.100"
        } else {
            return "battery.50"
        }
    }

    private var batteryColor: Color {
        if bt.battery.isChargingOrFull {
            return Color(red: 0.3, green: 0.85, blue: 0.4)
        } else if bt.battery.isLowBattery {
            return Color.red
        } else {
            return Color(red: 1.0, green: 0.7, blue: 0.0)
        }
    }
}
