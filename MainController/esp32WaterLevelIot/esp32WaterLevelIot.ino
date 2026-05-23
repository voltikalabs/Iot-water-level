#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <EEPROM.h>

#include <WiFi.h>
// #include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <MQTT.h>

#define DEBUG 0
#if DEBUG
#define DBG(x) Serial.println(x)
#else
#define DBG(x)
#endif

const char ssid[] = "SRJ_HomeSpot";
const char pass[] = "123Strahmaj";

// WiFiClientSecure net;// WSS
WiFiClient net;  // Local
MQTTClient client;

unsigned long lastMillis = 0;

#define EEPROM_SIZE 64
#define ADDR_SP_LOW 0
#define ADDR_SP_HIGH 4

#define RXD2 16
#define TXD2 17

HardwareSerial distanceSerial(2);
LiquidCrystal_I2C lcd(0x27, 20, 4);

char charStr[16];
byte bufIndex = 0;
int distCm = 0;
int SP_HIGH = 31;  // default
int SP_LOW = 79;   // default
int distCmLow = 0;
int distCmHigh = 0;

int levelPercent = 0;
uint32_t prevMillis = 0;

byte barEmpty[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000  // bottom row only
};
byte arrow[8] = {
  0b01000,
  0b00100,
  0b00010,
  0b11111,
  0b00010,
  0b00100,
  0b01000,
  0b00000  // bottom row only
};
byte barFull[8] = { 31, 31, 31, 31, 31, 31, 31, 31 };  // full

#define OUT_RLY_PUMP 18
#define SETUP_PUMP pinMode(OUT_RLY_PUMP, OUTPUT)
#define PUMP_ON digitalWrite(OUT_RLY_PUMP, LOW)
#define PUMP_OFF digitalWrite(OUT_RLY_PUMP, HIGH)
#define PUMP_STATUS (digitalRead(OUT_RLY_PUMP) == LOW)
bool pumpStatus = false;

#define VALVE_SWITCH 5
#define SETUP_VALVE_SWITCH pinMode(VALVE_SWITCH, INPUT)
#define VALVE_SWITCH_STATUS (digitalRead(VALVE_SWITCH) == HIGH)
#define BOOSTER_RLY_PUMP 19
#define SETUP_BOOSTER_PUMP pinMode(BOOSTER_RLY_PUMP, OUTPUT)
#define BOOSTER_ON digitalWrite(BOOSTER_RLY_PUMP, LOW)
#define BOOSTER_OFF digitalWrite(BOOSTER_RLY_PUMP, HIGH)
#define BOOSTER_STATUS (digitalRead(BOOSTER_RLY_PUMP) == LOW)
uint8_t valveSwitchOnDetectDelaySec = 6;
uint8_t valveSwitchOffDetectDelaySec = 2;

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG("WiFi not connected");
    return;
  }
  if (client.connected()) return;

  DBG("MQTT connecting...");

  // net.setInsecure();
  if (client.connect("dev1", "dev1", "dev1@mqtt.voltikalabs.web.id")) {

    DBG("MQTT connected");

    client.subscribe("dev/dev1/cmd");
    client.subscribe("dev/dev1/config");

    client.publish("dev/dev1/status", "online", true);

  } else {

    DBG("MQTT failed");
    DBG(client.lastError());
  }
}

void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) return;

  static unsigned long lastAttempt = 0;

  if (millis() - lastAttempt > 5000) {

    DBG("WiFi reconnect...");
    WiFi.begin(ssid, pass);

    lastAttempt = millis();
  }
}

