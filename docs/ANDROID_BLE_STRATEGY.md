# E-Slate Digital Clapperboard: Android BLE Strategy & Web Bluetooth PWA Solution

**Document Version**: 1.0.0  
**Target Platform**: Android Smartphones & Tablets (Android 6.0 Marshmallow to Android 15+)  
**Target Hardware**: Seeed Studio XIAO ESP32-S3 Plus (16MB Flash, 8MB PSRAM OPI) + EE04 Carrier Board + 7.5" Monochrome e-Paper Display (UC8179, 800x480)  
**Reference Firmware**: `eink_clap/eink_clap.ino`  
**Protocol Standard**: `docs/BLE_SPEC.md`  
**Web Application**: `web_pwa/` (Standalone Progressive Web App)

---

## 1. Executive Summary

This document specifies the authoritative wireless strategy for controlling the **E-Slate NEBRALTA** digital film clapperboard from Android devices via **Bluetooth Low Energy (BLE)**.

While the iOS ecosystem relies on a native Swift/SwiftUI application utilizing Apple's `CoreBluetooth` framework, the Android ecosystem presents distinct challenges: diverse OEM form factors, Google Play Store distribution hurdles, sideloading friction for crew members, and permission fragmentation.

To solve these challenges while maintaining parity with the iOS app, the E-Slate platform implements a **Progressive Web App (PWA) powered by the W3C Web Bluetooth API**. This approach delivers:
1. **Zero-Friction Distribution**: No `.apk` sideloading, developer settings override, or Play Store review delays. Users simply scan a QR code or visit a URL.
2. **True Offline Operation**: Leveraging a Service Worker (`sw.js`) and CacheStorage API, the PWA functions 100% offline in remote filming locations without cellular data or Wi-Fi.
3. **Cryptographic Compatibility**: Native hardware-accelerated **Web Crypto API** (`crypto.subtle`) performs the mandatory HMAC-SHA256 challenge-response handshake on GATT Characteristic `0xC405`.
4. **Binary Protocol Parity**: Deterministic 3-byte packet chunking (`[seq: 1B][total: 1B][len: 1B][data: N B]`) on Characteristic `0xC401` guarantees robust state transfer across variable ATT MTUs while preserving raw UTF-8 emoji bytes (🌸).
5. **Physical Set Feedback**: Real-time subscriptions to Characteristic `0xC403` stream EE04 carrier board button clicks directly into the Android device's vibration engine (`navigator.vibrate()`).

---

## 2. The Android Film Set Control Dilemma

On film sets, camera assistants (1st/2nd AC), script supervisors, and DITs work across heterogeneous hardware. Providing slate control on Android historically presented three flawed options:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             ARCHITECTURAL TRADEOFFS                         │
├─────────────────────┬───────────────────────────┬───────────────────────────┤
│ Option              │ Advantages                │ Disadvantages             │
├─────────────────────┼───────────────────────────┼───────────────────────────┤
│ 1. Native Android   │ Direct BLE API access;    │ High onboarding friction; │
│    App (Kotlin /    │ OS background services.   │ requires APK sideloading  │
│    Jetpack Compose) │                           │ or Play Store review.     │
├─────────────────────┼───────────────────────────┼───────────────────────────┤
│ 2. Wi-Fi SoftAP     │ Universal browser access; │ Kills cellular data on    │
│    Direct           │ simple HTTP form POSTs.   │ phone; high radio power   │
│    (192.168.4.1)    │                           │ draw (120-160 mA).        │
├─────────────────────┼───────────────────────────┼───────────────────────────┤
│ 3. Web Bluetooth    │ Zero installation barrier;│ Requires HTTPS context;   │
│    PWA (Chosen      │ 100% offline via SW; low  │ Chrome-based browser      │
│    Architecture)    │ power (15-25 mA); keeps   │ required (99.4% on        │
│                     │ cellular data active.     │ modern Android devices).  │
└─────────────────────┴───────────────────────────┴───────────────────────────┘
```

### 2.1 Cellular Data & Network Ergonomics
When an Android smartphone connects to the ESP32-S3 Wi-Fi SoftAP (`NEBRALTA_Clap`), Android's network manager detects no internet connectivity on the Wi-Fi interface. The OS either:
- Automatically disconnects from the Wi-Fi network to preserve internet access.
- Drops cellular mobile data (4G/5G), preventing crew members from receiving production emails, call sheet updates, or messaging apps.

By contrast, **BLE creates a personal local channel that does not interfere with cellular data or local Wi-Fi networks**.

### 2.2 Power Consumption Profile
On the ESP32-S3 Plus:
- **Wi-Fi SoftAP Active**: Radio transmitter operates at ~15 dBm with typical current draw of **120 mA to 160 mA**.
- **BLE GATT Server Active**: NimBLE stack operates with advertising and connection events consuming **15 mA to 25 mA** (an 83% reduction in radio energy).
- On battery-powered setups (18650 or LiPo on the EE04 carrier board), BLE drastically extends operational time on set.

---

## 3. Chrome Web Bluetooth API on Android

### 3.1 Platform Support & Engine Compatibility
The **W3C Web Bluetooth API** (`navigator.bluetooth`) is natively supported on Android without requiring experimental flags or developer tools:

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Android Version Support** | Android 6.0 (API Level 23) through Android 15+ | Covers 99.4% of all active Android devices globally. |
| **Supported Browsers** | Google Chrome for Android (v56+), Microsoft Edge, Samsung Internet (v6.4+), Opera Mobile | Chromium Blink engine provides standard GATT Central API. |
| **Desktop Android / ChromeOS** | Fully supported | Chromebooks and Android tablets on DIT carts can control the slate. |
| **iOS Browsers** | Not supported (Apple WebKit blocks Web Bluetooth) | iOS users use the native Swift companion app `ios_app/ESlate`. |

### 3.2 Android OS Permission Model
When the user taps **"Buscar Claqueta"** in the PWA, Chrome delegates discovery to the Android Bluetooth stack:
1. Chrome prompts the user with the native device picker dialog filtered to `E-Slate NEBRALTA`.
2. Chrome handles Android system runtime permissions automatically (`BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, and precise location if required on legacy Android 6-11).
3. Once the user selects the peripheral, the web page receives a scoped `BluetoothDevice` instance with granted GATT access.

