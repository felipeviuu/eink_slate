# E-Slate Digital Clapperboard: Bluetooth Low Energy (BLE) Specification & Security Architecture

**Document Version**: 1.0.0  
**Target Platform**: Seeed Studio XIAO ESP32-S3 Plus (16MB Flash, 8MB PSRAM OPI) + EE04 Carrier Board + 7.5" e-Paper Display (UC8179, 800x480)  
**Companion App**: Native iOS Application (`ios_app/ESlate`, Swift / SwiftUI / CoreBluetooth)  
**Reference Firmware**: `eink_clap/eink_clap.ino`  
**Security Level**: High (Multi-layer: Exclusive Single-Central Lock, Challenge-Response HMAC-SHA256 Handshake, Write-Gated Authorization with GATT Error 0x82, Brute-Force Rate Limiting)

---

## 1. System Overview & Architecture

The **E-Slate BLE Ecosystem** provides an ultra-low latency, bidirectional wireless link between the Seeed Studio XIAO ESP32-S3 Plus e-Paper digital clapperboard and authorized mobile companion devices (specifically Apple iPhone / iPad via CoreBluetooth).

The system operates in **Dual-Mode Coexistence**: the ESP32-S3 radio concurrently maintains the local Wi-Fi SoftAP (`NEBRALTA_Clap` / `192.168.4.1`) and the Bluetooth Low Energy (BLE) GATT server without brownouts or radio collisions.

```
                           ┌───────────────────────────────┐
                           │      Apple iOS Device         │
                           │   (SwiftUI / CoreBluetooth)   │
                           │     ios_app/ESlate            │
                           └───────────────┬───────────────┘
                                           │
                         Bluetooth Low Energy (GATT Client)
                        Link: AES-128 CCM / App: HMAC-SHA256
                                           │
                                           ▼
                           ┌───────────────────────────────┐
                           │    XIAO ESP32-S3 Plus (EE04)  │
                           │     Dual-Mode BLE + Wi-Fi     │
                           │    NimBLE GATT Server Core 0  │
                           └───────────────┬───────────────┘
                                           │
                         Asynchronous Refresh Flag (RAM)
                                           │
                                           ▼
                           ┌───────────────────────────────┐
                           │     7.5" Monochrome e-Paper   │
                           │       (800x480, UC8179)       │
                           │     Refreshed on Core 1       │
                           └───────────────────────────────┘
```

### Architectural Principles
1. **Asynchronous Non-Blocking Updates**: Writes to the BLE GATT server update state variables in RAM and flag `needDisplayUpdate = true`, responding to the iOS client in under 5 ms. The 3–4 second physical e-Paper refresh occurs decoupled on the main loop, preventing BLE connection supervision timeouts.
2. **Deterministic Chunking Across Variable MTUs**: Accommodates ATT MTUs from legacy 23 bytes (20 payload bytes) up to 512 bytes using a 3-byte framing protocol with sequence numbering and length validation.
3. **Multi-Layer Security Architecture**: Solves the unauthorized slate tampering problem via exclusive single-central connection lock, cryptographic challenge-response authentication, and strict write gating.
4. **UTF-8 Multi-Byte Character Integrity**: Preserves raw multi-byte byte sequences across chunk boundaries, ensuring that special glyphs such as the Sakura emoji 🌸 (`\xF0\x9F\x8C\xB8`) are never corrupted.
5. **Real-Time Bidirectional Event Streaming**: Hardware button presses on the physical EE04 carrier board (KEY1 TAKE, KEY2 ROLL, KEY0 Invert) immediately stream telemetry notifications to the companion app for instant haptic feedback.

---

## 2. GAP Advertising & Connection Parameters

### 2.1 Peripheral Discovery Profile

| Parameter | Specification | Technical Rationale |
| :--- | :--- | :--- |
| **Complete Local Name** | `E-Slate NEBRALTA` | Exactly 17 bytes ASCII. Matches cinematic brand identity. |
| **Advertising Mode** | `ADV_IND` (Connectable Undirected) | Allows swift discovery and immediate connection. |
| **Advertising Interval** | 100 ms (Min: `0x00A0`, Max: `0x00F0` / 150 ms) | Apple Recommended: optimizes iPhone scan latency without draining battery. |
| **Advertising Flags** | `0x06` (`LE_GENERAL_DISC_MODE \| BR_EDR_NOT_SUPPORTED`) | BLE-only peripheral advertising continuously while disconnected. |
| **Service Solicitation** | `0xF47AC10B-58CC-4372-A567-0E02B2C3C400` | Placed in Advertising / Scan Response payload for fast filtering. |
| **Appearance** | `0x0080` (Generic Display) | Bluetooth SIG standard appearance category. |
| **Max Concurrent Centrals** | `1` (Exclusive Connection Lock) | Advertising is stopped immediately once 1 central connects. |