void setup() {
#if DEBUG
  Serial.begin(115200);
#endif
  EEPROM.begin(EEPROM_SIZE);
  saveSetPoint();
  loadSetPoint();

  SETUP_VALVE_SWITCH;
  SETUP_BOOSTER_PUMP;
  BOOSTER_OFF;

  SETUP_PUMP;
  PUMP_OFF;

  distCmLow = SP_LOW;
  distCmHigh = SP_HIGH;

  distanceSerial.begin(4800, SERIAL_8N1, RXD2, TXD2);

  lcd.init();
  lcd.backlight();

  lcd.createChar(0, barEmpty);
  lcd.createChar(1, arrow);
  lcd.createChar(5, barFull);

  lcd.setCursor(0, 0);
  lcd.printf("SP L: %03dcm H: %03dcm", distCmLow, distCmHigh);
  lcd.setCursor(0, 1);
  lcd.print(F("Dist : 000cm V:0 B:0"));
  lcd.setCursor(0, 2);
  lcd.print(F("Level:000% NORMAL- 0"));  // "FULL  ", "NORMAL", "LOW   ", "V.LOW "
  lcd.setCursor(17, 2);
  lcd.write(byte(1));  // arrow
  drawBar(0);          // example: 62%

  ///
  WiFi.begin(ssid, pass);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported
  // by Arduino. You need to set the IP address directly.
  //
  // MQTT brokers usually use port 8883 for secure connections.
  // client.begin("mqtt.voltikalabs.web.id", 443, net);
  client.begin("192.168.0.101", 1883, net);
  // Auto set to offline status if device has been disconnected
  client.setWill(
    "dev/dev1/status",
    "offline",
    true,
    1);
  client.onMessage(messageReceived);
}

char lastPayload[128] = "";  // simpan payload terakhir

void loop() {
  connectWiFi();
  connectMQTT();
  client.loop();
  // delay(10);  // <- fixes some issues with WiFi stability

  if (millis() - lastMillis > 5000) {  // cek tiap 5 detik
    lastMillis = millis();

    char payload[128];

    snprintf(payload, sizeof(payload),
             "{\"dist_cm\":%d,\"level_percent\":%d,\"pump\":%d,\"booster\":%d,\"valve\":%d}",
             distCm,
             levelPercent,
             PUMP_STATUS,
             BOOSTER_STATUS,
             VALVE_SWITCH_STATUS);

    // only publish if there is a data changed
    if (strcmp(payload, lastPayload) != 0) {
      client.publish("dev/dev1/telemetry", payload);
      strcpy(lastPayload, payload);  // update payload terakhir
      DBG("Published telemetry: " + String(payload));
    }
  }

  ///
  while (distanceSerial.available()) {
    char c = distanceSerial.read();

    if (c == '\n') {
      charStr[bufIndex] = '\0';
      lcd.setCursor(7, 1);
      lcd.print(F("   "));
      String strInt = charStr;
      distCm = strInt.toInt();
      // int distInt = distCm;
      distCm = smoothDistance(distCm);
      // DBG(String() + strInt + " " + distInt + " " + distCm);
      strcpy(charStr, "");
      sprintf(charStr, "%03d", distCm);
      lcd.setCursor(7, 1);
      lcd.print(charStr);
      bufIndex = 0;  // reset buffer
    } else {
      if (bufIndex < sizeof(charStr) - 1) {
        if (c >= '0' && c <= '9' || c == '.') {
          charStr[bufIndex++] = c;
        }
      }
    }
  }

  static int lastPercent = -1;
  levelPercent = calculateLevelPercent(distCm);
  String status = getLevelStatus(levelPercent);

  if (levelPercent != lastPercent) {
    lcd.setCursor(6, 2);
    lcd.printf("%03d%%", levelPercent);

    lcd.setCursor(11, 2);
    lcd.print(status);

    drawBar(levelPercent);
    lastPercent = levelPercent;
  }

  if (levelPercent <= 15 && !pumpStatus) {
    if (prevMillis == 0) prevMillis = millis();
    if (((uint32_t)millis() - prevMillis) >= 5000) {  // If the level is less than 15% for 5 secs
                                                      // Turn on the pump
      if (!PUMP_STATUS) {
        pumpStatus = true;
        PUMP_ON;
        prevMillis = 0;
        delay(1000);
        lcd.setCursor(19, 2);
        lcd.print(F("1"));
      }
    }
  } else if (levelPercent >= 99 && pumpStatus) {
    if (prevMillis == 0) prevMillis = millis();
    if (((uint32_t)millis() - prevMillis) >= 5000) {  // If the level is greater than 99% for 5 secs
                                                      // Turn off the pump
      if (PUMP_STATUS) {
        pumpStatus = false;
        PUMP_OFF;
        prevMillis = 0;
        lcd.setCursor(19, 2);
        lcd.print(F("0"));
      }
    }
  } else {
    // do nothing
    prevMillis = 0;
  }

  /// Booster Pump Management Relay
  static unsigned long prevMillisSecs = millis();
  static uint8_t onCounter = 0;
  static uint8_t offCounter = 0;

  // Statement true every second
  if ((unsigned long)(millis() - prevMillisSecs) >= 1000) {

    prevMillisSecs += 1000;

    uint8_t valve = VALVE_SWITCH_STATUS;

    lcd.setCursor(15, 1);
    lcd.print(valve ? "1" : "0");

    if (valve) {
      // Valve ON detected
      onCounter++;
      offCounter = 0;

      if (!BOOSTER_STATUS && onCounter >= valveSwitchOnDetectDelaySec) {
        BOOSTER_ON;
        lcd.setCursor(19, 1);
        lcd.print(F("1"));
      }
    } else {
      // Valve OFF detected
      offCounter++;
      onCounter = 0;

      if (BOOSTER_STATUS && offCounter >= valveSwitchOffDetectDelaySec) {
        BOOSTER_OFF;
        lcd.setCursor(19, 1);
        lcd.print(F("0"));
      }
    }
  }
}

