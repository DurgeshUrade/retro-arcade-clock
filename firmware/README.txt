RETRO ARCADE CLOCK & GOOGLE WEATHER STATION
============================================

BOARD / DISPLAY
- ESP32 DevKit V1 (ESP32-WROOM-32)
- SSD1306 128x64 I2C OLED
- SDA GPIO21, SCL GPIO22, VCC 3.3V, GND GND
- Arduino IDE board: ESP32 Dev Module
- Upload port: select your actual COM port (you previously used COM8)

BEFORE UPLOADING
1. Open Retro_Arcade_Google_Weather.ino.
2. Enter your Wi-Fi SSID/password.
3. Enter your Google Maps Platform API key.
4. Install libraries:
   - Adafruit SSD1306
   - Adafruit GFX Library
   - ArduinoJson 7
5. Install Espressif ESP32 boards package.
6. Select ESP32 Dev Module and your COM port.
7. Upload. Open Serial Monitor at 115200 baud.

GOOGLE CLOUD SETUP
- Use one Google Cloud project and one API key.
- Enable Weather API and Air Quality API.
- Attach a billing account as required by Google Maps Platform.
- Restrict the key to Weather API and Air Quality API.
- Set API quotas / alerts in Google Cloud Console.
- Do not share the API key or publish this sketch with your real key.

API REQUEST CADENCE
- Current weather: every 10 minutes (144 requests/day)
- Air quality: every 60 minutes (24 requests/day)
- Daily high/low forecast: every 24 hours (1 request/day)
These are per-device request estimates, assuming each scheduled call succeeds once.
This firmware does not make requests every time the screen changes.

SCREEN LAYOUT
1. Clock: large 12-hour IST clock, date, outdoor temperature.
2. Weather: large temperature, condition, feels-like, humidity, wind.
3. Air: AQI, PM2.5, daily forecast high and low.

IMPORTANT AQI NOTE
The sketch requests local AQI and Universal AQI. It prefers a non-universal index if returned; otherwise it labels the fallback according to Google's returned displayName. Confirm the returned index label in Serial Monitor / actual display. AQI is modelled ambient outdoor air-quality data, not a measurement inside your room.

HIGH / LOW NOTE
The high and low are Google's forecast maximum and minimum for the current local forecast day, not sensor-measured observed extremes.

POWER RECOVERY
When USB power returns, the ESP32 restarts and runs setup again. Wi-Fi credentials are stored in the sketch and reconnection is automatic. The power bank must itself resume 5V output after a power interruption; some power banks require pressing their button.

TROUBLESHOOTING
- HTTP 403: check billing, API enablement, API-key restrictions, and quota.
- HTTP 400: inspect Serial Monitor; confirm APIs and key.
- HTTP 429: quota/rate limit reached; increase only if intended.
- Blank OLED: check wiring and try OLED_ADDRESS 0x3D.
- Wi-Fi: confirm 2.4 GHz compatibility and credentials.
- No AQI: confirm Air Quality API is enabled and that Google returns data for the coordinates.
- ArduinoJson compile error: install ArduinoJson 7.

SECURITY
For a hobby prototype, the key is embedded in firmware and can be extracted from a physical device. Restrict it to the two APIs, set quotas, and consider a small proxy/backend if you later want to protect the key properly.
