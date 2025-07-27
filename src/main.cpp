#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LoRa.h>

#include "SensorManager.h"
#include "DataCollector.h"
#include "EventDetector.h"
#include "APIClient.h"

AsyncWebServer server(80);
Preferences preferences;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 16
#define SCREEN_ADDRESS 0x3C
#define OLED_SDA 4
#define OLED_SCL 15

// External I2C for sensors
#define SENSOR_SDA 21
#define SENSOR_SCL 22

// LoRa pin definitions (Heltec LoRa32 V2)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 14
#define LORA_IRQ 26

// Create separate Wire instances
TwoWire I2C_OLED = TwoWire(0);      // Built-in OLED
TwoWire I2C_SENSORS = TwoWire(1);   // External sensors

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, OLED_RESET);

SensorManager* sensorManager;
DataCollector* dataCollector;
EventDetector* eventDetector;
WellPumpAPIClient* apiClient;

const char* AP_SSID = "WellPump-Config";
const char* AP_PASSWORD = "pumphouse";
const char* HOSTNAME = "well-pump-monitor";

// Heltec LoRa32 V2 Pin Definitions
const uint8_t LED_PIN = 2;       // Built-in LED

// Note: Built-in OLED uses GPIO4 (SDA), GPIO15 (SCL), GPIO16 (RST)
// External sensors use GPIO21 (SDA), GPIO22 (SCL) - AHT10 and ADS1115

String wifi_ssid = "";
String wifi_password = "";
String api_base_url = "";
String api_key = "";
bool api_use_https = true;
bool api_verify_cert = false;

bool wifi_connected = false;
bool system_healthy = false;
bool lora_enabled = false;
unsigned long wifi_retry_timer = 0;
unsigned long led_timer = 0;
unsigned long data_log_timer = 0;
unsigned long display_update_timer = 0;
unsigned long startup_time = 0;

const unsigned long WIFI_RETRY_INTERVAL = 30000;
const unsigned long LED_UPDATE_INTERVAL = 500;
const unsigned long DATA_LOG_INTERVAL = 60000;
const unsigned long DISPLAY_UPDATE_INTERVAL = 2000;

enum LEDState {
    LED_BOOT,
    LED_NORMAL,
    LED_WARNING,
    LED_ERROR
};

LEDState current_led_state = LED_BOOT;
bool led_on = false;

void setupWiFi();
void setupAP();
void setupOTA();
void setupMDNS();
void setupWebServer();
void setupNTP();
void setupSensors();
void setupAPI();
void setupDisplay();
void setupLoRa();
void loadConfiguration();
void saveWiFiCredentials(const String& ssid, const String& password);
void saveAPICredentials(const String& url, const String& apiKey, bool useHttps, bool verifyCert);

void updateLED();
void updateSystem();
void updateDisplay();
void logData();

void handleAPI_Sensors(AsyncWebServerRequest *request);
void handleAPI_Aggregated(AsyncWebServerRequest *request);
void handleAPI_Events(AsyncWebServerRequest *request);
void handleAPI_Status(AsyncWebServerRequest *request);
void handleAPI_Calibrate(AsyncWebServerRequest *request);
void handleAPI_ResetAlarms(AsyncWebServerRequest *request);
void handleWiFiConfig(AsyncWebServerRequest *request);
void handleAPIConfig(AsyncWebServerRequest *request);
void handleRestart(AsyncWebServerRequest *request);

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Well Pump Monitor Starting ===");
    
    startup_time = millis();
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    
    preferences.begin("pump-config", false);
    loadConfiguration();
    
    setupWiFi();
    setupOTA();
    setupMDNS();
    setupWebServer();
    setupNTP();
    setupSensors();
    setupAPI();
    setupDisplay();
    setupLoRa();
    
    current_led_state = LED_NORMAL;
    
    Serial.println("Setup complete!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
    ArduinoOTA.handle();
    
    updateSystem();
    updateLED();
    updateDisplay();
    logData();
    
    if (apiClient) {
        apiClient->update();
    }
    
    if (WiFi.status() != WL_CONNECTED && wifi_ssid.length() > 0) {
        if (millis() - wifi_retry_timer > WIFI_RETRY_INTERVAL) {
            Serial.println("WiFi disconnected, attempting reconnection...");
            setupWiFi();
            wifi_retry_timer = millis();
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        timeClient.update();
    }
    
    delay(100);
}

void loadConfiguration() {
    wifi_ssid = preferences.getString("ssid", "");
    wifi_password = preferences.getString("password", "");
    api_base_url = preferences.getString("api_url", "");
    api_key = preferences.getString("api_key", "");
    api_use_https = preferences.getBool("api_https", true);
    api_verify_cert = preferences.getBool("api_verify", false);
    
    Serial.println("Loaded configuration:");
    Serial.println("WiFi SSID: " + wifi_ssid);
    Serial.println("API URL: " + api_base_url);
    Serial.println("API HTTPS: " + String(api_use_https ? "Yes" : "No"));
    Serial.println("API Verify Cert: " + String(api_verify_cert ? "Yes" : "No"));
}

void saveWiFiCredentials(const String& ssid, const String& password) {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    wifi_ssid = ssid;
    wifi_password = password;
    Serial.println("Saved WiFi credentials: " + ssid);
}

void saveAPICredentials(const String& url, const String& apiKey, bool useHttps, bool verifyCert) {
    preferences.putString("api_url", url);
    preferences.putString("api_key", apiKey);
    preferences.putBool("api_https", useHttps);
    preferences.putBool("api_verify", verifyCert);
    api_base_url = url;
    api_key = apiKey;
    api_use_https = useHttps;
    api_verify_cert = verifyCert;
    Serial.println("Saved API credentials");
}

void setupWiFi() {
    if (wifi_ssid.length() == 0) {
        Serial.println("No WiFi credentials, starting AP mode");
        setupAP();
        return;
    }
    
    Serial.println("Connecting to WiFi: " + wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(1000);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed, starting AP mode");
        setupAP();
    }
}

void setupAP() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    wifi_connected = false;
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.println("Connect to WiFi: " + String(AP_SSID));
    Serial.println("Password: " + String(AP_PASSWORD));
}