### 2.2 Connection Parameters & Negotiation

| Parameter | Recommended Value | Permitted Range | Notes |
| :--- | :--- | :--- | :--- |
| **Connection Interval** | 30.0 ms (`0x0018`) | 15.0 ms – 45.0 ms | Low latency for rapid state and button sync. |
| **Slave Latency** | 0 | 0 – 4 | Zero latency ensures immediate hardware button dispatch. |
| **Supervision Timeout** | 4000 ms (`0x0190`) | 2000 ms – 6000 ms | Prevents premature disconnection during heavy CPU loads. |
| **ATT MTU Default** | 23 bytes | 23 bytes (Min) | Baseline fallback if MTU exchange is omitted. |
| **ATT MTU Max (Target)** | 512 bytes | Up to 512 bytes | Negotiated by iOS CoreBluetooth (`maximumUpdateValueLength`). |

---

## 3. GATT Profile & Service Hierarchy

The GATT database is organized into four distinct services:
1. **Generic Access Service (`0x1800`)**: Device name and appearance.
2. **Device Information Service (`0x180A`)**: Hardware revision, board model, and firmware version.
3. **Battery Service (`0x180F`)**: Standard Bluetooth SIG battery level percentage.
4. **E-Slate Primary Cinema Service (`0xF47AC10B-58CC-4372-A567-0E02B2C3C400`)**: Proprietary 128-bit cinema clapperboard service.

```
[Device: E-Slate NEBRALTA]
 ├── Generic Access Service (UUID: 0x1800)
 │    ├── Device Name (UUID: 0x2A00): "E-Slate NEBRALTA" [Read]
 │    └── Appearance (UUID: 0x2A01): 0x0080 [Read]
 │
 ├── Device Information Service (UUID: 0x180A)
 │    ├── Manufacturer Name (UUID: 0x2A29): "Seeed Studio / NEBRALTA" [Read]
 │    ├── Model Number (UUID: 0x2A24): "XIAO-ESP32S3-PLUS-EE04" [Read]
 │    ├── Firmware Revision (UUID: 0x2A26): "2.1.0-BLE" [Read]
 │    └── Hardware Revision (UUID: 0x2A27): "EE04-7.5INCH" [Read]
 │
 ├── Battery Service (UUID: 0x180F)
 │    └── Battery Level (UUID: 0x2A19): [uint8_t: 0-100%] [Read, Notify]
 │
 └── E-Slate Primary Cinema Service (UUID: F47AC10B-58CC-4372-A567-0E02B2C3C400)
      ├── Characteristic 1: State Sync (UUID: ...C401) [Read, Write, Notify]
      ├── Characteristic 2: Command / Action (UUID: ...C402) [Write, Write Without Response]
      ├── Characteristic 3: Button Event (UUID: ...C403) [Read, Notify]
      ├── Characteristic 4: Battery Telemetry (UUID: ...C404) [Read, Notify]
      └── Characteristic 5: Security / Auth Lock (UUID: ...C405) [Read, Write, Notify]
```

### 3.1 Characteristic Definition Matrix

