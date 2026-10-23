/**
 * E-Slate Cinema Controller — Progressive Web App (PWA)
 * Web Bluetooth GATT Client & Cryptographic Controller for Android & Chrome
 * Version: 1.0.0
 */

// ============================================================================
// 1. GATT CONSTANTS & PROTOCOL IDENTIFIERS (docs/BLE_SPEC.md)
// ============================================================================

const SLATE_SERVICE_UUID = 'f47ac10b-58cc-4372-a567-0e02b2c3c400';
const CHAR_STATE_UUID   = 'f47ac10b-58cc-4372-a567-0e02b2c3c401'; // State Sync (R/W/N)
const CHAR_CMD_UUID     = 'f47ac10b-58cc-4372-a567-0e02b2c3c402'; // Command / Action (W)
const CHAR_BTN_UUID     = 'f47ac10b-58cc-4372-a567-0e02b2c3c403'; // Button Telemetry (R/N)
const CHAR_BAT_UUID     = 'f47ac10b-58cc-4372-a567-0e02b2c3c404'; // Battery Telemetry (R/N)
const CHAR_AUTH_UUID    = 'f47ac10b-58cc-4372-a567-0e02b2c3c405'; // Security Lock (R/W/N)

const BATTERY_SERVICE_UUID = '0000180f-0000-1000-8000-00805f9b34fb';
const BATTERY_LEVEL_UUID   = '00002a19-0000-1000-8000-00805f9b34fb';

const DEFAULT_PIN = '123456789P';

// Opcode Definitions (docs/BLE_SPEC.md § 4.2 & SCOPE.md)
const CMD_OPCODE_INC_TAKE        = 0x01;
const CMD_OPCODE_INC_ROLL        = 0x02;
const CMD_OPCODE_TOGGLE_INVERT   = 0x03;
const CMD_OPCODE_FORCE_REFRESH   = 0x04;
const CMD_OPCODE_DISMISS_BOOT    = 0x05;
const CMD_OPCODE_SET_LAYOUT      = 0x06;
const CMD_OPCODE_TOGGLE_TIMECODE = 0x07;
const CMD_OPCODE_SET_TC_SLOT     = 0x08;
const CMD_OPCODE_SET_TC_SIZE     = 0x09;
const CMD_OPCODE_SET_TC_FPS      = 0x0A;
const CMD_OPCODE_SET_TC_VIS      = 0x0B;
const CMD_OPCODE_JAM_TOD         = 0x0C;

// ============================================================================
// 2. SLATE STATE MANAGEMENT & FRAME MATH
// ============================================================================

const slateState = {
  roll: 'A001',
  scene: '35A',
  take: '1',
  prod: 'NEBRALTA 🌸',
  dir: 'DIRECTOR',
  dop: 'FELIPELOTAS1',
  note: 'ISO 120',
  date: '25.09.2026',
  timecode: '00:00:00',
  tc_slot: 0,        // 0: Top Header, 1: Lower Bar, 2: Center Hero
  tc_size: 1,        // 0: Medium (24px), 1: Large (40px), 2: Giant XL (64px)
  tc_fps: '24',      // '23.976', '24', '25', '29.97', '30'
  tc_running: true,  // 1 Hz partial update ticker active
  cam: 'A',
  sound: 'SYNC',
  loc: 'INT',
  light: 'DAY',
  layout: 0,
  invert: false,
  vis: {
    roll: true,
    scene: true,
    take: true,
    prod: true,
    dir: true,
    dop: true,
    timecode: true,
    cam: true,
    sound: true,
    loc: true,
    light: true,
    date: true,
    note: true,
    bat: true
  }
};

const FPS_PROFILES = {
  '23.976': { value: 23.976, opcodeIdx: 0 },
  '24':     { value: 24.0,   opcodeIdx: 1 },
  '25':     { value: 25.0,   opcodeIdx: 2 },
  '29.97':  { value: 29.97,  opcodeIdx: 3 },
  '30':     { value: 30.0,   opcodeIdx: 4 }
};

function parseTimecodeComponents(tcStr) {
  if (!tcStr) return [0, 0, 0];
  const parts = tcStr.split(':').map(Number);
  const h = parts.length > 0 && !isNaN(parts[0]) ? Math.max(0, Math.min(23, parts[0])) : 0;
  const m = parts.length > 1 && !isNaN(parts[1]) ? Math.max(0, Math.min(59, parts[1])) : 0;
  const s = parts.length > 2 && !isNaN(parts[2]) ? Math.max(0, Math.min(59, parts[2])) : 0;
  return [h, m, s];
}

