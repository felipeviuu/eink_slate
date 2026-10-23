/*
 * Film_Clap_Board - Professional Cinema Edition (Seeed_GFX2)
 * Hardware: Seeed Studio XIAO ESP32-S3 Plus + XIAO ePaper Display Board (EE04)
 * Panel: 7.5-inch Monochrome ePaper (800x480, UC8179)
 *
 * Características avanzadas:
 *  - Indicador de estado LED en GPIO 21 (Encendido, latido de vida y carga
 * completa)
 *  - Detección y monitoreo de nivel de BATERÍA (A0 / GPIO1 con enable en D5 /
 * GPIO6)
 *  - Modo Invertido (Dark Mode: fondo negro y letras blancas)
 *  - 3 Diseños (Layouts) seleccionables: Cinema Pro, Big Slate y Commercial
 *  - Selección granular de campos visibles (activar / ocultar campos según
 * necesidad)
 *  - Protección contra resets involuntarios (Brownout prevention, Wi-Fi 15dBm,
 * refresco asíncrono)
 *  - Mapeo de 3 botones físicos EE04:
 *      * KEY0 (GPIO 2): Conmutar Modo Invertido (Blanco ↔ Negro)
 *      * KEY1 (GPIO 3): +1 TAKE (Siguiente toma)
 *      * KEY2 (GPIO 5): +1 ROLL (Siguiente rollo)
 *  - Arranque fijo en pantalla de instrucciones: No se borra hasta pulsar
 * CUALQUIER botón o actualizar vía Web
 *  - Sincronización automática de FECHA desde el dispositivo móvil
 *  - Soporte de emoji Sakura (🌸) embebido en bitmap monocromático
 *  - Mayúsculas sostenidas automáticas
 *
 * Versión: 2.0.0 (Cinema Pro Engine + BLE Dual Mode + Timecode Engine)
 */

#define ESLATE_VERSION "2.0.0"

#include "esp_task_wdt.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <ESPmDNS.h>
#include <Seeed_GFX.h>
#include <WebServer.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =========================================================================
// HIERARCHICAL ELASTIC SLICING (HES) ESTRUCTURAS DE DATOS
// =========================================================================
struct TierDef {
  int nominalWeight;
  bool active;
  int y;
  int h;
};

struct CellSlot {
  int nominalWeight;
  bool active;
  int x;
  int w;
};

// Sobrescribir la inicialización de Brownout del sistema ESP-IDF (etapa CORE pre-arranque)
// Esto impide que el núcleo active el reset por caída transitoria antes de llegar a setup()
extern "C" void esp_brownout_init(void) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

extern "C" void esp_brownout_disable(void) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

// Deshabilitar el detector de Brownout ANTES de inicializar objetos globales C++
void __attribute__((constructor)) disableBrownoutEarly() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

// Instancia Seeed_GFX2 para el panel 7.5" en la placa EE04
Seeed_GFX display(Seeed_Product::Seeed_ePaper_7INCH5);

// Pulsadores físicos de la placa EE04 (activos en nivel BAJO)
const int PIN_BTN_KEY0 = 2; // KEY0 (GPIO2) -> Modo Invertido (Blanco/Negro)
const int PIN_BTN_KEY1 = 3; // KEY1 (GPIO3) -> +1 TAKE
const int PIN_BTN_KEY2 = 5; // KEY2 (GPIO5) -> +1 ROLL

// LED integrado y ADC de batería en XIAO ESP32-S3 + EE04
const int PIN_LED = 21;    // LED_BUILTIN de la XIAO ESP32-S3 (Activo en BAJO)
const int PIN_BAT_ADC = 1; // Entrada ADC para batería (A0 / GPIO1)
const int PIN_ADC_EN = 6;  // Habilitador del divisor de batería (D5 / GPIO6)

// Configuración Wi-Fi SoftAP
const char *ssid = "NEBRALTA_Clap";
const char *password = "123456789P";

WebServer server(80);

// Campos Principales de la Claqueta
String roll = "A001";
String scene = "1A";
String take = "1";
String prod = "NEBRALTA 🌸";
String director = "DIRECTOR";
String dop = "FELIPELOTAS1";
String note = "ISO 120";
String date = "25.09.2026";
String timecode = "00:00:00";

// Metadatos de Cámara, Audio y Rodaje
String cam = "A";
String sound = "SYNC"; // "SYNC" o "MOS"
String loc = "INT";    // "INT" o "EXT"
String light = "DAY";  // "DAY" o "NIGHT"

// Configuración de Estilo y Diseños
bool invertedMode = false; // false: Fondo Blanco / Texto Negro; true: Fondo
                           // Negro / Texto Blanco
int layoutMode = 0;        // 0: Cinema Pro, 1: Big Slate, 2: Commercial

// Visibilidad individual de campos
bool showRoll = true;
bool showScene = true;
bool showTake = true;
bool showProd = true;
bool showDir = true;
bool showDop = true;
bool showCam = true;
bool showSound = true;
bool showLoc = true;
bool showLight = true;
bool showDate = true;
bool showTimecode = true;
bool showNote = true;
bool showBat = true;

// Motor de Timecode 1 Hz & Refresco Parcial UC8179
bool timecodeRunning = true;
int tcSlot = 0;       // 0: Top Header, 1: Lower Bar, 2: Center Hero
int tcSize = 1;       // 0: Med 24px (TextSize 3), 1: Large 40px (TextSize 5), 2: Giant XL 64px (TextSize 8)
String tcFps = "24";  // "23.976", "24", "25", "29.97", "30"
int tcOffset = 0;     // Desfase en frames (-128 a +127)
unsigned long lastTcMillis = 0;
uint16_t tcPartialRefreshCount = 0;
const uint16_t TC_GHOSTING_LIMIT = 60; // Refresco completo cada 60 ticks parciales para limpiar pigmento

// Bounding box por defecto (fallback UC8179)
const uint16_t TC_BOX_X = 280;
const uint16_t TC_BOX_Y = 140;
const uint16_t TC_BOX_W = 240;
const uint16_t TC_BOX_H = 40;

// Variables de interrupción para pulsadores (evita perder pulsaciones durante refresco parcial)
volatile bool isrKey0Triggered = false;
volatile bool isrKey1Triggered = false;
volatile bool isrKey2Triggered = false;
void IRAM_ATTR isrKey0() { isrKey0Triggered = true; }
void IRAM_ATTR isrKey1() { isrKey1Triggered = true; }
void IRAM_ATTR isrKey2() { isrKey2Triggered = true; }

// Estado del sistema
bool inBootScreen =
    true; // Se mantiene fija en pantalla de bienvenida hasta interacción
volatile bool needDisplayUpdate =
    false; // Bandera para procesar refresco de forma asíncrona y segura
volatile bool pinDisplayActive = false;
volatile bool needPinDisplayRender = false;
unsigned long pinDisplayExpiry = 0;
IPAddress localIP;
String resetReasonStr = "";

// Prototipos
void updateDisplay();
void renderPinModal();
void getSlotBoundsForCurrentLayout(int &slotX, int &slotY, int &slotW, int &slotH);
void getTimecodeBounds(uint16_t &x, uint16_t &y, uint16_t &w, uint16_t &h);
void parseTimecode(const String& tc, int& h, int& m, int& s);
String formatTimecode(int h, int m, int s);
void incrementTimecodeOneSecond();
void drawTimecodeBox();
void updateTimecodePartial();
void incrementTake();
void incrementRoll();
float getBatteryVoltage();
int getBatteryPercentage(float v);
void updateLED();

// =========================================================================
// MAPA DE BITS MONOCROMÁTICO DE 32x32 PÍXELES PARA EL EMOJI 🌸
// =========================================================================
const unsigned char epd_bitmap_sakura[128] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x20, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x00,
    0x00, 0x1F, 0xF8, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x0F, 0xF0, 0x00,
    0x07, 0xC7, 0xE3, 0xE0, 0x0F, 0xC3, 0xC3, 0xF0, 0x07, 0xE1, 0x87, 0xE0,
    0x07, 0xE0, 0x07, 0xE0, 0x0F, 0xF0, 0x0F, 0xF0, 0x1F, 0xF3, 0xCF, 0xF8,
    0x0F, 0xE3, 0xC7, 0xF0, 0x0F, 0x83, 0xC1, 0xF0, 0x03, 0x03, 0xC0, 0xC0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x01, 0xFC, 0x3F, 0x80,
    0x03, 0xFC, 0x3F, 0xC0, 0x03, 0xFC, 0x3F, 0xC0, 0x03, 0xFE, 0x7F, 0xC0,
    0x03, 0xFE, 0x7F, 0xC0, 0x00, 0x7E, 0x7E, 0x00, 0x00, 0x3C, 0x3C, 0x00,
    0x00, 0x30, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Secuencia UTF-8 de 4 bytes para el emoji 🌸
const String SAKURA_UTF8 = "\xF0\x9F\x8C\xB8";

// Colores activos según el modo invertido
inline uint16_t getBgColor() { return invertedMode ? TFT_BLACK : TFT_WHITE; }
inline uint16_t getFgColor() { return invertedMode ? TFT_WHITE : TFT_BLACK; }

// =========================================================================
// LECTURA DE BATERÍA Y GESTIÓN DE ENERGÍA
// =========================================================================
float getBatteryVoltage() {
  pinMode(PIN_BAT_ADC, INPUT);
  pinMode(PIN_ADC_EN, OUTPUT);
  digitalWrite(PIN_ADC_EN, HIGH); // Habilitar divisor analógico
  delay(10); // Permitir estabilización del condensador de filtrado

  // Promedio de 5 lecturas para mayor precisión y estabilidad
  long sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += analogRead(PIN_BAT_ADC);
    delay(2);
  }
  float raw = sum / 5.0f;

  // Fórmula oficial de calibración Seeed Studio para la placa EE04 (factor 7.16)
  float v = (raw / 4096.0f) * 7.16f;
  return v;
}

int getBatteryPercentage(float v) {
  // LiPo 3.7V estándar: 4.15V+ = 100%, 3.40V o menos = 0%
  if (v >= 4.15f)
    return 100;
  if (v <= 3.40f)
    return 0;
  int pct = (int)((v - 3.40f) / (4.15f - 3.40f) * 100.0f);
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  return pct;
}

// Control no bloqueante del LED de estado (GPIO 21)
void updateLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  unsigned long now = millis();

  static unsigned long lastBatCheck = 0;
  static float lastV = 4.0f;
  if (now - lastBatCheck > 4000 || lastBatCheck == 0) {
    lastBatCheck = now;
    lastV = getBatteryVoltage();
  }

  // Si está cargado al 100% o alimentado por USB (>= 4.15V) -> LED fijo
  // encendido
  if (lastV >= 4.15f) {
    digitalWrite(PIN_LED, LOW); // En XIAO ESP32-S3 LOW = Encendido
    return;
  }

  // Si batería baja (< 3.55V) -> Parpadeo rápido (alerta cada 200ms)
  // Si batería normal -> Latido suave de vida (pulso de 80ms cada 2 segundos)
  if (lastV < 3.55f) {
    if (now - lastBlink > 200) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(PIN_LED, ledState ? LOW : HIGH);
    }
  } else {
    if (ledState) {
      if (now - lastBlink > 80) {
        ledState = false;
        lastBlink = now;
        digitalWrite(PIN_LED, HIGH); // Apagar
      }
    } else {
      if (now - lastBlink > 1920) {
        ledState = true;
        lastBlink = now;
        digitalWrite(PIN_LED, LOW); // Encender breve pulso
      }
    }
  }
}

// Diagnóstico de causas de reinicio
String getResetReasonString() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
  case ESP_RST_POWERON:
    return "Power-on (Encendido normal)";
  case ESP_RST_EXT:
    return "External Pin Reset";
  case ESP_RST_SW:
    return "Software Reset";
  case ESP_RST_PANIC:
    return "Software Panic / Exception";
  case ESP_RST_INT_WDT:
    return "Interrupt Watchdog";
  case ESP_RST_TASK_WDT:
    return "Task Watchdog";
  case ESP_RST_WDT:
    return "Other Watchdog";
  case ESP_RST_DEEPSLEEP:
    return "Deep Sleep Wakeup";
  case ESP_RST_BROWNOUT:
    return "Brownout (Caida de tension detectada)";
  case ESP_RST_SDIO:
    return "SDIO Reset";
  default:
    return "Desconocido (" + String(reason) + ")";
  }
}

// =========================================================================
// FUNCIONES DE DIBUJO Y AJUSTE DE TEXTO
// =========================================================================

int getLineWidthWithSakura(const String &line, int size) {
  int w = 0;
  int pos = 0;
  while (pos < (int)line.length()) {
    int nextEmoji = line.indexOf(SAKURA_UTF8, pos);
    if (nextEmoji == -1) {
      w += (line.length() - pos) * 6 * size;
      break;
    } else {
      w += (nextEmoji - pos) * 6 * size;
      w += 32 + 6;
      pos = nextEmoji + SAKURA_UTF8.length();
    }
  }
  return w;
}

void drawLineWithSakura(const String &line, int startX, int startY, int size) {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();
  int curX = startX;
  int pos = 0;
  int len = line.length();

  while (pos < len) {
    int nextEmoji = line.indexOf(SAKURA_UTF8, pos);
    if (nextEmoji == -1) {
      String sub = line.substring(pos);
      display.setTextColor(fg, bg);
      display.setTextSize(size);
      display.drawString(sub, curX, startY);
      curX += sub.length() * 6 * size;
      break;
    } else {
      if (nextEmoji > pos) {
        String sub = line.substring(pos, nextEmoji);
        display.setTextColor(fg, bg);
        display.setTextSize(size);
        display.drawString(sub, curX, startY);
        curX += sub.length() * 6 * size;
      }

      int textH = 8 * size;
      int iconY = startY + ((textH - 32) / 2);
      if (iconY < startY)
        iconY = startY;

      // Dibujar icono con el color de primer plano actual
      display.drawBitmap(curX + 2, iconY, epd_bitmap_sakura, 32, 32, fg);
      curX += 32 + 6;
      pos = nextEmoji + SAKURA_UTF8.length();
    }
  }
}

void drawBadge(const String &label, int x, int y, int w, int h, bool active) {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();
  display.setFreeFont(nullptr);
  if (active) {
    display.fillRect(x, y, w, h, fg);
    display.setTextColor(bg, fg);
  } else {
    display.fillRect(x, y, w, h, bg);
    display.drawRect(x, y, w, h, fg);
    display.setTextColor(fg, bg);
  }
  display.setTextSize(2);
  display.drawCentreString(label, x + (w / 2), y + ((h - 14) / 2), 1);
}