---

## 4. The Critical W3C Secure Context Constraint

### 4.1 The Security Restriction
> ⚠️ **MANDATORY SPECIFICATION RULE**:  
> The W3C Web Bluetooth specification strictly mandates that `navigator.bluetooth` is **ONLY available in Secure Contexts** (`window.isSecureContext === true`).
> - In an insecure context (plain HTTP), `navigator.bluetooth` is `undefined`.

Therefore, **navigating to `http://192.168.4.1` (the ESP32-S3 SoftAP IP address) CANNOT run Web Bluetooth directly**. If the ESP32 web server serves an HTML page over plain HTTP, invoking `navigator.bluetooth.requestDevice()` will immediately throw:
```text
SecurityError: Failed to execute 'requestDevice' on 'Bluetooth': Must be in a secure context.
```

### 4.2 The Dual-Tier Solution Architecture

To eliminate this barrier seamlessly, the E-Slate platform implements a **Dual-Tier Architecture**:

```
                               ┌────────────────────────────────┐
                               │     Android Device / Chrome    │
                               └───────────────┬────────────────┘
                                               │
                       ┌───────────────────────┴───────────────────────┐
                       │                                               │
              [Tier 1: Gold Standard]                         [Tier 2: Offline Zero-Net]
                       │                                               │
                       ▼                                               ▼
         ┌───────────────────────────┐                   ┌───────────────────────────┐
         │ Hosted HTTPS Static PWA   │                   │ ESP32-S3 SoftAP HTTP Web  │
         │ (GitHub Pages or Custom)  │                   │ (http://192.168.4.1)      │
         ├───────────────────────────┤                   ├───────────────────────────┤
         │ • Cached via ServiceWorker│                   │ • Standard HTML Forms     │
         │ • "Add to Home Screen"    │                   │ • Direct HTTP POST        │
         │ • Full Web Bluetooth GATT │                   │ • Displays banner with QR │
         │ • 100% Offline execution  │                   │   code to open HTTPS PWA  │
         └─────────────┬─────────────┘                   └─────────────┬─────────────┘
                       │                                               │
              Bluetooth Low Energy                            Wi-Fi SoftAP 802.11b/g/n
             Service 0xC400 / GATT                          HTTP POST /update & /action
                       │                                               │
                       └───────────────────────┬───────────────────────┘
                                               │
                                               ▼
                                 ┌───────────────────────────┐
                                 │  XIAO ESP32-S3 Plus Board │
                                 │   Dual-Mode BLE + Wi-Fi   │
                                 └───────────────────────────┘
```