function formatTimecodeString(h, m, s) {
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

function timecodeToTotalFrames(tcStr, fps = 24) {
  const [h, m, s] = parseTimecodeComponents(tcStr);
  const totalSeconds = (h * 3600) + (m * 60) + s;
  return Math.round(totalSeconds * fps);
}

function totalFramesToTimecode(totalFrames, fps = 24) {
  const framesInDay = Math.round(86400 * fps);
  let frames = ((totalFrames % framesInDay) + framesInDay) % framesInDay;
  const totalSeconds = Math.floor((frames + 0.5) / fps);
  const s = totalSeconds % 60;
  const m = Math.floor(totalSeconds / 60) % 60;
  const h = Math.floor(totalSeconds / 3600) % 24;
  return formatTimecodeString(h, m, s);
}

function applyTimecodeSecondOffset(tcStr, deltaSeconds) {
  const [h, m, s] = parseTimecodeComponents(tcStr);
  let totalSec = (h * 3600) + (m * 60) + s + deltaSeconds;
  const daySec = 86400;
  while (totalSec < 0) totalSec += daySec;
  totalSec %= daySec;
  const newH = Math.floor(totalSec / 3600);
  const newM = Math.floor((totalSec % 3600) / 60);
  const newS = totalSec % 60;
  return formatTimecodeString(newH, newM, newS);
}

function applyTimecodeFrameOffset(tcStr, deltaFrames, fpsStr = '24') {
  const fps = FPS_PROFILES[fpsStr] ? FPS_PROFILES[fpsStr].value : 24.0;
  const currentFrames = timecodeToTotalFrames(tcStr, fps);
  return totalFramesToTimecode(currentFrames + deltaFrames, fps);
}

// Connection & GATT State
let bleDevice = null;
let gattServer = null;
let charState = null;
let charCmd = null;
let charBtn = null;
let charBat = null;
let charAuth = null;

let isConnecting = false;
let isConnected = false;
let isAuthenticated = false;
let isUpdating = false;

// Reassembly buffer for incoming chunked state
let incomingChunks = [];
let totalExpectedChunks = 0;
let reassemblyTimer = null;

// ============================================================================
// 3. CRYPTOGRAPHIC ENGINE (Native Web Crypto HMAC-SHA256)
// ============================================================================

/**
 * Computes hardware-accelerated HMAC-SHA256 using window.crypto.subtle.
 * @param {string} keyString - Pre-shared secret / PIN code.
 * @param {Uint8Array} nonceBytes - 16-byte random challenge from slate.
 * @returns {Promise<Uint8Array>} 32-byte HMAC digest.
 */
async function computeHmacSha256(keyString, nonceBytes) {
  const enc = new TextEncoder();
  const keyData = enc.encode(keyString);
  const cryptoKey = await window.crypto.subtle.importKey(
    'raw',
    keyData,
    { name: 'HMAC', hash: { name: 'SHA-256' } },
    false,
    ['sign']
  );
  const signature = await window.crypto.subtle.sign(
    'HMAC',
    cryptoKey,
    nonceBytes
  );
  return new Uint8Array(signature);
}

// ============================================================================
// 4. CHUNKING & FRAMING ENGINE (docs/BLE_SPEC.md § 5)
// ============================================================================

/**
 * Sends state payload over Characteristic 0xC401 with deterministic 3-byte chunking.
 * Format: [seq: 1B][total: 1B][len: 1B][data: N B]
 */
async function sendSlateStateChunked(stateObj) {
  if (!charState || !isConnected) {
    throw new Error('No hay conexión Bluetooth activa');
  }

  const jsonStr = JSON.stringify(stateObj);
  const rawBytes = new TextEncoder().encode(jsonStr);

  const maxSlice = 240; // Max payload bytes per frame
  const totalChunks = Math.ceil(rawBytes.length / maxSlice);

  for (let seq = 0; seq < totalChunks; seq++) {
    const start = seq * maxSlice;
    const end = Math.min(start + maxSlice, rawBytes.length);
    const slice = rawBytes.subarray(start, end);

    const packet = new Uint8Array(3 + slice.length);
    packet[0] = seq;          // Chunk Index
    packet[1] = totalChunks;  // Total Chunks
    packet[2] = slice.length; // Payload Length
    packet.set(slice, 3);

    await charState.writeValueWithResponse(packet);
  }
}

/**
 * Handles incoming state buffer, reassembling chunked frames if fragmented.
 */
function handleIncomingStateBytes(bytes) {
  if (bytes.length === 0) return;

  // Check 3-byte chunking header [seq, total, len, ...]
  if (bytes.length >= 3 && bytes[0] < bytes[1] && bytes[2] === (bytes.length - 3)) {
    const seq = bytes[0];
    const total = bytes[1];
    const chunkLen = bytes[2];
    const slice = bytes.subarray(3, 3 + chunkLen);

    if (seq === 0) {
      incomingChunks = [];
      totalExpectedChunks = total;
      clearTimeout(reassemblyTimer);
      reassemblyTimer = setTimeout(() => {
        incomingChunks = [];
        totalExpectedChunks = 0;
        console.warn('[Chunking] Tiempo de espera de reensamblado agotado (3000ms)');
      }, 3000);
    }

    incomingChunks[seq] = slice;

    // Check if all chunks received
    let complete = true;
    for (let i = 0; i < totalExpectedChunks; i++) {
      if (!incomingChunks[i]) {
        complete = false;
        break;
      }
    }

    if (complete && incomingChunks.length === totalExpectedChunks) {
      clearTimeout(reassemblyTimer);
      let totalLength = 0;
      for (const chunk of incomingChunks) {
        totalLength += chunk.length;
      }

      const fullBuffer = new Uint8Array(totalLength);
      let offset = 0;
      for (const chunk of incomingChunks) {
        fullBuffer.set(chunk, offset);
        offset += chunk.length;
      }

      incomingChunks = [];
      totalExpectedChunks = 0;

      const decodedJson = new TextDecoder('utf-8').decode(fullBuffer);
      applyStateFromJson(decodedJson);
    }
  } else {
    // Unchunked raw JSON payload
    const decodedJson = new TextDecoder('utf-8').decode(bytes);
    applyStateFromJson(decodedJson);
  }
}

/**
 * Parses JSON state from slate and updates local state & UI.
 */
function applyStateFromJson(jsonString) {
  try {
    const data = JSON.parse(jsonString);
    if (data.roll) slateState.roll = String(data.roll).toUpperCase();
    if (data.scene) slateState.scene = String(data.scene).toUpperCase();
    if (data.take) slateState.take = String(data.take).toUpperCase();
    if (data.prod) slateState.prod = String(data.prod).toUpperCase();
    if (data.dir || data.director) slateState.dir = String(data.dir || data.director).toUpperCase();
    if (data.dop) slateState.dop = String(data.dop).toUpperCase();
    if (data.note) slateState.note = String(data.note).toUpperCase();
    if (data.date) slateState.date = String(data.date).toUpperCase();
    if (data.timecode || data.tc) slateState.timecode = String(data.timecode || data.tc).toUpperCase();
    if (data.tc_slot !== undefined) slateState.tc_slot = Number(data.tc_slot);
    if (data.tc_size !== undefined) slateState.tc_size = Number(data.tc_size);
    if (data.tc_fps) slateState.tc_fps = String(data.tc_fps);
    if (data.tc_running !== undefined) slateState.tc_running = Boolean(data.tc_running);
    if (data.cam) slateState.cam = String(data.cam).toUpperCase();
    if (data.sound) slateState.sound = String(data.sound).toUpperCase();
    if (data.loc) slateState.loc = String(data.loc).toUpperCase();
    if (data.light) slateState.light = String(data.light).toUpperCase();
    if (data.layout !== undefined) slateState.layout = Number(data.layout);
    if (data.invert !== undefined) slateState.invert = Boolean(data.invert);

    if (data.vis && typeof data.vis === 'object') {
      for (const key of Object.keys(data.vis)) {
        slateState.vis[key] = Boolean(data.vis[key]);
      }
    }

    renderStateToDOM();
    showToast('Estado sincronizado con la claqueta');
  } catch (err) {
    console.error('[BLE] Error parseando estado JSON:', err);
  }
}

// ============================================================================
// 5. WEB BLUETOOTH GATT CLIENT (Chrome for Android / Desktop)
// ============================================================================

/**
 * Initiates device scan, connection, service discovery and security handshake.
 */
async function connectBluetooth() {
  if (!navigator.bluetooth) {
    showToast('❌ Web Bluetooth no disponible en este navegador');
    return;
  }

  try {
    setConnectionUI('scanning');
    isConnecting = true;

    console.log('[BLE] Solicitando dispositivo Bluetooth...');
    bleDevice = await navigator.bluetooth.requestDevice({
      filters: [{ name: 'E-Slate NEBRALTA' }],
      optionalServices: [SLATE_SERVICE_UUID, BATTERY_SERVICE_UUID]
    });

    bleDevice.addEventListener('gattserverdisconnected', onGattDisconnected);

    setConnectionUI('connecting');
    console.log('[BLE] Conectando a servidor GATT...');
    gattServer = await bleDevice.gatt.connect();

    console.log('[BLE] Obteniendo servicio primario E-Slate...');
    const primaryService = await gattServer.getPrimaryService(SLATE_SERVICE_UUID);

    // Discover characteristics
    charState = await primaryService.getCharacteristic(CHAR_STATE_UUID);
    charCmd   = await primaryService.getCharacteristic(CHAR_CMD_UUID);
    charBtn   = await primaryService.getCharacteristic(CHAR_BTN_UUID);
    charBat   = await primaryService.getCharacteristic(CHAR_BAT_UUID);
    charAuth  = await primaryService.getCharacteristic(CHAR_AUTH_UUID);

    isConnected = true;
    setConnectionUI('authenticating');

    // Subscribe to subscriptions
    await setupButtonNotifications();
    await setupBatteryNotifications();
    await setupStateNotifications();

    // Perform HMAC-SHA256 Challenge-Response Handshake
    await performSecurityHandshake();

  } catch (error) {
    console.error('[BLE] Error en conexión Bluetooth:', error);
    isConnecting = false;
    isConnected = false;
    isAuthenticated = false;
    setConnectionUI('disconnected');
    showToast(`Error: ${error.message || 'Conexión cancelada'}`);
  }
}

/**
 * Disconnects from current Bluetooth device.
 */
function disconnectBluetooth() {
  if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
    bleDevice.gatt.disconnect();
  }
  onGattDisconnected();
}

