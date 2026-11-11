# 🌬️ CO₂ Monitor (ESP32 + SCD41 + OLED + LED ring)

Firmware for a desk air-quality monitor that shows the CO₂ level of your room on a small OLED and on a glowing LED ring.

🖨️ **3D model, print settings, parts list, wiring and assembly:** see the MakerWorld page: **[ADD YOUR MAKERWORLD LINK HERE]**

## 🧠 What it does

- 💨 Measures **CO₂ (ppm), temperature and humidity** with a Sensirion SCD41.
- 📺 **1.3" OLED** shows the current values and a **graph of the last hour** with a reference line at 1000 ppm.
- 🚦 **LED ring** shows the air quality in colour (see below).
- 📶 **Optional:** log your measurements to a free [IoTPlotter](https://iotplotter.com) feed once a minute over WiFi. Set one line to 0 and the monitor works completely without WiFi.

## 🚦 LED colours

| CO₂ (ppm) | LED ring |
|---|---|
| below 700 | 🟢 dark green |
| 700 to 1000 | 🟢 light green |
| 1000 to 1200 | 🟠 orange |
| 1200 to 1500 | 🔴 red |
| above 1500 | 🚨 red Knight Rider sweep |

## 🔌 Pins

| Module | Pin | ESP32 |
|---|---|---|
| SCD41 and OLED (shared I²C bus) | SDA | GPIO 21 |
| SCD41 and OLED (shared I²C bus) | SCL | GPIO 22 |
| SCD41 and OLED | VCC / GND | 3V3 / GND |
| LED strip (WS2812B, 18 LEDs) | DIN | GPIO 18 |
| LED strip | 5 V / GND | VIN / GND |

Connect all grounds together. Pin numbers can be changed at the top of the sketch.

## 💻 Firmware

1. Install **Arduino IDE** and the **esp32 by Espressif Systems** board package (Boards Manager).
2. Install the libraries **U8g2**, **FastLED** and **Sensirion I2C SCD4x** (Library Manager).
3. Download this repository (green **Code** button, then **Download ZIP**) and open `co2_monitor_makerworld/co2_monitor_makerworld.ino`.
4. Select **ESP32 Dev Module** and your port, then upload.
5. **With or without WiFi logging:** near the top you find `#define ENABLE_IOTPLOTTER 1`. Change it to `0` if you don't want WiFi. Left at 1 without WiFi details, the monitor still works but can't upload.

**Settings at the top of the sketch**

- `ENABLE_IOTPLOTTER`: 1 = log to IoTPlotter, 0 = no WiFi
- `LIMIT_1` to `LIMIT_4`: colour thresholds in ppm
- `BRIGHTNESS` and the `COLOR_...` values: LED brightness and colours
- `MAX_MA`: current limit for the LEDs
- `NUM_LEDS`: number of LEDs in your strip
- `TEMP_OFFSET`: SCD41 temperature offset (a higher value gives a lower reading). Tune it after an hour in the closed case against a reference thermometer. Mine is 7.

💡 **ESP32 restarts by itself?** Lower `BRIGHTNESS` to about 100 and `MAX_MA` to about 250.

💡 **OLED shows a shifted or noisy picture?** Your display is an SSD1306 instead of an SH1106. Swap the two display lines in the sketch.

## 📶 Logging to IoTPlotter (optional)

1. Create a free account at [iotplotter.com](https://iotplotter.com) and create a feed.
2. Under **Manage feed** you find the **API key**. The **Feed ID** is the number of your feed.
3. Fill in your WiFi name, WiFi password, Feed ID and API key in the SETTINGS block of the sketch.
4. Upload and open the Serial Monitor at 115200 baud. After about a minute it should print `IoTPlotter: HTTP 200`. The graphs "co2", "temperature" and "humidity" appear in your feed automatically.

⚠️ **Good to know**

- The ESP32 only supports **2.4 GHz** WiFi, and no networks with a login page or username/password (WPA2-Enterprise). A 2.4 GHz phone hotspot works fine.
- Data is sent over plain HTTP, so the API key is not encrypted. Use a feed just for this project and don't reuse a password.
- Never publish your WiFi password or API key. Remove them from the sketch before sharing your copy.
- While uploading (once a minute), the display and LEDs can pause briefly on a slow connection.

## ℹ️ Good to know

- The SCD41 is accurate to about 40 ppm + 5% of the reading. Outside air should read roughly 420 to 450 ppm.
- The sensor self-calibrates but assumes it sees fresh air (about 400 ppm) regularly. Ventilate well at least once a week.
- The graph is kept in memory only, so it starts empty after a restart.
- This is a comfort indicator, not a safety device.