void drawTextAuto(const String &text, int x, int y, int maxW, int maxH,
                  int defaultSize, int minSize = 1) {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();
  display.setTextColor(fg, bg);
  display.setFreeFont(nullptr);

  String uText = text;
  uText.toUpperCase();
  uText.trim();

  if (uText.length() == 0)
    return;

  // 1. Probar tamaño óptimo en UNA SOLA LÍNEA (desde defaultSize hasta minSize/1)
  int bestSingleSize = -1;
  for (int s = defaultSize; s >= minSize; s--) {
    int w = getLineWidthWithSakura(uText, s);
    int h = 8 * s;
    if (w <= maxW && h <= maxH) {
      bestSingleSize = s;
      break;
    }
  }

  // 2. Partición óptima balanceada en DOS LÍNEAS (espacios, guiones, barras, puntos)
  String line1 = "", line2 = "";
  int bestSplit = -1;
  float minCost = 9999.0f;
  int len = uText.length();
  float mid = len / 2.0f;

  for (int i = 1; i < len - 1; i++) {
    char c = uText[i];
    float weight = 0.0f;
    if (c == ' ') weight = 1.0f;
    else if (c == '-' || c == '/' || c == '_') weight = 1.5f;
    else if (c == '.' || c == ',') weight = 2.0f;

    if (weight > 0.0f) {
      int l1 = i;
      int l2 = len - (i + (c == ' ' ? 1 : 0));
      float cost = (float)max(l1, l2) + (abs(i - mid) * 0.5f) * weight;
      if (cost < minCost) {
        minCost = cost;
        bestSplit = i;
      }
    }
  }

  // Si no hay delimitador natural pero la palabra es larga (>14 caracteres), partir con guión
  if (bestSplit == -1 && len > 14) {
    bestSplit = len / 2;
    line1 = uText.substring(0, bestSplit) + "-";
    line2 = uText.substring(bestSplit);
  } else if (bestSplit != -1) {
    char splitChar = uText[bestSplit];
    if (splitChar == ' ') {
      line1 = uText.substring(0, bestSplit);
      line2 = uText.substring(bestSplit + 1);
    } else {
      line1 = uText.substring(0, bestSplit + 1);
      line2 = uText.substring(bestSplit + 1);
    }
  }
  line1.trim();
  line2.trim();

  // Probar tamaño óptimo en DOS LÍNEAS (desde defaultSize - 1 hasta minSize/1)
  int bestDoubleSize = -1;
  if (line1.length() > 0 && line2.length() > 0) {
    for (int s = defaultSize - 1; s >= minSize; s--) {
      int w1 = getLineWidthWithSakura(line1, s);
      int w2 = getLineWidthWithSakura(line2, s);
      int gap = (s >= 3) ? 4 : 2;
      int totalH = (8 * s * 2) + gap;
      if (w1 <= maxW && w2 <= maxW && totalH <= maxH) {
        bestDoubleSize = s;
        break;
      }
    }
  }

  // 3. Decisión de renderizado: Legibilidad y contraste cinematográfico
  bool useSingleLine = false;
  if (bestSingleSize != -1) {
    if (bestDoubleSize == -1) {
      useSingleLine = true;
    } else if (bestSingleSize >= 4) {
      useSingleLine = true; // Mantener una línea si cabe en tamaño 4 o superior
    } else if (bestDoubleSize > bestSingleSize) {
      useSingleLine = false; // Ej: 2 líneas en tamaño 3 es mucho más legible que 1 línea en tamaño 2
    } else {
      useSingleLine = true;
    }
  }

  if (useSingleLine && bestSingleSize >= 1) {
    int textH = 8 * bestSingleSize;
    int drawY = y + ((maxH - textH) / 2);
    if (drawY < y)
      drawY = y;
    drawLineWithSakura(uText, x, drawY, bestSingleSize);
  } else if (bestDoubleSize >= 1) {
    int lineH = 8 * bestDoubleSize;
    int gap = (bestDoubleSize >= 3) ? 4 : 2;
    int totalH = (lineH * 2) + gap;
    int drawY = y + ((maxH - totalH) / 2);
    if (drawY < y)
      drawY = y;
    drawLineWithSakura(line1, x, drawY, bestDoubleSize);
    drawLineWithSakura(line2, x, drawY + lineH + gap, bestDoubleSize);
  } else {
    // 4. Protección absoluta contra desbordamiento: Elipsis garantizada a tamaño 1
    int s = 1;
    String clipped = uText;
    while (clipped.length() > 3 && getLineWidthWithSakura(clipped + "...", s) > maxW) {
      clipped = clipped.substring(0, clipped.length() - 1);
    }
    int textH = 8 * s;
    int drawY = y + ((maxH - textH) / 2);
    if (drawY < y)
      drawY = y;
    drawLineWithSakura(clipped + "...", x, drawY, s);
  }
}

// Dibuja icono pequeño de batería
void drawBatteryIcon(int x, int y, int pct) {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();
  display.drawRect(x, y, 26, 12, fg);
  display.fillRect(x + 26, y + 3, 2, 6, fg); // Terminal positivo
  int fillW = (pct * 22) / 100;
  if (fillW > 0)
    display.fillRect(x + 2, y + 2, fillW, 8, fg);
  display.setTextSize(1);
  display.setTextColor(fg, bg);
  display.drawString(String(pct) + "%", x + 32, y + 2);
}

// =========================================================================
// PANTALLA DE BIENVENIDA E INSTRUCCIONES FIJAS EN EL ARRANQUE
// =========================================================================
void showIPOnBoot() {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();

  display.fillScreen(bg);
  display.setTextColor(fg, bg);
  display.setFreeFont(nullptr);

  display.drawRect(18, 18, 764, 444, fg);
  display.drawRect(20, 20, 760, 440, fg);

  display.setTextSize(4);
  display.drawCentreString("E-SLATE DIGITAL CLAPPER", 400, 36, 1);
  display.drawLine(40, 85, 760, 85, fg);

  // Instrucciones de conexión
  display.setTextSize(3);
  display.drawString("1. Conectate a la red Wi-Fi:", 60, 98);
  display.setTextSize(3);
  display.drawString(String("SSID: ") + ssid, 90, 130);
  display.drawString(String("PASS / PIN: ") + password, 90, 160);

  display.setTextSize(3);
  display.drawString("2. Abre en tu navegador (o App E-Slate):", 60, 200);
  display.setTextSize(4);
  display.drawString("http://clap.local", 90, 235);
  display.setTextSize(2);
  display.drawString(
      String("(o IP directa: http://") + localIP.toString() + ")", 90, 275);

  display.drawLine(40, 310, 760, 310, fg);

  // Mapeo detallado de botones físicos
  display.setTextSize(2);
  display.drawString("Mapeo de Botones:", 60, 330);
  display.drawString(
      "[KEY0] (GPIO2): Conmutar Modo Invertido (Blanco <-> Negro)", 90, 355);
  display.drawString("[KEY1] (GPIO3): +1 TAKE   |   [KEY2] (GPIO5): +1 ROLL",
                     90, 380);

  display.drawLine(40, 410, 760, 410, fg);

  // Estado de Batería y Prompt de inicio
  float vBat = getBatteryVoltage();
  int pctBat = getBatteryPercentage(vBat);
  display.setTextSize(2);
  display.drawString("Bat: " + String(pctBat) + "% (" + String(vBat, 2) + "V)",
                     60, 430);

  display.setTextColor(bg, fg);
  display.fillRect(320, 425, 440, 25, fg);
  display.drawCentreString("Presiona CUALQUIER boton para comenzar", 540, 429,
                           1);

  display.refresh();
  inBootScreen = true;
  Serial.println(
      "[PANTALLA] Instrucciones fijas en pantalla. Esperando interaccion...");
}

// =========================================================================
// HIERARCHICAL ELASTIC SLICING (HES) - GENERIC DYNAMIC LAYOUT REFLOW
// =========================================================================

void solveTierHeights(TierDef* tiers, int numTiers, int totalHeight = 480) {
  int totalWeight = 0;
  int maxWeight = 0;
  int maxIdx = -1;

  for (int i = 0; i < numTiers; i++) {
    if (tiers[i].active) {
      totalWeight += tiers[i].nominalWeight;
      if (tiers[i].nominalWeight > maxWeight) {
        maxWeight = tiers[i].nominalWeight;
        maxIdx = i;
      }
    } else {
      tiers[i].h = 0;
      tiers[i].y = 0;
    }
  }

  if (totalWeight <= 0) return;

  int allocatedHeight = 0;
  for (int i = 0; i < numTiers; i++) {
    if (tiers[i].active) {
      tiers[i].h = (tiers[i].nominalWeight * totalHeight) / totalWeight;
      allocatedHeight += tiers[i].h;
    }
  }

  if (maxIdx >= 0) {
    tiers[maxIdx].h += (totalHeight - allocatedHeight);
  }

  int curY = 0;
  for (int i = 0; i < numTiers; i++) {
    if (tiers[i].active) {
      tiers[i].y = curY;
      curY += tiers[i].h;
    }
  }
}

void solveCellWidths(CellSlot* cells, int numCells, int startX = 0, int totalWidth = 800) {
  int totalWeight = 0;
  int maxWeight = 0;
  int maxIdx = -1;

  for (int i = 0; i < numCells; i++) {
    if (cells[i].active) {
      totalWeight += cells[i].nominalWeight;
      if (cells[i].nominalWeight > maxWeight) {
        maxWeight = cells[i].nominalWeight;
        maxIdx = i;
      }
    } else {
      cells[i].w = 0;
      cells[i].x = 0;
    }
  }

  if (totalWeight <= 0) return;

  int allocatedWidth = 0;
  for (int i = 0; i < numCells; i++) {
    if (cells[i].active) {
      cells[i].w = (cells[i].nominalWeight * totalWidth) / totalWeight;
      allocatedWidth += cells[i].w;
    }
  }

  if (maxIdx >= 0) {
    cells[maxIdx].w += (totalWidth - allocatedWidth);
  }

  int curX = startX;
  for (int i = 0; i < numCells; i++) {
    if (cells[i].active) {
      cells[i].x = curX;
      curX += cells[i].w;
    }
  }
}

void drawTierDividers(const TierDef* tiers, int numTiers, uint16_t fg) {
  bool first = true;
  for (int i = 0; i < numTiers; i++) {
    if (!tiers[i].active) continue;
    if (!first) {
      display.drawLine(0, tiers[i].y, 800, tiers[i].y, fg);
    }
    first = false;
  }
}

void drawCellDividers(const CellSlot* cells, int numCells, int y, int h, uint16_t fg) {
  bool first = true;
  for (int i = 0; i < numCells; i++) {
    if (!cells[i].active) continue;
    if (!first) {
      display.drawLine(cells[i].x, y, cells[i].x, y + h, fg);
    }
    first = false;
  }
}

// =========================================================================
// DISEÑOS DE CLAQUETA (LAYOUTS CON REFLOW DINÁMICO HES)
// =========================================================================

