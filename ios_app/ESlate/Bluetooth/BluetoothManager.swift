import Foundation
import CoreBluetooth
import CryptoKit
import UIKit

// MARK: - Bluetooth Manager

public class BluetoothManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    // MARK: - GATT UUIDs
    public static let primaryServiceUUID = CBUUID(string: "F47AC10B-58CC-4372-A567-0E02B2C3C400")
    public static let stateSyncCharUUID   = CBUUID(string: "F47AC10B-58CC-4372-A567-0E02B2C3C401")
    public static let commandCharUUID     = CBUUID(string: "F47AC10B-58CC-4372-A567-0E02B2C3C402")
    public static let buttonCharUUID      = CBUUID(string: "F47AC10B-58CC-4372-A567-0E02B2C3C403")
    public static let batteryCharUUID     = CBUUID(string: "F47AC10B-58CC-4372-A567-0E02B2C3C404")
    public static let authLockCharUUID    = CBUUID(string: "F47AC10B-58CC-4372-A567-0E02B2C3C405")

    public static let restoreIdentifier   = "com.nebralta.eslate.bluetooth"
    public static let pinDefaultsKey      = "com.nebralta.eslate.secretPin"

    // MARK: - Published Properties
    @Published public var isScanning: Bool = false
    @Published public var isConnected: Bool = false
    @Published public var isAuthenticated: Bool = false
    @Published public var isUpdating: Bool = false
    @Published public var state: SlateState = SlateState()
    @Published public var battery: BatteryTelemetry = BatteryTelemetry()
    @Published public var connectedPeripheralName: String = ""
    @Published public var statusMessage: String = "Desconectado"
    @Published public var lastError: String? = nil
    @Published public var secretPin: String = ""
    @Published public var isAuthPromptPresented: Bool = false
    @Published public var authFailureCount: Int = 0
    @Published public var isLockedOut: Bool = false
    @Published public var lockoutSecondsRemaining: Int = 0

    // MARK: - Private Properties
    private var centralManager: CBCentralManager!
    private var targetPeripheral: CBPeripheral?
    private var stateSyncCharacteristic: CBCharacteristic?
    private var commandCharacteristic: CBCharacteristic?
    private var buttonCharacteristic: CBCharacteristic?
    private var batteryCharacteristic: CBCharacteristic?
    private var authLockCharacteristic: CBCharacteristic?
    private var pendingNonce: Data?

    private let reassemblyEngine = BLEReassemblyEngine()
    private var reconnectTimer: Timer?
    private var isManualDisconnect = false

    // MARK: - Initialization
    public override init() {
        super.init()
        if let savedPin = KeychainHelper.load(), !savedPin.isEmpty {
            self.secretPin = savedPin
        }
        self.centralManager = CBCentralManager(
            delegate: self,
            queue: DispatchQueue.global(qos: .userInitiated),
            options: [CBCentralManagerOptionRestoreIdentifierKey: Self.restoreIdentifier]
        )
    }

    // MARK: - Scanning & Connection Controls

    public func startScanning() {
        guard centralManager.state == .poweredOn else { return }
        isManualDisconnect = false
        DispatchQueue.main.async {
            self.isScanning = true
            self.statusMessage = "Buscando claqueta..."
        }
        centralManager.scanForPeripherals(
            withServices: [Self.primaryServiceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    public func stopScanning() {
        centralManager.stopScan()
        DispatchQueue.main.async {
            self.isScanning = false
            if !self.isConnected {
                self.statusMessage = "Escaneo detenido"
            }
        }
    }

    public func disconnect() {
        isManualDisconnect = true
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        if let peripheral = targetPeripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
    }

    // MARK: - CBCentralManagerDelegate

    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        DispatchQueue.main.async {
            switch central.state {
            case .poweredOn:
                self.statusMessage = "Bluetooth listo"
                if !self.isManualDisconnect {
                    self.startScanning()
                }
            case .poweredOff:
                self.statusMessage = "Bluetooth apagado"
                self.isConnected = false
                self.isAuthenticated = false
            case .unauthorized:
                self.statusMessage = "Permiso Bluetooth no autorizado"
            case .unsupported:
                self.statusMessage = "Hardware BLE no soportado"
            default:
                self.statusMessage = "Inicializando Bluetooth..."
            }
        }
    }

    public func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        if let peripherals = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let restored = peripherals.first {
            self.targetPeripheral = restored
            restored.delegate = self
            central.connect(restored, options: nil)
        }
    }

    public func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let advName = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? ""
        if advName.contains("E-Slate") || advName.contains("NEBRALTA") || peripheral.name?.contains("E-Slate") == true {
            centralManager.stopScan()
            targetPeripheral = peripheral
            targetPeripheral?.delegate = self
            centralManager.connect(peripheral, options: [
                CBConnectPeripheralOptionNotifyOnConnectionKey: true,
                CBConnectPeripheralOptionNotifyOnDisconnectionKey: true
            ])
            DispatchQueue.main.async {
                self.isScanning = false
                self.connectedPeripheralName = advName.isEmpty ? "E-Slate NEBRALTA" : advName
                self.statusMessage = "Conectando..."
            }
        }
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        DispatchQueue.main.async {
            self.isConnected = true
            self.statusMessage = "Descubriendo servicios..."
            self.connectedPeripheralName = peripheral.name ?? "E-Slate NEBRALTA"
        }
        peripheral.discoverServices([Self.primaryServiceUUID])
    }

    public func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async {
            self.isConnected = false
            self.isAuthenticated = false
            self.statusMessage = "Fallo de conexión"
            self.lastError = error?.localizedDescription
            self.isAuthPromptPresented = false
            self.pendingNonce = nil
        }
        scheduleReconnect()
    }

    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async {
            self.isConnected = false
            self.isAuthenticated = false
            self.statusMessage = "Desconectado"
            self.isAuthPromptPresented = false
            self.pendingNonce = nil
            self.stateSyncCharacteristic = nil
            self.commandCharacteristic = nil
            self.buttonCharacteristic = nil
            self.batteryCharacteristic = nil
            self.authLockCharacteristic = nil
        }
        if !isManualDisconnect {
            scheduleReconnect()
        }
    }

    private func scheduleReconnect() {
        guard !isManualDisconnect else { return }
        DispatchQueue.main.async {
            self.reconnectTimer?.invalidate()
            self.reconnectTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: false) { [weak self = self] _ in
                self?.startScanning()
            }
        }
    }

    // MARK: - CBPeripheralDelegate

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for service in services where service.uuid == Self.primaryServiceUUID {
            peripheral.discoverCharacteristics([
                Self.stateSyncCharUUID,
                Self.commandCharUUID,
                Self.buttonCharUUID,
                Self.batteryCharUUID,
                Self.authLockCharUUID
            ], for: service)
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let characteristics = service.characteristics else { return }

        for char in characteristics {
            switch char.uuid {
            case Self.stateSyncCharUUID:
                self.stateSyncCharacteristic = char
                peripheral.setNotifyValue(true, for: char)
            case Self.commandCharUUID:
                self.commandCharacteristic = char
            case Self.buttonCharUUID:
                self.buttonCharacteristic = char
                peripheral.setNotifyValue(true, for: char)
            case Self.batteryCharUUID:
                self.batteryCharacteristic = char
                peripheral.setNotifyValue(true, for: char)
                peripheral.readValue(for: char)
            case Self.authLockCharUUID:
                self.authLockCharacteristic = char
                peripheral.setNotifyValue(true, for: char)
                // Trigger security handshake: Read 16-byte nonce challenge
                peripheral.readValue(for: char)
            default:
                break
            }
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let err = error {
            DispatchQueue.main.async {
                self.lastError = err.localizedDescription
            }
            return
        }

        guard let data = characteristic.value else { return }

        switch characteristic.uuid {
        case Self.authLockCharUUID:
            handleAuthData(data, peripheral: peripheral)

        case Self.stateSyncCharUUID:
            handleStateChunk(data)

        case Self.buttonCharUUID:
            handleButtonEvent(data)

        case Self.batteryCharUUID:
            handleBatteryData(data)

        default:
            break
        }
    }

    // MARK: - Cryptographic Handshake & Security

    private func handleAuthData(_ data: Data, peripheral: CBPeripheral) {
        guard !data.isEmpty else { return }
        let tag = data[0]

        if tag == 0x01 && data.count >= 17 {
            // Tag 0x01: CHALLENGE from ESP32 -> Nonce is 16 bytes
            let nonce = data.subdata(in: 1..<17)
            self.pendingNonce = nonce

            let effectivePin = !secretPin.isEmpty ? secretPin : (KeychainHelper.load() ?? "")
            if !effectivePin.isEmpty {
                self.secretPin = effectivePin
                sendHmacResponse(nonce: nonce, pin: effectivePin, peripheral: peripheral)
            } else {
                // Pause challenge until PIN is provided
                DispatchQueue.main.async {
                    self.isAuthPromptPresented = true
                    self.statusMessage = "Esperando PIN de claqueta..."
                }
            }
        } else if tag == 0x03 && data.count >= 2 {
            // Tag 0x03: STATUS notification
            let status = data[1]
            DispatchQueue.main.async {
                if status == 0x00 {
                    self.isAuthenticated = true
                    self.isAuthPromptPresented = false
                    self.authFailureCount = 0
                    self.isLockedOut = false
                    self.pendingNonce = nil
                    self.statusMessage = "🟢 Conectado: \(self.connectedPeripheralName)"
                    UINotificationFeedbackGenerator().notificationOccurred(.success)
                    // Request initial state
                    if let stateChar = self.stateSyncCharacteristic {
                        peripheral.readValue(for: stateChar)
                    }
                } else if status == 0x01 {
                    self.isAuthenticated = false
                    self.authFailureCount += 1
                    self.statusMessage = "⚠️ PIN de autenticación erróneo"
                    KeychainHelper.delete()
                    self.secretPin = ""
                    self.isAuthPromptPresented = true
                    UINotificationFeedbackGenerator().notificationOccurred(.error)
                } else if status == 0x02 {
                    self.isAuthenticated = false
                    self.isLockedOut = true
                    self.lockoutSecondsRemaining = 60
                    self.statusMessage = "⛔️ Bloqueado por 60s"
                    self.isAuthPromptPresented = true
                    UINotificationFeedbackGenerator().notificationOccurred(.error)
                }
            }
        }
    }

    private func sendHmacResponse(nonce: Data, pin: String, peripheral: CBPeripheral) {
        guard let authChar = authLockCharacteristic else { return }
        let key = SymmetricKey(data: Data(pin.utf8))
        let hmacCode = HMAC<SHA256>.authenticationCode(for: nonce, using: key)

        var response = Data([0x02]) // Tag 0x02: RESPONSE
        response.append(contentsOf: hmacCode) // 32 bytes HMAC

        peripheral.writeValue(response, for: authChar, type: .withResponse)
    }

    // MARK: - PIN Authentication Actions

    public func submitPinAuth(_ pin: String, remember: Bool = true) {
        let trimmed = pin.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        self.secretPin = trimmed
        if remember {
            KeychainHelper.save(pin: trimmed)
        }
        DispatchQueue.main.async {
            self.isAuthPromptPresented = false
        }

        guard let peripheral = targetPeripheral,
              let nonce = pendingNonce else {
            return
        }
        sendHmacResponse(nonce: nonce, pin: trimmed, peripheral: peripheral)
    }

    public func cancelAuthPrompt() {
        DispatchQueue.main.async {
            self.isAuthPromptPresented = false
            self.pendingNonce = nil
        }
        disconnect()
    }

    public func updateSavedPin(_ newPin: String) {
        let trimmed = newPin.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            KeychainHelper.delete()
            self.secretPin = ""
        } else {
            KeychainHelper.save(pin: trimmed)
            self.secretPin = trimmed
        }
    }

    public func clearSavedPin() {
        KeychainHelper.delete()
        self.secretPin = ""
    }

    // MARK: - State Sync Protocol (Chunking & Reassembly)

    private func handleStateChunk(_ chunk: Data) {
        let status = reassemblyEngine.feedChunk(chunk)
        switch status {
        case .complete(let fullData):
            if let decoded = try? JSONDecoder().decode(SlateState.self, from: fullData) {
                DispatchQueue.main.async {
                    self.state = decoded
                }
            }
        case .error(let msg):
            DispatchQueue.main.async {
                self.lastError = "Error reensamblaje: \(msg)"
            }
        case .inProgress:
            break
        }
    }

    public func sendSlateUpdate(newState: SlateState? = nil) {
        guard let peripheral = targetPeripheral,
              let stateChar = stateSyncCharacteristic,
              isConnected, isAuthenticated else {
            return
        }

        let stateToSend = newState ?? self.state
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.withoutEscapingSlashes]
        guard let jsonData = try? encoder.encode(stateToSend) else { return }

        DispatchQueue.main.async {
            self.isUpdating = true
        }

        let maxWrite = peripheral.maximumWriteValueLength(for: .withResponse)
        let chunks = BLEPacketChunker.chunkPayload(jsonData, mtu: maxWrite)

        DispatchQueue.global(qos: .userInitiated).async {
            for chunk in chunks {
                peripheral.writeValue(chunk, for: stateChar, type: .withResponse)
                Thread.sleep(forTimeInterval: 0.015)
            }
            DispatchQueue.main.async {
                self.isUpdating = false
                UINotificationFeedbackGenerator().notificationOccurred(.success)
            }
        }
    }

    // MARK: - Hardware Buttons & Telemetry

    private func handleButtonEvent(_ data: Data) {
        let event = ButtonEvent(data: data)
        DispatchQueue.main.async {
            switch event.code {
            case .key1Take:
                self.state.incrementTake()
                self.triggerHaptic(style: .medium)
            case .key2Roll:
                self.state.incrementRoll()
                self.triggerHaptic(style: .heavy)
            case .key0Invert:
                self.state.invert.toggle()
                self.triggerHaptic(style: .light)
            case .bootDismiss:
                self.triggerHaptic(style: .light)
            case .unknown:
                break
            }
        }
    }

    private func handleBatteryData(_ data: Data) {
        if let telemetry = BatteryTelemetry(data: data) {
            DispatchQueue.main.async {
                self.battery = telemetry
            }
        }
    }

    // MARK: - Commands & Quick Actions

    public func sendCommand(_ opcode: SlateCommandOpcode, parameter: UInt8? = nil) {
        guard let peripheral = targetPeripheral,
              let cmdChar = commandCharacteristic,
              isConnected, isAuthenticated else {
            return
        }
        let packet = opcode.buildPacket(parameter: parameter)
        let writeType: CBCharacteristicWriteType = cmdChar.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(packet, for: cmdChar, type: writeType)
    }

    public func triggerTakePlusOne() {
        state.incrementTake()
        triggerHaptic(style: .medium)
        sendCommand(.incrementTake)
    }

    public func triggerRollPlusOne() {
        state.incrementRoll()
        triggerHaptic(style: .heavy)
        sendCommand(.incrementRoll)
    }

    public func triggerToggleInvert() {
        state.invert.toggle()
        triggerHaptic(style: .light)
        sendCommand(.toggleInvert)
    }

    public func triggerForceRefresh() {
        triggerHaptic(style: .medium)
        sendCommand(.forceRefresh)
    }

    public func triggerSetLayout(_ mode: Int) {
        state.layout = max(0, min(2, mode))
        triggerHaptic(style: .light)
        sendCommand(.setLayout, parameter: UInt8(state.layout))
    }

    // MARK: - Timecode Camera Sync & Controls

    public func sendCommandData(_ packet: Data) {
        guard let peripheral = targetPeripheral,
              let cmdChar = commandCharacteristic,
              isConnected, isAuthenticated else {
            return
        }
        let writeType: CBCharacteristicWriteType = cmdChar.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(packet, for: cmdChar, type: writeType)
    }

    public func triggerJamSyncTod() {
        state.jamSyncTOD()
        triggerHaptic(style: .medium)
        let (h, m, s) = state.parseTimecodeComponents()
        sendCommandData(Data([SlateCommandOpcode.jamSyncTOD.rawValue, UInt8(h), UInt8(m), UInt8(s)]))
        sendSlateUpdate()
    }

    public func triggerTimecodePlayPause() {
        state.tc_running.toggle()
        triggerHaptic(style: .light)
        sendCommand(.toggleTimecode)
    }

    public func triggerSetTimecodeSlot(_ slot: Int) {
        state.tc_slot = max(0, min(2, slot))
        triggerHaptic(style: .light)
        sendCommand(.setTimecodeSlot, parameter: UInt8(state.tc_slot))
        sendSlateUpdate()
    }

    public func triggerSetTimecodeSize(_ size: Int) {
        state.tc_size = max(0, min(2, size))
        triggerHaptic(style: .light)
        sendCommand(.setTimecodeSize, parameter: UInt8(state.tc_size))
        sendSlateUpdate()
    }

    public func triggerSetTimecodeFps(_ fps: String) {
        state.tc_fps = fps
        triggerHaptic(style: .light)
        let fpsIdx = TimecodeFps(rawValue: fps)?.opcodeIndex ?? 1
        sendCommand(.setTimecodeFps, parameter: fpsIdx)
        sendSlateUpdate()
    }

    public func triggerApplyCameraJam(offsetFrames: Int) {
        state.applyFrameOffset(frames: offsetFrames)
        triggerHaptic(style: .medium)
        sendSlateUpdate()
    }

    public func triggerTimecodeStepSeconds(_ seconds: Int) {
        state.applySecondOffset(seconds: seconds)
        triggerHaptic(style: .light)
        sendSlateUpdate()
    }

    public func triggerToggleTimecodeVisibility(_ isVisible: Bool) {
        state.vis["timecode"] = isVisible
        triggerHaptic(style: .medium)
        sendCommand(.setTimecodeVisibility, parameter: isVisible ? 1 : 0)
        sendSlateUpdate()
    }

    public func triggerHaptic(style: UIImpactFeedbackGenerator.FeedbackStyle) {
        let generator = UIImpactFeedbackGenerator(style: style)
        generator.prepare()
        generator.impactOccurred()
    }
}
