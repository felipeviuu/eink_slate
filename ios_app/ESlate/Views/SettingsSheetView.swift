import SwiftUI

public struct SettingsSheetView: View {
    @ObservedObject var bt: BluetoothManager
    @Environment(\.presentationMode) var presentationMode

    private let layoutNames = ["Cinema Pro", "Big Slate (Escena Gigante)", "Commercial"]

    @State private var pinInput: String = ""
    @State private var isPinSaved: Bool = false
    @State private var cameraOffsetFrames: Int = 0

    public init(bt: BluetoothManager) {
        self.bt = bt
    }

    public var body: some View {
        NavigationView {
            Form {
                
                // SECTION: BLUETOOTH & AUTH
                Section(header: Text("CONEXIÓN BLUETOOTH")) {
                    HStack {
                        Text("Estado")
                        Spacer()
                        Text(bt.statusMessage)
                            .font(.system(.subheadline, design: .monospaced))
                            .foregroundColor(bt.isAuthenticated ? .green : .secondary)
                    }

                    if bt.isConnected {
                        HStack {
                            Text("Dispositivo")
                            Spacer()
                            Text(bt.connectedPeripheralName)
                                .font(.system(.subheadline, design: .monospaced))
                                .foregroundColor(.secondary)
                        }

                        Button(action: {
                            bt.disconnect()
                        }) {
                            Text("Desconectar")
                                .foregroundColor(.red)
                        }
                    } else {
                        Button(action: {
                            bt.startScanning()
                        }) {
                            HStack {
                                Text(bt.isScanning ? "Buscando..." : "Buscar Claqueta")
                                if bt.isScanning {
                                    Spacer()
                                    ProgressView()
                                }
                            }
                        }
                    }
                }

                // SECTION: SECURITY PIN
                Section(header: Text("SEGURIDAD & PIN")) {
                    HStack {
                        Text("Estado")
                        Spacer()
                        Text(KeychainHelper.hasSavedPin() ? "Configurado en Keychain" : "No configurado")
                            .font(.system(.subheadline, design: .monospaced))
                            .foregroundColor(KeychainHelper.hasSavedPin() ? .green : .secondary)
                    }

                    HStack {
                        Text("PIN Secreto")
                        Spacer()
                        TextField("Ingresa PIN", text: $pinInput)
                            .font(.system(.body, design: .monospaced))
                            .multilineTextAlignment(.trailing)
                            .autocapitalization(.none)
                            .disableAutocorrection(true)
                    }

                    Button(action: {
                        bt.updateSavedPin(pinInput)
                        isPinSaved = true
                    }) {
                        Text(isPinSaved ? "✓ PIN Guardado" : "Guardar PIN en Keychain")
                            .foregroundColor(Color(red: 1.0, green: 0.72, blue: 0.0))
                    }

                    if KeychainHelper.hasSavedPin() || !bt.secretPin.isEmpty {
                        Button(action: {
                            bt.clearSavedPin()
                            pinInput = ""
                            isPinSaved = false
                        }) {
                            Text("Borrar PIN Guardado")
                                .foregroundColor(.red)
                        }
                    }

                    Text("Se muestra en la pantalla e-Paper física al solicitar sincronización.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }

                // SECTION: TIMECODE & CAMERA SYNC
                Section(header: Text("TIMECODE & SINCRONIZACIÓN DE CÁMARA")) {
                    Toggle("Activar Timecode", isOn: Binding(
                        get: { bt.state.vis["timecode"] ?? true },
                        set: { bt.triggerToggleTimecodeVisibility($0) }
                    ))

                    if bt.state.vis["timecode"] ?? true {
                        Picker("Slot de Posición", selection: Binding(
                            get: { bt.state.tc_slot },
                            set: { bt.triggerSetTimecodeSlot($0) }
                        )) {
                            Text("Top Header (Superior)").tag(0)
                            Text("Lower Bar (Inferior)").tag(1)
                            Text("Center Hero (Central)").tag(2)
                        }

                        Picker("Tamaño de Fuente", selection: Binding(
                            get: { bt.state.tc_size },
                            set: { bt.triggerSetTimecodeSize($0) }
                        )) {
                            Text("Medio (24px)").tag(0)
                            Text("Grande (40px)").tag(1)
                            Text("Gigante XL (64px)").tag(2)
                        }

                        Picker("Frame Rate Base (FPS)", selection: Binding(
                            get: { bt.state.tc_fps },
                            set: { bt.triggerSetTimecodeFps($0) }
                        )) {
                            ForEach(["23.976", "24", "25", "29.97", "30"], id: \.self) { fps in
                                Text("\(fps) fps").tag(fps)
                            }
                        }

                        // Camera Offset Delta Stepper
                        VStack(alignment: .leading, spacing: 8) {
                            Text("AJUSTE DE CUADROS (CAMERA JAM OFFSET)")
                                .font(.system(size: 10, weight: .bold, design: .monospaced))
                                .foregroundColor(Color(red: 1.0, green: 0.72, blue: 0.0))

                            HStack {
                                Text("Delta: \(cameraOffsetFrames > 0 ? "+\(cameraOffsetFrames)" : "\(cameraOffsetFrames)") Frames")
                                    .font(.system(.subheadline, design: .monospaced))
                                Spacer()
                                Stepper("", value: $cameraOffsetFrames, in: -100...100)
                                    .labelsHidden()
                            }

                            HStack(spacing: 8) {
                                Button(action: {
                                    bt.triggerApplyCameraJam(offsetFrames: cameraOffsetFrames)
                                    cameraOffsetFrames = 0
                                }) {
                                    HStack {
                                        Image(systemName: "camera.viewfinder")
                                        Text("Aplicar Jam a Claqueta")
                                    }
                                    .font(.system(size: 13, weight: .bold))
                                    .frame(maxWidth: .infinity)
                                    .padding(.vertical, 8)
                                    .background(Color(red: 1.0, green: 0.72, blue: 0.0))
                                    .foregroundColor(.black)
                                    .cornerRadius(8)
                                }

                                if cameraOffsetFrames != 0 {
                                    Button(action: {
                                        cameraOffsetFrames = 0
                                    }) {
                                        Text("Reset")
                                            .font(.system(size: 12, weight: .bold))
                                            .foregroundColor(.red)
                                            .padding(.horizontal, 8)
                                    }
                                }
                            }
                        }
                        .padding(.vertical, 4)

                        Button(action: {
                            bt.triggerJamSyncTod()
                        }) {
                            Label("Sincronizar Reloj iPhone (TOD Jam)", systemImage: "clock.badge.checkmark.fill")
                                .foregroundColor(Color(red: 1.0, green: 0.72, blue: 0.0))
                        }
                    }
                }

                // SECTION: DISPLAY LAYOUT & MODE
                Section(header: Text("DISEÑO Y PANTALLA E-PAPER")) {
                    Picker("Diseño de Claqueta", selection: Binding(
                        get: { bt.state.layout },
                        set: { newLayout in
                            bt.triggerSetLayout(newLayout)
                        }
                    )) {
                        ForEach(0..<layoutNames.count, id: \.self) { idx in
                            Text(layoutNames[idx]).tag(idx)
                        }
                    }

                    Toggle("Modo Invertido (Fondo Negro)", isOn: Binding(
                        get: { bt.state.invert },
                        set: { _ in
                            bt.triggerToggleInvert()
                        }
                    ))

                    Button(action: {
                        bt.triggerForceRefresh()
                    }) {
                        Label("Forzar Refresco de Pantalla", systemImage: "arrow.clockwise")
                    }
                }

                // SECTION: GRANULAR VISIBILITY
                Section(header: Text("CAMPOS VISIBLES EN PANTALLA")) {
                    visibilityToggle(title: "ROLL", key: "roll")
                    visibilityToggle(title: "SCENE", key: "scene")
                    visibilityToggle(title: "TAKE", key: "take")
                    visibilityToggle(title: "PRODUCTION", key: "prod")
                    visibilityToggle(title: "DIRECTOR", key: "dir")
                    visibilityToggle(title: "D.O.P.", key: "dop")
                    visibilityToggle(title: "TIMECODE", key: "timecode")
                    visibilityToggle(title: "NOTE", key: "note")
                    visibilityToggle(title: "DATE", key: "date")
                    visibilityToggle(title: "CAMERA", key: "cam")
                    visibilityToggle(title: "SOUND (SYNC / MOS)", key: "sound")
                    visibilityToggle(title: "LOCATION (INT / EXT)", key: "loc")
                    visibilityToggle(title: "LIGHT (DAY / NIGHT)", key: "light")
                    visibilityToggle(title: "BATERÍA", key: "bat")
                }
                Section(header: Text("Diseñado por Nebralta - Felipelotas1")) {
                    
                    }
            }
            .navigationBarTitle("Configuración", displayMode: .inline)
            .navigationBarItems(trailing: Button("Listo") {
                presentationMode.wrappedValue.dismiss()
            })
            .onAppear {
                pinInput = bt.secretPin
            }
    
        }
        .preferredColorScheme(.dark)
    }

    private func visibilityToggle(title: String, key: String) -> some View {
        Toggle(title, isOn: Binding(
            get: { bt.state.vis[key] ?? true },
            set: { newValue in
                bt.state.vis[key] = newValue
            }
        ))
    }
}
