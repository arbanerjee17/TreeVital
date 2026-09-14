#define BLYNK_TEMPLATE_ID   "your_template_id"
#define BLYNK_TEMPLATE_NAME "TreeVital"
#define BLYNK_AUTH_TOKEN    "your_auth_token"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

char ssid[] = "your_wifi_ssid";
char pass[] = "your_wifi_password";

// ---------------- DHT11 INTEGRATION: step 1 ----------------
#include <DHT.h>
#define DHT_PIN 21
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);
bool USE_DHT11 = false;
// -------------------------------------------------------------

// ---------------- Pin assignments ----------------
const int SOIL_PIN   = 2;
const int LDR_PIN    = 3;
const int GREEN_LED  = 18;
const int RED_LED    = 19;
const int BUZZER_PIN = 20;
const int OLED_SDA = 22;
const int OLED_SCL = 23;

// ---------------- OLED display ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C   // try 0x3D instead if the display stays blank
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- Calibration ----------------
// FILL THESE IN using the raw values printed to Serial Monitor.
// It does not matter whether the "0%" number is bigger or smaller
// than the "100%" number - the math below handles either direction.
float SOIL_RAW_DRY  = 3000;  // raw reading -> treated as 0% moisture
float SOIL_RAW_WET  = 1200;  // raw reading -> treated as 100% moisture
float LDR_RAW_DARK  = 300;   // raw reading -> treated as 0% light
float LDR_RAW_BRIGHT = 3800; // raw reading -> treated as 100% light

// Acceptable "good" ranges (tune to your plant/species over time)
const float LIGHT_MIN_GOOD = 20, LIGHT_MAX_GOOD = 80;
const float LIGHT_HARD_MIN = 0,  LIGHT_HARD_MAX = 100;
const float TEMP_MIN_GOOD = 18, TEMP_MAX_GOOD = 28;
const float TEMP_HARD_MIN = 5,  TEMP_HARD_MAX = 40;
const float HUM_MIN_GOOD  = 40, HUM_MAX_GOOD  = 70;
const float HUM_HARD_MIN  = 10, HUM_HARD_MAX  = 95;

// ---------------- Baseline (auto-learned at boot) ----------------
float baselineSoilPct = -1;   // set once during setup()
float lastSoilPct = -1;       // used to measure rate of decline

// ---------------- Moving average filters ----------------
const int FILTER_SIZE = 5;
float soilBuf[FILTER_SIZE]  = {0};
float lightBuf[FILTER_SIZE] = {0};
int soilPos = 0, soilCount = 0;
int lightPos = 0, lightCount = 0;

// ---------------- History for trend + recovery ----------------
const int HISTORY_SIZE = 10;
float tviHistory[HISTORY_SIZE]    = {0};
float stressHistory[HISTORY_SIZE] = {0};
int historyPos = 0, historyCount = 0;
float peakStress = 0;

BlynkTimer timer;
String currentStatus = "Healthy";  // updated by sendData(), read continuously by loop()

// ================= Helper functions =================

float pushAndAverage(float* buf, int &pos, int &count, float newVal) {
  buf[pos] = newVal;
  pos = (pos + 1) % FILTER_SIZE;
  if (count < FILTER_SIZE) count++;
  float sum = 0;
  for (int i = 0; i < count; i++) sum += buf[i];
  return sum / count;
}

// Maps a raw ADC value onto 0-100%, given the raw values that represent 0% and 100%
float rawToPercent(float raw, float rawAt0, float rawAt100) {
  float pct = (raw - rawAt0) * 100.0 / (rawAt100 - rawAt0);
  return constrain(pct, 0, 100);
}

// 0 stress inside [minGood,maxGood], ramps up to 100 as value passes the hard limits
float rangeStress(float value, float minGood, float maxGood, float hardMin, float hardMax) {
  if (value >= minGood && value <= maxGood) return 0;
  if (value < minGood) {
    if (hardMin >= minGood) return 100;
    return constrain((minGood - value) * 100.0 / (minGood - hardMin), 0, 100);
  }
  if (hardMax <= maxGood) return 100;
  return constrain((value - maxGood) * 100.0 / (hardMax - maxGood), 0, 100);
}

