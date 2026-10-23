# E-Slate Claqueta Digital (Film Clapperboard) 🎬📟 `v2.0.0`

Esto lo he creado 100% con IA basado en las contribuciones de By
gokux en https://www.instructables.com/E-Slate-DIY-Wi-Fi-E-Ink-Digital-Film-Clapperboard/



Claqueta de cine digital y portátil construida sobre tinta electrónica (**e-ink / e-Paper**), usando **Seeed Studio XIAO ESP32-S3 Plus** y la placa controladora **XIAO ePaper Display Board (EE04)** con la librería gráfica **[Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2)**.

El dispositivo genera su propia red Wi-Fi y aloja un servidor web interno para que durante rodaje pueda actualizar remotamente los datos de la claqueta (ROLL, SCENE, TAKE, PROD, DIR, DOP, etc.) desde su teléfono móvil o tablet en tiempo real.

Tuve problemas intentando usar el platform IO en Antigravity IDE así que compilé y flashee la ESP32 desde Arduino.

---

## 🛠️ Especificaciones de Hardware

| Componente | Especificación Técnica |
| :--- | :--- |
| **Microcontrolador** | **Seeed Studio XIAO ESP32-S3 Plus** (Xtensa LX7 Dual-Core @ 240MHz) |
| **Memoria** | 16 MB Flash (QIO) + 8 MB PSRAM OPI (`qio_opi`) |
| **Carrier Board** | **XIAO ePaper Display Board - EE04** |
| **Pantalla** | **7.5" Monochrome ePaper (800 x 480 px, UC8179)** |
| **Librería Gráfica** | **Seeed_GFX2** (buffers nativos de 1 bpp) |
| **Conectividad** | Wi-Fi 802.11 b/g/n (Modo SoftAP autónomo) + WebServer HTTP |
| **Alimentación** | Batería LiPo 3.7V con conector JST 2.0mm o USB-C |

---

## 📁 Estructura del Proyecto

```text
eink_clap/
├── .vscode/               # Configuración para Antigravity IDE
├── eink_clap/
│   └── eink_clap.ino      # Firmware de la claqueta (Seeed_GFX2 + SoftAP + WebServer)
├── src -> eink_clap       # Enlace simbólico estándar para PlatformIO
├── include/               # Archivos de inclusión C/C++
├── lib/                   # Librerías locales
├── platformio.ini         # Configuración y dependencias de compilación
├── GEMINI.md              # Memoria, directivas y reglas para el agente de IA
└── README.md              # Documentación general del repositorio
```

---

## 💻 Desarrollo y Compilación con PlatformIO

Este proyecto está configurado para compilarse a máxima velocidad en **PlatformIO** dentro de **Antigravity IDE**:

1. **Abrir el proyecto**:
   - Si no aparece listado en PlatformIO Home, haz clic en la casita 🏠 → **Open Project** → selecciona la carpeta del repositorio.
   - O recarga la ventana con `Cmd + Shift + P` → `Developer: Reload Window`.
2. **Compilar**:
   - Presiona el icono del **check (✔)** en la barra inferior o ejecuta en la terminal:
     ```bash
     pio run
     ```
3. **Subir a la placa**:
   - Conecta la placa por USB y pulsa la **flecha (➜)** en la barra inferior o ejecuta:
     ```bash
     pio run -t upload
     ```
4. **Monitor Serie**:
   - Pulsa el icono del enchufe (**🔌**) a 115200 baudios o ejecuta:
     ```bash
     pio device monitor
     ```

---

## 📱 Uso en Rodaje

1. Al encender la claqueta, la pantalla mostrará las instrucciones de conexión durante unos segundos:
   - **Red Wi-Fi**: `NEBRALTA_Clap`
   - **Contraseña**: `123456789P`
   - **Dirección Web**: `http://clap.local` (o `http://192.168.4.1`)
2. Conéctate a la red Wi-Fi desde tu smartphone, tablet o portátil.
3. Abre tu navegador web y entra a **`http://clap.local`**.
4. **Sincronización de fecha**: El sistema lee automáticamente la fecha del reloj de tu dispositivo móvil y la transfiere al e-Paper al guardar.
5. **Botones físicos en la claqueta**:
   - **`KEY1`**: Suma **+1 TAKE** tras cada toma ("¡Corten!").
   - **`KEY3`**: Suma **+1 ROLL** al cambiar de tarjeta o rollo.

---

## ⚙️ Configuración para Arduino IDE

Si compilas desde **Arduino IDE**:
- **Placa**: `XIAO_ESP32S3` (o `ESP32S3 Dev Module`)
- **USB CDC On Boot**: `Enabled`
- **CPU Frequency**: `240MHz (WiFi)`
- **Flash Size**: `16MB (128Mb)`
- **Partition Scheme**: `16M Flash (3MB APP/9.9MB FATFS)` o `default 16MB`
- **PSRAM**: `OPI PSRAM`
- **Librería requerida**: Instalar o clonar [Seeed_GFX2](https://github.com/Seeed-Studio/Seeed_GFX2) en tu carpeta `Arduino/libraries`.

---

## 🔌 Solución de Problemas: Detección USB (macOS)

Si el puerto serie de la placa desaparece en tu Mac:
1. Mantén presionado el botón **`BOOT`** (marcado como `B`).
2. Presiona y suelta una vez el botón **`RESET`** (marcado como `R`).
3. Suelta el botón **`BOOT`**.
4. Verifica en la terminal con:
   ```bash
   ls -l /dev/cu.usb*
   ```

---

## 📄 Licencia

Este proyecto es de código abierto y está disponible bajo los términos de la [GNU General Public License v3.0 (GPLv3)](LICENSE).