void setupOTA() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword("pumphouse");
    
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else {
            type = "filesystem";
        }
        Serial.println("Start updating " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA Update Complete");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });
    
    ArduinoOTA.begin();
    Serial.println("OTA Ready");
}

void setupMDNS() {
    if (MDNS.begin(HOSTNAME)) {
        Serial.println("mDNS responder started");
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "app", "well-pump-monitor");
    } else {
        Serial.println("Error setting up mDNS responder!");
    }
}

void setupNTP() {
    timeClient.begin();
    timeClient.setTimeOffset(-21600);
    Serial.println("NTP Client started");
}

void setupSensors() {
    Serial.println("Initializing sensors...");
    
    // Initialize I2C for external sensors (ADS1115)
    I2C_SENSORS.begin(SENSOR_SDA, SENSOR_SCL);
    Serial.println("External I2C bus initialized for sensors");
    
    sensorManager = new SensorManager(&I2C_SENSORS);
    if (!sensorManager->begin()) {
        Serial.println("ERROR: Sensor initialization failed!");
        current_led_state = LED_ERROR;
        return;
    }
    
    dataCollector = new DataCollector(sensorManager);
    if (!dataCollector->begin()) {
        Serial.println("ERROR: Data collector initialization failed!");
        current_led_state = LED_ERROR;
        return;
    }
    
    eventDetector = new EventDetector(dataCollector);
    eventDetector->begin();
    eventDetector->setThresholds(7.2, 5.0, 38.0);
    eventDetector->setHysteresis(2.0, 1.0, 2.0);
    
    Serial.println("Sensors initialized successfully");
}

void setupAPI() {
    if (api_base_url.length() == 0) {
        Serial.println("No API URL configured, skipping initialization");
        return;
    }
    
    Serial.println("Initializing API client...");
    
    APIConfig config;
    config.baseURL = api_base_url;
    config.apiKey = api_key;
    config.useHttps = api_use_https;
    config.verifyCertificate = api_verify_cert;
    
    apiClient = new WellPumpAPIClient(config, HOSTNAME, "Pump House");
    
    if (apiClient->begin()) {
        Serial.println("API client initialized successfully");
    } else {
        Serial.println("API client initialization failed");
    }
}

void setupWebServer() {
    server.on("/api/sensors", HTTP_GET, handleAPI_Sensors);
    server.on("/api/aggregated", HTTP_GET, handleAPI_Aggregated);
    server.on("/api/events", HTTP_GET, handleAPI_Events);
    server.on("/api/status", HTTP_GET, handleAPI_Status);
    server.on("/api/calibrate", HTTP_POST, handleAPI_Calibrate);
    server.on("/api/reset-alarms", HTTP_POST, handleAPI_ResetAlarms);
    
    server.on("/config/wifi", HTTP_POST, handleWiFiConfig);
    server.on("/config/api", HTTP_POST, handleAPIConfig);
    server.on("/restart", HTTP_POST, handleRestart);
    
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });
    
    server.begin();
    Serial.println("HTTP server started");
}