/**
 * GATT Disconnect Event Handler.
 */
function onGattDisconnected() {
  console.log('[BLE] Dispositivo desconectado');
  isConnected = false;
  isAuthenticated = false;
  isConnecting = false;
  charState = null;
  charCmd = null;
  charBtn = null;
  charBat = null;
  charAuth = null;
  setConnectionUI('disconnected');
  showToast('🔴 Desconectado de E-Slate');
}

/**
 * Subscribes to Auth Status Notifications & executes HMAC-SHA256 handshake on 0xC405.
 */
async function performSecurityHandshake() {
  const secretPin = getStoredPin();

  // 1. Subscribe to status notification
  await charAuth.startNotifications();
  charAuth.addEventListener('characteristicvaluechanged', (event) => {
    const val = new Uint8Array(event.target.value.buffer);
    if (val.length >= 2 && val[0] === 0x03) {
      const status = val[1];
      if (status === 0x00) {
        // Success
        isAuthenticated = true;
        setConnectionUI('connected');
        showToast('🟢 Autenticado con éxito (HMAC-SHA256)');
        // Read initial state
        readInitialSlateState();
      } else if (status === 0x02) {
        showToast('⚠️ Slate bloqueado por intentos fallidos (60s)');
        setConnectionUI('locked');
      } else {
        showToast('❌ PIN incorrecto. Revisa los ajustes.');
        setConnectionUI('auth_failed');
        openSettingsModal();
      }
    }
  });

  // 2. Read Challenge (Tag 0x01 + 16-byte nonce)
  const challengeData = await charAuth.readValue();
  const challenge = new Uint8Array(challengeData.buffer);

  if (challenge.length < 17 || challenge[0] !== 0x01) {
    throw new Error('Formato de desafío inválido recibido del slate');
  }

  const nonce = challenge.subarray(1, 17);

  // 3. Compute HMAC-SHA256(PIN, Nonce)
  const hmac = await computeHmacSha256(secretPin, nonce);

  // 4. Send Response (Tag 0x02 + 32-byte HMAC = 33 bytes)
  const response = new Uint8Array(33);
  response[0] = 0x02;
  response.set(hmac, 1);

  await charAuth.writeValueWithResponse(response);
}

/**
 * Subscribes to hardware button notifications on 0xC403.
 */
async function setupButtonNotifications() {
  if (!charBtn) return;
  await charBtn.startNotifications();
  charBtn.addEventListener('characteristicvaluechanged', (event) => {
    const data = event.target.value;
    const eventCode = data.getUint8(0);
    const counter = data.getUint16(1, false);

    console.log(`[BOTON FÍSICO] Código: ${eventCode}, Contador: ${counter}`);

    if (eventCode === 0x01) {
      // KEY1: TAKE
      triggerHaptic([40]);
      incrementTakeLocal();
      showToast(`🎬 TAKE FÍSICO (+1): ${slateState.take}`);
    } else if (eventCode === 0x02) {
      // KEY2: ROLL
      triggerHaptic([40, 30, 40]);
      incrementRollLocal();
      showToast(`🎞️ ROLL FÍSICO (+1): ${slateState.roll}`);
    } else if (eventCode === 0x03) {
      // KEY0: INVERT
      triggerHaptic([60]);
      slateState.invert = !slateState.invert;
      renderStateToDOM();
      showToast(`Modo Invertido: ${slateState.invert ? 'Activo' : 'Inactivo'}`);
    }
  });
}