// LAYOUT 0: "CINEMA PRO" (Tradicional 4 franjas con reflow elástico)
void drawLayoutCinemaPro() {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();

  int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);

  TierDef tiers[6] = {
    {tcTierWeight, showTimecode && (tcSlot == 0), 0, 0},
    {180, showRoll || showScene || showTake, 0, 0},
    {tcTierWeight, showTimecode && (tcSlot == 2), 0, 0},
    {95,  showProd, 0, 0},
    {95,  showDir || showDop, 0, 0},
    {110, showDate || showNote || (showTimecode && tcSlot == 1) || showCam || showSound || showLoc || showLight || showBat, 0, 0}
  };

  solveTierHeights(tiers, 6, 480);
  drawTierDividers(tiers, 6, fg);

  // FILA 1: ROLL | SCENE | TAKE (Tier 1)
  if (tiers[1].active) {
    CellSlot cells[3] = {
      {266, showRoll, 0, 0},
      {282, showScene, 0, 0},
      {252, showTake, 0, 0}
    };
    solveCellWidths(cells, 3, 0, 800);
    drawCellDividers(cells, 3, tiers[1].y, tiers[1].h, fg);

    display.setTextColor(fg, bg);
    if (showRoll && cells[0].active) {
      display.setTextSize(3);
      display.drawString("ROLL", cells[0].x + 15, tiers[1].y + 12);
      int defFont = (cells[0].w >= 600) ? 12 : (cells[0].w >= 360 ? 10 : 8);
      drawTextAuto(roll, cells[0].x + 20, tiers[1].y + 55, cells[0].w - 35, tiers[1].h - 65, defFont, 3);
    }
    if (showScene && cells[1].active) {
      display.setTextSize(3);
      display.drawString("SCENE", cells[1].x + 15, tiers[1].y + 12);
      int defFont = (cells[1].w >= 600) ? 12 : (cells[1].w >= 360 ? 10 : 8);
      drawTextAuto(scene, cells[1].x + 20, tiers[1].y + 55, cells[1].w - 35, tiers[1].h - 65, defFont, 3);
    }
    if (showTake && cells[2].active) {
      display.setTextSize(3);
      display.drawString("TAKE", cells[2].x + 15, tiers[1].y + 12);
      int defFont = (cells[2].w >= 600) ? 12 : (cells[2].w >= 360 ? 10 : 8);
      drawTextAuto(take, cells[2].x + 20, tiers[1].y + 55, cells[2].w - 35, tiers[1].h - 65, defFont, 3);
    }
  }

  // FILA 2: PRODUCCIÓN (Tier 3)
  if (tiers[3].active && showProd) {
    display.setTextSize(3);
    display.setTextColor(fg, bg);
    display.drawString("PROD", 15, tiers[3].y + ((tiers[3].h - 24) / 2));
    drawTextAuto(prod, 105, tiers[3].y + 6, 800 - 120, tiers[3].h - 12, 7, 2);
  }

  // FILA 3: DIR & DOP (Tier 4)
  if (tiers[4].active) {
    CellSlot cells[2] = {
      {400, showDir, 0, 0},
      {400, showDop, 0, 0}
    };
    solveCellWidths(cells, 2, 0, 800);
    drawCellDividers(cells, 2, tiers[4].y, tiers[4].h, fg);

    display.setTextSize(2);
    display.setTextColor(fg, bg);
    if (showDir && cells[0].active) {
      display.drawString("DIR", cells[0].x + 15, tiers[4].y + ((tiers[4].h - 16) / 2));
      drawTextAuto(director, cells[0].x + 65, tiers[4].y + 6, cells[0].w - 80, tiers[4].h - 12, 5, 1);
    }
    if (showDop && cells[1].active) {
      display.drawString("DOP", cells[1].x + 15, tiers[4].y + ((tiers[4].h - 16) / 2));
      drawTextAuto(dop, cells[1].x + 65, tiers[4].y + 6, cells[1].w - 80, tiers[4].h - 12, 5, 1);
    }
  }

  // FILA 4: DATE, NOTE, LOWER BAR TC, CAM, BADGES (Tier 5)
  if (tiers[5].active) {
    CellSlot techCells[4] = {
      {280, showDate || showNote, 0, 0},
      {220, showTimecode && (tcSlot == 1), 0, 0},
      {100, showCam, 0, 0},
      {200, showSound || showLoc || showLight || showBat, 0, 0}
    };
    solveCellWidths(techCells, 4, 0, 800);
    drawCellDividers(techCells, 4, tiers[5].y, tiers[5].h, fg);

    // Sub-bloque A: Metadata (DATE & NOTE)
    if (techCells[0].active) {
      display.setTextColor(fg, bg);
      if (showDate && showNote) {
        int halfH = tiers[5].h / 2;
        display.setTextSize(2);
        display.drawString("DATE", techCells[0].x + 15, tiers[5].y + ((halfH - 16) / 2));
        display.setTextSize(4);
        display.drawString(date, techCells[0].x + 80, tiers[5].y + ((halfH - 32) / 2));

        display.setTextSize(2);
        display.drawString("NOTE", techCells[0].x + 15, tiers[5].y + halfH + ((halfH - 16) / 2));
        drawTextAuto(note, techCells[0].x + 80, tiers[5].y + halfH + 4, techCells[0].w - 95, halfH - 8, 3, 1);
      } else if (showDate) {
        display.setTextSize(2);
        display.drawString("DATE", techCells[0].x + 15, tiers[5].y + ((tiers[5].h - 16) / 2));
        display.setTextSize(4);
        display.drawString(date, techCells[0].x + 80, tiers[5].y + ((tiers[5].h - 32) / 2));
      } else if (showNote) {
        display.setTextSize(2);
        display.drawString("NOTE", techCells[0].x + 15, tiers[5].y + ((tiers[5].h - 16) / 2));
        drawTextAuto(note, techCells[0].x + 80, tiers[5].y + 8, techCells[0].w - 95, tiers[5].h - 16, 4, 1);
      }
    }

    // Sub-bloque B: Cámara (CAM)
    if (techCells[2].active && showCam) {
      display.setTextSize(2);
      display.drawString("CAM", techCells[2].x + 15, tiers[5].y + 12);
      display.setTextSize(6);
      display.drawCentreString(cam, techCells[2].x + (techCells[2].w / 2), tiers[5].y + ((tiers[5].h - 48) / 2) + 6, 1);
    }

    // Sub-bloque C: Badges y Batería
    if (techCells[3].active) {
      int curY = tiers[5].y + 8;
      if (showSound) {
        bool isSync = (sound == "SYNC");
        int bw = (techCells[3].w - 25) / 2;
        int bh = (showLoc || showLight) ? ((tiers[5].h / 2) - 12) : (tiers[5].h - 25);
        drawBadge("SYNC", techCells[3].x + 10, curY, bw, bh, isSync);
        drawBadge("MOS", techCells[3].x + 15 + bw, curY, bw, bh, !isSync);
        curY += bh + 6;
      }

      if (showLoc || showLight) {
        int bh = showSound ? ((tiers[5].h / 2) - 14) : (tiers[5].h - 25);
        if (showLoc && showLight) {
          int qw = (techCells[3].w - 28) / 4;
          drawBadge("INT", techCells[3].x + 8, curY, qw, bh, loc == "INT");
          drawBadge("EXT", techCells[3].x + 10 + qw, curY, qw, bh, loc != "INT");
          drawBadge("DAY", techCells[3].x + 14 + 2 * qw, curY, qw, bh, light == "DAY");
          drawBadge("NIGHT", techCells[3].x + 16 + 3 * qw, curY, qw, bh, light != "DAY");
        } else if (showLoc) {
          int hw = (techCells[3].w - 25) / 2;
          drawBadge("INT", techCells[3].x + 10, curY, hw, bh, loc == "INT");
          drawBadge("EXT", techCells[3].x + 15 + hw, curY, hw, bh, loc != "INT");
        } else if (showLight) {
          int hw = (techCells[3].w - 25) / 2;
          drawBadge("DAY", techCells[3].x + 10, curY, hw, bh, light == "DAY");
          drawBadge("NIGHT", techCells[3].x + 15 + hw, curY, hw, bh, light != "DAY");
        }
      }

      if (showBat) {
        float vBat = getBatteryVoltage();
        int pct = getBatteryPercentage(vBat);
        drawBatteryIcon(techCells[3].x + 15, tiers[5].y + tiers[5].h - 18, pct);
      }
    }
  }
}

// LAYOUT 1: "BIG SLATE" (Escena y Toma Gigantes con reflow elástico)
void drawLayoutBigSlate() {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();

  int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);

  TierDef tiers[5] = {
    {tcTierWeight, showTimecode && (tcSlot == 0), 0, 0},
    {260, showScene || showTake, 0, 0},
    {tcTierWeight, showTimecode && (tcSlot == 2), 0, 0},
    {110, showRoll || showProd || showCam, 0, 0},
    {110, showDir || showDop || (showTimecode && tcSlot == 1) || showDate || showSound || showLoc || showLight || showBat, 0, 0}
  };

  solveTierHeights(tiers, 5, 480);
  drawTierDividers(tiers, 5, fg);

  // Gran franja superior para SCENE y TAKE (Tier 1)
  if (tiers[1].active) {
    CellSlot cells[2] = {
      {400, showScene, 0, 0},
      {400, showTake, 0, 0}
    };
    solveCellWidths(cells, 2, 0, 800);
    drawCellDividers(cells, 2, tiers[1].y, tiers[1].h, fg);

    display.setTextColor(fg, bg);
    if (showScene && cells[0].active) {
      display.setTextSize(3);
      display.drawString("SCENE", cells[0].x + 20, tiers[1].y + 15);
      int defFont = (cells[0].w > 500) ? 12 : 10;
      drawTextAuto(scene, cells[0].x + 25, tiers[1].y + 55, cells[0].w - 50, tiers[1].h - 70, defFont, 4);
    }
    if (showTake && cells[1].active) {
      display.setTextSize(3);
      display.drawString("TAKE", cells[1].x + 20, tiers[1].y + 15);
      int defFont = (cells[1].w > 500) ? 12 : 10;
      drawTextAuto(take, cells[1].x + 25, tiers[1].y + 55, cells[1].w - 50, tiers[1].h - 70, defFont, 4);
    }
  }

  // Franja intermedia: ROLL, PROD, CAM (Tier 3)
  if (tiers[3].active) {
    CellSlot cells[3] = {
      {200, showRoll, 0, 0},
      {450, showProd, 0, 0},
      {150, showCam, 0, 0}
    };
    solveCellWidths(cells, 3, 0, 800);
    drawCellDividers(cells, 3, tiers[3].y, tiers[3].h, fg);

    display.setTextColor(fg, bg);
    if (showRoll && cells[0].active) {
      display.setTextSize(2);
      display.drawString("ROLL", cells[0].x + 15, tiers[3].y + 12);
      drawTextAuto(roll, cells[0].x + 15, tiers[3].y + 40, cells[0].w - 30, tiers[3].h - 50, 6, 2);
    }
    if (showProd && cells[1].active) {
      display.setTextSize(2);
      display.drawString("PROD", cells[1].x + 15, tiers[3].y + 12);
      drawTextAuto(prod, cells[1].x + 15, tiers[3].y + 40, cells[1].w - 30, tiers[3].h - 50, 6, 2);
    }
    if (showCam && cells[2].active) {
      display.setTextSize(2);
      display.drawString("CAM", cells[2].x + 15, tiers[3].y + 12);
      display.setTextSize(5);
      display.drawCentreString(cam, cells[2].x + (cells[2].w / 2), tiers[3].y + ((tiers[3].h - 40) / 2) + 6, 1);
    }
  }

  // Franja inferior: DIR, DOP, LOWER BAR TC, DATE, BADGES (Tier 4)
  if (tiers[4].active) {
    CellSlot cells[3] = {
      {300, showDir || showDop, 0, 0},
      {220, showTimecode && (tcSlot == 1), 0, 0},
      {280, showDate || showSound || showLoc || showLight || showBat, 0, 0}
    };
    solveCellWidths(cells, 3, 0, 800);
    drawCellDividers(cells, 3, tiers[4].y, tiers[4].h, fg);

    display.setTextColor(fg, bg);
    // Bloque Izquierdo: Crew (DIR / DOP)
    if (cells[0].active) {
      if (showDir && showDop) {
        int halfH = tiers[4].h / 2;
        display.setTextSize(2);
        display.drawString("DIR: ", cells[0].x + 15, tiers[4].y + 10);
        drawTextAuto(director, cells[0].x + 75, tiers[4].y + 6, cells[0].w - 90, halfH - 10, 3, 1);

        display.setTextSize(2);
        display.drawString("DOP: ", cells[0].x + 15, tiers[4].y + halfH + 8);
        drawTextAuto(dop, cells[0].x + 75, tiers[4].y + halfH + 6, cells[0].w - 90, halfH - 10, 3, 1);
      } else if (showDir) {
        display.setTextSize(2);
        display.drawString("DIR: ", cells[0].x + 15, tiers[4].y + ((tiers[4].h - 16) / 2));
        drawTextAuto(director, cells[0].x + 75, tiers[4].y + 10, cells[0].w - 95, tiers[4].h - 20, 4, 1);
      } else if (showDop) {
        display.setTextSize(2);
        display.drawString("DOP: ", cells[0].x + 15, tiers[4].y + ((tiers[4].h - 16) / 2));
        drawTextAuto(dop, cells[0].x + 75, tiers[4].y + 10, cells[0].w - 95, tiers[4].h - 20, 4, 1);
      }
    }

    // Bloque Derecho: Date, Badges y Batería
    if (cells[2].active) {
      if (showDate) {
        display.setTextSize(2);
        display.drawString("DATE: " + date, cells[2].x + 15, tiers[4].y + 12);
      }
      if (showSound) {
        bool isSync = (sound == "SYNC");
        drawBadge(isSync ? "SYNC" : "MOS", cells[2].x + cells[2].w - 110, tiers[4].y + 8, 95, 34, true);
      }
      int badgeY = tiers[4].y + 48;
      if (showLoc && showLight) {
        drawBadge(loc, cells[2].x + 15, badgeY, 70, 36, true);
        drawBadge(light, cells[2].x + 95, badgeY, 70, 36, true);
      } else if (showLoc) {
        drawBadge(loc, cells[2].x + 15, badgeY, 90, 36, true);
      } else if (showLight) {
        drawBadge(light, cells[2].x + 15, badgeY, 90, 36, true);
      }
      if (showBat) {
        float vBat = getBatteryVoltage();
        int pct = getBatteryPercentage(vBat);
        drawBatteryIcon(cells[2].x + cells[2].w - 90, tiers[4].y + 55, pct);
      }
    }
  }
}

