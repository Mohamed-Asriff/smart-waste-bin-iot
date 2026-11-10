
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>

// ============================================
// WI-FI CONFIGURATION
// ============================================
const char* WIFI_SSID = "Flip 6";
const char* WIFI_PASSWORD = "0987654321";

// Computer's current Wi-Fi IPv4 address
const char* API_URL =
    "http://10.36.22.162:8081/api/bins/data";

// ============================================
// HC-SR04 SENSOR PINS
// ============================================
#define TRIG_PIN 5
#define ECHO_PIN 18

// IMPORTANT:
// HC-SR04 ECHO can output 5V.
// Use a voltage divider to reduce ECHO to 3.3V
// before connecting it to ESP32 GPIO 18.

// ============================================
// BIN CONFIGURATION
// ============================================
const char* BIN_ID = "BIN-003";
const float BIN_HEIGHT_CM = 20.0;

// ============================================
// UPDATE INTERVAL
// ============================================
const unsigned long UPDATE_INTERVAL = 7000;
unsigned long lastUpdateTime = 0;

// ============================================
// CONNECT TO WI-FI
// ============================================
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("Connecting to Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected!");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("Computer API: ");
    Serial.println(API_URL);

    return true;
  }

  Serial.println("Wi-Fi connection failed.");
  return false;
}

// ============================================
// READ ONE ULTRASONIC DISTANCE
// ============================================
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    return -1.0;
  }

  float distance = duration * 0.0343f / 2.0f;

  if (distance < 2.0f ||
      distance > BIN_HEIGHT_CM) {
    return -1.0;
  }

  return distance;
}

// ============================================
// GET AVERAGE VALID SENSOR READING
// ============================================
float getAverageDistance() {
  const int samples = 5;

  float sum = 0.0f;
  int validReadings = 0;

  for (int i = 0; i < samples; i++) {
    float distance = readDistance();

    if (distance >= 2.0f &&
        distance <= BIN_HEIGHT_CM) {
      sum += distance;
      validReadings++;
    }

    delay(60);
  }

  if (validReadings == 0) {
    return -1.0f;
  }

  return sum / validReadings;
}

// ============================================
// CALCULATE FILL PERCENTAGE
// ============================================
float calculateFillLevel(float distance) {
  float fill =
      ((BIN_HEIGHT_CM - distance) /
       BIN_HEIGHT_CM) * 100.0f;

  return constrain(fill, 0.0f, 100.0f);
}

// ============================================
// SEND JSON TO SPRING BOOT
// ============================================
bool sendBinData(float distance, float fillPercentage) {
  if (!connectWiFi()) {
    Serial.println("Cannot send: Wi-Fi unavailable.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(client, API_URL)) {
    Serial.println("HTTP initialization failed.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  // JSON request body
  String json = "{";
  json += "\"binId\":\"";
  json += BIN_ID;
  json += "\",";
  json += "\"distanceCm\":";
  json += String(distance, 1);
  json += ",";
  json += "\"fillPercentage\":";
  json += String(fillPercentage, 1);
  json += ",";
  json += "\"status\":\"ACTIVE\"";
  json += "}";

  Serial.println();
  Serial.println("Sending bin data:");
  Serial.println(json);

  int httpCode = http.POST(json);

  bool success = false;

  if (httpCode > 0) {
    Serial.print("HTTP status code: ");
    Serial.println(httpCode);

    String response = http.getString();

    Serial.print("Backend response: ");
    Serial.println(response);

    if (httpCode >= 200 && httpCode < 300) {
      Serial.println("HTTP request successful.");
      success = true;
    } else {
      Serial.println(
          "Backend rejected the request or returned an error.");
    }
  } else {
    Serial.print("HTTP connection error: ");
    Serial.println(http.errorToString(httpCode));
    Serial.println(
        "Check the server IP, port, network and firewall.");
  }

  http.end();

  return success;
}

// ============================================
// UPDATE BIN
// ============================================
void updateBin() {
  Serial.println();
  Serial.println("========== BIN UPDATE ==========");

  float distance = getAverageDistance();

  if (distance < 0.0f) {
    Serial.println("Sensor reading failed.");
    Serial.println("Skipping this update.");
    return;
  }

  distance = roundf(distance * 10.0f) / 10.0f;

  float fillPercentage = calculateFillLevel(distance);
  fillPercentage =
      roundf(fillPercentage * 10.0f) / 10.0f;

  Serial.print("Bin ID: ");
  Serial.println(BIN_ID);

  Serial.print("Distance: ");
  Serial.print(distance, 1);
  Serial.println(" cm");

  Serial.print("Fill percentage: ");
  Serial.print(fillPercentage, 1);
  Serial.println("%");

  bool sent = sendBinData(distance, fillPercentage);

  if (sent) {
    Serial.println("Bin update sent successfully.");
  } else {
    Serial.println("Bin update was not confirmed.");
  }

  Serial.println("================================");
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("SMART WASTE BIN");
  Serial.println("ESP32 + HC-SR04 + Wi-Fi");
  Serial.println("Spring Boot integration");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);

  connectWiFi();

  // Send the first reading immediately
  updateBin();

  lastUpdateTime = millis();
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (millis() - lastUpdateTime >= UPDATE_INTERVAL) {
    lastUpdateTime = millis();
    updateBin();
  }

  delay(50);
}