/**
 * Subscribes to battery telemetry notifications on 0xC404.
 */
async function setupBatteryNotifications() {
  if (!charBat) return;
  await charBat.startNotifications();
  charBat.addEventListener('characteristicvaluechanged', (event) => {
    const data = event.target.value;
    const mV = data.getUint16(0, false);
    const pct = data.getUint8(2);
    const flags = data.getUint8(3);
    updateBatteryDisplay(mV, pct, flags);
  });

  // Also read initial battery value
  try {
    const val = await charBat.readValue();
    const mV = val.getUint16(0, false);
    const pct = val.getUint8(2);
    const flags = val.getUint8(3);
    updateBatteryDisplay(mV, pct, flags);
  } catch (e) {
    console.log('[BAT] No se pudo leer telemetría inicial');
  }
}

/**
 * Subscribes to state notifications on 0xC401.
 */
async function setupStateNotifications() {
  if (!charState) return;
  try {
    await charState.startNotifications();
    charState.addEventListener('characteristicvaluechanged', (event) => {
      const raw = new Uint8Array(event.target.value.buffer);
      handleIncomingStateBytes(raw);
    });
  } catch (e) {
    console.log('[STATE] Notificaciones no soportadas en 0xC401');
  }
}

/**
 * Reads initial state from Characteristic 0xC401.
 */
async function readInitialSlateState() {
  if (!charState) return;
  try {
    const rawData = await charState.readValue();
    const bytes = new Uint8Array(rawData.buffer);
    handleIncomingStateBytes(bytes);
  } catch (err) {
    console.error('[BLE] Error leyendo estado inicial:', err);
  }
}

/**
 * Sends a 1-2 byte binary command over Characteristic 0xC402.
 */
async function sendBleCommand(opcode, param = 0x00) {
  if (!charCmd || !isConnected) {
    showToast('❌ Sin conexión Bluetooth');
    return;
  }
  try {
    const payload = new Uint8Array([opcode, param]);
    await charCmd.writeValueWithResponse(payload);
  } catch (err) {
    console.error('[BLE] Error enviando comando:', err);
    showToast(`Error comando: ${err.message}`);
  }
}

/**
 * Sends a raw multi-byte binary packet over Characteristic 0xC402.
 */
async function sendBleCommandRaw(payload) {
  if (!charCmd || !isConnected) {
    return;
  }
  try {
    await charCmd.writeValueWithResponse(payload);
  } catch (err) {
    console.error('[BLE] Error enviando comando raw:', err);
  }
}

// Timecode Dispatchers
async function triggerJamSyncTod() {
  triggerHaptic([30, 40, 30]);
  const now = new Date();
  const h = now.getHours();
  const m = now.getMinutes();
  const s = now.getSeconds();
  slateState.timecode = formatTimecodeString(h, m, s);
  slateState.tc_running = true;
  renderStateToDOM();

  if (isConnected) {
    // Fast Jam 0x0C: [0x0C, HH, MM, SS]
    await sendBleCommandRaw(new Uint8Array([CMD_OPCODE_JAM_TOD, h, m, s]));
    try {
      await sendSlateStateChunked(slateState);
    } catch (e) {
      console.warn('[BLE] Error sincronizando estado tras TOD Jam:', e);
    }
  }
  showToast(`⚡ Jam-Sync TOD aplicado: ${slateState.timecode} (${slateState.tc_fps} FPS)`);
}

async function triggerTimecodePlayPause() {
  triggerHaptic(30);
  slateState.tc_running = !slateState.tc_running;
  renderStateToDOM();
  if (isConnected) {
    await sendBleCommand(CMD_OPCODE_TOGGLE_TIMECODE);
  }
  showToast(slateState.tc_running ? '▶ Timecode Reanudado' : '⏸ Timecode Pausado');
}

async function triggerSetTimecodeSlot(slot) {
  triggerHaptic(30);
  slateState.tc_slot = Math.max(0, Math.min(2, Number(slot)));
  renderStateToDOM();
  if (isConnected) {
    await sendBleCommand(CMD_OPCODE_SET_TC_SLOT, slateState.tc_slot);
    try {
      await sendSlateStateChunked(slateState);
    } catch (e) {}
  }
}

async function triggerSetTimecodeSize(size) {
  triggerHaptic(30);
  slateState.tc_size = Math.max(0, Math.min(2, Number(size)));
  renderStateToDOM();
  if (isConnected) {
    await sendBleCommand(CMD_OPCODE_SET_TC_SIZE, slateState.tc_size);
    try {
      await sendSlateStateChunked(slateState);
    } catch (e) {}
  }
}

async function triggerSetTimecodeFps(fpsStr) {
  triggerHaptic(30);
  slateState.tc_fps = fpsStr;
  renderStateToDOM();
  if (isConnected) {
    const fpsIdx = FPS_PROFILES[fpsStr] ? FPS_PROFILES[fpsStr].opcodeIdx : 1;
    await sendBleCommand(CMD_OPCODE_SET_TC_FPS, fpsIdx);
    try {
      await sendSlateStateChunked(slateState);
    } catch (e) {}
  }
  showToast(`FPS actualizado: ${fpsStr} FPS`);
}

async function triggerTimecodeStepSeconds(deltaSec) {
  triggerHaptic(20);
  slateState.timecode = applyTimecodeSecondOffset(slateState.timecode, deltaSec);
  renderStateToDOM();
  if (isConnected) {
    try {
      await sendSlateStateChunked(slateState);
    } catch (e) {}
  }
}

