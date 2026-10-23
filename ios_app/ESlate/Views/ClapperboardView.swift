import SwiftUI

public enum SlateField: Hashable, CaseIterable {
    case roll, scene, take, prod, dir, dop, note, date, timecode

    public var displayName: String {
        switch self {
        case .roll: return "EDITANDO ROLL"
        case .scene: return "EDITANDO SCENE"
        case .take: return "EDITANDO TAKE"
        case .prod: return "EDITANDO PROD"
        case .dir: return "EDITANDO DIRECTOR"
        case .dop: return "EDITANDO D.O.P."
        case .note: return "EDITANDO NOTE"
        case .date: return "EDITANDO DATE"
        case .timecode: return "EDITANDO TIMECODE"
        }
    }
}

public struct ClapperboardView: View {
    @ObservedObject var bt: BluetoothManager
    @FocusState.Binding var focusedField: SlateField?

    public init(bt: BluetoothManager, focusedField: FocusState<SlateField?>.Binding) {
        self.bt = bt
        self._focusedField = focusedField
    }

    public var body: some View {
        VStack(spacing: 12) {
            // Visual Chevrons Header (Traditional Slate Sticks)
            SlateSticksHeader()
                .frame(height: 18)
                .padding(.horizontal, 4)

            // TIMECODE HERO CARD
            timecodeHeroCard()

            // ROW 1: THE TRINITY (ROLL / SCENE / TAKE)
            HStack(spacing: 10) {
                if bt.state.vis["roll"] ?? true {
                    slateCard(title: "ROLL") {
                        TextField("A001", text: binding(for: \.roll))
                            .font(.system(size: 26, weight: .heavy, design: .monospaced))
                            .multilineTextAlignment(.center)
                            .foregroundColor(.white)
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .roll)
                            .id(SlateField.roll)
                    }
                }

                if bt.state.vis["scene"] ?? true {
                    slateCard(title: "SCENE") {
                        TextField("35A", text: binding(for: \.scene))
                            .font(.system(size: 26, weight: .heavy, design: .monospaced))
                            .multilineTextAlignment(.center)
                            .foregroundColor(.white)
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .scene)
                            .id(SlateField.scene)
                    }
                }

                if bt.state.vis["take"] ?? true {
                    slateCard(title: "TAKE") {
                        TextField("1", text: binding(for: \.take))
                            .font(.system(size: 26, weight: .heavy, design: .monospaced))
                            .multilineTextAlignment(.center)
                            .foregroundColor(Color(red: 1.0, green: 0.75, blue: 0.0))
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .take)
                            .id(SlateField.take)
                    }
                }
            }

            // ROW 2: PRODUCTION (PROD) WITH INSTANT 🌸 BUTTON
            if bt.state.vis["prod"] ?? true {
                slateCard(title: "PRODUCTION") {
                    HStack(spacing: 8) {
                        TextField("NEBRALTA 🌸", text: binding(for: \.prod))
                            .font(.system(size: 18, weight: .bold, design: .monospaced))
                            .foregroundColor(.white)
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .prod)
                            .id(SlateField.prod)

                        Button(action: {
                            bt.triggerHaptic(style: .light)
                            if !bt.state.prod.contains("🌸") {
                                bt.state.prod = (bt.state.prod + " 🌸").trimmingCharacters(in: .whitespaces)
                            }
                        }) {
                            Text("🌸")
                                .font(.system(size: 20))
                                .padding(.horizontal, 10)
                                .padding(.vertical, 6)
                                .background(Color(red: 0.22, green: 0.16, blue: 0.22))
                                .cornerRadius(8)
                                .overlay(
                                    RoundedRectangle(cornerRadius: 8)
                                        .stroke(Color(red: 0.8, green: 0.4, blue: 0.6), lineWidth: 1)
                                )
                        }
                    }
                }
            }

            // ROW 3: DIRECTORIAL CREDITS (DIR / DOP)
            HStack(spacing: 10) {
                if bt.state.vis["dir"] ?? true {
                    slateCard(title: "DIRECTOR") {
                        TextField("DIRECTOR", text: binding(for: \.director))
                            .font(.system(size: 15, weight: .bold, design: .monospaced))
                            .foregroundColor(.white)
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .dir)
                            .id(SlateField.dir)
                    }
                }

                if bt.state.vis["dop"] ?? true {
                    slateCard(title: "D.O.P.") {
                        TextField("FELIPELOTAS1", text: binding(for: \.dop))
                            .font(.system(size: 15, weight: .bold, design: .monospaced))
                            .foregroundColor(.white)
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .dop)
                            .id(SlateField.dop)
                    }
                }
            }

            // ROW 4: NOTE & DATE (WITH 📱 HOY AUTO-SYNC)
            HStack(spacing: 10) {
                if bt.state.vis["note"] ?? true {
                    slateCard(title: "NOTE") {
                        TextField("ISO 120", text: binding(for: \.note))
                            .font(.system(size: 14, weight: .medium, design: .monospaced))
                            .foregroundColor(.white)
                            .autocapitalization(.allCharacters)
                            .disableAutocorrection(true)
                            .focused($focusedField, equals: .note)
                            .id(SlateField.note)
                    }
                }

                if bt.state.vis["date"] ?? true {
                    slateCard(title: "DATE") {
                        HStack(spacing: 6) {
                            TextField("25.09.2026", text: binding(for: \.date))
                                .font(.system(size: 14, weight: .bold, design: .monospaced))
                                .foregroundColor(.white)
                                .autocapitalization(.allCharacters)
                                .disableAutocorrection(true)
                                .focused($focusedField, equals: .date)
                                .id(SlateField.date)

                            Button(action: {
                                bt.triggerHaptic(style: .light)
                                bt.state.syncDateFromSystem()
                            }) {
                                HStack(spacing: 3) {
                                    Image(systemName: "clock.arrow.circlepath")
                                        .font(.system(size: 11, weight: .bold))
                                    Text("HOY")
                                        .font(.system(size: 11, weight: .black, design: .monospaced))
                                }
                                .padding(.horizontal, 7)
                                .padding(.vertical, 5)
                                .background(Color(red: 1.0, green: 0.7, blue: 0.0))
                                .foregroundColor(.black)
                                .cornerRadius(6)
                            }
                        }
                    }
                }
            }

            // ROW 5: CAMERA & TECHNICAL BADGES (CAM / SOUND / LOC / LIGHT)
            VStack(spacing: 10) {
                HStack(spacing: 10) {
                    // CAMERA PICKER
                    if bt.state.vis["cam"] ?? true {
                        slateCard(title: "CAM") {
                            HStack(spacing: 6) {
                                ForEach(["A", "B", "C"], id: \.self) { letter in
                                    cameraBadge(letter: letter)
                                }
                            }
                        }
                    }

                    // SOUND BADGE
                    if bt.state.vis["sound"] ?? true {
                        slateCard(title: "SOUND") {
                            HStack(spacing: 6) {
                                badgeButton(title: "SYNC", isSelected: bt.state.sound == "SYNC") {
                                    bt.state.sound = "SYNC"
                                }
                                badgeButton(title: "MOS", isSelected: bt.state.sound == "MOS") {
                                    bt.state.sound = "MOS"
                                }
                            }
                        }
                    }
                }

                HStack(spacing: 10) {
                    // LOCATION BADGE
                    if bt.state.vis["loc"] ?? true {
                        slateCard(title: "LOC") {
                            HStack(spacing: 6) {
                                badgeButton(title: "INT", isSelected: bt.state.loc == "INT") {
                                    bt.state.loc = "INT"
                                }
                                badgeButton(title: "EXT", isSelected: bt.state.loc == "EXT") {
                                    bt.state.loc = "EXT"
                                }
                            }
                        }
                    }

                    // LIGHT BADGE
                    if bt.state.vis["light"] ?? true {
                        slateCard(title: "LIGHT") {
                            HStack(spacing: 6) {
                                badgeButton(title: "DAY", isSelected: bt.state.light == "DAY") {
                                    bt.state.light = "DAY"
                                }
                                badgeButton(title: "NIGHT", isSelected: bt.state.light == "NIGHT") {
                                    bt.state.light = "NIGHT"
                                }
                            }
                        }
                    }
                }
            }
        }
        .padding(.horizontal, 16)
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                if let current = focusedField {
                    Text(current.displayName)
                        .font(.system(size: 12, weight: .bold, design: .monospaced))
                        .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))
                }
                Spacer()
                Button("Listo") {
                    focusedField = nil
                }
                .font(.system(size: 15, weight: .bold))
                .foregroundColor(Color(red: 1.0, green: 0.72, blue: 0.0))
            }
        }
    }

    // MARK: - Timecode Hero Card

    private func timecodeHeroCard() -> some View {
        let isTcVisible = bt.state.vis["timecode"] ?? true

        return VStack(spacing: 8) {
            // Header: Label, Running Dot, FPS Badge, On/Off Switch
            HStack {
                HStack(spacing: 6) {
                    Circle()
                        .fill(bt.state.tc_running && isTcVisible ? Color.green : Color.orange)
                        .frame(width: 8, height: 8)
                    Text("TIMECODE")
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))

                    Text("\(bt.state.tc_fps) FPS")
                        .font(.system(size: 9, weight: .heavy, design: .monospaced))
                        .foregroundColor(Color(red: 1.0, green: 0.72, blue: 0.0))
                        .padding(.horizontal, 5)
                        .padding(.vertical, 2)
                        .background(Color(red: 0.15, green: 0.15, blue: 0.20))
                        .cornerRadius(4)
                }

                Spacer()

                Toggle("", isOn: Binding(
                    get: { bt.state.vis["timecode"] ?? true },
                    set: { bt.triggerToggleTimecodeVisibility($0) }
                ))
                .labelsHidden()
                .scaleEffect(0.8)
            }

            if isTcVisible {
                // Readout Row: [-1s], [HH:MM:SS], [+1s]
                HStack(spacing: 8) {
                    Button(action: {
                        bt.triggerTimecodeStepSeconds(-1)
                    }) {
                        Text("-1s")
                            .font(.system(size: 13, weight: .bold, design: .monospaced))
                            .foregroundColor(.white)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 8)
                            .background(Color(red: 0.16, green: 0.16, blue: 0.22))
                            .cornerRadius(8)
                    }

                    TextField("00:00:00", text: Binding(
                        get: { bt.state.timecode },
                        set: { bt.state.timecode = $0.uppercased() }
                    ))
                    .font(.system(size: 28, weight: .heavy, design: .monospaced))
                    .multilineTextAlignment(.center)
                    .foregroundColor(Color(red: 1.0, green: 0.75, blue: 0.0))
                    .autocapitalization(.allCharacters)
                    .disableAutocorrection(true)
                    .focused($focusedField, equals: .timecode)
                    .id(SlateField.timecode)

                    Button(action: {
                        bt.triggerTimecodeStepSeconds(1)
                    }) {
                        Text("+1s")
                            .font(.system(size: 13, weight: .bold, design: .monospaced))
                            .foregroundColor(.white)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 8)
                            .background(Color(red: 0.16, green: 0.16, blue: 0.22))
                            .cornerRadius(8)
                    }
                }
                .padding(.vertical, 2)

                // Actions: ⚡ TOD JAM-SYNC & Play/Pause
                HStack(spacing: 8) {
                    Button(action: {
                        bt.triggerJamSyncTod()
                    }) {
                        HStack(spacing: 6) {
                            Image(systemName: "bolt.fill")
                                .font(.system(size: 13, weight: .bold))
                            Text("⚡ TOD JAM-SYNC")
                                .font(.system(size: 13, weight: .black, design: .monospaced))
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 9)
                        .background(Color(red: 1.0, green: 0.72, blue: 0.0))
                        .foregroundColor(.black)
                        .cornerRadius(8)
                    }

                    Button(action: {
                        bt.triggerTimecodePlayPause()
                    }) {
                        HStack(spacing: 4) {
                            Image(systemName: bt.state.tc_running ? "pause.fill" : "play.fill")
                                .font(.system(size: 12, weight: .bold))
                            Text(bt.state.tc_running ? "PAUSA" : "PLAY")
                                .font(.system(size: 11, weight: .bold, design: .monospaced))
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 9)
                        .background(Color(red: 0.16, green: 0.16, blue: 0.22))
                        .foregroundColor(.white)
                        .cornerRadius(8)
                    }
                }

                // Segmented Controls: Slot (0: Top, 1: Lower, 2: Center) & Size (0: Med, 1: Large, 2: XL)
                VStack(spacing: 6) {
                    HStack(spacing: 8) {
                        Text("POS:")
                            .font(.system(size: 9, weight: .bold, design: .monospaced))
                            .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))
                            .frame(width: 32, alignment: .leading)

                        Picker("Slot", selection: Binding(
                            get: { bt.state.tc_slot },
                            set: { bt.triggerSetTimecodeSlot($0) }
                        )) {
                            Text("TOP").tag(0)
                            Text("BARRA").tag(1)
                            Text("HERO").tag(2)
                        }
                        .pickerStyle(SegmentedPickerStyle())
                    }

                    HStack(spacing: 8) {
                        Text("TAM:")
                            .font(.system(size: 9, weight: .bold, design: .monospaced))
                            .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))
                            .frame(width: 32, alignment: .leading)

                        Picker("Size", selection: Binding(
                            get: { bt.state.tc_size },
                            set: { bt.triggerSetTimecodeSize($0) }
                        )) {
                            Text("MED").tag(0)
                            Text("GRANDE").tag(1)
                            Text("XL").tag(2)
                        }
                        .pickerStyle(SegmentedPickerStyle())
                    }
                }
                .padding(.top, 2)
            } else {
                HStack {
                    Spacer()
                    Text("TIMECODE DESACTIVADO — LAYOUT EXPANDIDO")
                        .font(.system(size: 10, weight: .bold, design: .monospaced))
                        .foregroundColor(Color(red: 0.5, green: 0.5, blue: 0.55))
                    Spacer()
                }
                .padding(.vertical, 4)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .background(Color(red: 0.09, green: 0.09, blue: 0.12))
        .cornerRadius(10)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(isTcVisible ? Color(red: 0.35, green: 0.30, blue: 0.15) : Color(red: 0.18, green: 0.18, blue: 0.22), lineWidth: 1)
        )
    }

    // MARK: - Helper Views & Bindings

    private func binding(for keyPath: WritableKeyPath<SlateState, String>) -> Binding<String> {
        Binding(
            get: { bt.state[keyPath: keyPath] },
            set: { newValue in
                bt.state[keyPath: keyPath] = newValue.uppercased()
            }
        )
    }

    private func slateCard<Content: View>(title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundColor(Color(red: 0.6, green: 0.6, blue: 0.65))

            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .background(Color(red: 0.09, green: 0.09, blue: 0.12))
        .cornerRadius(10)
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(Color(red: 0.18, green: 0.18, blue: 0.22), lineWidth: 1)
        )
    }

    private func cameraBadge(letter: String) -> some View {
        Button(action: {
            bt.triggerHaptic(style: .light)
            bt.state.cam = letter
        }) {
            Text(letter)
                .font(.system(size: 13, weight: .black, design: .monospaced))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 6)
                .background(bt.state.cam == letter ? Color(red: 1.0, green: 0.7, blue: 0.0) : Color(red: 0.14, green: 0.14, blue: 0.18))
                .foregroundColor(bt.state.cam == letter ? .black : .white)
                .cornerRadius(6)
        }
    }

    private func badgeButton(title: String, isSelected: Bool, action: @escaping () -> Void) -> some View {
        Button(action: {
            bt.triggerHaptic(style: .light)
            action()
        }) {
            Text(title)
                .font(.system(size: 12, weight: .black, design: .monospaced))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 6)
                .background(isSelected ? Color.white : Color(red: 0.14, green: 0.14, blue: 0.18))
                .foregroundColor(isSelected ? .black : Color(red: 0.6, green: 0.6, blue: 0.65))
                .cornerRadius(6)
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(isSelected ? Color.white : Color(red: 0.22, green: 0.22, blue: 0.26), lineWidth: 1)
                )
        }
    }
}

// MARK: - Clapper Sticks Graphic
private struct SlateSticksHeader: View {
    var body: some View {
        GeometryReader { geometry in
            HStack(spacing: 0) {
                let stripeWidth: CGFloat = 28
                let count = Int(geometry.size.width / stripeWidth) + 1
                ForEach(0..<count, id: \.self) { index in
                    Rectangle()
                        .fill(index % 2 == 0 ? Color.white : Color(red: 0.1, green: 0.1, blue: 0.12))
                        .frame(width: stripeWidth)
                        .transformEffect(.init(shearX: -0.5, shearY: 0))
                }
            }
            .clipped()
            .cornerRadius(4)
        }
    }
}

private extension CGAffineTransform {
    init(shearX: CGFloat, shearY: CGFloat) {
        self.init(1.0, shearY, shearX, 1.0, 0.0, 0.0)
    }
}
