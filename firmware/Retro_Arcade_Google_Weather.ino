/*
  RETRO ARCADE CLOCK & GOOGLE WEATHER STATION
  Hardware: ESP32 DevKit V1 (ESP32-WROOM-32)
  OLED: 0.96" SSD1306 I2C, 128x64, address usually 0x3C
  OLED wiring: VCC->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22
  Arduino IDE port: select your board's current port (you previously used COM8).

  Screens (rotate every 5 seconds):
    1. Large 12-hour IST clock, date, current outdoor temperature
    2. Large current outdoor temperature, feels-like, humidity, wind
    3. AQI, PM2.5, today's forecast high and low

  Google endpoints:
    Weather current conditions: every 10 minutes
    Air Quality current conditions: every 60 minutes
    Daily forecast: every 24 hours

  IMPORTANT:
  - Enter Wi-Fi credentials and Google Maps Platform API key below.
  - Enable Weather API and Air Quality API in the same Google Cloud project.
  - A billing-enabled Google Cloud project is required for production use.
  - Restrict the API key to these APIs and set quota limits in Google Cloud.
  - ArduinoJson 7 is required.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------------- USER SETTINGS ----------------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";
const char* GOOGLE_API_KEY = "YOUR_API_KEY";

constexpr double LATITUDE = YOUR_LOCATION_COORDINATES;
constexpr double LONGITUDE = YOUR_LOCATION_COORDINATES;

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr uint8_t OLED_ADDRESS = 0x3C; // Try 0x3D if required
constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr unsigned long SCREEN_INTERVAL_MS = 5000UL;
constexpr unsigned long WEATHER_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr unsigned long AIR_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr unsigned long DAILY_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr unsigned long WIFI_RETRY_MS = 10000UL;
constexpr unsigned long NTP_RETRY_MS = 60000UL;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

struct WeatherState {
  bool currentValid = false;
  bool dailyValid = false;
  float temp = NAN;
  float feels = NAN;
  int humidity = -1;
  float windKmh = NAN;
  String condition = "WAITING";
  float high = NAN;
  float low = NAN;
} wx;

struct AirState {
  bool valid = false;
  int aqi = -1;
  String aqiLabel = "AQI";
  String category = "WAITING";
  float pm25 = NAN;
} air;

unsigned long lastScreenMs = 0;
unsigned long lastWeatherMs = 0;
unsigned long lastAirMs = 0;
unsigned long lastDailyMs = 0;
unsigned long lastWiFiMs = 0;
unsigned long lastNtpMs = 0;
unsigned long lastClockDrawMs = 0;
int screenIndex = 0;
bool oledOK = false;
bool fetchTaskRunning = false;
bool doWeatherFetch = false;
bool doAirFetch = false;
bool doDailyFetch = false;

String two(int n) { return n < 10 ? "0" + String(n) : String(n); }

bool validApiKey() {
  return strlen(GOOGLE_API_KEY) > 20 &&
         String(GOOGLE_API_KEY) != "YOUR_GOOGLE_MAPS_PLATFORM_API_KEY";
}

bool validWiFiCredentials() {
  return String(WIFI_SSID) != "YOUR_WIFI_NAME" && strlen(WIFI_SSID) > 0;
}

String clockText() {
  struct tm t;
  if (!getLocalTime(&t, 30)) return "--:-- --";
  int h = t.tm_hour % 12;
  if (h == 0) h = 12;
  return two(h) + ":" + two(t.tm_min) + (t.tm_hour >= 12 ? " PM" : " AM");
}

String dateText() {
  struct tm t;
  if (!getLocalTime(&t, 30)) return "DATE SYNCING";
  const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  return String(days[t.tm_wday]) + " " + two(t.tm_mday) + " " + months[t.tm_mon];
}

void drawWifiIcon() {
  if (WiFi.status() == WL_CONNECTED) {
    display.drawLine(108, 4, 120, 4, SSD1306_WHITE);
    display.drawLine(110, 7, 118, 7, SSD1306_WHITE);
    display.drawLine(113, 10, 115, 10, SSD1306_WHITE);
    display.fillCircle(114, 13, 1, SSD1306_WHITE);
  } else {
    display.drawLine(108, 3, 120, 14, SSD1306_WHITE);
    display.drawLine(120, 3, 108, 14, SSD1306_WHITE);
  }
}

void drawHeaderLine() {
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
  drawWifiIcon();
}

void centerText(const String& s, int y, uint8_t size) {
  display.setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, y, &x1, &y1, &w, &h);
  display.setCursor((128 - (int)w) / 2, y);
  display.print(s);
}

void drawClockScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(2, 4);
  display.print(dateText());
  drawWifiIcon();

  // Large clock uses the space previously occupied by the heading.
  centerText(clockText(), 21, 2);

  display.drawLine(0, 48, 127, 48, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(3, 53);
  display.print("OUT ");
  if (wx.currentValid && isfinite(wx.temp)) {
    display.print(wx.temp, 1);
    display.print((char)247);
    display.print("C");
  } else {
    display.print("--.- C");
  }
  display.setCursor(82, 53);
  display.print(wx.currentValid ? "LIVE" : "WAIT");
  display.display();
}

void drawWeatherScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawWifiIcon();

  if (wx.currentValid && isfinite(wx.temp)) {
    // No "LOCAL WEATHER" heading; large digits take the top area.
    centerText(String(wx.temp, 1) + String((char)247) + "C", 1, 3);
  } else {
    centerText("--.- C", 7, 2);
  }

  centerText(wx.currentValid ? wx.condition : "WEATHER WAITING", 31, 1);
  display.drawLine(0, 42, 127, 42, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(2, 48);
  display.print("FEELS ");
  if (isfinite(wx.feels)) display.print(wx.feels, 1); else display.print("--");
  display.print("C");

  display.setCursor(69, 48);
  display.print("HUM ");
  if (wx.humidity >= 0) display.print(wx.humidity); else display.print("--");
  display.print("%");

  display.setCursor(2, 57);
  display.print("WIND ");
  if (isfinite(wx.windKmh)) display.print(wx.windKmh, 0); else display.print("--");
  display.print(" KM/H");

  display.display();
}

void drawAirScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("AIR QUALITY");
  drawWifiIcon();

  display.setCursor(3, 19);
  display.print(air.valid ? air.aqiLabel : "AQI");
  display.setTextSize(2);
  display.setCursor(3, 29);
  if (air.valid && air.aqi >= 0) display.print(air.aqi);
  else display.print("--");

  display.setTextSize(1);
  display.setCursor(72, 19);
  display.print("PM2.5");
  display.setTextSize(2);
  display.setCursor(72, 29);
  if (air.valid && isfinite(air.pm25)) display.print(air.pm25, 0);
  else display.print("--");
  display.setTextSize(1);
  display.setCursor(106, 48);
  display.print("ug/m3");

  display.drawLine(0, 49, 127, 49, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(2, 54);
  display.print("H:");
  if (wx.dailyValid && isfinite(wx.high)) display.print(wx.high, 0); else display.print("--");
  display.print((char)247);
  display.print("C");

  display.setCursor(67, 54);
  display.print("L:");
  if (wx.dailyValid && isfinite(wx.low)) display.print(wx.low, 0); else display.print("--");
  display.print((char)247);
  display.print("C");

  display.display();
}

void renderScreen() {
  if (!oledOK) return;
  if (screenIndex == 0) drawClockScreen();
  else if (screenIndex == 1) drawWeatherScreen();
  else drawAirScreen();
}

// Return JSON response body for GET or POST. Caller handles parsing.
bool httpsGetJson(const String& url, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure(); // TLS transport; certificate verification is not enabled.
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  String body = code == HTTP_CODE_OK ? http.getString() : "";
  if (code != HTTP_CODE_OK) {
    Serial.printf("GET failed: HTTP %d\n", code);
    http.end();
    return false;
  }
  http.end();
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("GET JSON parse error: %s\n", err.c_str());
    return false;
  }
  return true;
}

bool httpsPostJson(const String& url, const String& payload, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure(); // TLS transport; certificate verification is not enabled.
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  String body = code == HTTP_CODE_OK ? http.getString() : "";
  if (code != HTTP_CODE_OK) {
    Serial.printf("POST failed: HTTP %d\n", code);
    http.end();
    return false;
  }
  http.end();
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("POST JSON parse error: %s\n", err.c_str());
    return false;
  }
  return true;
}

void fetchCurrentWeather() {
  String url = "https://weather.googleapis.com/v1/currentConditions:lookup?key=" +
    String(GOOGLE_API_KEY) + "&location.latitude=" + String(LATITUDE, 6) +
    "&location.longitude=" + String(LONGITUDE, 6) + "&languageCode=en";

  JsonDocument doc;
  if (!httpsGetJson(url, doc)) return;

  JsonObject temp = doc["temperature"];
  JsonObject feels = doc["feelsLikeTemperature"];
  JsonObject wind = doc["wind"];
  JsonObject cond = doc["weatherCondition"]["description"];

  if (!temp.isNull()) {
    wx.temp = temp["degrees"] | NAN;
    wx.feels = feels["degrees"] | NAN;
    wx.humidity = doc["relativeHumidity"] | -1;
    wx.windKmh = wind["speed"]["value"] | NAN;
    wx.condition = cond["text"] | "WEATHER";
    wx.condition.toUpperCase();
    wx.currentValid = true;
    Serial.println("Google current weather updated.");
  }
}

void fetchDailyForecast() {
  String url = "https://weather.googleapis.com/v1/forecast/days:lookup?key=" +
    String(GOOGLE_API_KEY) + "&location.latitude=" + String(LATITUDE, 6) +
    "&location.longitude=" + String(LONGITUDE, 6) + "&days=1";

  JsonDocument doc;
  if (!httpsGetJson(url, doc)) return;

  JsonArray days = doc["forecastDays"].as<JsonArray>();
  if (days.size() == 0) {
    Serial.println("Daily forecast returned no forecastDays.");
    return;
  }

  JsonObject today = days[0];
  JsonObject maxT = today["maxTemperature"];
  JsonObject minT = today["minTemperature"];
  if (!maxT.isNull() && !minT.isNull()) {
    wx.high = maxT["degrees"] | NAN;
    wx.low = minT["degrees"] | NAN;
    wx.dailyValid = true;
    Serial.println("Google daily high/low updated.");
  }
}

void fetchAirQuality() {
  String url = "https://airquality.googleapis.com/v1/currentConditions:lookup?key=" +
    String(GOOGLE_API_KEY);

  JsonDocument requestDoc;
  requestDoc["universalAqi"] = true;
  requestDoc["languageCode"] = "en";
  requestDoc["location"]["latitude"] = LATITUDE;
  requestDoc["location"]["longitude"] = LONGITUDE;
  JsonArray extra = requestDoc["extraComputations"].to<JsonArray>();
  extra.add("POLLUTANT_CONCENTRATION");
  extra.add("LOCAL_AQI");

  String payload;
  serializeJson(requestDoc, payload);

  JsonDocument response;
  if (!httpsPostJson(url, payload, response)) return;

  // Prefer a local AQI index if Google returns one; otherwise display
  // Universal AQI and label it explicitly to avoid implying India's scale.
  JsonArray indexes = response["indexes"].as<JsonArray>();
  bool foundIndex = false;
  for (JsonObject idx : indexes) {
    String code = idx["code"] | "";
    if (code != "uaqi") {
      air.aqi = idx["aqi"] | -1;
      air.aqiLabel = idx["displayName"] | "LOCAL AQI";
      air.category = idx["category"] | "";
      foundIndex = true;
      break;
    }
  }
  if (!foundIndex && indexes.size() > 0) {
    JsonObject idx = indexes[0];
    air.aqi = idx["aqi"] | -1;
    air.aqiLabel = idx["displayName"] | "UNIVERSAL AQI";
    air.category = idx["category"] | "";
    foundIndex = true;
  }

  JsonArray pollutants = response["pollutants"].as<JsonArray>();
  for (JsonObject pollutant : pollutants) {
    String code = pollutant["code"] | "";
    if (code == "pm25") {
      air.pm25 = pollutant["concentration"]["value"] | NAN;
      break;
    }
  }

  if (foundIndex) {
    air.valid = true;
    Serial.println("Google Air Quality updated.");
  } else {
    Serial.println("Air Quality response contained no AQI index.");
  }
}

void networkFetchTask(void* parameter) {
  if (WiFi.status() == WL_CONNECTED && validApiKey()) {
    // The main loop snapshots which requests are due before launching this task.
    if (doWeatherFetch) fetchCurrentWeather();
    if (doAirFetch) fetchAirQuality();
    if (doDailyFetch) fetchDailyForecast();
  } else if (!validApiKey()) {
    Serial.println("Add your Google API key in GOOGLE_API_KEY.");
  }

  doWeatherFetch = false;
  doAirFetch = false;
  doDailyFetch = false;
  // OLED drawing stays in loop() only; this avoids concurrent I2C display writes.
  fetchTaskRunning = false;
  vTaskDelete(nullptr);
}

void startNetworkFetchIfDue() {
  if (fetchTaskRunning || WiFi.status() != WL_CONNECTED) return;
  if (!validApiKey()) return;

  unsigned long now = millis();
  bool weatherDue = (lastWeatherMs == 0 || now - lastWeatherMs >= WEATHER_INTERVAL_MS);
  bool airDue = (lastAirMs == 0 || now - lastAirMs >= AIR_INTERVAL_MS);
  bool dailyDue = (lastDailyMs == 0 || now - lastDailyMs >= DAILY_INTERVAL_MS);
  if (!weatherDue && !airDue && !dailyDue) return;

  // Mark attempt times before launching to avoid repeated retries in a tight loop.
  if (weatherDue) lastWeatherMs = now;
  if (airDue) lastAirMs = now;
  if (dailyDue) lastDailyMs = now;
  doWeatherFetch = weatherDue;
  doAirFetch = airDue;
  doDailyFetch = dailyDue;

  fetchTaskRunning = true;
  BaseType_t ok = xTaskCreatePinnedToCore(
    networkFetchTask, "GoogleWeather", 10000, nullptr, 1, nullptr, 0
  );
  if (ok != pdPASS) {
    fetchTaskRunning = false;
    Serial.println("Could not create network task.");
  }
}

void maintainWiFi() {
  if (!validWiFiCredentials() || WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWiFiMs >= WIFI_RETRY_MS) {
    lastWiFiMs = now;
    Serial.println("Wi-Fi reconnect attempt...");
    WiFi.disconnect(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  if (oledOK) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    centerText("ARCADE WEATHER", 18, 1);
    centerText("STARTING...", 34, 1);
    display.display();
  } else {
    Serial.println("SSD1306 not found. Check I2C wiring/address 0x3C or 0x3D.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  if (validWiFiCredentials()) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWiFiMs = millis();
  } else {
    Serial.println("Enter Wi-Fi credentials in the sketch.");
  }

  // India Standard Time = UTC+05:30, no daylight-saving time.
  configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
  lastNtpMs = millis();
  renderScreen();
}

void loop() {
  unsigned long now = millis();

  maintainWiFi();

  struct tm t;
  if (!getLocalTime(&t, 20) && now - lastNtpMs >= NTP_RETRY_MS) {
    configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
    lastNtpMs = now;
  }

  startNetworkFetchIfDue();

  if (now - lastScreenMs >= SCREEN_INTERVAL_MS) {
    lastScreenMs = now;
    screenIndex = (screenIndex + 1) % 3;
    renderScreen();
  }

  if (screenIndex == 0 && now - lastClockDrawMs >= 1000UL) {
    lastClockDrawMs = now;
    renderScreen();
  }

  delay(10);
}