async function triggerApplyCameraJam(offsetFrames) {
  triggerHaptic([40, 30]);
  slateState.timecode = applyTimecodeFrameOffset(slateState.timecode, offsetFrames, slateState.tc_fps);
  renderStateToDOM();
  if (isConnected) {
    try {
      await sendSlateStateChunked(slateState);
      showToast(`📹 Jam de Cámara aplicado: ${offsetFrames > 0 ? '+' : ''}${offsetFrames}F -> ${slateState.timecode}`);
    } catch (e) {
      showToast(`Error aplicando jam: ${e.message}`);
    }
  } else {
    showToast(`Offset local aplicado: ${offsetFrames > 0 ? '+' : ''}${offsetFrames}F -> ${slateState.timecode}`);
  }
}

async function triggerToggleTimecodeVisibility(isVisible) {
  triggerHaptic(30);
  slateState.vis.timecode = isVisible;
  renderStateToDOM();
  if (isConnected) {
    await sendBleCommand(CMD_OPCODE_SET_TC_VIS, isVisible ? 0x01 : 0x00);
    try {
      await sendSlateStateChunked(slateState);
    } catch (e) {}
  }
}

// ============================================================================
// 6. UI SYNCHRONIZATION & EVENT HANDLING
// ============================================================================

function getStoredPin() {
  return localStorage.getItem('eslate_pin') || DEFAULT_PIN;
}

function setStoredPin(pin) {
  localStorage.setItem('eslate_pin', pin.trim());
}

function setConnectionUI(status) {
  const dot = document.getElementById('status-dot');
  const text = document.getElementById('status-text');
  const shield = document.getElementById('auth-shield');
  const modalStatus = document.getElementById('modal-conn-status');
  const modalBtn = document.getElementById('modal-btn-connect');
  const btnUpdate = document.getElementById('btn-full-update');

  dot.className = 'status-dot';

  switch (status) {
    case 'scanning':
      dot.classList.add('scanning');
      text.textContent = '🔵 Buscando...';
      shield.classList.add('hidden');
      modalStatus.textContent = 'Buscando dispositivo...';
      modalStatus.style.color = '#3b82f6';
      modalBtn.textContent = 'Buscando...';
      break;

    case 'connecting':
    case 'authenticating':
      dot.classList.add('authenticating');
      text.textContent = '🟡 Autenticando...';
      shield.classList.add('hidden');
      modalStatus.textContent = 'Autenticando HMAC-SHA256...';
      modalStatus.style.color = '#f59e0b';
      modalBtn.textContent = 'Conectando...';
      break;

    case 'connected':
      dot.classList.add('connected');
      text.textContent = '🟢 E-Slate NEBRALTA';
      shield.classList.remove('hidden');
      modalStatus.textContent = 'Conectado y Autenticado';
      modalStatus.style.color = '#10b981';
      modalBtn.textContent = 'Desconectar';
      btnUpdate.disabled = false;
      break;

    case 'auth_failed':
      dot.classList.add('authenticating');
      text.textContent = '❌ PIN Inválido';
      shield.classList.add('hidden');
      modalStatus.textContent = 'Error: PIN Secreto incorrecto';
      modalStatus.style.color = '#ef4444';
      modalBtn.textContent = 'Desconectar';
      break;

    case 'locked':
      text.textContent = '⚠️ Bloqueado (60s)';
      shield.classList.add('hidden');
      modalStatus.textContent = 'Bloqueado por seguridad (60s)';
      modalStatus.style.color = '#ef4444';
      modalBtn.textContent = 'Desconectar';
      break;

    case 'disconnected':
    default:
      text.textContent = '🔴 Desconectado';
      shield.classList.add('hidden');
      modalStatus.textContent = 'Desconectado';
      modalStatus.style.color = '#94a3b8';
      modalBtn.textContent = 'Buscar Claqueta';
      document.getElementById('battery-badge').classList.add('hidden');
      break;
  }
}

function updateBatteryDisplay(mV, pct, flags) {
  const badge = document.getElementById('battery-badge');
  const icon = document.getElementById('battery-icon');
  const text = document.getElementById('battery-text');

  badge.classList.remove('hidden');

  const isUsb = (flags & 0x01) !== 0;
  const isLow = (flags & 0x02) !== 0;
  const voltageStr = (mV / 1000.0).toFixed(2) + 'V';

  if (isUsb) {
    icon.textContent = '⚡';
  } else if (isLow) {
    icon.textContent = '🪫';
  } else {
    icon.textContent = '🔋';
  }

  text.textContent = `${pct}% (${voltageStr})`;
}