| Short ID | Full 128-bit UUID | Characteristic Name | Properties | Access Permissions | Payload Format |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0xC401` | `F47AC10B-58CC-4372-A567-0E02B2C3C401` | **State Sync** | Read, Write, Notify | Read: Open<br>Write: **Authenticated Session Only** | 3-Byte Chunking Header + UTF-8 JSON Payload |
| `0xC402` | `F47AC10B-58CC-4372-A567-0E02B2C3C402` | **Command / Action** | Write, Write No Resp | Write: **Authenticated Session Only** | 1 to 2 Bytes Binary Opcode |
| `0xC403` | `F47AC10B-58CC-4372-A567-0E02B2C3C403` | **Button Event Telemetry** | Read, Notify | Read: Open<br>Notify: CCCD (`0x2902`) | 3 Bytes Binary `[EventCode: 1B][Counter: 2B]` |
| `0xC404` | `F47AC10B-58CC-4372-A567-0E02B2C3C404` | **Battery Telemetry** | Read, Notify | Read: Open<br>Notify: CCCD (`0x2902`) | 4 Bytes Binary `[mV: 2B][Percent: 1B][Flags: 1B]` |
| `0xC405` | `F47AC10B-58CC-4372-A567-0E02B2C3C405` | **Security / Auth Lock** | Read, Write, Notify | Read: Open (Challenge)<br>Write: Open (Handshake port) | Binary Handshake Frames (`Tag 0x01, 0x02, 0x03`) |

---

## 4. Application Data Models & Characteristics in Detail

### 4.1 Characteristic `0xC401`: State Sync

The **State Sync** characteristic handles full clapperboard state serialization.
- **Reading**: Returns the current slate state encoded as UTF-8 JSON (framed with chunk headers if payload exceeds ATT MTU - 3).
- **Writing**: Central writes an updated JSON payload.
  * If unauthenticated, the write is immediately rejected with GATT Application Error `0x82` (`ESP_GATT_INSUF_AUTHORIZATION`).
  * If authenticated, the firmware parses the JSON, updates all global variables, enforces uppercase strings, preserves the 🌸 emoji, flags `needDisplayUpdate = true`, and emits a notification to subscribed centrals.

#### JSON State Schema Definition
```json
{
  "roll": "A001",
  "scene": "35A",
  "take": "1",
  "prod": "NEBRALTA 🌸",
  "dir": "DIRECTOR",
  "dop": "FELIPELOTAS1",
  "note": "ISO 120",
  "date": "25.09.2026",
  "timecode": "00:00:00",
  "cam": "A",
  "sound": "SYNC",
  "loc": "INT",
  "light": "DAY",
  "layout": 0,
  "invert": false,
  "vis": {
    "roll": true,
    "scene": true,
    "take": true,
    "prod": true,
    "dir": true,
    "dop": true,
    "cam": true,
    "sound": true,
    "loc": true,
    "light": true,
    "date": true,
    "timecode": true,
    "note": true,
    "bat": true
  }
}
```

#### Field Specifications & Constraints

| JSON Key | Type | Max Chars | Description / Permitted Values | Default |
| :--- | :--- | :--- | :--- | :--- |
| `"roll"` | String | 10 | Roll identifier. Alphanumeric (e.g. `"A001"`, `"B12"`). | `"A001"` |
| `"scene"` | String | 10 | Scene designation (e.g. `"35A"`, `"102"`). | `"1A"` |
| `"take"` | String | 10 | Take count (e.g. `"1"`, `"4B"`). | `"1"` |
| `"prod"` | String | 40 | Production title. Supports UTF-8 Sakura emoji 🌸 (`\xF0\x9F\x8C\xB8`). | `"NEBRALTA 🌸"` |
| `"dir"` | String | 35 | Director name (also accepts alias `"director"` on write). | `"DIRECTOR"` |
| `"dop"` | String | 35 | Director of Photography / Cinematographer name. | `"FELIPELOTAS1"` |
| `"note"` | String | 35 | Notes (e.g. `"ISO 120"`, `"50MM T1.5"`). | `"ISO 120"` |
| `"date"` | String | 15 | Date formatted as `DD.MM.YYYY` (e.g. `"25.09.2026"`). Auto-synced from iPhone. | System Date |
| `"timecode"` | String | 8 | Timecode formatted as `HH:MM:SS` (e.g. `"14:32:00"`). Advanced locally on 1 Hz ticker, synced via BLE/Web. | `"00:00:00"` |
| `"cam"` | String | 5 | Camera letter or identifier (e.g. `"A"`, `"B"`, `"C"`). | `"A"` |
| `"sound"` | String | 4 | Sound sync mode. Strictly `"SYNC"` or `"MOS"`. | `"SYNC"` |
| `"loc"` | String | 3 | Location mode. Strictly `"INT"` or `"EXT"`. | `"INT"` |
| `"light"` | String | 5 | Lighting condition. Strictly `"DAY"` or `"NIGHT"`. | `"DAY"` |
| `"layout"` | Int | 1 | Layout preset: `0` = Cinema Pro, `1` = Big Slate, `2` = Commercial. | `0` |
| `"invert"` | Bool | 1 | Inverted dark mode: `false` = White BG / Black Text; `true` = Black BG / White Text. | `false` |
| `"vis"` | Object | - | 14 boolean flags controlling visual visibility of individual fields (including timecode). | All `true` |

---

### 4.2 Characteristic `0xC402`: Command / Quick Action

Used for atomic, single-operation commands without transmitting the entire JSON state dictionary.
- **Properties**: Write, Write Without Response.
- **Security**: Requires authenticated session (`session_authenticated == true`).
- **Binary Format**:
  `[Byte 0: Opcode] [Byte 1: Optional Parameter]`

```
+--------------------+------------------------+
| Byte 0: Opcode     | Byte 1: Parameter      |
| (uint8_t)          | (Optional / Contextual)|
+--------------------+------------------------+
```

#### Opcode Definitions

| Opcode | Name | Parameter | Description |
| :--- | :--- | :--- | :--- |
| `0x01` | **INCREMENT_TAKE** | None (0x00) | Increments TAKE count (`"1"` -> `"2"`, `"3A"` -> `"4A"`). Triggers e-Paper refresh. |
| `0x02` | **INCREMENT_ROLL** | None (0x00) | Increments ROLL count (`"A001"` -> `"A002"`, `"B"` -> `"C"`). Triggers e-Paper refresh. |
| `0x03` | **TOGGLE_INVERT** | None (0x00) | Inverts background and text colors. Triggers e-Paper refresh. |
| `0x04` | **FORCE_REFRESH** | None (0x00) | Forces full display redraw via `needDisplayUpdate = true`. |
| `0x05` | **DISMISS_BOOT** | None (0x00) | Clears boot instructions screen and renders active slate immediately. |
| `0x06` | **SET_LAYOUT** | `0` to `2` | Changes layout: `0` (Cinema Pro), `1` (Big Slate), `2` (Commercial). |
| `0x07` | **TOGGLE_TIMECODE**| None (0x00) | Toggles 1 Hz timecode clock running state (`timecodeRunning`). |

---

### 4.3 Characteristic `0xC403`: Button Event Telemetry

Emitted in real-time when physical buttons on the EE04 carrier board are pressed.
- **Properties**: Read, Notify.
- **Payload Format**: 3 Bytes Big-Endian
  `[Byte 0: EventCode (uint8_t)] [Byte 1-2: EventCounter (uint16_t Big-Endian)]`

```
+------------------------+------------------------------------+
| Byte 0: EventCode (1B) | Bytes 1-2: EventCounter (2B UInt16)|
+------------------------+------------------------------------+
```

#### Event Codes

| EventCode | Hardware Trigger | Description |
| :--- | :--- | :--- |
| `0x01` | **KEY1 (GPIO 3)** | Physical TAKE button pressed. +1 TAKE applied. iOS triggers haptic feedback. |
| `0x02` | **KEY2 (GPIO 5)** | Physical ROLL button pressed. +1 ROLL applied. iOS triggers haptic feedback. |
| `0x03` | **KEY0 (GPIO 2)** | Physical Invert button pressed. Color scheme toggled. |
| `0x04` | **ANY_KEY (Boot)** | Physical button pressed during boot screen. Boot screen dismissed. |

*Note: The EE04 firmware enforces a 1500 ms anti-bounce cooldown per physical button to prevent accidental duplicate triggers on film sets.*

---

### 4.4 Characteristic `0xC404`: Battery Telemetry

High-resolution telemetry measured on the XIAO ESP32-S3 analog pin `PIN_BAT_ADC` (GPIO 1) enabled by `PIN_ADC_EN` (GPIO 6).
- **Properties**: Read, Notify.
- **Payload Format**: 4 Bytes Big-Endian
  `[Byte 0-1: Voltage in mV (uint16_t)] [Byte 2: Percentage (uint8_t: 0-100)] [Byte 3: Flags (uint8_t)]`

```
+----------------------------+-------------------+--------------------+
| Bytes 0-1: Voltage (mV)    | Byte 2: SOC (%)   | Byte 3: Status Bit |
| uint16_t Big-Endian        | uint8_t (0-100)   | uint8_t bitmask    |
+----------------------------+-------------------+--------------------+
```

#### Status Flag Bitmask (Byte 3)

| Bit | Identifier | Condition | Description |
| :--- | :--- | :--- | :--- |
| **0** | `FLAG_USB_CONNECTED_OR_FULL` | `Voltage >= 4150 mV` | Device is powered via USB-C or fully charged. |
| **1** | `FLAG_BATTERY_LOW` | `Voltage < 3550 mV` | Battery is low (< 20%). LED pulses warning. |
| **2-7** | `RESERVED` | - | Reserved for future telemetry expansions (set to 0). |

#### Conversion Formula
- Raw ADC to Voltage (mV): `Voltage_mV = (uint16_t)(((raw_adc / 4096.0f) * 7.16f) * 1000.0f)`
- Voltage to Percentage: `Percent = clamp((Voltage_V - 3.40f) / (4.15f - 3.40f) * 100.0f, 0, 100)`

---

### 4.5 Characteristic `0xC405`: Security / Auth Lock

Cryptographic challenge-response handshake port. Protects against unauthorized modification of clapperboard metadata.
- **Properties**: Read, Write, Notify.
- **Default Pre-Shared Secret**: `"123456789P"` (matches default Wi-Fi password; customizable in iOS settings).

#### Handshake Tag Protocol

| Message Tag | Direction | Format | Description |
| :--- | :--- | :--- | :--- |
| `0x01` (**CHALLENGE**) | ESP32 -> Central (Read) | `[0x01: Tag (1B)] [Nonce (16B)]` | ESP32 generates fresh 16-byte cryptographically secure pseudorandom nonce. |
| `0x02` (**RESPONSE**) | Central -> ESP32 (Write) | `[0x02: Tag (1B)] [HMAC-SHA256 (32B)]` | Central computes `HMAC-SHA256(Nonce, PreSharedKey)` and writes 33 bytes. |
| `0x03` (**STATUS**) | ESP32 -> Central (Notify) | `[0x03: Tag (1B)] [Status (1B)] [SessionToken (16B)]` *(Variable Length: 18B on Success, 2B on Failure/Lockout)* | Result: `0x00` = AUTH_SUCCESS (18 bytes with 16B Token), `0x01` = AUTH_FAILED (2 bytes `[0x03, 0x01]`), `0x02` = LOCKED_OUT (2 bytes `[0x03, 0x02]`). |

> **Variable-Length Notification Framing**:
> - On `AUTH_SUCCESS` (`0x00`), the notification payload is 18 bytes: `[0x03][0x00][SessionToken (16B)]`.
> - On `AUTH_FAILED` (`0x01`) and `LOCKED_OUT` (`0x02`), the notification payload is 2 bytes: `[0x03][Status]`.
> - Central clients must guard `data.count >= 2`, and extract `SessionToken` only when `data.count >= 18 && Status == 0x00`.

---

## 5. MTU Negotiation & 3-Byte Packet Chunking Engine

### 5.1 The MTU Dilemma
Standard Bluetooth Low Energy defines a baseline ATT MTU of **23 bytes**, which leaves **20 bytes** of usable payload after deducting the 3-byte ATT opcode and handle overhead.
A complete E-Slate JSON state payload is typically **260 to 420 bytes**.
- When connected to modern iOS devices (iOS 15–18), the OS initiates an MTU exchange negotiating up to **512 bytes**.
- However, if the MTU exchange fails, is delayed, or occurs over older Bluetooth bridges, the payload exceeds the MTU.
- Attempting to send unfragmented data larger than `ATT_MTU - 3` results in immediate transmission failure or silent truncation.

### 5.2 Deterministic 3-Byte Framing Header
To guarantee 100% reliability regardless of negotiated MTU, all transmissions over State Sync Characteristic `0xC401` utilize a deterministic 3-byte framing header:

```
+---------------+---------------+--------------------+--------------------------+
| Byte 0        | Byte 1        | Byte 2             | Bytes 3 .. (3 + L - 1)   |
+---------------+---------------+--------------------+--------------------------+
| Chunk Index   | Total Chunks  | Payload Length (L) | Raw Payload Data Slice   |
| (0 to N - 1)  | (N >= 1)      | (1 to 240)         | (Raw UTF-8 Byte Slice)   |
+---------------+---------------+--------------------+--------------------------+
```

#### Framing Specification:
- **`Byte 0` (Chunk Index)**: 0-indexed sequence position (`0, 1, ..., N-1`).
- **`Byte 1` (Total Chunks)**: Total chunk count (`N >= 1`).
- **`Byte 2` (Payload Length)**: Number of raw payload bytes in this frame (`L`).
- **`Bytes 3 .. (3 + L - 1)`**: The exact slice of payload bytes.
- **Header Validation Bounds**: If `Total Chunks < 1` or `Chunk Index >= Total Chunks`, the frame is invalid. The reassembly engine immediately aborts with `ERROR_INVALID_HEADER` and resets the reassembly buffer to prevent unrecoverable zombie states.

### 5.3 Chunk Size & Splitting Logic
Because Byte 2 (Payload Length) is an 8-bit unsigned integer (`uint8_t`), the maximum data slice per chunk is bounded by both the negotiated MTU and the 240-byte ceiling:

```python
effective_mtu = negotiated_mtu  # e.g. 23, 64, 128, 512
max_att_payload = effective_mtu - 3  # Deduct ATT header (3 bytes)
max_slice_size = min(max_att_payload - 3, 240)  # Deduct 3-byte framing header, max 240