// LAYOUT 2: "COMMERCIAL / BRAND" (Cabecera destacada con reflow elástico)
void drawLayoutCommercial() {
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();

  int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);

  TierDef tiers[5] = {
    {tcTierWeight, showTimecode && (tcSlot == 0), 0, 0},
    {110, showProd, 0, 0},
    {tcTierWeight, showTimecode && (tcSlot == 2), 0, 0},
    {200, showRoll || showScene || showTake, 0, 0},
    {170, showDir || showDop || showNote || (showTimecode && tcSlot == 1) || showCam || showDate || showSound || showLoc || showLight || showBat, 0, 0}
  };

  solveTierHeights(tiers, 5, 480);
  drawTierDividers(tiers, 5, fg);

  // Cabecera superior prominente para el Cliente/Producción (Tier 1)
  if (tiers[1].active && showProd) {
    display.setTextSize(3);
    display.setTextColor(fg, bg);
    display.drawString("COMMERCIAL / CLIENT", 20, tiers[1].y + 12);
    drawTextAuto(prod, 30, tiers[1].y + 45, 740, tiers[1].h - 55, 7, 2);
  }

  // Bloque Central: ROLL, SCENE, TAKE (Tier 3)
  if (tiers[3].active) {
    CellSlot cells[3] = {
      {240, showRoll, 0, 0},
      {280, showScene, 0, 0},
      {280, showTake, 0, 0}
    };
    solveCellWidths(cells, 3, 0, 800);
    drawCellDividers(cells, 3, tiers[3].y, tiers[3].h, fg);

    display.setTextColor(fg, bg);
    if (showRoll && cells[0].active) {
      display.setTextSize(3);
      display.drawString("ROLL", cells[0].x + 15, tiers[3].y + 15);
      int defFont = (cells[0].w > 350) ? 10 : 8;
      drawTextAuto(roll, cells[0].x + 20, tiers[3].y + 55, cells[0].w - 35, tiers[3].h - 65, defFont, 3);
    }
    if (showScene && cells[1].active) {
      display.setTextSize(3);
      display.drawString("SCENE", cells[1].x + 15, tiers[3].y + 15);
      int defFont = (cells[1].w > 350) ? 10 : 8;
      drawTextAuto(scene, cells[1].x + 20, tiers[3].y + 55, cells[1].w - 35, tiers[3].h - 65, defFont, 3);
    }
    if (showTake && cells[2].active) {
      display.setTextSize(3);
      display.drawString("TAKE", cells[2].x + 15, tiers[3].y + 15);
      int defFont = (cells[2].w > 350) ? 10 : 8;
      drawTextAuto(take, cells[2].x + 20, tiers[3].y + 55, cells[2].w - 35, tiers[3].h - 65, defFont, 3);
    }
  }

  // Bloque Inferior: DIR, DOP, NOTE, LOWER BAR TC, CAM, DATE y BADGES (Tier 4 - 4 Columnas)
  if (tiers[4].active) {
    CellSlot cols[4] = {
      {280, showDir || showDop || showNote, 0, 0},
      {220, showTimecode && (tcSlot == 1), 0, 0},
      {140, showCam || showDate, 0, 0},
      {160, showSound || showLoc || showLight || showBat, 0, 0}
    };
    solveCellWidths(cols, 4, 0, 800);
    drawCellDividers(cols, 4, tiers[4].y, tiers[4].h, fg);

    display.setTextColor(fg, bg);
    // Columna 1: DIR, DOP y NOTE
    if (cols[0].active) {
      int cCount = (showDir ? 1 : 0) + (showDop ? 1 : 0) + (showNote ? 1 : 0);
      if (cCount > 0) {
        int sH = tiers[4].h / cCount;
        int curSlotY = tiers[4].y;
        if (showDir) {
          display.setTextSize(2);
          display.drawString("DIR", cols[0].x + 15, curSlotY + ((sH - 16) / 2));
          drawTextAuto(director, cols[0].x + 65, curSlotY + 5, cols[0].w - 80, sH - 10, 4, 1);
          curSlotY += sH;
        }
        if (showDop) {
          display.setTextSize(2);
          display.drawString("DOP", cols[0].x + 15, curSlotY + ((sH - 16) / 2));
          drawTextAuto(dop, cols[0].x + 65, curSlotY + 5, cols[0].w - 80, sH - 10, 4, 1);
          curSlotY += sH;
        }
        if (showNote) {
          display.setTextSize(2);
          display.drawString("NOTE", cols[0].x + 15, curSlotY + ((sH - 16) / 2));
          drawTextAuto(note, cols[0].x + 75, curSlotY + 5, cols[0].w - 90, sH - 10, 3, 1);
          curSlotY += sH;
        }
      }
    }

    // Columna 3: CAM y DATE (cols[2])
    if (cols[2].active) {
      if (showCam && showDate) {
        int halfH = tiers[4].h / 2;
        display.setTextSize(2);
        display.drawString("CAM", cols[2].x + 15, tiers[4].y + 12);
        display.setTextSize(5);
        display.drawString(cam, cols[2].x + (cols[2].w / 2) - 10, tiers[4].y + 35);
        display.setTextSize(2);
        display.drawString("DATE", cols[2].x + 15, tiers[4].y + halfH + 12);
        display.drawString(date, cols[2].x + 15, tiers[4].y + halfH + 38);
      } else if (showCam) {
        display.setTextSize(2);
        display.drawString("CAM", cols[2].x + 15, tiers[4].y + 15);
        display.setTextSize(6);
        display.drawCentreString(cam, cols[2].x + (cols[2].w / 2), tiers[4].y + ((tiers[4].h - 48) / 2) + 6, 1);
      } else if (showDate) {
        display.setTextSize(2);
        display.drawString("DATE", cols[2].x + 15, tiers[4].y + ((tiers[4].h - 16) / 2));
        display.setTextSize(4);
        display.drawString(date, cols[2].x + 75, tiers[4].y + ((tiers[4].h - 32) / 2));
      }
    }

    // Columna 4: BADGES y BATTERY (cols[3])
    if (cols[3].active) {
      if (showSound) {
        bool isSync = (sound == "SYNC");
        drawBadge(isSync ? "SYNC" : "MOS", cols[3].x + 10, tiers[4].y + 10, cols[3].w - 20, 38, true);
      }
      int badgeY = showSound ? (tiers[4].y + 55) : (tiers[4].y + 10);
      if (showLoc && showLight) {
        int hw = (cols[3].w - 25) / 2;
        drawBadge(loc, cols[3].x + 10, badgeY, hw, 38, true);
        drawBadge(light, cols[3].x + 15 + hw, badgeY, hw, 38, true);
      } else if (showLoc) {
        drawBadge(loc, cols[3].x + 10, badgeY, cols[3].w - 20, 38, true);
      } else if (showLight) {
        drawBadge(light, cols[3].x + 10, badgeY, cols[3].w - 20, 38, true);
      }
      if (showBat) {
        float vBat = getBatteryVoltage();
        int pct = getBatteryPercentage(vBat);
        drawBatteryIcon(cols[3].x + 15, tiers[4].y + tiers[4].h - 22, pct);
      }
    }
  }
}

// Modal superpuesto de alta visibilidad para el PIN de emparejamiento BLE
void renderPinModal() {
  const uint16_t ox = 184, oy = 140, ow = 432, oh = 200;
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();

  display.setFreeFont(nullptr);

  // Marco de diálogo de alto contraste con triple borde
  display.fillRect(ox, oy, ow, oh, bg);
  display.drawRect(ox, oy, ow, oh, fg);
  display.drawRect(ox + 1, oy + 1, ow - 2, oh - 2, fg);
  display.drawRect(ox + 4, oy + 4, ow - 8, oh - 8, fg);

  display.setTextColor(fg, bg);
  display.setTextSize(2);
  display.drawCentreString("PAIRING PIN / VINCULACION", ox + (ow / 2), oy + 25, 1);

  display.setTextSize(4);
  display.drawCentreString(String("PIN: ") + password, ox + (ow / 2), oy + 85, 1);

  display.setTextSize(2);
  display.drawCentreString("INTRODUCIR EN APP / IPHONE", ox + (ow / 2), oy + 150, 1);

  display.refreshPartial(ox, oy, ow, oh);
  Serial.println("[PANTALLA] Modal de PIN de vinculacion mostrado con refreshPartial.");
}

void getSlotBoundsForCurrentLayout(int &slotX, int &slotY, int &slotW, int &slotH) {
  int tcTierWeight = (tcSize == 2) ? 80 : ((tcSize == 1) ? 56 : 40);

  if (layoutMode == 1) {
    // Big Slate
    TierDef tiers[5] = {
      {tcTierWeight, tcSlot == 0, 0, 0},
      {260, showScene || showTake, 0, 0},
      {tcTierWeight, tcSlot == 2, 0, 0},
      {110, showRoll || showProd || showCam, 0, 0},
      {110, showDir || showDop || (tcSlot == 1) || showDate || showSound || showLoc || showLight || showBat, 0, 0}
    };
    solveTierHeights(tiers, 5, 480);
    if (tcSlot == 0) {
      slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
    } else if (tcSlot == 2) {
      slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
    } else { // tcSlot == 1
      CellSlot cells[3] = {
        {300, showDir || showDop, 0, 0},
        {220, true, 0, 0},
        {280, showDate || showSound || showLoc || showLight || showBat, 0, 0}
      };
      solveCellWidths(cells, 3, 0, 800);
      slotX = cells[1].x; slotY = tiers[4].y; slotW = cells[1].w; slotH = tiers[4].h;
    }
  } else if (layoutMode == 2) {
    // Commercial
    TierDef tiers[5] = {
      {tcTierWeight, tcSlot == 0, 0, 0},
      {110, showProd, 0, 0},
      {tcTierWeight, tcSlot == 2, 0, 0},
      {200, showRoll || showScene || showTake, 0, 0},
      {170, showDir || showDop || showNote || (tcSlot == 1) || showCam || showDate || showSound || showLoc || showLight || showBat, 0, 0}
    };
    solveTierHeights(tiers, 5, 480);
    if (tcSlot == 0) {
      slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
    } else if (tcSlot == 2) {
      slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
    } else { // tcSlot == 1
      CellSlot cols[4] = {
        {280, showDir || showDop || showNote, 0, 0},
        {220, true, 0, 0},
        {140, showCam || showDate, 0, 0},
        {160, showSound || showLoc || showLight || showBat, 0, 0}
      };
      solveCellWidths(cols, 4, 0, 800);
      slotX = cols[1].x; slotY = tiers[4].y; slotW = cols[1].w; slotH = tiers[4].h;
    }
  } else {
    // Cinema Pro (layoutMode == 0)
    TierDef tiers[6] = {
      {tcTierWeight, tcSlot == 0, 0, 0},
      {180, showRoll || showScene || showTake, 0, 0},
      {tcTierWeight, tcSlot == 2, 0, 0},
      {95,  showProd, 0, 0},
      {95,  showDir || showDop, 0, 0},
      {110, showDate || showNote || (tcSlot == 1) || showCam || showSound || showLoc || showLight || showBat, 0, 0}
    };
    solveTierHeights(tiers, 6, 480);
    if (tcSlot == 0) {
      slotX = 0; slotY = tiers[0].y; slotW = 800; slotH = tiers[0].h;
    } else if (tcSlot == 2) {
      slotX = 0; slotY = tiers[2].y; slotW = 800; slotH = tiers[2].h;
    } else { // tcSlot == 1
      CellSlot techCells[4] = {
        {280, showDate || showNote, 0, 0},
        {220, true, 0, 0},
        {100, showCam, 0, 0},
        {200, showSound || showLoc || showLight || showBat, 0, 0}
      };
      solveCellWidths(techCells, 4, 0, 800);
      slotX = techCells[1].x; slotY = tiers[5].y; slotW = techCells[1].w; slotH = tiers[5].h;
    }
  }
}

void getTimecodeBounds(uint16_t &x, uint16_t &y, uint16_t &w, uint16_t &h) {
  int slotX = 0, slotY = 0, slotW = 800, slotH = 480;
  getSlotBoundsForCurrentLayout(slotX, slotY, slotW, slotH);

  int boxW = 288;
  int boxH = 52;
  if (tcSize == 0) {
    boxW = 192;
    boxH = 36;
  } else if (tcSize == 2) {
    boxW = 432;
    boxH = 76;
  } else {
    boxW = 288;
    boxH = 52;
  }

  // Si el slot es más estrecho que la caja (ej. en Lower Bar), ajustar dinámicamente
  if (boxW > slotW - 8 && slotW > 0) {
    if (slotW >= 200) {
      boxW = 192;
      boxH = 36;
    } else {
      boxW = 144;
      boxH = 28;
    }
  }

  // Ensure bounding box never exceeds allocated slot height (preventing tier divider erasure)
  if (boxH > slotH - 4 && slotH > 8) {
    boxH = slotH - 4;
  }

  int rawX = slotX + (slotW - boxW) / 2;
  int rawY = slotY + (slotH - boxH) / 2;
  if (rawX < 0) rawX = 0;
  if (rawY < 0) rawY = 0;

  uint16_t alignedX = (rawX / 8) * 8;
  uint16_t alignedW = ((boxW + (rawX - alignedX) + 7) / 8) * 8;

  if (alignedX + alignedW > 800) {
    alignedW = 800 - alignedX;
  }
  if (rawY + boxH > 480) {
    boxH = 480 - rawY;
  }

  x = alignedX;
  y = (uint16_t)rawY;
  w = alignedW;
  h = (uint16_t)boxH;
}

void parseTimecode(const String& tc, int& h, int& m, int& s) {
  h = 0; m = 0; s = 0;
  if (tc.length() >= 8) {
    h = tc.substring(0, 2).toInt();
    m = tc.substring(3, 5).toInt();
    s = tc.substring(6, 8).toInt();
  } else if (tc.length() >= 5) {
    m = tc.substring(0, 2).toInt();
    s = tc.substring(3, 5).toInt();
  }
  h = constrain(h, 0, 23);
  m = constrain(m, 0, 59);
  s = constrain(s, 0, 59);
}

String formatTimecode(int h, int m, int s) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

void incrementTimecodeOneSecond() {
  int h = 0, m = 0, s = 0;
  parseTimecode(timecode, h, m, s);
  s++;
  if (s >= 60) {
    s = 0;
    m++;
    if (m >= 60) {
      m = 0;
      h++;
      if (h >= 24) {
        h = 0;
      }
    }
  }
  timecode = formatTimecode(h, m, s);
}

// Renderizado del recuadro de timecode en el framebuffer RAM
void drawTimecodeBox() {
  if (!showTimecode) return;
  uint16_t fg = getFgColor();
  uint16_t bg = getBgColor();

  uint16_t x, y, w, h;
  getTimecodeBounds(x, y, w, h);

  display.setFreeFont(nullptr);
  display.fillRect(x, y, w, h, bg);
  display.drawRect(x, y, w, h, fg);
  display.drawRect(x + 1, y + 1, w - 2, h - 2, fg);

  int tSize = 5;
  if (tcSize == 0) tSize = 3;
  else if (tcSize == 2) tSize = 8;
  else tSize = 5;

  int strW = 48 * tSize;
  int strH = 8 * tSize;
  while (tSize > 1 && (strW > (int)w - 6 || strH > (int)h - 4)) {
    tSize--;
    strW = 48 * tSize;
    strH = 8 * tSize;
  }

  display.setTextColor(fg, bg);
  display.setTextSize(tSize);
  int textX = x + (w - strW) / 2;
  int textY = y + (h - strH) / 2;
  display.drawString(timecode, textX, textY);
}

// Refresco parcial 1 Hz (UC8179) sin parpadeo del panel completo
void updateTimecodePartial() {
  if (inBootScreen || !showTimecode || pinDisplayActive) return;

  drawTimecodeBox();

  uint16_t x, y, w, h;
  getTimecodeBounds(x, y, w, h);
  display.refreshPartial(x, y, w, h);
  tcPartialRefreshCount++;

  // Mitigación de ghosting: forzar refresco completo cada 60 parciales
  if (tcPartialRefreshCount >= TC_GHOSTING_LIMIT) {
    tcPartialRefreshCount = 0;
    needDisplayUpdate = true;
    Serial.println("[EPD] Mitigacion de ghosting: 60 refrescos parciales alcanzados. Programando refresco completo.");
  }
}

// Renderizado principal en el panel e-paper
void updateDisplay() {
  uint16_t bg = getBgColor();
  display.fillScreen(bg);
  display.setFreeFont(nullptr);

  switch (layoutMode) {
  case 1:
    drawLayoutBigSlate();
    break;
  case 2:
    drawLayoutCommercial();
    break;
  case 0:
  default:
    drawLayoutCinemaPro();
    break;
  }

  // Si timecode está visible, renderizar caja dedicada en framebuffer
  if (showTimecode) {
    drawTimecodeBox();
  }

  // Si el modal de PIN está activo durante un refresco completo, dibujarlo encima
  if (pinDisplayActive) {
    const uint16_t ox = 184, oy = 140, ow = 432, oh = 200;
    uint16_t fg = getFgColor();
    display.fillRect(ox, oy, ow, oh, bg);
    display.drawRect(ox, oy, ow, oh, fg);
    display.drawRect(ox + 1, oy + 1, ow - 2, oh - 2, fg);
    display.drawRect(ox + 4, oy + 4, ow - 8, oh - 8, fg);
    display.setTextColor(fg, bg);
    display.setTextSize(2);
    display.drawCentreString("PAIRING PIN / VINCULACION", ox + (ow / 2), oy + 25, 1);
    display.setTextSize(4);
    display.drawCentreString(String("PIN: ") + password, ox + (ow / 2), oy + 85, 1);
    display.setTextSize(2);
    display.drawCentreString("INTRODUCIR EN APP / IPHONE", ox + (ow / 2), oy + 150, 1);
  }

  // Refrescar panel e-paper
  const GfxResult res = display.refresh();
  tcPartialRefreshCount = 0; // Reiniciar contador de parciales tras refresco completo
  if (!res) {
    Serial.printf("[Seeed_GFX2 Error]: %s\n", res.message);
  } else {
    Serial.println("[Seeed_GFX2] Display actualizado con exito.");
  }
}