function renderStateToDOM() {
  document.getElementById('input-roll').value = slateState.roll;
  document.getElementById('input-scene').value = slateState.scene;
  document.getElementById('input-take').value = slateState.take;
  document.getElementById('input-prod').value = slateState.prod;
  document.getElementById('input-dir').value = slateState.dir;
  document.getElementById('input-dop').value = slateState.dop;
  document.getElementById('input-note').value = slateState.note;
  document.getElementById('input-date').value = slateState.date;

  // TIMECODE HERO CARD SYNCHRONIZATION
  const inputTc = document.getElementById('input-timecode');
  if (inputTc && document.activeElement !== inputTc) {
    inputTc.value = slateState.timecode;
  }
  const toggleTcActive = document.getElementById('toggle-timecode-active');
  if (toggleTcActive) toggleTcActive.checked = slateState.vis.timecode;

  const badgeTcFps = document.getElementById('badge-tc-fps');
  if (badgeTcFps) badgeTcFps.textContent = `${slateState.tc_fps} FPS`;

  const dotRunning = document.getElementById('dot-tc-running');
  const playPauseIcon = document.getElementById('tc-playpause-icon');
  if (dotRunning && playPauseIcon) {
    if (slateState.tc_running && slateState.vis.timecode) {
      dotRunning.className = 'tc-running-dot active';
      playPauseIcon.textContent = '⏸';
    } else {
      dotRunning.className = 'tc-running-dot paused';
      playPauseIcon.textContent = '▶';
    }
  }

  const tcBody = document.getElementById('tc-body');
  const tcDisabled = document.getElementById('tc-disabled-state');
  if (tcBody && tcDisabled) {
    if (slateState.vis.timecode) {
      tcBody.classList.remove('hidden');
      tcDisabled.classList.add('hidden');
    } else {
      tcBody.classList.add('hidden');
      tcDisabled.classList.remove('hidden');
    }
  }

  // Segmented Slot & Size Buttons
  document.querySelectorAll('#seg-tc-slot .seg-btn').forEach(btn => {
    btn.classList.toggle('active', Number(btn.dataset.slot) === slateState.tc_slot);
  });
  document.querySelectorAll('#seg-tc-size .seg-btn').forEach(btn => {
    btn.classList.toggle('active', Number(btn.dataset.size) === slateState.tc_size);
  });

  // Modal Timecode Selects
  const selFps = document.getElementById('select-tc-fps');
  if (selFps) selFps.value = slateState.tc_fps;
  const selSlot = document.getElementById('select-tc-slot');
  if (selSlot) selSlot.value = String(slateState.tc_slot);
  const selSize = document.getElementById('select-tc-size');
  if (selSize) selSize.value = String(slateState.tc_size);

  // CAM
  ['A', 'B', 'C'].forEach((letter) => {
    const btn = document.getElementById(`btn-cam-${letter.toLowerCase()}`);
    if (btn) {
      if (slateState.cam === letter) {
        btn.className = 'badge-btn active-gold';
      } else {
        btn.className = 'badge-btn';
      }
    }
  });

  // SOUND
  const btnSync = document.getElementById('btn-sound-sync');
  const btnMos = document.getElementById('btn-sound-mos');
  if (btnSync && btnMos) {
    btnSync.className = slateState.sound === 'SYNC' ? 'badge-btn active' : 'badge-btn';
    btnMos.className = slateState.sound === 'MOS' ? 'badge-btn active' : 'badge-btn';
  }

  // LOC
  const btnInt = document.getElementById('btn-loc-int');
  const btnExt = document.getElementById('btn-loc-ext');
  if (btnInt && btnExt) {
    btnInt.className = slateState.loc === 'INT' ? 'badge-btn active' : 'badge-btn';
    btnExt.className = slateState.loc === 'EXT' ? 'badge-btn active' : 'badge-btn';
  }

  // LIGHT
  const btnDay = document.getElementById('btn-light-day');
  const btnNight = document.getElementById('btn-light-night');
  if (btnDay && btnNight) {
    btnDay.className = slateState.light === 'DAY' ? 'badge-btn active' : 'badge-btn';
    btnNight.className = slateState.light === 'NIGHT' ? 'badge-btn active' : 'badge-btn';
  }

  // Layout & Invert in Settings
  const selLayout = document.getElementById('select-layout');
  if (selLayout) selLayout.value = String(slateState.layout);

  const toggleInvert = document.getElementById('toggle-invert');
  if (toggleInvert) {
    if (slateState.invert) {
      toggleInvert.classList.add('active');
    } else {
      toggleInvert.classList.remove('active');
    }
  }

  // Visibility Checkboxes & DOM Visibility
  for (const [key, isVisible] of Object.entries(slateState.vis)) {
    const chk = document.getElementById(`vis-${key}`);
    if (chk) chk.checked = isVisible;

    const card = document.getElementById(`card-${key}`);
    if (card) {
      if (isVisible) {
        card.classList.remove('hidden');
      } else {
        card.classList.add('hidden');
      }
    }
  }
}

function readStateFromDOM() {
  slateState.roll = document.getElementById('input-roll').value.trim().toUpperCase();
  slateState.scene = document.getElementById('input-scene').value.trim().toUpperCase();
  slateState.take = document.getElementById('input-take').value.trim().toUpperCase();
  slateState.prod = document.getElementById('input-prod').value.trim().toUpperCase();
  slateState.dir = document.getElementById('input-dir').value.trim().toUpperCase();
  slateState.dop = document.getElementById('input-dop').value.trim().toUpperCase();
  slateState.note = document.getElementById('input-note').value.trim().toUpperCase();
  slateState.date = document.getElementById('input-date').value.trim().toUpperCase();

  const inputTc = document.getElementById('input-timecode');
  if (inputTc) slateState.timecode = inputTc.value.trim().toUpperCase();

  const selLayout = document.getElementById('select-layout');
  if (selLayout) slateState.layout = Number(selLayout.value);

  // Read visibilities
  const visKeys = ['roll', 'scene', 'take', 'prod', 'dir', 'dop', 'timecode', 'cam', 'sound', 'loc', 'light', 'date', 'note', 'bat'];
  visKeys.forEach((key) => {
    const chk = document.getElementById(`vis-${key}`);
    if (chk) slateState.vis[key] = chk.checked;
  });
}

/**
 * Increments a take string ("1" -> "2", "3A" -> "4A").
 */
function incrementStringValue(val) {
  let numPart = '';
  let suffix = '';
  let i = 0;
  while (i < val.length && val[i] >= '0' && val[i] <= '9') {
    numPart += val[i++];
  }
  suffix = val.substring(i);

  if (numPart.length > 0) {
    const nextNum = parseInt(numPart, 10) + 1;
    return `${nextNum}${suffix}`;
  } else if (val.length === 1 && val >= 'A' && val < 'Z') {
    return String.fromCharCode(val.charCodeAt(0) + 1);
  }
  return val + '1';
}

function incrementTakeLocal() {
  slateState.take = incrementStringValue(slateState.take);
  document.getElementById('input-take').value = slateState.take;
}

function incrementRollLocal() {
  slateState.roll = incrementStringValue(slateState.roll);
  document.getElementById('input-roll').value = slateState.roll;
}

function triggerHaptic(pattern = [40]) {
  if (navigator.vibrate) {
    navigator.vibrate(pattern);
  }
}

let toastTimer = null;
function showToast(message) {
  const toast = document.getElementById('toast');
  toast.textContent = message;
  toast.classList.add('active');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    toast.classList.remove('active');
  }, 2400);
}