if len(raw_payload_bytes) <= max_slice_size:
    # Single-packet transmission (zero fragmentation overhead)
    chunks = [bytes([0, 1, len(raw_payload_bytes)]) + raw_payload_bytes]
else:
    # Multi-packet chunking
    total_chunks = math.ceil(len(raw_payload_bytes) / max_slice_size)
    for seq in range(total_chunks):
        slice_data = raw_payload_bytes[seq * max_slice_size : (seq + 1) * max_slice_size]
        chunks.append(bytes([seq, total_chunks, len(slice_data)]) + slice_data)
```

### 5.4 Reassembly State Machine & UTF-8 Multi-Byte Integrity
- **Raw Byte Level Reassembly**: Fragmentation and reassembly operate strictly on **raw binary bytes** (`uint8_t[]`), never on intermediate strings.
- **Multi-Byte Splitting Safety**: The Sakura emoji 🌸 is a 4-byte UTF-8 sequence (`0xF0 0x9F 0x8C 0xB8`). If `0xF0 0x9F` falls at the tail of Chunk 0 and `0x8C 0xB8` at the head of Chunk 1, **no UTF-8 decoding occurs until all chunks are received**.
- **Guard Timer & Timeout Recovery**: The reassembly engine on both ESP32-S3 and iOS initializes a **3000 ms guard timer** upon receiving Chunk 0. If all `N` chunks are not received within 3000 ms, partial state is wiped. If an arriving packet after expiration is Chunk 0 (`seq == 0`), it immediately starts a new transmission rather than being consumed as a timeout error on the expired session. If the packet is `seq != 0`, it returns `ERROR_TIMEOUT`.
- **Strict Sequencing**: If a chunk is received out-of-order (`ChunkIndex != expectedIndex`), the reassembly buffer is reset to avoid corrupted memory.

---

## 6. Multi-Layer Security Architecture

To fulfill the authoritative security mandate and prevent unauthorized crew members or malicious actors from altering slate metadata during filming, the E-Slate implements a **4-Layer Defense Architecture**:

```
+---------------------------------------------------------------------------------+
|                       E-SLATE 4-LAYER SECURITY ARCHITECTURE                     |
+---------------------------------------------------------------------------------+
|                                                                                 |
|  [ LAYER 1: Connection Exclusivity Lock ]                                        |
|  • Single Central Limit: Max Concurrent Connections = 1.                        |
|  • Stop BLE Advertising immediately when an authorized central connects.         |
|  • Reject & immediately disconnect any rogue secondary central attempts.        |
|                                                                                 |
|  [ LAYER 2: Link Layer Encryption ]                                             |
|  • Optional LE Secure Connections with AES-128 CCM link encryption.             |
|                                                                                 |
|  [ LAYER 3: Application Challenge-Response Handshake (0xC405) ]                 |
|  • Central reads 0xC405 -> ESP32 returns 16-byte random Nonce.                  |
|  • Central computes HMAC-SHA256(Nonce, SecretPIN) -> writes back 32-byte HMAC.  |
|  • ESP32 verifies HMAC in constant time; sets session_authenticated = true.     |
|                                                                                 |
|  [ LAYER 4: State Guard & Unauthorized Write Gating ]                           |
|  • All writes to State Sync (0xC401) and Command (0xC402) gated by             |
|    session_authenticated == true.                                               |
|  • Unauthorized writes rejected with GATT Application Error 0x82.               |
|  • Brute-Force Rate Limiting: 3 failed attempts triggers 60s quarantine lockout.|
+---------------------------------------------------------------------------------+
```

### 6.1 Layer 1: Exclusive Single-Central Connection Lock
1. When the ESP32-S3 accepts a connection from an iOS central, it executes:
   ```cpp
   NimBLEDevice::getAdvertising()->stop();
   ```
2. The peripheral vanishes from all BLE scan lists while connected.
3. If a rogue device attempts a direct connection knowing the MAC address, the connection callback verifies `activeConnections >= 1` and immediately calls:
   ```cpp
   pServer->disconnect(rogueConnId);
   ```
4. Upon clean or unexpected disconnection of the authorized central, `session_authenticated` is immediately revoked and BLE advertising resumes after a 500 ms safety debounce.

### 6.2 Layer 3: HMAC-SHA256 Challenge-Response Handshake
```
   iOS Central                                           ESP32-S3 Peripheral
        │                                                         │
        │ ─── 1. GATT Read Request (0xC405) ────────────────────> │
        │                                                         │ [Generates 16B Random Nonce]
        │ <── 2. GATT Read Response [0x01 (Tag)][16B Nonce] ───── │
        │                                                         │
   [Computes HMAC-SHA256]                                         │
   HMAC(Nonce, Key="123456789P")                                  │
        │                                                         │
        │ ─── 3. GATT Write Request [0x02 (Tag)][32B HMAC] ─────> │
        │                                                         │ [Validates HMAC in constant-time]
        │                                                         │ [Matches -> session_authenticated = true]
        │ <── 4. GATT Notify [0x03 (Tag)][0x00 (OK)][16B Token] ── │
        │                                                         │
