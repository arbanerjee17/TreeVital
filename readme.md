# TreeVital

**A low-cost Tree/Plant Vitality Monitoring System built on the ESP32-C6.**

TreeVital goes beyond a simple sensor dashboard. Instead of just showing raw soil moisture, light, temperature and humidity readings, it interprets them into a small set of easy-to-read scores — an overall **Tree Vitality Index**, a **Thirst Index**, a **Stress Index**, a **Trend**, and a plain-language **Status** — displayed locally on an OLED and pushed live to a Blynk dashboard.
<img width="1634" height="822" alt="Screenshot (129)" src="https://github.com/user-attachments/assets/bb6de87c-64f4-497e-8815-57964b092ef0" />

---

## Core Outputs

| Output | What it means | Range |
|---|---|---|
| **Tree Vitality Index (TVI)** | Overall estimated condition of the plant | 0–100 (higher = healthier) |
| **Thirst Index** | Water deficit, combining current moisture level *and* how fast it's drying out | 0–100 (higher = thirstier) |
| **Stress Index** | Combined environmental stress from soil, light, temperature, and humidity | 0–100 (higher = more stressed) |
| **Trend** | Whether vitality is improving, stable, or deteriorating over the recent history window | Learning / Stable / Improving / Deteriorating |
| **Overall Status** | Simple health classification | Healthy / Partially Healthy / Unhealthy / Critical |
| **Dominant Factor** | Which single stressor is currently doing the most damage (printed to Serial) | e.g. "Water deficit", "Poor light" |

Soil moisture uses a **learned baseline** (captured at boot, so the sensor must already be inserted in moist soil when the board powers on). Light, temperature, and humidity instead use **configurable acceptable ranges**, since those naturally swing throughout the day and a single baseline snapshot wouldn't make sense for them.

---

## Hardware

| Component | Role |
|---|---|
| ESP32-C6 (any DevKit) | Main controller, Wi-Fi, runs all logic |
| FC-28 soil moisture sensor | Soil moisture (analog) |
| Photoresistor (LDR) + 10kΩ resistor | Ambient light (analog, via voltage divider) |
| DHT11 | Temperature + humidity |
| SSD1306 OLED (I2C, 128x64) | Local live display of all scores |
| Green LED + 220Ω resistor | Lit when status is Healthy |
| Red LED + 220Ω resistor | Lit when status is Unhealthy or Critical |
| Buzzer | Audible alert — beeps faster as condition worsens |

---

## Wiring / Connections

All grounds are shared on a common GND rail. All sensors and the OLED run on 3.3V.

| Component | Pin | ESP32-C6 GPIO |
|---|---|---|
| FC-28 — AO | Analog in | GPIO2 |
| FC-28 — VCC / GND | Power | 3V3 / GND |
| LDR divider — node (between LDR and 10kΩ resistor) | Analog in | GPIO3 |
| LDR — one leg | Power | 3V3 |
| 10kΩ resistor — one leg | Ground | GND |
| OLED — SDA | I2C | GPIO22 |
| OLED — SCL | I2C | GPIO23 |
| OLED — VCC / GND | Power | 3V3 / GND |
| Green LED (through 220Ω) | Digital out | GPIO18 |
| Red LED (through 220Ω) | Digital out | GPIO19 |
| Buzzer + | Digital out | GPIO20 |
| DHT11 — DATA (with 10kΩ pull-up to 3V3) | Digital | GPIO21 |
| DHT11 — VCC / GND | Power | 3V3 / GND |

**LDR voltage divider:**
```
3V3 ──[ LDR ]──●──[ 10kΩ ]── GND
                │
              GPIO3
```

---

## Libraries Required

Install via Arduino IDE Library Manager:

- **Blynk** by Volodymyr Shymanskyy
- **Adafruit SSD1306**
- **Adafruit GFX Library**
- **DHT sensor library** by Adafruit (for DHT11)

Board support: install **esp32 by Espressif Systems** (Boards Manager), then select **Board → ESP32C6 Dev Module**.

---

## Blynk Setup

1. Create a free account at [blynk.cloud](https://blynk.cloud).
2. Create a new **Template** (Hardware: ESP32, Connection: WiFi) named `TreeVital`.
3. Add datastreams:
   - `V0` — TVI (Integer, 0–100)
   - `V1` — Thirst Index (Integer, 0–100)
   - `V2` — Stress Index (Integer, 0–100)
   - `V3` — Trend (String)
   - `V4` — Status (String)
4. Create a **Device** from the template — this generates a unique **Auth Token**.
5. Add Gauge widgets for V0–V2 and Label widgets for V3–V4 on the dashboard.
6. In `TreeVital.ino`, fill in:
   ```cpp
   #define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
   #define BLYNK_TEMPLATE_NAME "TreeVital"
   #define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"

   char ssid[] = "YOUR_WIFI_SSID";
   char pass[] = "YOUR_WIFI_PASSWORD";
   ```

> ⚠️ Never commit your real Auth Token to a public repository. Keep the placeholders in version control and fill in real credentials only on your local copy, or load them from a separate untracked config file.

---

## Calibration

Both analog sensors need calibrating against your actual hardware — default constants in the code are placeholders and won't be accurate out of the box.

1. Flash the sketch and open Serial Monitor (115200 baud).
2. **Soil:** note the raw value with the FC-28 in dry air (`SOIL_RAW_DRY`) and dipped in water (`SOIL_RAW_WET`).
3. **Light:** note the raw value with the LDR fully covered (`LDR_RAW_DARK`) and under bright light (`LDR_RAW_BRIGHT`).
4. Update the four constants near the top of the sketch, then re-flash.
5. Insert the FC-28 into the plant's soil (ideally right after watering) *before* powering on, since the baseline is learned automatically during `setup()`.

---

## DHT11 Integration

The sketch ships with DHT11 support pre-written but disabled (`USE_DHT11 = false`), so it runs fully without the sensor connected. To enable it:

1. Wire the DHT11 as shown in the connections table above.
2. In the sketch, uncomment the DHT11 include/init block (search for `DHT11 INTEGRATION`).
3. Set `USE_DHT11 = true`.

Once enabled:
- Temperature and humidity are read each cycle and printed to **Serial Monitor only** (no OLED, Blynk, or LED/buzzer changes tied directly to raw values).
- The Stress Index weighting shifts from `water 60% / light 40%` to `water 35% / light 25% / temp 25% / humidity 15%`.
- The "Dominant Factor" diagnosis can now also report "Temperature stress" or "Humidity stress".
- If a DHT11 read fails (common with this sensor), the code falls back to a neutral placeholder value rather than crashing.

---

## System Architecture

```
Sensors (soil, light, temp/humidity)
   → ESP32-C6
   → filtering (moving average) + calibration
   → soil baseline / light-temp-humidity acceptable ranges
   → per-factor stress scores
   → combined Stress Index, Thirst Index, Tree Vitality Index
   → trend & recovery analysis
   → OLED + LEDs + buzzer (local) / Blynk (remote)
```

---

## Roadmap / Future Extensions

- Persist calibration and baseline values across reboots (e.g. Preferences/EEPROM)
- Ultrasonic sensor + water reservoir for automatic irrigation feedback
- pH / NPK sensing for deeper soil health analysis
- Historical charting and long-term trend export

---

## License

Add your preferred license here (e.g. MIT).