// Incremento inteligente de TAKE (+1)
void incrementTake() {
  long val = take.toInt();
  if (val > 0 || take == "0") {
    take = String(val + 1);
  } else {
    int i = 0;
    while (i < (int)take.length() && isDigit(take[i]))
      i++;
    if (i > 0) {
      int num = take.substring(0, i).toInt() + 1;
      take = String(num) + take.substring(i);
    } else {
      take = "1";
    }
  }
  Serial.printf("[BOTON] TAKE -> %s\n", take.c_str());
  tcPartialRefreshCount = 0;
  needDisplayUpdate = true;
  notifyButtonEvent(0x01);
  bleBroadcastState();
}

// Incremento inteligente de ROLL (+1)
void incrementRoll() {
  int numStart = -1;
  for (int i = 0; i < (int)roll.length(); i++) {
    if (isDigit(roll[i])) {
      numStart = i;
      break;
    }
  }

  if (numStart != -1) {
    String prefix = roll.substring(0, numStart);
    String numStr = roll.substring(numStart);
    int numLen = numStr.length();
    long nextNum = numStr.toInt() + 1;

    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%0%dld", numLen);
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, nextNum);

    roll = prefix + String(buf);
  } else if (roll.length() == 1 && isAlpha(roll[0])) {
    char nextL = roll[0] + 1;
    if (nextL > 'Z' && roll[0] <= 'Z')
      nextL = 'A';
    roll = String(nextL);
  } else {
    roll = "A001";
  }

  Serial.printf("[BOTON] ROLL -> %s\n", roll.c_str());
  tcPartialRefreshCount = 0;
  needDisplayUpdate = true;
  notifyButtonEvent(0x02);
  bleBroadcastState();
}

// =========================================================================
// SERVIDOR BLE GATT NATIVO & SEGURIDAD HMAC-SHA256 (CERO LIBRERÍAS EXTERNAS)
// =========================================================================

#define BLE_SERVICE_UUID        "F47AC10B-58CC-4372-A567-0E02B2C3C400"
#define BLE_CHAR_STATE_UUID     "F47AC10B-58CC-4372-A567-0E02B2C3C401"
#define BLE_CHAR_CMD_UUID       "F47AC10B-58CC-4372-A567-0E02B2C3C402"
#define BLE_CHAR_BTN_UUID       "F47AC10B-58CC-4372-A567-0E02B2C3C403"
#define BLE_CHAR_BAT_UUID       "F47AC10B-58CC-4372-A567-0E02B2C3C404"
#define BLE_CHAR_AUTH_UUID      "F47AC10B-58CC-4372-A567-0E02B2C3C405"

BLEServer* pBleServer = nullptr;
BLECharacteristic* pCharState = nullptr;
BLECharacteristic* pCharCmd = nullptr;
BLECharacteristic* pCharBtn = nullptr;
BLECharacteristic* pCharBat = nullptr;
BLECharacteristic* pCharAuth = nullptr;

bool bleClientConnected = false;
bool isSessionAuthorized = false;
uint8_t currentNonce[16];
bool nonceValid = false;
uint8_t failedAuthCount = 0;
unsigned long lockoutUntil = 0;

uint8_t reassemblyBuf[1024];
size_t reassemblyLen = 0;
uint8_t expectedSeq = 0;
uint8_t totalChunks = 0;
unsigned long reassemblyStart = 0;
bool reassemblyInProgress = false;

uint16_t btnCounterTake = 0;
uint16_t btnCounterRoll = 0;
uint16_t btnCounterInvert = 0;

// SHA-256 (RFC 6234) & HMAC-SHA256 (RFC 2104) autónomo sin dependencias externas
static inline uint32_t sha256_rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

void sha256_compute(const uint8_t* data, size_t len, uint8_t out[32]) {
  static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  };
  uint32_t H[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };

  size_t bitLen = len * 8;
  size_t paddedLen = ((len + 9 + 63) / 64) * 64;
  uint8_t block[64];

  for (size_t offset = 0; offset < paddedLen; offset += 64) {
    memset(block, 0, 64);
    for (size_t i = 0; i < 64; i++) {
      size_t idx = offset + i;
      if (idx < len) block[i] = data[idx];
      else if (idx == len) block[i] = 0x80;
    }
    if (offset + 64 == paddedLen) {
      block[60] = (uint8_t)((bitLen >> 24) & 0xFF);
      block[61] = (uint8_t)((bitLen >> 16) & 0xFF);
      block[62] = (uint8_t)((bitLen >> 8) & 0xFF);
      block[63] = (uint8_t)(bitLen & 0xFF);
    }

    uint32_t W[64];
    for (int t = 0; t < 16; t++) {
      W[t] = ((uint32_t)block[t * 4] << 24) |
             ((uint32_t)block[t * 4 + 1] << 16) |
             ((uint32_t)block[t * 4 + 2] << 8) |
             ((uint32_t)block[t * 4 + 3]);
    }
    for (int t = 16; t < 64; t++) {
      uint32_t s0 = sha256_rotr(W[t - 15], 7) ^ sha256_rotr(W[t - 15], 18) ^ (W[t - 15] >> 3);
      uint32_t s1 = sha256_rotr(W[t - 2], 17) ^ sha256_rotr(W[t - 2], 19) ^ (W[t - 2] >> 10);
      W[t] = W[t - 16] + s0 + W[t - 7] + s1;
    }

    uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
    uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

    for (int t = 0; t < 64; t++) {
      uint32_t S1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + S1 + ch + K[t] + W[t];
      uint32_t S0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = S0 + maj;

      h = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }

    H[0] += a; H[1] += b; H[2] += c; H[3] += d;
    H[4] += e; H[5] += f; H[6] += g; H[7] += h;
  }

  for (int i = 0; i < 8; i++) {
    out[i * 4]     = (uint8_t)((H[i] >> 24) & 0xFF);
    out[i * 4 + 1] = (uint8_t)((H[i] >> 16) & 0xFF);
    out[i * 4 + 2] = (uint8_t)((H[i] >> 8) & 0xFF);
    out[i * 4 + 3] = (uint8_t)(H[i] & 0xFF);
  }
}

bool verifyHmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, const uint8_t* expectedHmac) {
  uint8_t kPad[64];
  memset(kPad, 0, 64);
  if (keyLen > 64) {
    sha256_compute(key, keyLen, kPad);
  } else {
    memcpy(kPad, key, keyLen);
  }

  uint8_t iBuf[64 + 64];
  uint8_t oBuf[64 + 32];
  for (int i = 0; i < 64; i++) {
    iBuf[i] = kPad[i] ^ 0x36;
    oBuf[i] = kPad[i] ^ 0x5c;
  }
  size_t copyLen = (msgLen <= 64) ? msgLen : 64;
  memcpy(&iBuf[64], msg, copyLen);

  uint8_t innerHash[32];
  sha256_compute(iBuf, 64 + copyLen, innerHash);
  memcpy(&oBuf[64], innerHash, 32);

  uint8_t hmacOutput[32];
  sha256_compute(oBuf, 64 + 32, hmacOutput);

  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) {
    diff |= (hmacOutput[i] ^ expectedHmac[i]);
  }
  return (diff == 0);
}

// Serializador y Parser JSON nativo ligero (sin ArduinoJson)
String jsonEscape(const String& s) {
  String out = "";
  out.reserve(s.length() + 4);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

String getSlateStateJson() {
  String out = "{";
  out.reserve(512);
  out += "\"roll\":\"" + jsonEscape(roll) + "\",";
  out += "\"scene\":\"" + jsonEscape(scene) + "\",";
  out += "\"take\":\"" + jsonEscape(take) + "\",";
  out += "\"prod\":\"" + jsonEscape(prod) + "\",";
  out += "\"dir\":\"" + jsonEscape(director) + "\",";
  out += "\"dop\":\"" + jsonEscape(dop) + "\",";
  out += "\"note\":\"" + jsonEscape(note) + "\",";
  out += "\"date\":\"" + jsonEscape(date) + "\",";
  out += "\"timecode\":\"" + jsonEscape(timecode) + "\",";
  out += "\"tc_slot\":" + String(tcSlot) + ",";
  out += "\"tc_size\":" + String(tcSize) + ",";
  out += "\"tc_fps\":\"" + jsonEscape(tcFps) + "\",";
  out += "\"tc_offset\":" + String(tcOffset) + ",";
  out += "\"tc_running\":" + String(timecodeRunning ? "true" : "false") + ",";
  out += "\"cam\":\"" + jsonEscape(cam) + "\",";
  out += "\"sound\":\"" + jsonEscape(sound) + "\",";
  out += "\"loc\":\"" + jsonEscape(loc) + "\",";
  out += "\"light\":\"" + jsonEscape(light) + "\",";
  out += "\"layout\":" + String(layoutMode) + ",";
  out += "\"invert\":" + String(invertedMode ? "true" : "false") + ",";
  out += "\"vis\":{";
  out += "\"roll\":" + String(showRoll ? "true" : "false") + ",";
  out += "\"scene\":" + String(showScene ? "true" : "false") + ",";
  out += "\"take\":" + String(showTake ? "true" : "false") + ",";
  out += "\"prod\":" + String(showProd ? "true" : "false") + ",";
  out += "\"dir\":" + String(showDir ? "true" : "false") + ",";
  out += "\"dop\":" + String(showDop ? "true" : "false") + ",";
  out += "\"cam\":" + String(showCam ? "true" : "false") + ",";
  out += "\"sound\":" + String(showSound ? "true" : "false") + ",";
  out += "\"loc\":" + String(showLoc ? "true" : "false") + ",";
  out += "\"light\":" + String(showLight ? "true" : "false") + ",";
  out += "\"date\":" + String(showDate ? "true" : "false") + ",";
  out += "\"timecode\":" + String(showTimecode ? "true" : "false") + ",";
  out += "\"note\":" + String(showNote ? "true" : "false") + ",";
  out += "\"bat\":" + String(showBat ? "true" : "false");
  out += "}}";
  return out;
}

bool jsonExtractString(const String& json, const char* key, String& outVal) {
  String pattern = String("\"") + key + "\"";
  int searchFrom = 0;
  int len = json.length();
  while (searchFrom < len) {
    int kPos = json.indexOf(pattern, searchFrom);
    if (kPos < 0) return false;
    int p = kPos + pattern.length();
    while (p < len && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) p++;
    if (p >= len || json[p] != ':') {
      searchFrom = p;
      continue;
    }
    p++;
    while (p < len && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) p++;
    if (p >= len || json[p] != '\"') {
      searchFrom = p;
      continue;
    }
    p++;
    String res = "";
    while (p < len) {
      char c = json[p++];
      if (c == '\\' && p < len) {
        char esc = json[p++];
        if (esc == '\"' || esc == '\\' || esc == '/') res += esc;
        else if (esc == 'n') res += '\n';
        else if (esc == 'r') res += '\r';
        else if (esc == 't') res += '\t';
        else res += esc;
      } else if (c == '\"') {
        outVal = res;
        return true;
      } else {
        res += c;
      }
    }
    return false;
  }
  return false;
}

bool jsonExtractInt(const String& json, const char* key, int& outVal) {
  String pattern = String("\"") + key + "\"";
  int kPos = json.indexOf(pattern);
  if (kPos < 0) return false;
  int p = kPos + pattern.length();
  int len = json.length();
  while (p < len && (json[p] == ' ' || json[p] == '\t' || json[p] == ':')) p++;
  if (p >= len) return false;
  int start = p;
  if (json[p] == '-') p++;
  while (p < len && isDigit(json[p])) p++;
  if (p == start || (p == start + 1 && json[start] == '-')) return false;
  outVal = json.substring(start, p).toInt();
  return true;
}

bool jsonExtractBool(const String& json, const char* key, bool& outVal) {
  String pattern = String("\"") + key + "\"";
  int kPos = json.indexOf(pattern);
  if (kPos < 0) return false;
  int p = kPos + pattern.length();
  int len = json.length();
  while (p < len && (json[p] == ' ' || json[p] == '\t' || json[p] == ':')) p++;
  if (p >= len) return false;
  if (json.startsWith("true", p) || json[p] == '1') {
    outVal = true;
    return true;
  }
  if (json.startsWith("false", p) || json[p] == '0') {
    outVal = false;
    return true;
  }
  return false;
}

bool applySlateStateJson(const String& jsonStr) {
  int braceStart = jsonStr.indexOf('{');
  int braceEnd = jsonStr.lastIndexOf('}');
  if (braceStart < 0 || braceEnd <= braceStart) {
    Serial.println("[BLE] Error parseando JSON: formato invalido");
    return false;
  }

  jsonExtractString(jsonStr, "roll", roll);
  jsonExtractString(jsonStr, "scene", scene);
  jsonExtractString(jsonStr, "take", take);
  jsonExtractString(jsonStr, "prod", prod);
  if (!jsonExtractString(jsonStr, "dir", director)) {
    jsonExtractString(jsonStr, "director", director);
  }
  jsonExtractString(jsonStr, "dop", dop);
  jsonExtractString(jsonStr, "note", note);
  jsonExtractString(jsonStr, "date", date);
  if (!jsonExtractString(jsonStr, "timecode", timecode)) {
    jsonExtractString(jsonStr, "tc", timecode);
  }
  int slotVal = tcSlot;
  if (jsonExtractInt(jsonStr, "tc_slot", slotVal)) {
    tcSlot = constrain(slotVal, 0, 2);
  }
  int sizeVal = tcSize;
  if (jsonExtractInt(jsonStr, "tc_size", sizeVal)) {
    tcSize = constrain(sizeVal, 0, 2);
  }
  String fpsVal;
  if (jsonExtractString(jsonStr, "tc_fps", fpsVal)) {
    tcFps = fpsVal;
  }
  int offsetVal = tcOffset;
  if (jsonExtractInt(jsonStr, "tc_offset", offsetVal)) {
    tcOffset = offsetVal;
  }
  bool runningVal = timecodeRunning;
  if (jsonExtractBool(jsonStr, "tc_running", runningVal)) {
    timecodeRunning = runningVal;
  }
  jsonExtractString(jsonStr, "cam", cam);
  jsonExtractString(jsonStr, "sound", sound);
  jsonExtractString(jsonStr, "loc", loc);
  jsonExtractString(jsonStr, "light", light);

  int lMode = layoutMode;
  if (jsonExtractInt(jsonStr, "layout", lMode)) {
    layoutMode = constrain(lMode, 0, 2);
  }
  jsonExtractBool(jsonStr, "invert", invertedMode);

  int visPos = jsonStr.indexOf("\"vis\"");
  if (visPos >= 0) {
    int vOpen = jsonStr.indexOf('{', visPos);
    int vClose = jsonStr.indexOf('}', vOpen);
    if (vOpen >= 0 && vClose > vOpen) {
      String visSub = jsonStr.substring(vOpen, vClose + 1);
      jsonExtractBool(visSub, "roll", showRoll);
      jsonExtractBool(visSub, "scene", showScene);
      jsonExtractBool(visSub, "take", showTake);
      jsonExtractBool(visSub, "prod", showProd);
      jsonExtractBool(visSub, "dir", showDir);
      jsonExtractBool(visSub, "dop", showDop);
      jsonExtractBool(visSub, "cam", showCam);
      jsonExtractBool(visSub, "sound", showSound);
      jsonExtractBool(visSub, "loc", showLoc);
      jsonExtractBool(visSub, "light", showLight);
      jsonExtractBool(visSub, "date", showDate);
      if (!jsonExtractBool(visSub, "timecode", showTimecode)) {
        jsonExtractBool(visSub, "tc", showTimecode);
      }
      jsonExtractBool(visSub, "note", showNote);
      jsonExtractBool(visSub, "bat", showBat);
    }
  }

  roll.toUpperCase();
  scene.toUpperCase();
  take.toUpperCase();
  prod.toUpperCase();
  director.toUpperCase();
  dop.toUpperCase();
  note.toUpperCase();
  date.toUpperCase();
  timecode.toUpperCase();
  cam.toUpperCase();
  sound.toUpperCase();
  loc.toUpperCase();
  light.toUpperCase();

  tcPartialRefreshCount = 0; // Reiniciar contador de parciales ante cambio de estado
  inBootScreen = false;
  needDisplayUpdate = true;
  return true;
}

void notifyButtonEvent(uint8_t btnId) {
  if (!pCharBtn || !bleClientConnected) return;
  uint16_t counter = 0;
  if (btnId == 0x01) counter = ++btnCounterTake;
  else if (btnId == 0x02) counter = ++btnCounterRoll;
  else if (btnId == 0x00) counter = ++btnCounterInvert;

  uint8_t payload[3];
  payload[0] = btnId;
  payload[1] = (uint8_t)((counter >> 8) & 0xFF);
  payload[2] = (uint8_t)(counter & 0xFF);
  pCharBtn->setValue(payload, 3);
  pCharBtn->notify();
}

void notifyBatteryTelemetry() {
  if (!pCharBat) return;
  float vBat = getBatteryVoltage();
  int pct = getBatteryPercentage(vBat);
  uint16_t mV = (uint16_t)(vBat * 1000.0f);
  uint8_t flags = 0;
  if (mV >= 4150) flags |= 0x01;
  if (mV < 3550) flags |= 0x02;

  uint8_t payload[4];
  payload[0] = (uint8_t)((mV >> 8) & 0xFF);
  payload[1] = (uint8_t)(mV & 0xFF);
  payload[2] = (uint8_t)(pct & 0xFF);
  payload[3] = flags;

  pCharBat->setValue(payload, 4);
  if (bleClientConnected) {
    pCharBat->notify();
  }
}

void bleBroadcastState() {
  if (!pCharState || !bleClientConnected) return;
  String jsonStr = getSlateStateJson();
  pCharState->setValue((uint8_t*)jsonStr.c_str(), jsonStr.length());
  pCharState->notify();
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleClientConnected = true;
    isSessionAuthorized = false;
    nonceValid = false;
    Serial.println("[BLE] Central conectada. Deteniendo publicidad para conexion exclusiva.");
    BLEDevice::getAdvertising()->stop();
  }

  void onDisconnect(BLEServer* pServer) override {
    bleClientConnected = false;
    isSessionAuthorized = false;
    nonceValid = false;
    if (pinDisplayActive) {
      pinDisplayActive = false;
      needDisplayUpdate = true;
    }
    Serial.println("[BLE] Central desconectada. Reanudando publicidad.");
    BLEDevice::startAdvertising();
  }
};

class StateCharCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) override {
    String json = getSlateStateJson();
    pCharacteristic->setValue((uint8_t*)json.c_str(), json.length());
  }

  void onWrite(BLECharacteristic* pCharacteristic) override {
    if (!isSessionAuthorized) {
      Serial.println("[BLE Sec] Escritura en 0xC401 rechazada: No autorizado");
      return;
    }
    auto rawVal = pCharacteristic->getValue();
    size_t len = rawVal.length();
    const uint8_t* val = (const uint8_t*)rawVal.c_str();
    if (len == 0) return;

    if (len >= 3 && val[0] < val[1] && val[2] == (len - 3)) {
      uint8_t seq = val[0];
      uint8_t total = val[1];
      uint8_t chunkLen = val[2];
      unsigned long now = millis();

      if (reassemblyInProgress && (now - reassemblyStart > 3000)) {
        reassemblyInProgress = false;
      }

      if (!reassemblyInProgress) {
        if (seq != 0) return;
        reassemblyInProgress = true;
        expectedSeq = 0;
        totalChunks = total;
        reassemblyLen = 0;
        reassemblyStart = now;
      } else {
        if (seq != expectedSeq || total != totalChunks) {
          reassemblyInProgress = false;
          return;
        }
      }

      if (reassemblyLen + chunkLen < sizeof(reassemblyBuf)) {
        memcpy(&reassemblyBuf[reassemblyLen], &val[3], chunkLen);
        reassemblyLen += chunkLen;
        expectedSeq++;
      }

      if (expectedSeq == totalChunks) {
        reassemblyInProgress = false;
        reassemblyBuf[reassemblyLen] = '\0';
        String fullJson = String((char*)reassemblyBuf);
        applySlateStateJson(fullJson);
        bleBroadcastState();
      }
    } else {
      String jsonStr = String(rawVal.c_str());
      applySlateStateJson(jsonStr);
      bleBroadcastState();
    }
  }
};

class CmdCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    if (!isSessionAuthorized) {
      Serial.println("[BLE Sec] Comando en 0xC402 rechazado: No autorizado");
      return;
    }
    auto rawVal = pCharacteristic->getValue();
    if (rawVal.length() == 0) return;
    const uint8_t* val = (const uint8_t*)rawVal.c_str();
    uint8_t opcode = val[0];

    inBootScreen = false;
    tcPartialRefreshCount = 0;
    switch (opcode) {
      case 0x01:
        incrementTake();
        notifyButtonEvent(0x01);
        bleBroadcastState();
        break;
      case 0x02:
        incrementRoll();
        notifyButtonEvent(0x02);
        bleBroadcastState();
        break;
      case 0x03:
        invertedMode = !invertedMode;
        notifyButtonEvent(0x00);
        needDisplayUpdate = true;
        bleBroadcastState();
        break;
      case 0x04:
        needDisplayUpdate = true;
        break;
      case 0x05:
        needDisplayUpdate = true;
        break;
      case 0x06:
        if (rawVal.length() > 1) {
          int l = (int)val[1];
          layoutMode = constrain(l, 0, 2);
          needDisplayUpdate = true;
          bleBroadcastState();
        }
        break;
      case 0x07:
        timecodeRunning = !timecodeRunning;
        Serial.printf("[BLE] Timecode 1 Hz ticker: %s\n", timecodeRunning ? "ACTIVO" : "PAUSADO");
        bleBroadcastState();
        break;
      case 0x08:
        if (rawVal.length() > 1) {
          tcSlot = constrain((int)val[1], 0, 2);
          needDisplayUpdate = true;
          bleBroadcastState();
          Serial.printf("[BLE] Set TC Slot: %d\n", tcSlot);
        }
        break;
      case 0x09:
        if (rawVal.length() > 1) {
          tcSize = constrain((int)val[1], 0, 2);
          needDisplayUpdate = true;
          bleBroadcastState();
          Serial.printf("[BLE] Set TC Size: %d\n", tcSize);
        }
        break;
      case 0x0A:
        if (rawVal.length() > 1) {
          uint8_t fpsIdx = val[1];
          if (fpsIdx == 0) tcFps = "23.976";
          else if (fpsIdx == 1) tcFps = "24";
          else if (fpsIdx == 2) tcFps = "25";
          else if (fpsIdx == 3) tcFps = "29.97";
          else if (fpsIdx == 4) tcFps = "30";
          else tcFps = "24";
          bleBroadcastState();
          Serial.printf("[BLE] Set TC FPS: %s\n", tcFps.c_str());
        }
        break;
      case 0x0B:
        if (rawVal.length() > 1) {
          showTimecode = (val[1] != 0);
          needDisplayUpdate = true;
          bleBroadcastState();
          Serial.printf("[BLE] Set TC Visibility: %s\n", showTimecode ? "VISIBLE" : "OCULTO");
        }
        break;
      case 0x0C:
        if (rawVal.length() >= 4) {
          int h = constrain((int)val[1], 0, 23);
          int m = constrain((int)val[2], 0, 59);
          int s = constrain((int)val[3], 0, 59);
          timecode = formatTimecode(h, m, s);
          lastTcMillis = millis();
          timecodeRunning = true;
          tcPartialRefreshCount = 0;
          Serial.printf("[BLE] Fast Jam-Sync TOD: %s\n", timecode.c_str());
          if (!pinDisplayActive && !inBootScreen && showTimecode) {
            updateTimecodePartial();
          }
          bleBroadcastState();
        }
        break;
      default:
        Serial.printf("[BLE] Opcode desconocido: 0x%02X\n", opcode);
        break;
    }
  }
};

class AuthCharCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) override {
    esp_fill_random(currentNonce, 16);
    nonceValid = true;
    uint8_t payload[17];
    payload[0] = 0x01;
    memcpy(&payload[1], currentNonce, 16);
    pCharacteristic->setValue(payload, 17);

    // Milestone 8: Trigger On-Screen PIN Modal
    pinDisplayActive = true;
    pinDisplayExpiry = millis() + 30000;
    needPinDisplayRender = true;
    Serial.println("[BLE Sec] Nonce de autenticacion generado. Activando PIN en pantalla (30s).");
  }

  void onWrite(BLECharacteristic* pCharacteristic) override {
    auto rawVal = pCharacteristic->getValue();
    size_t len = rawVal.length();
    const uint8_t* val = (const uint8_t*)rawVal.c_str();
    unsigned long now = millis();

    if (now < lockoutUntil) {
      uint8_t lockPayload[2] = {0x03, 0x02};
      pCharacteristic->setValue(lockPayload, 2);
      pCharacteristic->notify();
      Serial.println("[BLE Sec] Intento rechazado: En periodo de bloqueo (60s)");
      return;
    } else if (lockoutUntil > 0) {
      failedAuthCount = 0;
      lockoutUntil = 0;
    }

    if (len < 33 || val[0] != 0x02 || !nonceValid) {
      handleAuthFailure(pCharacteristic, now);
      return;
    }

    const uint8_t* clientHmac = &val[1];
    bool valid = verifyHmacSha256((const uint8_t*)password, strlen(password), currentNonce, 16, clientHmac);

    if (valid) {
      isSessionAuthorized = true;
      failedAuthCount = 0;
      nonceValid = false;

      // Descartar modal de PIN y restaurar claqueta al autenticar con éxito
      if (pinDisplayActive) {
        pinDisplayActive = false;
        needDisplayUpdate = true;
      }

      uint8_t token[16];
      esp_fill_random(token, 16);

      uint8_t resp[18];
      resp[0] = 0x03;
      resp[1] = 0x00;
      memcpy(&resp[2], token, 16);
      pCharacteristic->setValue(resp, 18);
      pCharacteristic->notify();
      Serial.println("[BLE Sec] ¡Autenticacion EXITOSA! Sesion autorizada.");
    } else {
      handleAuthFailure(pCharacteristic, now);
    }
  }

  void handleAuthFailure(BLECharacteristic* pChar, unsigned long now) {
    failedAuthCount++;
    Serial.printf("[BLE Sec] Fallo de autenticacion (#%d)\n", failedAuthCount);
    if (failedAuthCount >= 3) {
      lockoutUntil = now + 60000;
      if (pinDisplayActive) {
        pinDisplayActive = false;
        needDisplayUpdate = true;
      }
      Serial.println("[BLE Sec] 3 fallos consecutivos: Bloqueo activado por 60s.");
    }
    uint8_t failPayload[2] = {0x03, 0x01};
    pChar->setValue(failPayload, 2);
    pChar->notify();
  }
};

