import SwiftUI

public struct ContentView: View {
    @EnvironmentObject var bt: BluetoothManager
    @State private var showSettings = false
    @State private var isShowingSplash = true
    @FocusState private var focusedField: SlateField?

    public init() {}

    public var body: some View {
        ZStack {
            Color(red: 0.05, green: 0.05, blue: 0.07)
                .ignoresSafeArea()

            VStack(spacing: 0) {
                // HEADER STATUS BAR
                HeaderStatusView(bt: bt, showSettings: $showSettings)

                // MAIN CLAPPERBOARD INTERFACE
                ScrollViewReader { proxy in
                    ScrollView {
                        VStack(spacing: 16) {
                            ClapperboardView(bt: bt, focusedField: $focusedField)
                        }
                        .padding(.top, 8)
                        .padding(.bottom, focusedField != nil ? 24 : 120) // Space for sticky bottom controls
                    }
                    .scrollDismissesKeyboard(.immediately)
                    .onChange(of: focusedField) { newField in
                        if let field = newField {
                            withAnimation(.easeInOut(duration: 0.25)) {
                                proxy.scrollTo(field, anchor: .center)
                            }
                        }
                    }
                }
            }

            // STICKY BOTTOM QUICK ACTIONS BAR (Auto-hides during text input to prevent occluding fields)
            if focusedField == nil {
                VStack {
                    Spacer()
                    quickActionBar
                        .transition(.move(edge: .bottom).combined(with: .opacity))
                }
                .animation(.easeInOut(duration: 0.25), value: focusedField == nil)
            }

            // BRANDED SPLASH SCREEN OVERLAY
            if isShowingSplash {
                SplashScreenView()
                    .transition(.opacity)
                    .zIndex(100)
            }
        }
        .onAppear {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.9) {
                withAnimation(.easeInOut(duration: 0.4)) {
                    isShowingSplash = false
                }
            }
        }
        .sheet(isPresented: $showSettings) {
            SettingsSheetView(bt: bt)
        }
        .sheet(isPresented: $bt.isAuthPromptPresented) {
            AuthPromptSheetView(bt: bt)
        }
    }

    private var quickActionBar: some View {
        VStack(spacing: 10) {
            HStack(spacing: 12) {
                // +1 TAKE BUTTON (MEDIUM HAPTIC)
                Button(action: {
                    bt.triggerTakePlusOne()
                }) {
                    HStack(spacing: 6) {
                        Image(systemName: "plus.circle.fill")
                            .font(.system(size: 16, weight: .bold))
                        Text("TAKE (+1)")
                            .font(.system(size: 15, weight: .heavy, design: .monospaced))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
                    .background(Color(red: 1.0, green: 0.72, blue: 0.0))
                    .foregroundColor(.black)
                    .cornerRadius(12)
                    .shadow(color: Color(red: 1.0, green: 0.72, blue: 0.0).opacity(0.3), radius: 6, x: 0, y: 3)
                }

                // +1 ROLL BUTTON (HEAVY HAPTIC)
                Button(action: {
                    bt.triggerRollPlusOne()
                }) {
                    HStack(spacing: 6) {
                        Image(systemName: "plus.circle.fill")
                            .font(.system(size: 16, weight: .bold))
                        Text("ROLL (+1)")
                            .font(.system(size: 15, weight: .heavy, design: .monospaced))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
                    .background(Color(red: 0.16, green: 0.16, blue: 0.20))
                    .foregroundColor(.white)
                    .cornerRadius(12)
                    .overlay(
                        RoundedRectangle(cornerRadius: 12)
                            .stroke(Color(red: 0.28, green: 0.28, blue: 0.35), lineWidth: 1)
                    )
                }
            }

            // INSTANT FULL UPDATE BUTTON
            Button(action: {
                bt.sendSlateUpdate()
            }) {
                HStack(spacing: 8) {
                    if bt.isUpdating {
                        ProgressView()
                            .progressViewStyle(CircularProgressViewStyle(tint: .black))
                    } else {
                        Image(systemName: "bolt.fill")
                            .font(.system(size: 15, weight: .bold))
                    }
                    Text(bt.isUpdating ? "ACTUALIZANDO..." : "ACTUALIZAR CLAQUETA")
                        .font(.system(size: 15, weight: .heavy, design: .monospaced))
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 14)
                .background(bt.isConnected ? Color.white : Color(red: 0.3, green: 0.3, blue: 0.35))
                .foregroundColor(.black)
                .cornerRadius(12)
            }
            .disabled(bt.isUpdating)
        }
        .padding(.horizontal, 16)
        .padding(.top, 12)
        .padding(.bottom, 24)
        .background(
            Color(red: 0.07, green: 0.07, blue: 0.09)
                .shadow(color: Color.black.opacity(0.6), radius: 10, x: 0, y: -4)
                .ignoresSafeArea(edges: .bottom)
        )
    }
}