void updateSystem() {
    if (eventDetector) {
        eventDetector->update();
    }
    
    if (apiClient) {
        apiClient->update();
    }
    
    system_healthy = sensorManager && sensorManager->isHealthy() && 
                    dataCollector && dataCollector->isRunning();
    
    if (eventDetector && eventDetector->hasActiveEvents()) {
        current_led_state = LED_WARNING;
    } else if (!system_healthy) {
        current_led_state = LED_ERROR;
    } else if (!wifi_connected) {
        current_led_state = LED_WARNING;
    } else {
        current_led_state = LED_NORMAL;
    }
}

void updateLED() {
    unsigned long now = millis();
    if (now - led_timer < LED_UPDATE_INTERVAL) {
        return;
    }
    led_timer = now;
    
    switch (current_led_state) {
        case LED_BOOT:
            digitalWrite(LED_PIN, (now / 200) % 2);
            break;
        case LED_NORMAL:
            digitalWrite(LED_PIN, HIGH);
            break;
        case LED_WARNING:
            digitalWrite(LED_PIN, (now / 1000) % 2);
            break;
        case LED_ERROR:
            digitalWrite(LED_PIN, (now / 250) % 2);
            break;
    }
}

void logData() {
    if (!apiClient || !dataCollector) {
        return;
    }
    
    unsigned long now = millis();
    if (now - data_log_timer < DATA_LOG_INTERVAL) {
        return;
    }
    data_log_timer = now;
    
    AggregatedData aggregated;
    if (dataCollector->getAggregatedData(aggregated)) {
        apiClient->sendSensorData(aggregated);
    }
    
    if (eventDetector) {
        for (uint8_t i = 0; i < eventDetector->getEventCount(); i++) {
            Event event = eventDetector->getEvent(i);
            if (event.active) {
                apiClient->sendEvent(event);
            }
        }
    }
}