void initBLE() {
  BLEDevice::init("E-Slate NEBRALTA");
  BLEDevice::setMTU(512);

  pBleServer = BLEDevice::createServer();
  pBleServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pBleServer->createService(BLE_SERVICE_UUID);

  pCharState = pService->createCharacteristic(
      BLE_CHAR_STATE_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharState->addDescriptor(new BLE2902());
  pCharState->setCallbacks(new StateCharCallbacks());

  pCharCmd = pService->createCharacteristic(
      BLE_CHAR_CMD_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pCharCmd->setCallbacks(new CmdCharCallbacks());

  pCharBtn = pService->createCharacteristic(
      BLE_CHAR_BTN_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharBtn->addDescriptor(new BLE2902());

  pCharBat = pService->createCharacteristic(
      BLE_CHAR_BAT_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharBat->addDescriptor(new BLE2902());
  notifyBatteryTelemetry();

  pCharAuth = pService->createCharacteristic(
      BLE_CHAR_AUTH_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharAuth->addDescriptor(new BLE2902());
  pCharAuth->setCallbacks(new AuthCharCallbacks());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Servidor GATT nativo activo y anunciando como 'E-Slate NEBRALTA'");
}

// =========================================================================
// INTERFAZ WEB MÓVIL PROFESIONAL (CONTROL TOTAL DE LA CLAQUETA)
// =========================================================================
String htmlPage() {
  float vBat = getBatteryVoltage();
  int pctBat = getBatteryPercentage(vBat);

  String html = R"HTML(<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>E-Slate Cinema Pro</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: #0f0f11;
      color: #eee;
      padding: 12px;
      display: flex;
      justify-content: center;
      min-height: 100vh;
    }
    .container { max-width: 680px; width: 100%; margin: 0 auto; }
    .header { text-align: center; margin-bottom: 12px; }
    .header h1 { font-size: 1.6rem; letter-spacing: 2px; text-transform: uppercase; color: #fff; }
    .header p { font-size: 0.85rem; color: #8fc31f; font-weight: 700; }
    .card {
      background: #18181b;
      border: 1px solid #27272a;
      padding: 16px;
      border-radius: 12px;
      box-shadow: 0 8px 30px rgba(0,0,0,0.6);
      margin-bottom: 12px;
    }
    .quick-actions {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 8px;
      margin-bottom: 14px;
    }
    .btn-quick {
      background: #27272a;
      border: 1px solid #8fc31f;
      color: #8fc31f;
      padding: 10px 4px;
      border-radius: 8px;
      font-weight: 800;
      font-size: 0.85rem;
      cursor: pointer;
      text-transform: uppercase;
      text-align: center;
    }
    .btn-quick:active { background: #8fc31f; color: #000; }
    .btn-invert {
      border-color: #38bdf8;
      color: #38bdf8;
    }
    .form-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      margin-bottom: 14px;
    }
    .form-group { margin-bottom: 8px; }
    label {
      display: block;
      font-weight: 700;
      margin-bottom: 4px;
      color: #a1a1aa;
      font-size: 0.72rem;
      letter-spacing: 1px;
    }
    input, select {
      width: 100%;
      padding: 10px;
      border-radius: 6px;
      border: 1px solid #3f3f46;
      background: #27272a;
      color: #fff;
      font-size: 14px;
      font-weight: 600;
      text-transform: uppercase;
    }
    input:focus, select:focus {
      outline: none;
      border-color: #8fc31f;
      background: #323238;
    }
    .full-width { grid-column: 1 / -1; }
    .two-cols { grid-column: span 2; }
    
    .toggle-group {
      display: flex;
      gap: 6px;
    }
    .toggle-group button {
      flex: 1;
      padding: 9px;
      border: 1px solid #3f3f46;
      background: #27272a;
      color: #aaa;
      font-weight: 700;
      font-size: 0.8rem;
      border-radius: 6px;
      cursor: pointer;
    }
    .toggle-group button.active {
      background: #8fc31f;
      color: #000;
      border-color: #8fc31f;
    }
    
    .btn-submit {
      background: #8fc31f;
      color: #000;
      padding: 14px;
      border-radius: 8px;
      font-weight: 800;
      border: none;
      cursor: pointer;
      font-size: 1.05rem;
      letter-spacing: 1px;
      width: 100%;
      text-transform: uppercase;
      margin-top: 6px;
    }
    .btn-submit:hover { background: #9ee024; }

    /* Estilo del Acordeón de Configuración */
    details {
      background: #202024;
      border: 1px solid #3f3f46;
      border-radius: 8px;
      padding: 10px 14px;
      margin-bottom: 14px;
    }
    details summary {
      cursor: pointer;
      font-weight: 700;
      font-size: 0.85rem;
      color: #8fc31f;
      user-select: none;
    }
    .fields-grid {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 8px;
      margin-top: 10px;
    }
    .chk-label {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 0.8rem;
      color: #e4e4e7;
      text-transform: none;
      cursor: pointer;
    }
    .chk-label input[type="checkbox"] {
      width: 18px;
      height: 18px;
      cursor: pointer;
      accent-color: #8fc31f;
    }

    .status-bar {
      display: flex;
      justify-content: space-between;
      background: #27272a;
      border-radius: 6px;
      padding: 8px 12px;
      font-size: 0.78rem;
      color: #a1a1aa;
      margin-bottom: 12px;
    }
    .status-bar span { color: #fff; font-weight: bold; }
    .footer-links {
      text-align: center;
      margin-top: 14px;
      color: #71717a;
      font-size: 0.8rem;
    }
    .footer-links a { color: #8fc31f; text-decoration: none; font-weight: bold; }
    @media (max-width: 520px) {
      .form-grid { grid-template-columns: 1fr; }
      .quick-actions { grid-template-columns: 1fr 1fr; }
      .two-cols { grid-column: span 1; }
      .fields-grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🎬 E-Slate Cinema Pro</h1>
      <p>http://clap.local</p>
    </div>

    <!-- Barra de Estado de Energía y Diagnóstico -->
    <div class="status-bar">
      <div>🔋 Batería: <span>)HTML";
  html += String(pctBat) + "% (" + String(vBat, 2) + "V)";
  if (vBat >= 4.15f)
    html += " [USB / Completa]";
  html += R"HTML(</span></div>
      <div>Modo: <span>)HTML";
  html += invertedMode ? "⚫ Invertido (Negro)" : "⚪ Normal (Blanco)";
  html += R"HTML(</span></div>
    </div>
    
    <div class="card">
      <div class="quick-actions">
        <form method="POST" action="/inc-roll" style="display:contents;">
          <button class="btn-quick" type="submit">➕ ROLL (+1)</button>
        </form>
        <form method="POST" action="/inc-take" style="display:contents;">
          <button class="btn-quick" type="submit">➕ TAKE (+1)</button>
        </form>
        <form method="POST" action="/toggle-invert" style="display:contents;">
          <button class="btn-quick btn-invert" type="submit">🌓 )HTML";
  html += invertedMode ? "BLANCO" : "INVERTIR";
  html += R"HTML(</button>
        </form>
      </div>

      <form method="POST" action="/update" id="slateForm">
        <!-- Selector de Diseño / Layout -->
        <div class="form-group full-width" style="margin-bottom:12px;">
          <label for="layout">DISEÑO DE PANTALLA (LAYOUT)</label>
          <select id="layout" name="layout">
            <option value="0")HTML";
  if (layoutMode == 0)
    html += " selected";
  html += R"HTML(>🎬 Cinema Pro (Estándar 4 franjas)</option>
            <option value="1")HTML";
  if (layoutMode == 1)
    html += " selected";
  html += R"HTML(>🔍 Big Slate (Escena y Toma Gigantes)</option>
            <option value="2")HTML";
  if (layoutMode == 2)
    html += " selected";
  html += R"HTML(>📺 Commercial / Brand (Cabecera Destacada)</option>
          </select>
        </div>

        <!-- Campos Principales -->
        <div class="form-grid">
          <div class="form-group">
            <label for="roll">ROLL</label>
            <input type="text" id="roll" name="roll" value=")HTML";
  html += roll;
  html += R"HTML(" maxlength="10">
          </div>
          <div class="form-group">
            <label for="scene">SCENE</label>
            <input type="text" id="scene" name="scene" value=")HTML";
  html += scene;
  html += R"HTML(" maxlength="10">
          </div>
          <div class="form-group">
            <label for="take">TAKE</label>
            <input type="text" id="take" name="take" value=")HTML";
  html += take;
  html += R"HTML(" maxlength="10">
          </div>
          <div class="form-group full-width">
            <label for="prod">PRODUCCIÓN (PROD)</label>
            <div style="display:flex; gap:6px;">
              <input type="text" id="prod" name="prod" value=")HTML";
  html += prod;
  html += R"HTML(" maxlength="40">
              <button type="button" onclick="insertSakura()" style="background:#27272a;border:1px solid #ec4899;color:#fff;border-radius:6px;padding:0 12px;font-size:1.1rem;cursor:pointer;">🌸</button>
            </div>
          </div>
          <div class="form-group two-cols">
            <label for="director">DIRECTOR (DIR)</label>
            <input type="text" id="director" name="director" value=")HTML";
  html += director;
  html += R"HTML(" maxlength="35">
          </div>
          <div class="form-group">
            <label for="dop">FOTOGRAFÍA (DOP)</label>
            <input type="text" id="dop" name="dop" value=")HTML";
  html += dop;
  html += R"HTML(" maxlength="35">
          </div>
        </div>

        <!-- Metadatos de Cámara, Audio y Rodaje -->
        <div class="form-grid">
          <div class="form-group">
            <label for="cam">CÁMARA (CAM)</label>
            <input type="text" id="cam" name="cam" value=")HTML";
  html += cam;
  html += R"HTML(" maxlength="5">
          </div>
          <div class="form-group">
            <label>AUDIO</label>
            <div class="toggle-group">
              <input type="hidden" name="sound" id="soundInput" value=")HTML";
  html += sound;
  html += R"HTML(">
              <button type="button" id="btnSync" onclick="setToggle('soundInput','SYNC',this,'btnMos')">SYNC</button>
              <button type="button" id="btnMos" onclick="setToggle('soundInput','MOS',this,'btnSync')">MOS</button>
            </div>
          </div>
          <div class="form-group">
            <label>LOCACIÓN</label>
            <div class="toggle-group">
              <input type="hidden" name="loc" id="locInput" value=")HTML";
  html += loc;
  html += R"HTML(">
              <button type="button" id="btnInt" onclick="setToggle('locInput','INT',this,'btnExt')">INT</button>
              <button type="button" id="btnExt" onclick="setToggle('locInput','EXT',this,'btnInt')">EXT</button>
            </div>
          </div>
          <div class="form-group">
            <label>LUZ</label>
            <div class="toggle-group">
              <input type="hidden" name="light" id="lightInput" value=")HTML";
  html += light;
  html += R"HTML(">
              <button type="button" id="btnDay" onclick="setToggle('lightInput','DAY',this,'btnNight')">DAY</button>
              <button type="button" id="btnNight" onclick="setToggle('lightInput','NIGHT',this,'btnDay')">NIGHT</button>
            </div>
          </div>
          <div class="form-group two-cols">
            <label for="date">FECHA</label>
            <div style="display:flex; gap:6px;">
              <input type="text" id="date" name="date" value=")HTML";
  html += date;
  html += R"HTML(" maxlength="15">
              <button type="button" onclick="syncPhoneDate()" title="Sincronizar fecha de este móvil" style="background:#27272a;border:1px solid #8fc31f;color:#8fc31f;border-radius:6px;padding:0 10px;font-size:0.75rem;cursor:pointer;font-weight:bold;">📱 HOY</button>
            </div>
            <label style="margin-top:6px; font-size:0.72rem; color:#8fc31f; display:flex; align-items:center; gap:6px; cursor:pointer;">
              <input type="checkbox" id="autoSyncDate" checked style="width:auto; margin:0; cursor:pointer;">
              Auto-sincronizar con reloj de este dispositivo al actualizar
            </label>
          </div>
          <div class="form-group two-cols">
            <label for="timecode">TIMECODE (HH:MM:SS)</label>
            <div style="display:flex; gap:6px;">
              <input type="text" id="timecode" name="timecode" value=")HTML";
  html += timecode;
  html += R"HTML(" maxlength="8" pattern="[0-9]{2}:[0-9]{2}:[0-9]{2}">
              <button type="button" onclick="syncPhoneTime()" title="Sincronizar hora de este móvil" style="background:#27272a;border:1px solid #8fc31f;color:#8fc31f;border-radius:6px;padding:0 10px;font-size:0.75rem;cursor:pointer;font-weight:bold;">⏱️ AHORA</button>
            </div>
          </div>
          <div class="form-group two-cols">
            <label for="tc_slot">POSICIÓN TIMECODE</label>
            <select id="tc_slot" name="tc_slot">
              <option value="0")HTML";
  if (tcSlot == 0) html += " selected";
  html += R"HTML(>0: Cabecera Superior (Top)</option>
              <option value="1")HTML";
  if (tcSlot == 1) html += " selected";
  html += R"HTML(>1: Barra Inferior (Bottom)</option>
              <option value="2")HTML";
  if (tcSlot == 2) html += " selected";
  html += R"HTML(>2: Centro Destacado (Hero)</option>
            </select>
          </div>
          <div class="form-group two-cols">
            <label for="tc_size">TAMAÑO TIMECODE</label>
            <select id="tc_size" name="tc_size">
              <option value="0")HTML";
  if (tcSize == 0) html += " selected";
  html += R"HTML(>Mediano (24px / Size 3)</option>
              <option value="1")HTML";
  if (tcSize == 1) html += " selected";
  html += R"HTML(>Grande (40px / Size 5)</option>
              <option value="2")HTML";
  if (tcSize == 2) html += " selected";
  html += R"HTML(>Gigante XL (64px / Size 8)</option>
            </select>
          </div>
          <div class="form-group two-cols">
            <label for="tc_fps">FRAME RATE (FPS)</label>
            <select id="tc_fps" name="tc_fps">
              <option value="23.976")HTML";
  if (tcFps == "23.976") html += " selected";
  html += R"HTML(>23.976 fps</option>
              <option value="24")HTML";
  if (tcFps == "24") html += " selected";
  html += R"HTML(>24 fps</option>
              <option value="25")HTML";
  if (tcFps == "25") html += " selected";
  html += R"HTML(>25 fps</option>
              <option value="29.97")HTML";
  if (tcFps == "29.97") html += " selected";
  html += R"HTML(>29.97 fps</option>
              <option value="30")HTML";
  if (tcFps == "30") html += " selected";
  html += R"HTML(>30 fps</option>
            </select>
          </div>
          <div class="form-group full-width">
            <label for="note">NOTA / LENTE</label>
            <input type="text" id="note" name="note" value=")HTML";
  html += note;
  html += R"HTML(" maxlength="35">
          </div>
        </div>

        <!-- Acordeón para Activar / Ocultar Campos Específicos -->
        <details>
          <summary>⚙️ Configurar Campos Visibles en Pantalla</summary>
          <div class="fields-grid">
            <label class="chk-label"><input type="checkbox" name="show_roll" value="1")HTML";
  if (showRoll)
    html += " checked";
  html += R"HTML(> Mostrar ROLL</label>
            <label class="chk-label"><input type="checkbox" name="show_scene" value="1")HTML";
  if (showScene)
    html += " checked";
  html += R"HTML(> Mostrar SCENE</label>
            <label class="chk-label"><input type="checkbox" name="show_take" value="1")HTML";
  if (showTake)
    html += " checked";
  html += R"HTML(> Mostrar TAKE</label>
            <label class="chk-label"><input type="checkbox" name="show_prod" value="1")HTML";
  if (showProd)
    html += " checked";
  html += R"HTML(> Mostrar PRODUCCIÓN</label>
            <label class="chk-label"><input type="checkbox" name="show_dir" value="1")HTML";
  if (showDir)
    html += " checked";
  html += R"HTML(> Mostrar DIRECTOR</label>
            <label class="chk-label"><input type="checkbox" name="show_dop" value="1")HTML";
  if (showDop)
    html += " checked";
  html += R"HTML(> Mostrar FOTOGRAFÍA (DOP)</label>
            <label class="chk-label"><input type="checkbox" name="show_cam" value="1")HTML";
  if (showCam)
    html += " checked";
  html += R"HTML(> Mostrar CÁMARA (CAM)</label>
            <label class="chk-label"><input type="checkbox" name="show_sound" value="1")HTML";
  if (showSound)
    html += " checked";
  html += R"HTML(> Mostrar AUDIO (SYNC/MOS)</label>
            <label class="chk-label"><input type="checkbox" name="show_loc" value="1")HTML";
  if (showLoc)
    html += " checked";
  html += R"HTML(> Mostrar LOCACIÓN (INT/EXT)</label>
            <label class="chk-label"><input type="checkbox" name="show_light" value="1")HTML";
  if (showLight)
    html += " checked";
  html += R"HTML(> Mostrar LUZ (DAY/NIGHT)</label>
            <label class="chk-label"><input type="checkbox" name="show_date" value="1")HTML";
  if (showDate)
    html += " checked";
  html += R"HTML(> Mostrar FECHA</label>
            <label class="chk-label"><input type="checkbox" name="show_timecode" value="1")HTML";
  if (showTimecode)
    html += " checked";
  html += R"HTML(> Mostrar TIMECODE (1 Hz)</label>
            <label class="chk-label"><input type="checkbox" name="show_note" value="1")HTML";
  if (showNote)
    html += " checked";
  html += R"HTML(> Mostrar NOTA</label>
            <label class="chk-label"><input type="checkbox" name="show_bat" value="1")HTML";
  if (showBat)
    html += " checked";
  html += R"HTML(> Mostrar Indicador BATERÍA</label>
            <label class="chk-label"><input type="checkbox" name="inverted" value="1")HTML";
  if (invertedMode)
    html += " checked";
  html += R"HTML(> Modo Invertido (Dark Mode)</label>
          </div>
        </details>

        <button class="btn-submit" type="submit">Actualizar Claqueta</button>
      </form>
    </div>
    
    <div class="footer-links">
      Acceso web: <a href="http://clap.local">http://clap.local</a> | IP: <strong>)HTML";
  html += localIP.toString();
  html += R"HTML(</strong><br>
      <span style="font-size:0.75rem; color:#71717a;">
        Diagnóstico: Reinicio previo por: <strong>)HTML";
  html += resetReasonStr;
  html += R"HTML(</strong><br>
        Botones EE04: [KEY0] = Modo Invertido | [KEY1] = +1 TAKE | [KEY2] = +1 ROLL
      </span>
    </div>
  </div>

  <script>
    function setToggle(inputId, val, btnActive, btnOtherId) {
      document.getElementById(inputId).value = val;
      btnActive.classList.add('active');
      document.getElementById(btnOtherId).classList.remove('active');
    }

    function insertSakura() {
      const p = document.getElementById('prod');
      p.value = p.value ? p.value + ' 🌸' : '🌸';
    }

    function syncPhoneDate() {
      const now = new Date();
      const dd = String(now.getDate()).padStart(2, '0');
      const mm = String(now.getMonth() + 1).padStart(2, '0');
      const yyyy = now.getFullYear();
      const d = document.getElementById('date');
      if (d) d.value = `${dd}.${mm}.${yyyy}`;
    }

    function syncPhoneTime() {
      const now = new Date();
      const hh = String(now.getHours()).padStart(2, '0');
      const mm = String(now.getMinutes()).padStart(2, '0');
      const ss = String(now.getSeconds()).padStart(2, '0');
      const tc = document.getElementById('timecode');
      if (tc) tc.value = `${hh}:${mm}:${ss}`;
    }

    document.addEventListener('DOMContentLoaded', function() {
      const autoSync = document.getElementById('autoSyncDate');
      if (autoSync && autoSync.checked) {
        syncPhoneDate();
      }

      const form = document.getElementById('slateForm');
      if (form) {
        form.addEventListener('submit', function() {
          const autoSync = document.getElementById('autoSyncDate');
          if (autoSync && autoSync.checked) {
            syncPhoneDate();
          }
        });
      }

      const s = document.getElementById('soundInput').value;
      if (s === 'MOS') document.getElementById('btnMos').classList.add('active');
      else document.getElementById('btnSync').classList.add('active');

      const l = document.getElementById('locInput').value;
      if (l === 'EXT') document.getElementById('btnExt').classList.add('active');
      else document.getElementById('btnInt').classList.add('active');

      const lt = document.getElementById('lightInput').value;
      if (lt === 'NIGHT') document.getElementById('btnNight').classList.add('active');
      else document.getElementById('btnDay').classList.add('active');
    });
  </script>