```

### 6.3 Layer 4: State Guard & Unauthorized Write Rejection
- Any write received on `0xC401` (State Sync) or `0xC402` (Command) before completing Layer 3 triggers an immediate GATT application error:
  ```cpp
  // ESP_GATT_INSUF_AUTHORIZATION = 0x82
  return pCharacteristic->sendResponse(connId, transId, 0x82, nullptr, 0);
  ```
- **Brute-Force Rate Limiting**:
  * An invalid HMAC signature increments `failed_auth_attempts`.
  * If `failed_auth_attempts >= 3`, the peripheral enters **Quarantine Lockout** for 60 seconds.
  * During lockout, any auth attempt returns `0x02` (`LOCKED_OUT`) and the onboard LED flashes rapid alert pulses.
  * Once the 60-second lockout window expires, the failure counter resets to 0, granting users normal 3-attempt tolerance rather than an immediate 1-strike re-lockout.

---

## 7. Data Formatting & Normalization Rules

### 7.1 Forced Uppercase Coercion (ALL CAPS)
To maintain the standard film clapperboard aesthetic, all text fields are coerced to uppercase ASCII upon parsing:
- `"roll"`, `"scene"`, `"take"`, `"prod"`, `"dir"`, `"dop"`, `"note"`, `"cam"`, `"sound"`, `"loc"`, `"light"`.
- Coercion logic processes individual ASCII characters `[a-z]` -> `[A-Z]` while leaving multi-byte UTF-8 sequences (such as 🌸 `\xF0\x9F\x8C\xB8`) unmodified.

### 7.2 Alphanumeric Take Increment Algorithm
Matching `eink_clap.ino:798-815`:
```
function incrementTake(takeStr):
    if takeStr is pure integer:
        return String(toInt(takeStr) + 1)
    else if takeStr starts with digits (e.g. "4B"):
        prefixNum = leadingDigits(takeStr) + 1
        suffix = remainingChars(takeStr)
        return String(prefixNum) + suffix
    else:
        return "1"