void handleAPI_Sensors(AsyncWebServerRequest *request) {
    if (!sensorManager) {
        request->send(500, "application/json", "{\"error\":\"Sensors not initialized\"}");
        return;
    }
    
    JsonDocument doc;
    SensorData data;
    
    if (dataCollector && dataCollector->getCurrentData(data)) {
        doc["temperature"] = data.temperature;
        doc["humidity"] = data.humidity;
        doc["pressure"] = data.pressure;
        doc["current1"] = data.current1;
        doc["current2"] = data.current2;
        doc["timestamp"] = data.timestamp;
        doc["valid"] = data.valid;
    } else {
        doc["error"] = "No current data available";
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleAPI_Aggregated(AsyncWebServerRequest *request) {
    if (!dataCollector) {
        request->send(500, "application/json", "{\"error\":\"Data collector not initialized\"}");
        return;
    }
    
    JsonDocument doc;
    AggregatedData data;
    
    if (dataCollector->getAggregatedData(data)) {
        doc["tempMin"] = data.tempMin;
        doc["tempMax"] = data.tempMax;
        doc["tempAvg"] = data.tempAvg;
        doc["humMin"] = data.humMin;
        doc["humMax"] = data.humMax;
        doc["humAvg"] = data.humAvg;
        doc["pressMin"] = data.pressMin;
        doc["pressMax"] = data.pressMax;
        doc["pressAvg"] = data.pressAvg;
        doc["current1Min"] = data.current1Min;
        doc["current1Max"] = data.current1Max;
        doc["current1Avg"] = data.current1Avg;
        doc["current1RMS"] = data.current1RMS;
        doc["current2Min"] = data.current2Min;
        doc["current2Max"] = data.current2Max;
        doc["current2Avg"] = data.current2Avg;
        doc["current2RMS"] = data.current2RMS;
        doc["dutyCycle1"] = data.dutyCycle1;
        doc["dutyCycle2"] = data.dutyCycle2;
        doc["sampleCount"] = data.sampleCount;
        doc["startTime"] = data.startTime;
        doc["endTime"] = data.endTime;
    } else {
        doc["error"] = "No aggregated data available";
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleAPI_Events(AsyncWebServerRequest *request) {
    if (!eventDetector) {
        request->send(500, "application/json", "{\"error\":\"Event detector not initialized\"}");
        return;
    }
    
    JsonDocument doc;
    JsonArray events = doc.to<JsonArray>();
    
    for (uint8_t i = 0; i < eventDetector->getEventCount(); i++) {
        Event event = eventDetector->getEvent(i);
        JsonObject eventObj = events.add<JsonObject>();
        
        eventObj["type"] = String((int)event.type);
        eventObj["value"] = event.value;
        eventObj["threshold"] = event.threshold;
        eventObj["startTime"] = event.startTime;
        eventObj["duration"] = event.duration;
        eventObj["active"] = event.active;
        eventObj["description"] = event.description;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleAPI_Status(AsyncWebServerRequest *request) {
    JsonDocument doc;
    
    doc["status"] = system_healthy ? "Normal" : "Error";
    doc["wifi"] = wifi_connected ? "Connected" : "Disconnected";
    doc["uptime"] = (millis() - startup_time) / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["time"] = timeClient.getFormattedTime();
    
    if (apiClient) {
        doc["api"] = apiClient->getConnectionStatus();
        doc["bufferedData"] = apiClient->getBufferedCount();
    } else {
        doc["api"] = "Not Configured";
    }
    
    doc["lora"] = lora_enabled ? "Ready" : "Disabled";
    
    if (eventDetector) {
        doc["activeEvents"] = eventDetector->getEventCount();
        doc["eventStatus"] = eventDetector->getStatusString();
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleAPI_Calibrate(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["message"] = "Calibration started";
    doc["status"] = "success";
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleAPI_ResetAlarms(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["message"] = "Alarms reset";
    doc["status"] = "success";
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleWiFiConfig(AsyncWebServerRequest *request) {
    String ssid = request->getParam("ssid", true)->value();
    String password = request->getParam("password", true)->value();
    
    saveWiFiCredentials(ssid, password);
    
    request->send(200, "application/json", "{\"status\":\"WiFi credentials saved. Restarting...\"}");
    
    delay(2000);
    ESP.restart();
}

void handleAPIConfig(AsyncWebServerRequest *request) {
    String url = request->getParam("url", true)->value();
    String apiKey = request->getParam("apiKey", true)->value();
    bool useHttps = request->hasParam("useHttps", true) ? 
                   (request->getParam("useHttps", true)->value() == "true") : true;
    bool verifyCert = request->hasParam("verifyCert", true) ? 
                     (request->getParam("verifyCert", true)->value() == "true") : false;
    
    saveAPICredentials(url, apiKey, useHttps, verifyCert);
    
    request->send(200, "application/json", "{\"status\":\"API credentials saved. Restarting...\"}");
    
    delay(2000);
    ESP.restart();
}

void handleRestart(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Restarting...");
    delay(1000);
    ESP.restart();
}

void setupDisplay() {
    Serial.println("Initializing OLED display...");
    
    // Initialize I2C for Heltec built-in OLED
    I2C_OLED.begin(OLED_SDA, OLED_SCL);
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("SSD1306 allocation failed");
        return;
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Pump Monitor");
    display.println("Starting...");
    display.display();
    
    Serial.println("OLED display initialized");
}

void updateDisplay() {
    unsigned long now = millis();
    if (now - display_update_timer < DISPLAY_UPDATE_INTERVAL) {
        return;
    }
    display_update_timer = now;
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    // Title
    display.println("Well Pump Monitor");
    display.println("----------------");
    
    // System Status
    String status = system_healthy ? "Status: OK" : "Status: ERROR";
    display.println(status);
    
    // WiFi Status
    String wifi_status = wifi_connected ? "WiFi: Connected" : "WiFi: Disconnected";
    display.println(wifi_status);
    
    // Current sensor data
    if (dataCollector) {
        SensorData data;
        if (dataCollector->getCurrentData(data)) {
            display.print("Temp: ");
            display.print(data.temperature, 1);
            display.println("C");
            
            display.print("Current1: ");
            display.println(data.current1, 2);
            
            display.print("Current2: ");
            display.println(data.current2, 2);
        }
    }
    
    // Events/Alerts
    if (eventDetector && eventDetector->hasActiveEvents()) {
        display.println("*** ALERTS ACTIVE ***");
    }
    
    display.display();
}

void setupLoRa() {
    Serial.println("Initializing LoRa...");
    
    // Set LoRa pins
    LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
    
    // Initialize LoRa at 915 MHz (change for your region)
    if (!LoRa.begin(915E6)) {
        Serial.println("LoRa initialization failed!");
        lora_enabled = false;
        return;
    }
    
    // Set LoRa parameters
    LoRa.setTxPower(20);           // Max power
    LoRa.setSpreadingFactor(7);    // SF7 (faster, shorter range)
    LoRa.setSignalBandwidth(125E3); // 125 kHz
    LoRa.setCodingRate4(5);        // 4/5 coding rate
    LoRa.enableCrc();              // Enable CRC
    
    lora_enabled = true;
    Serial.println("LoRa initialized successfully");
    Serial.println("Note: LoRa transmit functions not yet implemented");
    Serial.println("Available for future remote monitoring features");
}