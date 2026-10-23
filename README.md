# E-Slate Claqueta Digital (Film Clapperboard) 🎬📟 `v2.0.0`

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: ESP32-S3](https://img.shields.io/badge/ESP32--S3-Xtensa%20LX7-red.svg)](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html)
[![Display: 7.5" ePaper](https://img.shields.io/badge/Display-7.5%22%20UC8179%20ePaper-lightgrey.svg)](https://www.seeedstudio.com)
[![iOS Companion](https://img.shields.io/badge/iOS-SwiftUI%20%7C%20BLE-black.svg?logo=apple)](ios_app/)
[![Web PWA](https://img.shields.io/badge/PWA-Web%20Bluetooth-orange.svg)](web_pwa/)

Ecosistema profesional de claqueta digital de cine de bajo consumo construida sobre tinta electrónica (**7.5" e-Paper / e-Ink**), impulsada por el microcontrolador **Seeed Studio XIAO ESP32-S3 Plus** y la controladora **XIAO ePaper Display Board (EE04)**.

Permite controlar y sincronizar todos los datos de rodaje (**ROLL, SCENE, TAKE, PROD, DIR, DOP, FPS, TIMECODE, etc.**) de forma inalámbrica vía **Bluetooth Low Energy (BLE)** o **Wi-Fi WebServer**, mediante una app nativa para iOS o una Progressive Web App (PWA) offline.

> *Inspirado en la idea original de Gokux en [Instructables](https://www.instructables.com/E-Slate-DIY-Wi-Fi-E-Ink-Digital-Film-Clapperboard/), rediseñado y expandido a v2.0.0 con motor tipográfico elástico (HES), sincronización BLE bidireccional, timecode y aplicaciones cliente.*

---

## 🚀 Características Principales (v2.0.0)

- **Pantalla de Tinta Electrónica de 7.5" (800×480 px, UC8179)**: Lectura perfecta bajo luz directa de sol en set y consumo ultrabajo de batería.
- **Motor de Layout Elástico (HES - Hierarchical Elastic Slicing)**:
  - 3 diseños seleccionables al instante: **Cinema Pro**, **Big Slate** y **Commercial**.
  - Ajuste tipográfico dinámico: el nombre del D.O.P. y Directores largos se auto-escala o divide en 2 líneas sin solapamientos.
  - Modo Invertido (Dark Mode: fondo negro con letras blancas).
- **Motor de Timecode Local & Sincronización Jam**:
  - Reloj interno de Timecode a 1 Hz con soporte para frame rates de cine (`23.976`, `24`, `25`, `29.97`, `30 fps`).
  - Sincronización instantánea con el reloj del iPhone (**Time-of-Day Jam Sync**).
  - Compensación y ajuste fino de cuadros (**Camera Offset Delta**).
- **Conectividad Dual Simultánea**:
  - **Bluetooth Low Energy (BLE 5.0)**: Sincronización instantánea sin necesidad de cambiar de red Wi-Fi en el rodaje.
  - **Wi-Fi SoftAP Autónomo + WebServer HTTP**: Genera su propia red Wi-Fi (`NEBRALTA_Clap`) accesible desde cualquier navegador en `http://clap.local`.
- **Seguridad en Set**:
  - Autenticación por PIN/Passkey con almacenamiento seguro en **Apple Keychain**.
  - El PIN de vinculación se muestra temporalmente en la pantalla e-Paper para autorizar la conexión.
- **Mapeo de Botones Físicos en Placa EE04**:
  - `KEY0` (GPIO 2): Conmutar Modo Invertido (Blanco ↔ Negro).
  - `KEY1` (GPIO 3): **+1 TAKE** tras cada corte.
  - `KEY2` (GPIO 5): **+1 ROLL** al cambiar de tarjeta o rollo.

---

## 🛠️ Especificaciones de Hardware

| Componente | Especificación Técnica |
| :--- | :--- |
| **Microcontrolador** | **Seeed Studio XIAO ESP32-S3 Plus** (Xtensa LX7 Dual-Core @ 240MHz) |
| **Memoria** | 16 MB Flash (QIO) + 8 MB PSRAM OPI (`qio_opi`) |
| **Carrier Board** | **XIAO ePaper Display Board - EE04** |
| **Pantalla** | **7.5" Monocromática ePaper (800 x 480 px, controlador UC8179)** |
| **Librería Gráfica** | **Seeed_GFX2** (renderizado de 1 bpp en búfer PSRAM) |
| **Inalámbrico** | Wi-Fi 802.11 b/g/n (SoftAP) + Bluetooth Low Energy (BLE GATT) |
| **Alimentación** | Batería LiPo 3.7V (conector JST 2.0mm) o alimentación continua por USB-C |

---

## 📁 Estructura del Ecosistema

```text
eink_slate/
├── eink_clap/
│   └── eink_clap.ino      # Firmware central del ESP32-S3 (HES, BLE, WebServer, TC)
├── ios_app/               # Aplicación nativa para iPhone / iPad en SwiftUI
│   ├── ESlate.xcodeproj   # Proyecto Xcode
│   └── ESlate/            # Vistas, Bluetooth Manager, Keychain y Modelos
├── web_pwa/               # Progressive Web App (HTML5 + Web Bluetooth API)
│   ├── index.html         # Panel de control cinematográfico para navegadores
│   ├── app.js             # Lógica cliente y conexión BLE
│   └── sw.js              # Service Worker para ejecución 100% offline
├── docs/                  # Especificaciones técnicas
│   ├── BLE_SPEC.md        # Especificación formal del protocolo GATT y JSON
│   └── ANDROID_BLE_STRATEGY.md
├── tests/                 # Suite de pruebas automatizadas
│   ├── e2e_test_runner.py # Validador End-to-End en Python
│   ├── test_ble_protocol.py
│   └── cpp_stress_harness.cpp
├── platformio.ini         # Configuración para PlatformIO
└── README.md
```

---

## 📱 Clientes de Control

### 1. App Nativa para iOS (`ios_app/`)
Construida en **SwiftUI** con arquitectura reactiva:
- Detección y conexión automática por Bluetooth Low Energy.
- Splash Screen fluido sin pantallazos negros.
- Modal de vinculación por PIN seguro guardado en el Keychain de iOS.
- Pestaña de Timecode con Jam Sync al reloj del iPhone y ajuste de offset por frames.
- Soporte para rotación Portrait y Landscape.

*Para instalar*: Abre `ios_app/ESlate.xcodeproj` en **Xcode**, selecciona tu dispositivo y pulsa **Run**.

### 2. Web PWA Multiplataforma (`web_pwa/`)
Diseñada para rodajes con dispositivos **Android, tabletas o computadores (Chrome/Edge)**:
- Soporta **Web Bluetooth API** para control inalámbrico directo sin instalar aplicaciones.
- Service Worker precacheado: funciona sin conexión a Internet una vez cargada.
- Acceso directo vía Wi-Fi conectándose al AP de la claqueta (`http://clap.local`).

---

## 💻 Compilación e Instalación del Firmware

### Opción A: PlatformIO
```bash
# Compilar firmware
pio run

# Subir a la placa vía USB-C
pio run -t upload

# Abrir monitor serie (115200 baud)
pio device monitor
```

### Opción B: Arduino IDE
1. Selecciona la placa **XIAO_ESP32S3** (o *ESP32S3 Dev Module*).
2. Ajusta los parámetros en el menú **Tools**:
   - **USB CDC On Boot**: `Enabled`
   - **CPU Frequency**: `240MHz (WiFi)`
   - **Flash Size**: `16MB (128Mb)`
   - **Partition Scheme**: `16M Flash (3MB APP/9.9MB FATFS)` o `default 16MB`
   - **PSRAM**: `OPI PSRAM`
3. Instala la librería gráfica [Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2) en tu directorio `Arduino/libraries`.
4. Abre `eink_clap/eink_clap.ino` y haz clic en **Upload**.

---

## 🎬 Guía Rápida de Uso en Set

1. **Encendido**:
   - La pantalla e-Paper muestra la pantalla de instrucciones con el nombre de la red y el código QR de acceso.
2. **Conexión vía Bluetooth (Recomendada)**:
   - Abre la app **E-Slate** en tu iPhone o abre la **PWA** en un navegador compatible con Web Bluetooth.
   - Presiona **Conectar**: la claqueta mostrará un PIN temporal de 4 dígitos si es la primera vinculación.
   - Ingresa el PIN y el dispositivo quedará emparejado permanentemente.
3. **Conexión vía Wi-Fi autónomo**:
   - Conéctate a la red Wi-Fi: **`NEBRALTA_Clap`** (Contraseña: `123456789P`).
   - Abre tu navegador y entra a **`http://clap.local`** (o `http://192.168.4.1`).
4. **Durante la toma**:
   - Usa los botones físicos de la placa (`KEY1` para +1 Toma) o el botón gigante **TAKE** en la app para avanzar el conteo rápidamente.

---

## 📄 Licencia

Este proyecto es software libre bajo los términos de la licencia **[GNU General Public License v3.0 (GPLv3)](LICENSE)**.