```

### 7.3 Alphanumeric Roll Increment Algorithm
Matching `eink_clap.ino:818-850`:
```
function incrementRoll(rollStr):
    if rollStr contains digits (e.g. "A001" or "ROLL09"):
        prefix = charsBeforeDigits(rollStr)
        numStr = digits(rollStr)
        nextNum = toInt(numStr) + 1
        return prefix + formatWithLeadingZeros(nextNum, length(numStr))
    else if length(rollStr) == 1 and isAlpha(rollStr):
        nextChar = rollStr[0] + 1
        if nextChar > 'Z': nextChar = 'A'
        return String(nextChar)
    else:
        return "A001"
```

---

## 8. Dual-Core FreeRTOS Coexistence & Radio Stability

The Seeed Studio XIAO ESP32-S3 features a dual-core Xtensa LX7 processor operating at 160 MHz.

```
+--------------------------------------------------------------------+
|                        XIAO ESP32-S3 PLUS                          |
+----------------------------------+---------------------------------+
|             CORE 0               |             CORE 1              |
|       (Radio / Protocol)         |      (Application / Render)     |
+----------------------------------+---------------------------------+
| • Wi-Fi SoftAP ("NEBRALTA_Clap") | • Main Arduino loop()           |
| • HTTP Web Server (Port 80)      | • Hardware button debouncing    |
| • NimBLE GATT Server             | • ADC battery sampling (GPIO 1) |
| • Nonce generation & HMAC verify | • e-Paper display refresh       |
| • Non-blocking state update (<5ms)|   (UC8179 SPI 800x480, ~3.5s)   |
+----------------------------------+---------------------------------+
```

### Critical Radio & Power Safeguards
1. **NimBLE-Arduino Stack**: Consumes ~25 KB RAM (compared to Bluedroid's >130 KB), preventing heap exhaustion when Wi-Fi and BLE run concurrently.
2. **Brownout Prevention**: The brownout detector is masked and CPU frequency is locked to 160 MHz. Wi-Fi TX power is constrained to `WIFI_POWER_15dBm` to prevent instantaneous current spikes from collapsing the 3.3V LDO regulator on battery operation.
3. **Decoupled Render Pipeline**: When BLE receives state updates or opcodes, it sets `needDisplayUpdate = true` and returns immediately. Core 1 checks this flag in its main loop and initiates the synchronous SPI e-Paper refresh without stalling Core 0.

---

## 9. Verification & Automated Test Specification

The protocol specification is verified using the executable test suite `tests/test_ble_protocol.py`.

### Test Coverage Matrix

| Test ID | Module | Description | Acceptance Criteria |
| :--- | :--- | :--- | :--- |
| **TC-01** | `test_json_serialization_all_fields_and_visibilities` | Full 12 fields, 13 visibilities, layout, invert | 100% field preservation across JSON round-trip |
| **TC-02** | `test_utf8_emoji_sakura_preservation` | UTF-8 `\xF0\x9F\x8C\xB8` inside `"prod"` | Raw byte preservation without corruption |
| **TC-03** | `test_all_caps_normalization` | Lowercase inputs transformed to uppercase | All text fields normalized; emoji unaffected |
| **TC-04** | `test_chunking_and_reassembly_mtu_23` | Legacy MTU 23 fragmentation (~25 chunks) | Perfect reassembly, correct seq & total |
| **TC-05** | `test_chunking_and_reassembly_mtu_64` | Medium MTU 64 fragmentation (~6 chunks) | Perfect reassembly |
| **TC-06** | `test_chunking_and_reassembly_mtu_128` | High MTU 128 fragmentation (~3 chunks) | Perfect reassembly |
| **TC-07** | `test_chunking_and_reassembly_mtu_512` | Modern iOS MTU 512 (single chunk) | Transmitted as single unfragmented frame |
| **TC-08** | `test_utf8_emoji_split_across_chunk_boundary` | 4-byte 🌸 split across chunk boundary | No decoding error; byte-stream reassembly intact |
| **TC-09** | `test_reassembly_out_of_order_chunk_rejection` | Chunk 2 received before Chunk 1 | Out-of-order rejection; buffer cleared |
| **TC-10** | `test_reassembly_timeout_expiration` | Guard timer > 3000 ms expires | Buffer discarded; timeout logged |
| **TC-11** | `test_hmac_sha256_handshake_success` | Challenge nonce + valid key | Authentication success (`0x00`) |
| **TC-12** | `test_hmac_sha256_handshake_invalid_key_rejected` | Challenge nonce + invalid key | Authentication failure (`0x01`); access denied |
| **TC-13** | `test_security_gating_unauthorized_write_rejected` | Write to 0xC401/0xC402 while unauthenticated | Simulated GATT error `0x82` returned |
| **TC-14** | `test_brute_force_lockout_after_three_failures` | 3 failed auth attempts in sequence | Enters 60s quarantine lockout; rejects attempt 4 |
| **TC-15** | `test_exclusive_connection_lock` | 2nd central attempts connection | 2nd connection rejected; advertising disabled |
| **TC-16** | `test_command_opcodes_and_smart_increments` | Opcodes 0x01, 0x02, 0x03, 0x06 | Correct TAKE/ROLL increment & mode toggles |
| **TC-17** | `test_button_event_telemetry_packing` | Hardware buttons KEY1, KEY2, KEY0 | Correct 3-byte binary packing & counter |
| **TC-18** | `test_battery_telemetry_encoding_and_flags` | ADC mV, percentage calculation, status bits | Correct 4-byte binary packing & flag bitmask |

### Command to Run Verification
```bash
python3 tests/test_ble_protocol.py
```
Expected Output: All 18 tests passing with return code `0`.