void drawBar(int percent) {
  int totalBlocks = (percent * 20) / 100;  // 20 columns

  lcd.setCursor(0, 3);

  for (int i = 0; i < 20; i++) {
    if (i < totalBlocks) {
      lcd.write(byte(5));  // full block
    } else {
      lcd.write(byte(0));  // empty
    }
  }
}

int calculateLevelPercent(int dist) {
  if (dist <= SP_HIGH) return 100;
  if (dist >= SP_LOW) return 0;

  return (SP_LOW - dist) * 100 / (SP_LOW - SP_HIGH);
}

String getLevelStatus(int percent) {
  if (percent >= 90) return "FULL  ";
  if (percent >= 40) return "NORMAL";
  if (percent >= 20) return "LOW   ";
  return "V.LOW ";
}

int smoothDistance(int newVal) {
  static int readings[5] = { 0, 0, 0, 0, 0 };
  static int index = 0;
  static long total = 0;

  // Remove the oldest reading from total
  total = total - readings[index];

  // Add the new reading
  readings[index] = newVal;
  total = total + newVal;

  // Move to next index
  index = (index + 1) % 5;

  // Calculate and return the average
  return total / 5;
}

void saveSetPoint() {
  EEPROM.put(ADDR_SP_LOW, SP_LOW);
  EEPROM.put(ADDR_SP_HIGH, SP_HIGH);
  EEPROM.commit();
}

void loadSetPoint() {
  EEPROM.get(ADDR_SP_LOW, SP_LOW);
  EEPROM.get(ADDR_SP_HIGH, SP_HIGH);

  // validasi biar tidak baca sampah
  if (SP_LOW < 10 || SP_LOW > 500) SP_LOW = 120;
  if (SP_HIGH < 5 || SP_HIGH > 300) SP_HIGH = 20;
}

int extractValue(String payload, String key) {

  int startIndex = payload.indexOf("\"" + key + "\":");

  if (startIndex == -1) return -1;

  startIndex += key.length() + 3;

  int endIndex = payload.indexOf(",", startIndex);

  if (endIndex == -1) {
    endIndex = payload.indexOf("}", startIndex);
  }

  String value = payload.substring(startIndex, endIndex);

  return value.toInt();
}

/*
Kirim command pump ON:
mosquitto_pub \
-h localhost \
-t dev/dev1/cmd \
-u aq -P aq@mqtt.voltikalabs.web.id \
-m '{"pump":1}'

Kirim config:
mosquitto_pub \
-h localhost \
-t dev/dev1/config \
-u aq -P aq@mqtt.voltikalabs.web.id \
-m '{"sp_low":80,"sp_high":30}'
*/

void messageReceived(String &topic, String &payload) {
  DBG("CMD: " + topic + " -> " + payload);

  if (topic == "dev/dev1/cmd") {

    if (payload.indexOf("\"pump\":1") >= 0) {
      PUMP_ON;
      pumpStatus = true;
    }

    if (payload.indexOf("\"pump\":0") >= 0) {
      PUMP_OFF;
      pumpStatus = false;
    }
  }

  if (topic == "dev/dev1/config") {

    int spLow = extractValue(payload, "sp_low");
    int spHigh = extractValue(payload, "sp_high");

    if (spLow > 0) SP_LOW = spLow;
    if (spHigh > 0) SP_HIGH = spHigh;

    saveSetPoint();

    distCmLow = SP_LOW;
    distCmHigh = SP_HIGH;

    lcd.setCursor(0, 0);
    lcd.printf("SP L: %03dcm H: %03dcm", distCmLow, distCmHigh);

    DBG("Config updated");
  }
}