#### Tier 1: Standalone Hosted HTTPS PWA (Gold Standard)
1. Hosted statically on an HTTPS endpoint (e.g. `https://felipesalas.github.io/eink_clap/` or a production domain).
2. The user visits the URL once while on Wi-Fi or cellular internet, or scans the QR code printed on the physical slate chassis.
3. The Service Worker (`sw.js`) immediately caches all assets (`index.html`, `style.css`, `app.js`, `manifest.json`, icons) in local `CacheStorage`.
4. Chrome prompts: *"Añadir E-Slate a la pantalla de inicio"*.
5. Once installed, the PWA launches in full-screen standalone mode without URL bar or browser chrome.
6. **From that point onward, the PWA is completely self-contained and operates 100% offline**. It opens instantly anywhere in the world and communicates directly with the E-Slate over Web Bluetooth.

#### Tier 2: SoftAP HTTP Web Interface Fallback
1. For crew members on set who have never previously opened the PWA and have zero cellular reception.
2. They join Wi-Fi SSID `NEBRALTA_Clap` (password `123456789P`) and open `http://192.168.4.1` or `http://clap.local`.
3. The built-in ESP32 HTTP web server provides standard HTML form controls for updating the slate.
4. A prominent banner at the top explains:
   *"📲 Para controlar la claqueta por Bluetooth sin desconectarte de tus datos móviles, abre la PWA oficial la próxima vez que tengas conexión."*

---

## 5. Web Bluetooth GATT Client Architecture

### 5.1 Service & Characteristic Mapping
The PWA GATT client directly mirrors the service contracts established in `docs/BLE_SPEC.md`:

```javascript
const SLATE_SERVICE_UUID = 'f47ac10b-58cc-4372-a567-0e02b2c3c400';
const CHAR_STATE_UUID   = 'f47ac10b-58cc-4372-a567-0e02b2c3c401'; // State Sync (R/W/N)
const CHAR_CMD_UUID     = 'f47ac10b-58cc-4372-a567-0e02b2c3c402'; // Commands (W)
const CHAR_BTN_UUID     = 'f47ac10b-58cc-4372-a567-0e02b2c3c403'; // Button Telemetry (R/N)
const CHAR_BAT_UUID     = 'f47ac10b-58cc-4372-a567-0e02b2c3c404'; // Battery Telemetry (R/N)
const CHAR_AUTH_UUID    = 'f47ac10b-58cc-4372-a567-0e02b2c3c405'; // Security Auth Lock (R/W/N)

const BATTERY_SERVICE_UUID = '0000180f-0000-1000-8000-00805f9b34fb';
const BATTERY_LEVEL_UUID   = '00002a19-0000-1000-8000-00805f9b34fb';
```

### 5.2 Discovery & Connection Sequence
Under W3C security rules, invoking `navigator.bluetooth.requestDevice()` requires an **explicit user gesture** (such as tapping a button). The connection sequence proceeds as follows:

```
User Tap "Buscar Claqueta"
       │
       ▼
navigator.bluetooth.requestDevice({
  filters: [{ name: 'E-Slate NEBRALTA' }],
  optionalServices: [SLATE_SERVICE_UUID, BATTERY_SERVICE_UUID]
})
       │ (User selects device in native dialog)
       ▼
device.gatt.connect()
       │
       ▼
service = gatt.getPrimaryService(SLATE_SERVICE_UUID)
       │
       ├─► Discover Characteristics (0xC401..0xC405)
       ├─► Subscribe to Button Notifications (0xC403)
       ├─► Subscribe to Battery Telemetry Notifications (0xC404)
       └─► Initiate Web Crypto HMAC-SHA256 Handshake (0xC405)
```

---

## 6. Cryptographic Security via Web Crypto API

### 6.1 Authentication Protocol
The E-Slate hardware prevents unauthorized crew members or spectators from transmitting unauthorized metadata to the display. All writes to Characteristic `0xC401` (State) and `0xC402` (Command) are strictly rejected with GATT Application Error `0x82` (`ESP_GATT_INSUF_AUTHORIZATION`) until the central completes the challenge-response handshake.

### 6.2 Hardware-Accelerated Web Crypto Handshake
The PWA utilizes `window.crypto.subtle` (W3C Web Cryptography API), ensuring hardware-accelerated, timing-safe computation without external third-party JavaScript libraries:

```javascript
/**
 * Computes HMAC-SHA256(keyString, nonceBytes) using the native Web Crypto API.
 */
async function computeHmacSha256(keyString, nonceBytes) {
  const enc = new TextEncoder();
  const keyData = enc.encode(keyString);
  const cryptoKey = await window.crypto.subtle.importKey(
    "raw",
    keyData,
    { name: "HMAC", hash: { name: "SHA-256" } },
    false,
    ["sign"]
  );
  const signature = await window.crypto.subtle.sign(
    "HMAC",
    cryptoKey,
    nonceBytes
  );
  return new Uint8Array(signature);
}
```