</body>
</html>)HTML";

  return html;
}

void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleUpdate() {
  if (server.hasArg("roll"))
    roll = server.arg("roll");
  if (server.hasArg("scene"))
    scene = server.arg("scene");
  if (server.hasArg("take"))
    take = server.arg("take");
  if (server.hasArg("prod"))
    prod = server.arg("prod");
  if (server.hasArg("director"))
    director = server.arg("director");
  if (server.hasArg("dop"))
    dop = server.arg("dop");
  if (server.hasArg("note"))
    note = server.arg("note");
  if (server.hasArg("date"))
    date = server.arg("date");
  if (server.hasArg("timecode"))
    timecode = server.arg("timecode");
  if (server.hasArg("cam"))
    cam = server.arg("cam");
  if (server.hasArg("sound"))
    sound = server.arg("sound");
  if (server.hasArg("loc"))
    loc = server.arg("loc");
  if (server.hasArg("light"))
    light = server.arg("light");

  if (server.hasArg("layout"))
    layoutMode = server.arg("layout").toInt();

  if (server.hasArg("tc_slot"))
    tcSlot = constrain(server.arg("tc_slot").toInt(), 0, 2);
  if (server.hasArg("tc_size"))
    tcSize = constrain(server.arg("tc_size").toInt(), 0, 2);
  if (server.hasArg("tc_fps"))
    tcFps = server.arg("tc_fps");
  if (server.hasArg("tc_offset"))
    tcOffset = server.arg("tc_offset").toInt();
  if (server.hasArg("tc_running"))
    timecodeRunning = (server.arg("tc_running") == "1" || server.arg("tc_running") == "true");

  // Visibilidad de campos
  showRoll = server.hasArg("show_roll");
  showScene = server.hasArg("show_scene");
  showTake = server.hasArg("show_take");
  showProd = server.hasArg("show_prod");
  showDir = server.hasArg("show_dir");
  showDop = server.hasArg("show_dop");
  showCam = server.hasArg("show_cam");
  showSound = server.hasArg("show_sound");
  showLoc = server.hasArg("show_loc");
  showLight = server.hasArg("show_light");
  showDate = server.hasArg("show_date");
  showTimecode = server.hasArg("show_timecode");
  showNote = server.hasArg("show_note");
  showBat = server.hasArg("show_bat");

  // Modo invertido
  invertedMode = server.hasArg("inverted");

  // Forzar mayúsculas sostenidas
  roll.toUpperCase();
  scene.toUpperCase();
  take.toUpperCase();
  prod.toUpperCase();
  director.toUpperCase();
  dop.toUpperCase();
  note.toUpperCase();
  date.toUpperCase();
  timecode.toUpperCase();
  cam.toUpperCase();
  sound.toUpperCase();
  loc.toUpperCase();
  light.toUpperCase();

  // Reset de contador de refrescos parciales ante cambio de estado
  tcPartialRefreshCount = 0;

  // Salir de la pantalla de bienvenida si estaba activa
  inBootScreen = false;

  // Responder de inmediato al cliente HTTP para evitar bloqueos del navegador
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Actualizado. Redirigiendo...");

  // Marcar para refresco asíncrono
  needDisplayUpdate = true;
  bleBroadcastState();
  notifyBatteryTelemetry();
}

void handleIncTake() {
  inBootScreen = false;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Incrementando TAKE...");
  incrementTake();
}

void handleIncRoll() {
  inBootScreen = false;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Incrementando ROLL...");
  incrementRoll();
}

void handleToggleInvert() {
  inBootScreen = false;
  invertedMode = !invertedMode;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Modo invertido cambiado...");
  tcPartialRefreshCount = 0;
  needDisplayUpdate = true;
  notifyButtonEvent(0x00);
  bleBroadcastState();
}

// Lectura de los 3 pulsadores físicos con debounce e interrupciones
void checkButtons() {
  static unsigned long lastActionTime = 0;
  unsigned long now = millis();

  bool k0 = (digitalRead(PIN_BTN_KEY0) == LOW) || isrKey0Triggered;
  bool k1 = (digitalRead(PIN_BTN_KEY1) == LOW) || isrKey1Triggered;
  bool k2 = (digitalRead(PIN_BTN_KEY2) == LOW) || isrKey2Triggered;

  // Cooldown de 1.5 segundos para evitar rebotes y permitir que el e-paper termine
  if (now - lastActionTime < 1500)
    return;

  if (!k0 && !k1 && !k2)
    return;

  isrKey0Triggered = false;
  isrKey1Triggered = false;
  isrKey2Triggered = false;
  lastActionTime = now;
  tcPartialRefreshCount = 0;

  // Si el modal de PIN estaba activo, cualquier botón del operador lo descarta
  if (pinDisplayActive) {
    Serial.println("[BOTON] Interaccion del operador -> Descartando modal PIN");
    pinDisplayActive = false;
    needDisplayUpdate = true;
  }

  // REGLA CLAVE: Al prender, si la imagen está fija en las instrucciones de
  // uso, al presionar CUALQUIER botón se va a la claqueta
  if (inBootScreen) {
    Serial.println(
        "[BOTON] Saliendo de pantalla de instrucciones -> A la claqueta");
    inBootScreen = false;
    needDisplayUpdate = true;
    return;
  }

  // Operación normal de rodaje:
  if (k0) {
    // KEY0 (GPIO 2): Conmutar Modo Invertido (Fondo Blanco <-> Fondo Negro)
    invertedMode = !invertedMode;
    Serial.printf("[BOTON KEY0] Modo Invertido: %s\n",
                  invertedMode ? "ACTIVO (Negro)" : "NORMAL (Blanco)");
    needDisplayUpdate = true;
    notifyButtonEvent(0x00);
    bleBroadcastState();
    return;
  }

  if (k1) {
    // KEY1 (GPIO 3): +1 TAKE
    incrementTake();
    return;
  }

  if (k2) {
    // KEY2 (GPIO 5): +1 ROLL
    incrementRoll();
    return;
  }
}

// =========================================================================
// SETUP Y LOOP
// =========================================================================

void setup() {
  // Deshabilitar detector de brownout para prevenir reinicios por caídas
  // transitorias de tensión
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  setCpuFrequencyMhz(160); // Reducir frecuencia a 160MHz para disminuir picos de corriente

  Serial.begin(115200);

  // Configurar LED integrado y ADC de batería para la placa EE04
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED,
               LOW); // Encender brevemente durante el arranque (Active LOW)
  pinMode(PIN_ADC_EN, OUTPUT);
  digitalWrite(PIN_ADC_EN, HIGH); // Habilitar divisor de batería
  pinMode(PIN_BAT_ADC, INPUT);
  analogReadResolution(12);

  // Configurar los 3 botones físicos con pull-up interno e interrupciones
  pinMode(PIN_BTN_KEY0, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY1, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_KEY0), isrKey0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_KEY1), isrKey1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_KEY2), isrKey2, FALLING);

  // Espera breve para Serial JTAG / USB CDC
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 1500))
    ;

  resetReasonStr = getResetReasonString();
  float bootV = getBatteryVoltage();
  int bootPct = getBatteryPercentage(bootV);
  Serial.println("\n==============================================");
  Serial.println("  E-Slate Cinema Pro (Seeed_GFX2)             ");
  Serial.printf("  Reinicio previo: %s\n", resetReasonStr.c_str());
  Serial.printf("  Bateria detectada: %.2f V (%d%%)\n", bootV, bootPct);
  Serial.println("==============================================");

  // Inicializar controlador gráfico Seeed_GFX2
  if (!display.begin()) {
    Serial.printf("Error Seeed_GFX2: %s\n", display.lastResult().message);
  } else {
    Serial.println("Seeed_GFX2 iniciado correctamente.");
  }

  // Ajustar potencia Wi-Fi a 15dBm para reducir picos de consumo
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.softAP(ssid, password);
  localIP = WiFi.softAPIP();

  Serial.printf("Wi-Fi SoftAP: %s (Password: %s)\n", ssid, password);
  Serial.printf("IP del Servidor: %s\n", localIP.toString().c_str());

  // Iniciar servicio mDNS
  if (MDNS.begin("clap")) {
    Serial.println("mDNS activo: http://clap.local");
    MDNS.addService("http", "tcp", 80);
  }

  // Rutas HTTP
  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/inc-take", HTTP_POST, handleIncTake);
  server.on("/inc-roll", HTTP_POST, handleIncRoll);
  server.on("/toggle-invert", HTTP_POST, handleToggleInvert);
  server.begin();
  Serial.println("Servidor Web HTTP activo.");

  // Inicializar servidor BLE GATT (NimBLE)
  initBLE();

  // Mostrar pantalla fija de instrucciones
  showIPOnBoot();
  // NOTA: No llamamos a updateDisplay() para que las instrucciones permanezcan
  // fijas en el panel e-paper hasta que se presione cualquier botón.
}

void loop() {
  server.handleClient();
  checkButtons();
  updateLED();

  unsigned long now = millis();

  // Expiración por timeout de 30s del modal de PIN
  if (pinDisplayActive && (now >= pinDisplayExpiry)) {
    pinDisplayActive = false;
    tcPartialRefreshCount = 0;
    needDisplayUpdate = true;
    Serial.println("[BLE Sec] Timeout de modal PIN (30s). Restaurando claqueta.");
  }

  // Renderizar modal de PIN superpuesto
  if (needPinDisplayRender) {
    needPinDisplayRender = false;
    renderPinModal();
  }

  // Refresco asíncrono seguro
  if (needDisplayUpdate) {
    needDisplayUpdate = false;
    tcPartialRefreshCount = 0;
    lastTcMillis = now;
    delay(40); // Permitir que los paquetes TCP/HTTP se envíen
    updateDisplay();
    return;
  }

  // Motor de Timecode 1 Hz & Programador de refresco parcial UC8179
  if (timecodeRunning && (now - lastTcMillis >= 1000)) {
    lastTcMillis = now;
    incrementTimecodeOneSecond();
    if (!pinDisplayActive && !inBootScreen && showTimecode) {
      updateTimecodePartial();
    }
  }

  yield();
}
