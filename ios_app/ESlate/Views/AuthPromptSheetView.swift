import SwiftUI

/// Authentication modal prompt presented when connecting to an E-Slate
/// without a stored PIN or after an authentication failure.
public struct AuthPromptSheetView: View {
    @ObservedObject var bt: BluetoothManager
    @State private var inputPin: String = ""
    @State private var rememberPin: Bool = true
    @State private var isPinVisible: Bool = false

    public init(bt: BluetoothManager) {
        self.bt = bt
    }

    public var body: some View {
        NavigationView {
            ZStack {
                Color(red: 0.08, green: 0.08, blue: 0.10)
                    .ignoresSafeArea()

                VStack(spacing: 20) {
                    // SHIELD ICON
                    Image(systemName: bt.isLockedOut ? "lock.slash.fill" : "lock.shield.fill")
                        .font(.system(size: 46))
                        .foregroundColor(bt.isLockedOut ? .red : Color(red: 1.0, green: 0.72, blue: 0.0))
                        .padding(.top, 16)

                    // TITLE & INSTRUCTIONS
                    VStack(spacing: 8) {
                        Text("AUTORIZACIÓN DE CLAQUETA")
                            .font(.system(size: 17, weight: .heavy, design: .monospaced))
                            .foregroundColor(.white)

                        Text("Observa la pantalla e-Paper de la claqueta: ingresa el PIN de autorización mostrado en el panel físico para sincronizar.")
                            .font(.system(size: 13))
                            .multilineTextAlignment(.center)
                            .foregroundColor(Color(red: 0.75, green: 0.75, blue: 0.80))
                            .padding(.horizontal, 24)
                    }

                    // PIN INPUT FIELD
                    VStack(alignment: .leading, spacing: 8) {
                        Text("PIN DE LA CLAQUETA")
                            .font(.system(size: 11, weight: .bold, design: .monospaced))
                            .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))

                        HStack {
                            if isPinVisible {
                                TextField("PIN", text: $inputPin)
                                    .font(.system(size: 20, weight: .bold, design: .monospaced))
                                    .foregroundColor(.white)
                                    .autocapitalization(.none)
                                    .disableAutocorrection(true)
                            } else {
                                SecureField("••••••••", text: $inputPin)
                                    .font(.system(size: 20, weight: .bold, design: .monospaced))
                                    .foregroundColor(.white)
                                    .autocapitalization(.none)
                                    .disableAutocorrection(true)
                            }

                            Button(action: {
                                isPinVisible.toggle()
                            }) {
                                Image(systemName: isPinVisible ? "eye.slash.fill" : "eye.fill")
                                    .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))
                            }
                        }
                        .padding()
                        .background(Color(red: 0.13, green: 0.13, blue: 0.17))
                        .cornerRadius(10)
                        .overlay(
                            RoundedRectangle(cornerRadius: 10)
                                .stroke(
                                    bt.isLockedOut ? Color.red :
                                    (bt.authFailureCount > 0 ? Color.orange : Color(red: 0.25, green: 0.25, blue: 0.3)),
                                    lineWidth: 1
                                )
                        )
                    }
                    .padding(.horizontal, 24)

                    // STATUS / ERROR BANNER
                    if bt.isLockedOut {
                        HStack(spacing: 8) {
                            Image(systemName: "exclamationmark.octagon.fill")
                            Text("Claqueta bloqueada por 60 segundos por exceso de intentos erróneos.")
                        }
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundColor(.red)
                        .padding(.horizontal, 24)
                    } else if bt.authFailureCount > 0 {
                        HStack(spacing: 8) {
                            Image(systemName: "exclamationmark.triangle.fill")
                            Text("PIN incorrecto (Intento \(bt.authFailureCount)). Verifica el PIN en la pantalla e-Paper.")
                        }
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundColor(.orange)
                        .padding(.horizontal, 24)
                    }

                    // REMEMBER PIN TOGGLE
                    Toggle("Recordar PIN en este iPhone", isOn: $rememberPin)
                        .font(.system(size: 13, weight: .medium))
                        .foregroundColor(.white)
                        .padding(.horizontal, 24)

                    Spacer()

                    // SUBMIT BUTTON
                    Button(action: {
                        guard !inputPin.isEmpty, !bt.isLockedOut else { return }
                        bt.submitPinAuth(inputPin, remember: rememberPin)
                    }) {
                        Text(bt.isLockedOut ? "BLOQUEADO" : "AUTORIZAR Y CONECTAR")
                            .font(.system(size: 15, weight: .heavy, design: .monospaced))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 14)
                            .background(
                                inputPin.isEmpty || bt.isLockedOut
                                    ? Color(red: 0.3, green: 0.3, blue: 0.35)
                                    : Color(red: 1.0, green: 0.72, blue: 0.0)
                            )
                            .foregroundColor(inputPin.isEmpty || bt.isLockedOut ? .white : .black)
                            .cornerRadius(12)
                    }
                    .disabled(inputPin.isEmpty || bt.isLockedOut)
                    .padding(.horizontal, 24)
                    .padding(.bottom, 20)
                }
            }
            .navigationBarItems(trailing: Button("Cancelar") {
                bt.cancelAuthPrompt()
            }.foregroundColor(.white))
        }
        .preferredColorScheme(.dark)
    }
}