### 6.3 Handshake Protocol Execution
1. **Subscribe to Auth Status**: PWA subscribes to notifications on `0xC405` (`startNotifications()`).
2. **Read Challenge**: PWA reads `0xC405`.
   - Firmware returns: `[0x01: Tag (1B)] [Nonce (16B)]`.
3. **Compute Response**: PWA extracts the 16-byte nonce and calculates:
   $$\text{HMAC} = \text{HMAC-SHA256}(\text{PIN}, \text{Nonce})$$
4. **Write Response**: PWA writes 33 bytes to `0xC405`:
   - `[0x02: Tag (1B)] [HMAC (32B)]`.
5. **Receive Auth Status**: Firmware verifies the HMAC against the stored shared secret (`123456789P` or custom PIN).
   - If Valid: Firmware notifies `[0x03, 0x00, SessionToken(16B)]`. PWA marks session authenticated and queries current state.
   - If Invalid: Firmware notifies `[0x03, 0x01]`. PWA prompts user to re-enter PIN.
   - If Locked Out: Firmware notifies `[0x03, 0x02]`. PWA warns that the slate is locked for 60 seconds due to 3 consecutive failures.

---

## 7. Deterministic 3-Byte Packet Chunking Engine

### 7.1 ATT MTU Fragmentation on Android
Android devices negotiate ATT MTU values ranging from 23 bytes up to 517 bytes depending on the Bluetooth chipset and Android OS version. A complete E-Slate JSON state payload is typically 280 to 420 bytes.

To guarantee zero packet loss, zero truncation, and zero reliance on Android's underlying MTU negotiation quirks, the PWA implements the deterministic 3-byte framing protocol:

```
+---------------+---------------+--------------------+--------------------------+
| Byte 0        | Byte 1        | Byte 2             | Bytes 3 .. (3 + L - 1)   |
+---------------+---------------+--------------------+--------------------------+
| Chunk Index   | Total Chunks  | Payload Length (L) | Raw Payload Data Slice   |
| (0 to N - 1)  | (N >= 1)      | (1 to 240)         | (Raw UTF-8 Byte Slice)   |
+---------------+---------------+--------------------+--------------------------+
```

### 7.2 Transmission Algorithm
```javascript
async function sendSlateStateChunked(charState, stateObject) {
  const jsonString = JSON.stringify(stateObject);
  const rawBytes = new TextEncoder().encode(jsonString);

  // Maximum slice size bounded to 240 bytes (safe for all MTU sizes)
  const maxSliceSize = 240;
  const totalChunks = Math.ceil(rawBytes.length / maxSliceSize);

  for (let seq = 0; seq < totalChunks; seq++) {
    const start = seq * maxSliceSize;
    const end = Math.min(start + maxSliceSize, rawBytes.length);
    const slice = rawBytes.subarray(start, end);

    const packet = new Uint8Array(3 + slice.length);
    packet[0] = seq;          // Chunk Index
    packet[1] = totalChunks;  // Total Chunks
    packet[2] = slice.length; // Payload Length
    packet.set(slice, 3);

    await charState.writeValueWithResponse(packet);
  }
}
```

### 7.3 UTF-8 Multi-Byte Integrity (🌸 Sakura Emoji Protection)
The Sakura cherry blossom emoji 🌸 is represented by the 4-byte sequence `0xF0 0x9F 0x8C 0xB8`.
- If fragmentation occurs mid-sequence (e.g. `0xF0 0x9F` at the end of chunk 0 and `0x8C 0xB8` at the start of chunk 1), string decoding on raw chunks would corrupt the stream.
- The PWA chunking engine operates strictly at the **`Uint8Array` binary level**.
- Reassembly stitches the raw byte buffer before invoking `new TextDecoder('utf-8').decode(completeBuffer)`. This guarantees 100% fidelity for emojis and extended characters.

---

## 8. Real-Time Telemetry & Android Haptic Feedback

### 8.1 Hardware Button Event Streaming
The XIAO ESP32-S3 ePaper Board (EE04) features physical tactile buttons:
- **KEY1 (GPIO 3)**: Increment TAKE (+1)
- **KEY2 (GPIO 5)**: Increment ROLL (+1)
- **KEY0 (GPIO 2)**: Toggle Invert Mode (Black ↔ White)

