import SwiftUI

/// Branded cinematic dark splash screen for E-Slate
public struct SplashScreenView: View {
    public init() {}

    public var body: some View {
        ZStack {
            Color(red: 0.05, green: 0.05, blue: 0.07)
                .ignoresSafeArea()

            VStack(spacing: 24) {
                Spacer()

                // CLAPPER GRAPHIC WITH CHEVRONS
                VStack(spacing: 12) {
                    SplashSticksHeader()
                        .frame(width: 220, height: 18)

                    Image(systemName: "film.stack.fill")
                        .font(.system(size: 54))
                        .foregroundColor(Color(red: 1.0, green: 0.72, blue: 0.0))
                }

                // TYPOGRAPHY
                VStack(spacing: 8) {
                    Text("E-SLATE")
                        .font(.system(size: 36, weight: .heavy, design: .monospaced))
                        .foregroundColor(.white)
                        .tracking(6)

                    Text("NEBRALTA DIGITAL CLAPPERBOARD")
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))
                        .tracking(3)
                }

                Spacer()

                // LOADING SPINNER & INDICATOR
                VStack(spacing: 12) {
                    ProgressView()
                        .progressViewStyle(CircularProgressViewStyle(tint: Color(red: 1.0, green: 0.72, blue: 0.0)))
                        .scaleEffect(1.3)

                    Text("INICIALIZANDO...")
                        .font(.system(size: 11, weight: .semibold, design: .monospaced))
                        .foregroundColor(Color(red: 0.5, green: 0.5, blue: 0.55))
                        .tracking(2)
                }
                .padding(.bottom, 50)
            }
        }
    }
}

// MARK: - Clapper Sticks Graphic
private struct SplashSticksHeader: View {
    var body: some View {
        HStack(spacing: 0) {
            ForEach(0..<8, id: \.self) { index in
                Rectangle()
                    .fill(index % 2 == 0 ? Color.white : Color(red: 0.15, green: 0.15, blue: 0.18))
                    .frame(width: 28)
                    .transformEffect(CGAffineTransform(1.0, 0.0, -0.4, 1.0, 0.0, 0.0))
            }
        }
        .clipped()
        .cornerRadius(4)
    }
}