void pushHistory(float tvi, float stress) {
  tviHistory[historyPos] = tvi;
  stressHistory[historyPos] = stress;
  historyPos = (historyPos + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

String computeTrend() {
  if (historyCount < HISTORY_SIZE) return "Learning";
  float firstHalf = 0, secondHalf = 0;
  int half = HISTORY_SIZE / 2;
  for (int i = 0; i < half; i++) firstHalf += tviHistory[i];
  for (int i = half; i < HISTORY_SIZE; i++) secondHalf += tviHistory[i];
  firstHalf /= half;
  secondHalf /= (HISTORY_SIZE - half);
  float diff = secondHalf - firstHalf;
  if (diff > 3) return "Improving";
  if (diff < -3) return "Deteriorating";
  return "Stable";
}

// ================= Setup =================

void setup() {
  Serial.begin(115200);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("SSD1306 not found - check wiring/address");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("TreeVital");
    display.println("Starting up...");
    display.display();
  }

  // ---------------- DHT11 INTEGRATION: step 2 ----------------
  // if (USE_DHT11) dht.begin();
  // -------------------------------------------------------------

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  Serial.println("Learning soil baseline... IMPORTANT: sensor must already be");
  Serial.println("inserted in moist soil (ideally just after watering) for this to be meaningful.");
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    int raw = analogRead(SOIL_PIN);
    float pct = rawToPercent(raw, SOIL_RAW_DRY, SOIL_RAW_WET);
    sum += pct;
    delay(300);
  }
  baselineSoilPct = sum / 10.0;
  lastSoilPct = baselineSoilPct;
  Serial.print("Baseline soil moisture set to: ");
  Serial.print(baselineSoilPct);
  Serial.println("%");

  timer.setInterval(3000L, sendData);
}

// ================= Main loop =================

void loop() {
  Blynk.run();
  timer.run();
  updateBuzzer();
}