When a camera assistant presses a physical button on the physical slate, the firmware updates its internal state, triggers an asynchronous e-paper refresh, and emits a 3-byte notification on Characteristic `0xC403`:
```
[Byte 0: EventCode (0x01=TAKE, 0x02=ROLL, 0x03=INVERT)] [Bytes 1-2: Counter (Big-Endian UInt16)]
```

### 8.2 Vibration Engine Integration
The PWA subscribes to `0xC403` and maps physical button triggers directly to the Android vibration actuator:
```javascript
function onButtonEventReceived(eventCode, counter) {
  if (eventCode === 0x01) {
    // TAKE +1: Crisp 40ms physical tick
    if (navigator.vibrate) navigator.vibrate(40);
    applyTakeIncrementLocal();
  } else if (eventCode === 0x02) {
    // ROLL +1: Double-pulse confirmation
    if (navigator.vibrate) navigator.vibrate([40, 30, 40]);
    applyRollIncrementLocal();
  } else if (eventCode === 0x03) {
    // Invert: 60ms pulse
    if (navigator.vibrate) navigator.vibrate(60);
    toggleInvertLocal();
  }
}
```

### 8.3 Battery Telemetry
Characteristic `0xC404` notifies 4 bytes:
`[mV (UInt16BE)][Percentage (UInt8)][Flags (UInt8)]`
- Bit 0 of Flags indicates USB power / charging.
- The PWA header updates an interactive battery pill showing real-time voltage and battery percentage (e.g. `⚡ 100% (4.18V)` or `🔋 74% (3.82V)`).

---

## 9. PWA Offline Architecture & Installation Lifecycle

### 9.1 Manifest Specification (`manifest.json`)
The Web App Manifest configures standalone execution without browser UI:
- `"display": "standalone"`: Eliminates URL address bars, navigation buttons, and tab switchers.
- `"theme_color": "#09090b"`: Matches Android system bars with the cinematic dark OLED background.
- `"orientation": "portrait"`: Locks clapperboard interface to portrait mode for one-handed operation.
- Complete high-resolution PNG & SVG icons for the home screen and app launcher.

### 9.2 Service Worker Strategy (`sw.js`)
The Service Worker utilizes a **Cache-First / Stale-While-Revalidate** strategy:
1. **Install Event**: Pre-caches `index.html`, `style.css`, `app.js`, `manifest.json`, and icons into a versioned cache (`eslate-pwa-v1`).
2. **Activate Event**: Clears outdated caches during updates and calls `self.clients.claim()`.
3. **Fetch Event**: Serves requested assets directly from cache. If an asset is missing or stale, it fetches in the background and updates the cache.

---

## 10. Film Set Deployment & Operational Flow

```
1. PRE-PRODUCTION (Connected to Internet)
   • 1st AC / DIT visits https://felipesalas.github.io/eink_clap on Android Chrome.
   • Browser displays install prompt: "Añadir a la pantalla de inicio".
   • Tap "Añadir". Icon appears on Android home screen.
   • Service Worker caches 100% of PWA assets.

2. ON LOCATION (No Internet / Remote Set)
   • 1st AC powers on E-Slate. Display shows boot screen with PIN.
   • 1st AC taps E-Slate icon on phone home screen. PWA opens instantly.
   • Tap "Buscar Claqueta" (Connection Pill).
   • Select "E-Slate NEBRALTA" in Chrome dialog.
   • PWA completes HMAC-SHA256 handshake using saved PIN.
   • Status turns "🟢 Conectado: E-Slate NEBRALTA".

3. FILMING WORKFLOW
   • Adjust Scene, Take, Roll, Director, D.O.P., Badges.
   • Tap "📱 HOY" to sync slate date with smartphone clock.
   • Tap "➕ TAKE (+1)" for quick take increments with haptic vibration.
   • Tap "ACTUALIZAR CLAQUETA" to broadcast full state over BLE.
   • Slate refreshes e-paper asynchronously in 3 seconds without freezing BLE.
```

---

## 11. Verification & Compliance Checklist

- [x] Full architectural specification of Web Bluetooth on Android.
- [x] In-depth analysis of W3C Secure Context constraint (`window.isSecureContext`) and dual-tier resolution.
- [x] Zero-dependency Web Crypto API (`crypto.subtle`) HMAC-SHA256 handshake.
- [x] Deterministic 3-byte packet chunking protocol specification.
- [x] UTF-8 multi-byte character and emoji protection (🌸).
- [x] Android vibration engine (`navigator.vibrate`) integration for hardware button events.
- [x] Battery ADC telemetry parsing and display.
- [x] PWA offline caching lifecycle via `sw.js` and `manifest.json`.