function openSettingsModal() {
  document.getElementById('settings-modal').classList.add('active');
  document.getElementById('input-pin').value = getStoredPin();
}

function closeSettingsModal() {
  document.getElementById('settings-modal').classList.remove('active');
}

// ============================================================================
// 7. INITIALIZATION & DOM BINDINGS
// ============================================================================

window.addEventListener('DOMContentLoaded', () => {
  // Check Secure Context & Web Bluetooth Availability
  if (!window.isSecureContext || !navigator.bluetooth) {
    const banner = document.getElementById('compat-banner');
    const msg = document.getElementById('compat-msg');
    banner.classList.remove('hidden');

    if (!window.isSecureContext) {
      msg.innerHTML = '⚠️ <strong>Contexto no seguro:</strong> Web Bluetooth requiere HTTPS (o localhost). Si abriste esta web por HTTP SoftAP (192.168.4.1), instala la PWA oficial desde HTTPS para usar Bluetooth.';
    } else {
      msg.innerHTML = '⚠️ Tu navegador no soporta Web Bluetooth. Usa <strong>Google Chrome</strong> en Android para controlar la claqueta.';
    }
  }

  document.getElementById('btn-close-compat').addEventListener('click', () => {
    document.getElementById('compat-banner').classList.add('hidden');
  });

  // Top Connection Pill click
  document.getElementById('btn-connect').addEventListener('click', () => {
    if (isConnected) {
      openSettingsModal();
    } else {
      connectBluetooth();
    }
  });

  // Settings Modal Buttons
  document.getElementById('btn-open-settings').addEventListener('click', openSettingsModal);
  document.getElementById('btn-close-settings').addEventListener('click', closeSettingsModal);
  document.getElementById('settings-modal').addEventListener('click', (e) => {
    if (e.target.id === 'settings-modal') closeSettingsModal();
  });

  document.getElementById('modal-btn-connect').addEventListener('click', () => {
    if (isConnected) {
      disconnectBluetooth();
    } else {
      closeSettingsModal();
      connectBluetooth();
    }
  });

  document.getElementById('btn-save-settings').addEventListener('click', () => {
    const pin = document.getElementById('input-pin').value.trim();
    if (pin.length > 0) {
      setStoredPin(pin);
    }
    readStateFromDOM();
    renderStateToDOM();
    closeSettingsModal();
    showToast('Ajustes guardados');
  });

  // ==================== TIMECODE HERO CARD LISTENERS ====================
  // TOD Jam-Sync Button
  document.getElementById('btn-jam-tod').addEventListener('click', () => {
    triggerJamSyncTod();
  });

  // Timecode Active Switch
  document.getElementById('toggle-timecode-active').addEventListener('change', (e) => {
    triggerToggleTimecodeVisibility(e.target.checked);
  });

  // Play / Pause Ticker Button
  document.getElementById('btn-tc-playpause').addEventListener('click', () => {
    triggerTimecodePlayPause();
  });

  // Step -1s / +1s Buttons
  document.getElementById('btn-tc-minus-1s').addEventListener('click', () => {
    triggerTimecodeStepSeconds(-1);
  });
  document.getElementById('btn-tc-plus-1s').addEventListener('click', () => {
    triggerTimecodeStepSeconds(1);
  });

  // Segmented Slot buttons
  document.querySelectorAll('#seg-tc-slot .seg-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      triggerSetTimecodeSlot(btn.dataset.slot);
    });
  });

  // Segmented Size buttons
  document.querySelectorAll('#seg-tc-size .seg-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      triggerSetTimecodeSize(btn.dataset.size);
    });
  });

  // FPS badge click opens Settings
  document.getElementById('badge-tc-fps').addEventListener('click', () => {
    openSettingsModal();
    const sel = document.getElementById('select-tc-fps');
    if (sel) sel.focus();
  });

  // Timecode manual text input
  const inputTc = document.getElementById('input-timecode');
  if (inputTc) {
    inputTc.addEventListener('change', (e) => {
      const val = e.target.value.trim().toUpperCase();
      const parts = val.split(':');
      if (parts.length >= 2) {
        slateState.timecode = val;
      }
      if (isConnected) {
        sendSlateStateChunked(slateState).catch(err => console.error(err));
      }
    });
  }

  // ==================== MODAL TIMECODE SETTINGS ====================
  // FPS Select
  const selectTcFps = document.getElementById('select-tc-fps');
  if (selectTcFps) {
    selectTcFps.addEventListener('change', (e) => {
      triggerSetTimecodeFps(e.target.value);
    });
  }

  // Slot Select in Modal
  const selectTcSlot = document.getElementById('select-tc-slot');
  if (selectTcSlot) {
    selectTcSlot.addEventListener('change', (e) => {
      triggerSetTimecodeSlot(e.target.value);
    });
  }

  // Size Select in Modal
  const selectTcSize = document.getElementById('select-tc-size');
  if (selectTcSize) {
    selectTcSize.addEventListener('change', (e) => {
      triggerSetTimecodeSize(e.target.value);
    });
  }

  // Offset Steppers in Modal
  const btnOffsetMinus = document.getElementById('btn-offset-minus');
  const btnOffsetPlus = document.getElementById('btn-offset-plus');
  const inputTcOffset = document.getElementById('input-tc-offset');
  if (btnOffsetMinus && inputTcOffset) {
    btnOffsetMinus.addEventListener('click', () => {
      inputTcOffset.value = String(parseInt(inputTcOffset.value || '0', 10) - 1);
    });
  }
  if (btnOffsetPlus && inputTcOffset) {
    btnOffsetPlus.addEventListener('click', () => {
      inputTcOffset.value = String(parseInt(inputTcOffset.value || '0', 10) + 1);
    });
  }

  // Apply Camera Jam Button
  const btnApplyCameraJam = document.getElementById('btn-apply-camera-jam');
  if (btnApplyCameraJam && inputTcOffset) {
    btnApplyCameraJam.addEventListener('click', () => {
      const deltaFrames = parseInt(inputTcOffset.value || '0', 10);
      triggerApplyCameraJam(deltaFrames);
      inputTcOffset.value = '0';
    });
  }

  // Visibility Checkbox in Modal
  const visTcChk = document.getElementById('vis-timecode');
  if (visTcChk) {
    visTcChk.addEventListener('change', (e) => {
      triggerToggleTimecodeVisibility(e.target.checked);
    });
  }

  // Invert Mode Toggle in Settings
  document.getElementById('toggle-invert').addEventListener('click', () => {
    slateState.invert = !slateState.invert;
    renderStateToDOM();
    if (isConnected) {
      sendBleCommand(CMD_OPCODE_TOGGLE_INVERT);
    }
  });

  // Force Refresh in Settings
  document.getElementById('btn-force-refresh').addEventListener('click', () => {
    if (isConnected) {
      sendBleCommand(CMD_OPCODE_FORCE_REFRESH);
      showToast('Refresco forzado enviado');
    } else {
      showToast('Sin conexión');
    }
  });

  // Layout Select
  document.getElementById('select-layout').addEventListener('change', (e) => {
    slateState.layout = Number(e.target.value);
    if (isConnected) {
      sendBleCommand(CMD_OPCODE_SET_LAYOUT, slateState.layout);
    }
  });

  // 🌸 Button Shortcut
  document.getElementById('btn-sakura').addEventListener('click', () => {
    triggerHaptic(30);
    const prodInput = document.getElementById('input-prod');
    if (!prodInput.value.includes('🌸')) {
      prodInput.value = (prodInput.value + ' 🌸').trim();
      slateState.prod = prodInput.value;
    }
  });

  // 📱 HOY Auto-Sync Date Button
  document.getElementById('btn-hoy').addEventListener('click', () => {
    triggerHaptic(30);
    const now = new Date();
    const day = String(now.getDate()).padStart(2, '0');
    const month = String(now.getMonth() + 1).padStart(2, '0');
    const year = now.getFullYear();
    const formattedDate = `${day}.${month}.${year}`;
    document.getElementById('input-date').value = formattedDate;
    slateState.date = formattedDate;
    showToast(`Fecha sincronizada: ${formattedDate}`);
  });

  // Camera Badges
  ['A', 'B', 'C'].forEach((letter) => {
    document.getElementById(`btn-cam-${letter.toLowerCase()}`).addEventListener('click', () => {
      triggerHaptic(30);
      slateState.cam = letter;
      renderStateToDOM();
    });
  });

  // Sound Badges
  document.getElementById('btn-sound-sync').addEventListener('click', () => {
    triggerHaptic(30);
    slateState.sound = 'SYNC';
    renderStateToDOM();
  });
  document.getElementById('btn-sound-mos').addEventListener('click', () => {
    triggerHaptic(30);
    slateState.sound = 'MOS';
    renderStateToDOM();
  });

  // Location Badges
  document.getElementById('btn-loc-int').addEventListener('click', () => {
    triggerHaptic(30);
    slateState.loc = 'INT';
    renderStateToDOM();
  });
  document.getElementById('btn-loc-ext').addEventListener('click', () => {
    triggerHaptic(30);
    slateState.loc = 'EXT';
    renderStateToDOM();
  });

  // Lighting Badges
  document.getElementById('btn-light-day').addEventListener('click', () => {
    triggerHaptic(30);
    slateState.light = 'DAY';
    renderStateToDOM();
  });
  document.getElementById('btn-light-night').addEventListener('click', () => {
    triggerHaptic(30);
    slateState.light = 'NIGHT';
    renderStateToDOM();
  });

  // Quick Action Buttons
  document.getElementById('btn-quick-take').addEventListener('click', () => {
    triggerHaptic([40]);
    incrementTakeLocal();
    if (isConnected) {
      sendBleCommand(CMD_OPCODE_INC_TAKE);
      showToast(`TAKE (+1): ${slateState.take}`);
    } else {
      showToast(`TAKE (+1) local: ${slateState.take}`);
    }
  });

  document.getElementById('btn-quick-roll').addEventListener('click', () => {
    triggerHaptic([40, 30, 40]);
    incrementRollLocal();
    if (isConnected) {
      sendBleCommand(CMD_OPCODE_INC_ROLL);
      showToast(`ROLL (+1): ${slateState.roll}`);
    } else {
      showToast(`ROLL (+1) local: ${slateState.roll}`);
    }
  });

  // Full Update Button
  document.getElementById('btn-full-update').addEventListener('click', async () => {
    readStateFromDOM();
    triggerHaptic(50);

    if (!isConnected) {
      showToast('⚠️ Sin conexión Bluetooth a la claqueta');
      return;
    }

    const spinner = document.getElementById('update-spinner');
    const icon = document.getElementById('update-icon');
    const label = document.getElementById('update-label');

    try {
      spinner.classList.remove('hidden');
      icon.classList.add('hidden');
      label.textContent = 'ACTUALIZANDO...';

      await sendSlateStateChunked(slateState);
      showToast('⚡ Claqueta actualizada');
    } catch (err) {
      console.error('[BLE] Error actualizando claqueta:', err);
      showToast(`Error: ${err.message}`);
    } finally {
      spinner.classList.add('hidden');
      icon.classList.remove('hidden');
      label.textContent = 'ACTUALIZAR CLAQUETA';
    }
  });

  // Auto-uppercase inputs on typing
  const textInputs = document.querySelectorAll('.slate-input');
  textInputs.forEach((input) => {
    input.addEventListener('input', (e) => {
      const start = e.target.selectionStart;
      const end = e.target.selectionEnd;
      e.target.value = e.target.value.toUpperCase();
      e.target.setSelectionRange(start, end);
    });
  });

  // Initial DOM render
  renderStateToDOM();

  // Register Offline Service Worker
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js')
      .then((reg) => {
        console.log('[SW] Service Worker registrado con éxito:', reg.scope);
      })
      .catch((err) => {
        console.log('[SW] Registro de Service Worker omitido:', err);
      });
  }
});
