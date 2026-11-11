/*
 * ESP32 CO2 Monitor
 * ------------------------------------------------------------------
 * An air-quality monitor that shows the CO2 level of a room on a small
 * OLED display and on an LED ring/strip, and (optionally) logs the
 * measurements to IoTPlotter so you can follow them online.
 *
 * What it does
 *   - Measures CO2 (ppm), temperature and humidity every 5 seconds.
 *   - OLED: big CO2 value, temperature, humidity and a graph of the
 *     last hour (the dotted line marks 1000 ppm).
 *   - LEDs change colour with the CO2 level:
 *       dark green -> light green -> orange -> red,
 *       and a red "Knight Rider" animation when the air is really bad.
 *   - Optional: sends the values to IoTPlotter once a minute over WiFi.
 *
 * Hardware
 *   - ESP32 development board (selected as "ESP32 Dev Module")
 *   - Sensirion SCD41 CO2 sensor (I2C)
 *   - 1.3" 128x64 OLED, I2C (usually an SH1106, sometimes an SSD1306)
 *   - WS2812B LEDs (18 pieces in this build)
 *
 * Wiring (change the pin numbers below if you wire it differently)
 *   - SCD41 and OLED, I2C: SDA = GPIO 21, SCL = GPIO 22
 *   - WS2812B data in: GPIO 18
 *   - Power the LEDs from 5 V and connect all grounds together
 *
 * Libraries (Arduino IDE > Library Manager)
 *   - U8g2 (olikraus)
 *   - FastLED
 *   - Sensirion I2C SCD4x (Sensirion)
 *   Board package: "esp32" by Espressif (Boards Manager)
 *
 * Setting up IoTPlotter logging (optional)
 *   1. Create a free account at https://iotplotter.com and create a feed.
 *   2. Open your feed and go to "Manage feed". There you find the
 *      API key. The Feed ID is the number of your feed.
 *   3. Fill in the four values in the SETTINGS block below
 *      (WiFi name, WiFi password, Feed ID and API key).
 *   4. Upload the sketch and open the Serial Monitor at 115200 baud.
 *      After about a minute it should print "IoTPlotter: HTTP 200".
 *      The graphs "co2", "temperature" and "humidity" then appear in
 *      your feed automatically.
 *   Don't want logging? Set ENABLE_IOTPLOTTER to 0 below.
 *
 * Note: the ESP32 only supports 2.4 GHz WiFi (not 5 GHz or 6 GHz), and
 * it cannot log in on networks that need a login page or a
 * username/password (WPA2-Enterprise). A phone hotspot set to 2.4 GHz
 * works fine.
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <FastLED.h>
#include <SensirionI2cScd4x.h>

// ==================== SETTINGS ====================

// 1 = log to IoTPlotter over WiFi, 0 = work without WiFi
#define ENABLE_IOTPLOTTER 1

#if ENABLE_IOTPLOTTER
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// Change this to your WiFi name (2.4 GHz network)
const char* WIFI_SSID    = "YOUR_WIFI_NAME";
// Change this to your WiFi password
const char* WIFI_PASS    = "YOUR_WIFI_PASSWORD";
// Create an account on iotplotter.com and make a feed. Your feed number:
const char* IOTP_FEED_ID = "YOUR_FEED_ID";
// Find the API key on iotplotter.com under "Manage feed". Keep it private!
const char* IOTP_API_KEY = "YOUR_API_KEY";

#define IOTP_INTERVAL_MS 60000UL   // send a measurement every 60 s

uint32_t lastUpload = 0;
#endif

// Pins
#define SDA_PIN    21       // I2C data (SCD41 + OLED)
#define SCL_PIN    22       // I2C clock (SCD41 + OLED)
#define LED_PIN    18       // WS2812B data
#define NUM_LEDS   18       // number of LEDs

// LED power. The ESP32 is powered from USB, so keep the total current low.
#define MAX_MA     300      // current limit for the LEDs (safe for a USB port)
#define BRIGHTNESS 120      // 0-255. If the ESP32 restarts by itself, lower
                            // this to about 100 and MAX_MA to about 250.

// Temperature offset of the SCD41. A HIGHER number gives a LOWER temperature.
// The sensor reads too warm because of heat from the board and the LEDs.
// Compare with a reference thermometer and adjust until it matches.
#define TEMP_OFFSET 7.0f

// CO2 thresholds in ppm (LED colours)
#define LIMIT_1 700         // below 700: dark green
#define LIMIT_2 1000        // below 1000: light green
#define LIMIT_3 1200        // below 1200: orange
#define LIMIT_4 1500        // below 1500: red, above that: Knight Rider

// LED colours (feel free to change them)
const CRGB COLOR_DARK_GREEN  = CRGB(0, 90, 0);
const CRGB COLOR_LIGHT_GREEN = CRGB(40, 255, 0);
const CRGB COLOR_ORANGE      = CRGB(255, 100, 0);
const CRGB COLOR_RED         = CRGB(255, 0, 0);

// Graph of the last hour (fixed scale)
#define HIST_SIZE        123        // number of stored points
#define HIST_INTERVAL_MS 29000UL    // each point = average over 29 s (123 x 29 s = about 1 hour)
#define GRAPH_TOP        25         // pixel row for GRAPH_MAX_PPM
#define GRAPH_BOTTOM     60         // pixel row for GRAPH_MIN_PPM
#define GRAPH_X_RIGHT    125        // right-most column (= now)
#define GRAPH_MIN_PPM    500
#define GRAPH_MAX_PPM    1500
#define GRAPH_REF_PPM    1000       // dotted reference line

// ==================================================

// A 1.3" OLED is usually an SH1106. Does the picture look shifted by 2 pixels
// or show noise at the edge? Then it is an SSD1306: swap to the line below.
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

SensirionI2cScd4x scd4x;
CRGB leds[NUM_LEDS];

uint16_t co2 = 0;
float temperature = 0, humidity = 0;
bool haveData = false;           // becomes true after the first measurement

uint16_t hist[HIST_SIZE];        // history for the graph
int histLen = 0;
uint32_t accSum = 0;             // running sum for the 29 s average
uint16_t accCount = 0;

uint32_t lastPoll = 0;
uint32_t lastAnim = 0;
uint32_t lastHist = 0;
uint32_t lastSplash = 0;

// All LEDs in one colour, depending on the CO2 value (up to LIMIT_4)
void showStaticColor(uint16_t ppm) {
  CRGB color;
  if (ppm < LIMIT_1)      color = COLOR_DARK_GREEN;
  else if (ppm < LIMIT_2) color = COLOR_LIGHT_GREEN;
  else if (ppm < LIMIT_3) color = COLOR_ORANGE;
  else                    color = COLOR_RED;

  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

// One step of the red Knight Rider animation (above LIMIT_4)
void knightRiderStep() {
  static int pos = 0;
  static int dir = 1;

  fadeToBlackBy(leds, NUM_LEDS, 80);   // let the tail fade out
  leds[pos] = COLOR_RED;
  FastLED.show();

  pos += dir;
  if (pos >= NUM_LEDS - 1) dir = -1;
  else if (pos <= 0)       dir = 1;
}

// Add a value to the history; the oldest point drops off when it is full
void pushHistory(uint16_t value) {
  if (histLen < HIST_SIZE) {
    hist[histLen++] = value;
  } else {
    memmove(hist, hist + 1, (HIST_SIZE - 1) * sizeof(uint16_t));
    hist[HIST_SIZE - 1] = value;
  }
}

// Convert a ppm value to a pixel row on the display
int ppmToY(uint16_t ppm) {
  int p = constrain((int)ppm, GRAPH_MIN_PPM, GRAPH_MAX_PPM);
  return GRAPH_BOTTOM - (int)((long)(p - GRAPH_MIN_PPM) * (GRAPH_BOTTOM - GRAPH_TOP)
                              / (GRAPH_MAX_PPM - GRAPH_MIN_PPM));
}

// Draw the graph of the last hour
void drawGraph() {
  // Baseline
  display.drawHLine(2, GRAPH_BOTTOM + 1, GRAPH_X_RIGHT - 1);

  // Dotted line at 1000 ppm with a small label
  int yRef = ppmToY(GRAPH_REF_PPM);
  for (int x = 2; x <= GRAPH_X_RIGHT; x += 4) display.drawHLine(x, yRef, 2);
  display.setFont(u8g2_font_4x6_tr);
  display.drawStr(3, yRef - 2, "1000");

  // Line: stored points + the current value as the last point (right = now)
  int n = histLen + 1;
  int prevX = 0, prevY = 0;
  for (int i = 0; i < n; i++) {
    uint16_t v = (i < histLen) ? hist[i] : co2;
    int x = GRAPH_X_RIGHT - (n - 1 - i);
    int y = ppmToY(v);
    if (i > 0) display.drawLine(prevX, prevY, x, y);
    prevX = x;
    prevY = y;
  }
  display.drawDisc(prevX, prevY, 1);   // dot on the current value
}

// Start-up screen: a cloud with "CO2", an air stream and a progress bar
// (about 6 s, or until the first measurement arrives)
void showSplash() {
  uint32_t p = millis() / 60;
  if (p > 100) p = 100;
  int progress = (int)p;

  display.clearBuffer();

  // Cloud
  display.setDrawColor(1);
  display.drawDisc(33, 35, 10);
  display.drawDisc(49, 27, 14);
  display.drawDisc(70, 30, 12);
  display.drawDisc(81, 37, 8);
  display.drawBox(33, 30, 48, 15);

  // "CO" with a small "2" inside the cloud (black on white)
  display.setDrawColor(0);
  display.setFont(u8g2_font_helvB14_tr);
  int wCO = display.getStrWidth("CO");
  display.setFont(u8g2_font_helvB08_tr);
  int w2 = display.getStrWidth("2");
  int x0 = 56 - (wCO + 1 + w2) / 2;
  display.setFont(u8g2_font_helvB14_tr);
  display.drawStr(x0, 40, "CO");
  display.setFont(u8g2_font_helvB08_tr);
  display.drawStr(x0 + wCO + 1, 43, "2");
  display.setDrawColor(1);

  // Air stream to the right of the cloud: three lines with a curl (2 pixels thick)
  // Top line, curl upwards
  display.drawHLine(97, 17, 14);
  display.drawHLine(97, 18, 14);
  display.drawCircle(110, 14, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  display.drawCircle(110, 14, 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  // Middle line, curl upwards
  display.drawHLine(97, 28, 21);
  display.drawHLine(97, 29, 21);
  display.drawCircle(117, 25, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  display.drawCircle(117, 25, 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  // Bottom line, curl downwards
  display.drawHLine(97, 40, 12);
  display.drawHLine(97, 41, 12);
  display.drawCircle(108, 44, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  display.drawCircle(108, 44, 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);

  // Progress bar
  display.drawFrame(14, 55, 100, 7);
  display.drawBox(16, 57, progress * 96 / 100, 3);

  display.sendBuffer();
}

#if ENABLE_IOTPLOTTER
// Send CO2, temperature and humidity to IoTPlotter.
// Skips quietly (and prints a message) when there is no WiFi or no time yet.
void sendToIoTPlotter() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("IoTPlotter: no WiFi");
    return;
  }
  time_t t = time(nullptr);
  if (t < 1700000000) {                 // clock not synchronised yet (NTP)
    Serial.println("IoTPlotter: waiting for time (NTP)");
    return;
  }

  char url[96];
  snprintf(url, sizeof(url), "http://iotplotter.com/api/v2/feed/%s", IOTP_FEED_ID);

  // The graph names (co2, temperature, humidity) are created automatically
  char body[256];
  snprintf(body, sizeof(body),
    "{\"data\":{"
    "\"co2\":[{\"value\":%u,\"epoch\":%ld}],"
    "\"temperature\":[{\"value\":%.1f,\"epoch\":%ld}],"
    "\"humidity\":[{\"value\":%.0f,\"epoch\":%ld}]}}",
    co2, (long)t, temperature, (long)t, humidity, (long)t);

  HTTPClient http;
  http.setTimeout(4000);                // give up after 4 s so the display keeps running
  http.begin(url);
  http.addHeader("api-key", IOTP_API_KEY);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.printf("IoTPlotter: HTTP %d\n", code);   // 200 = OK
  http.end();
}
#endif

// Redraw the OLED: splash screen until the first measurement, then the values + graph
void showDisplay() {
  display.clearBuffer();

  if (!haveData) {
    showSplash();
    return;
  }

  char buf[16];

  // Top left: CO2 value + ppm
  display.setFont(u8g2_font_logisoso16_tn);
  snprintf(buf, sizeof(buf), "%u", co2);
  display.drawStr(3, 17, buf);
  int w = display.getStrWidth(buf);
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(3 + w + 4, 17, "ppm");

  // Top right: temperature and humidity
  snprintf(buf, sizeof(buf), "%.1fC", temperature);
  display.drawStr(125 - display.getStrWidth(buf), 9, buf);
  snprintf(buf, sizeof(buf), "%.0f%%", humidity);
  display.drawStr(125 - display.getStrWidth(buf), 18, buf);

  drawGraph();
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  display.begin();
  showDisplay();

#if ENABLE_IOTPLOTTER
  // Connect to WiFi in the background and set the clock via NTP.
  // The monitor keeps working if the connection fails.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
#endif

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MA);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  scd4x.begin(Wire, SCD41_I2C_ADDR_62);
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.setTemperatureOffset(TEMP_OFFSET);
  uint16_t err = scd4x.startPeriodicMeasurement();
  if (err) Serial.printf("SCD41 start error: %u\n", err);
}

void loop() {
  uint32_t now = millis();

  // Start-up screen with progress bar until the first measurement arrives
  if (!haveData && now - lastSplash >= 250) {
    lastSplash = now;
    showSplash();
  }

  // Check the sensor for new data every second (new data arrives every 5 s)
  if (now - lastPoll >= 1000) {
    lastPoll = now;
    Wire.setClock(100000);   // the SCD41 supports 100 kHz at most
    bool ready = false;
    if (scd4x.getDataReadyStatus(ready) == 0 && ready) {
      uint16_t c; float t, h;
      if (scd4x.readMeasurement(c, t, h) == 0 && c != 0) {
        co2 = c; temperature = t; humidity = h;
        if (!haveData) {
          haveData = true;
          lastHist = now;
        }
        accSum += co2;
        accCount++;
        Serial.printf("CO2 %u ppm, %.1f C, %.0f %%RH\n", co2, temperature, humidity);
        if (co2 <= LIMIT_4) showStaticColor(co2);
        showDisplay();
      }
    }
  }

  // Every 29 seconds store the average for the graph
  if (haveData && now - lastHist >= HIST_INTERVAL_MS) {
    lastHist = now;
    if (accCount > 0) {
      pushHistory(accSum / accCount);
      accSum = 0;
      accCount = 0;
    }
  }

#if ENABLE_IOTPLOTTER
  // Send the latest measurement to IoTPlotter once a minute
  if (haveData && now - lastUpload >= IOTP_INTERVAL_MS) {
    lastUpload = now;
    sendToIoTPlotter();
  }
#endif

  // Knight Rider animation above LIMIT_4 (one step every 40 ms)
  if (haveData && co2 > LIMIT_4 && now - lastAnim >= 40) {
    lastAnim = now;
    knightRiderStep();
  }
}