// Runs every loop() iteration (not just every 3s like sendData) so the
// beep timing is actually accurate instead of being sampled too rarely.
void updateBuzzer() {
  const int BEEP_ON_MS = 200;            // how long each beep lasts
  const int CRITICAL_PERIOD_MS = 800;    // beep every 0.8s - urgent
  const int UNHEALTHY_PERIOD_MS = 2000;  // beep every 2s - beep..beep..beep
  if (currentStatus == "Critical") {
    digitalWrite(BUZZER_PIN, (millis() % CRITICAL_PERIOD_MS) < BEEP_ON_MS ? HIGH : LOW);
  } else if (currentStatus == "Unhealthy") {
    digitalWrite(BUZZER_PIN, (millis() % UNHEALTHY_PERIOD_MS) < BEEP_ON_MS ? HIGH : LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ================= Core logic =================

void sendData() {
  // ---- Raw readings ----
  int soilRaw = analogRead(SOIL_PIN);
  int lightRaw = analogRead(LDR_PIN);

  // Print raw values so you can fill in the calibration constants above
  Serial.print("RAW soil="); Serial.print(soilRaw);
  Serial.print("  RAW light="); Serial.println(lightRaw);

  float soilPctRaw = rawToPercent(soilRaw, SOIL_RAW_DRY, SOIL_RAW_WET);
  float lightPctRaw = rawToPercent(lightRaw, LDR_RAW_DARK, LDR_RAW_BRIGHT);

  // ---- Filtering (smooth out noisy single readings) ----
  float soilPct = pushAndAverage(soilBuf, soilPos, soilCount, soilPctRaw);
  float lightPct = pushAndAverage(lightBuf, lightPos, lightCount, lightPctRaw);

  // ---- DHT11 INTEGRATION: step 3 ----
  float tempC = TEMP_MIN_GOOD + (TEMP_MAX_GOOD - TEMP_MIN_GOOD) / 2; // neutral placeholder
  float humPct = HUM_MIN_GOOD + (HUM_MAX_GOOD - HUM_MIN_GOOD) / 2;   // neutral placeholder
  // if (USE_DHT11) {
  //   float t = dht.readTemperature();
  //   float h = dht.readHumidity();
  //   if (!isnan(t)) tempC = t;
  //   if (!isnan(h)) humPct = h;
  // }
  // ------------------------------------

  // ---- Individual stress scores (0-100) ----
  // minGood has a hard floor of 20% so a bad/low baseline (e.g. sensor not
  // yet inserted into soil when it learned) can never make "very dry" look normal.
  float waterMinGood = max((float)20, baselineSoilPct - 15);
  float waterStress = rangeStress(soilPct, waterMinGood, 100, 0, 100);
  float lightStress = rangeStress(lightPct, LIGHT_MIN_GOOD, LIGHT_MAX_GOOD, LIGHT_HARD_MIN, LIGHT_HARD_MAX);
  float tempStress = rangeStress(tempC, TEMP_MIN_GOOD, TEMP_MAX_GOOD, TEMP_HARD_MIN, TEMP_HARD_MAX);
  float humStress = rangeStress(humPct, HUM_MIN_GOOD, HUM_MAX_GOOD, HUM_HARD_MIN, HUM_HARD_MAX);

  // ---- DEBUG: watch these while calibrating. If soil%/light% barely move
  // out of the 20-80 range even at extremes, your raw calibration constants
  // above don't match your actual sensor - re-measure and update them. ----
  Serial.print("  [debug] soil%="); Serial.print(soilPct);
  Serial.print(" light%="); Serial.print(lightPct);
  Serial.print(" waterMinGood="); Serial.print(waterMinGood);
  Serial.print(" waterStress="); Serial.print(waterStress);
  Serial.print(" lightStress="); Serial.println(lightStress);

  // ---- Combined Stress Index (weights redistribute if DHT11 isn't active yet) ----
  float stressIndex;
  if (USE_DHT11) {
    stressIndex = waterStress * 0.35 + lightStress * 0.25 + tempStress * 0.25 + humStress * 0.15;
  } else {
    stressIndex = waterStress * 0.6 + lightStress * 0.4;
  }

  // ---- Thirst Index: current deficit + rate of decline (early warning) ----
  float declineRate = lastSoilPct - soilPct; // positive = drying out
  lastSoilPct = soilPct;
  float thirstIndex = constrain((100 - soilPct) + declineRate * 5, 0, 100);

  // ---- Tree Vitality Index ----
  float tvi = constrain(100 - stressIndex, 0, 100);

  // ---- Recovery Index ----
  if (stressIndex > peakStress) peakStress = stressIndex;
  float recoveryIndex = 0;
  if (peakStress > 0) {
    recoveryIndex = constrain((peakStress - stressIndex) / peakStress * 100.0, 0, 100);
  }
  if (stressIndex < 5) peakStress = 0; // fully recovered, reset for next episode

  pushHistory(tvi, stressIndex);
  String trend = computeTrend();

  // ---- Overall status ----
  String status;
  if (tvi >= 80) status = "Healthy";
  else if (tvi >= 50) status = "Partially Healthy";
  else if (tvi >= 25) status = "Unhealthy";
  else status = "Critical";

  // ---- Dominant stress factor (explainable diagnosis) ----
  String dominant = "None";
  float maxStress = waterStress;
  dominant = "Water deficit";
  if (lightStress > maxStress) { maxStress = lightStress; dominant = "Poor light"; }
  if (USE_DHT11 && tempStress > maxStress) { maxStress = tempStress; dominant = "Temperature stress"; }
  if (USE_DHT11 && humStress > maxStress) { maxStress = humStress; dominant = "Humidity stress"; }
  if (maxStress < 10) dominant = "None - conditions good";

  // ---- LEDs + buzzer ----
  digitalWrite(GREEN_LED, status == "Healthy" ? HIGH : LOW);
  digitalWrite(RED_LED, (status == "Unhealthy" || status == "Critical") ? HIGH : LOW);
  currentStatus = status; // updateBuzzer() in loop() reads this continuously

  // ---- Update OLED ----
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("TVI: "); display.println((int)tvi);
  display.print("Thirst: "); display.println((int)thirstIndex);
  display.print("Stress: "); display.println((int)stressIndex);
  display.print("Trend: "); display.println(trend);
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("Status: "); display.println(status);
  display.display();

  // ---- Send to Blynk ----
  Blynk.virtualWrite(V0, tvi);
  Blynk.virtualWrite(V1, thirstIndex);
  Blynk.virtualWrite(V2, stressIndex);
  Blynk.virtualWrite(V3, trend);
  Blynk.virtualWrite(V4, status);
  // You've only created datastreams V0-V4 so far. If you later add V5 (Dominant
  // Factor), V6 (Soil %) or V7 (Light %) in the Blynk template, uncomment below:
  // Blynk.virtualWrite(V5, dominant);
  // Blynk.virtualWrite(V6, soilPct);
  // Blynk.virtualWrite(V7, lightPct);

  Serial.print("TVI="); Serial.print(tvi);
  Serial.print(" Thirst="); Serial.print(thirstIndex);
  Serial.print(" Stress="); Serial.print(stressIndex);
  Serial.print(" Recovery="); Serial.print(recoveryIndex);
  Serial.print(" Trend="); Serial.print(trend);
  Serial.print(" Status="); Serial.print(status);
  Serial.print(" Dominant="); Serial.println(dominant);
}