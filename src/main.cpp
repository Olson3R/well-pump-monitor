#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
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
unsigned long last_data_send_time = 0;
unsigned long last_send_attempt_time = 0;
bool last_send_success = false;
uint32_t send_error_count = 0;
int last_http_status_code = -1;

const unsigned long WIFI_RETRY_INTERVAL = 120000;  // 2 minutes
const unsigned long LED_UPDATE_INTERVAL = 500;
const unsigned long DATA_LOG_INTERVAL = 60000;
const unsigned long DISPLAY_UPDATE_INTERVAL = 2000;
const unsigned long PAGE_SWITCH_INTERVAL = 5000;  // Switch pages every 5 seconds

enum LEDState {
    LED_BOOT,
    LED_NORMAL,
    LED_WARNING,
    LED_ERROR
};

LEDState current_led_state = LED_BOOT;
bool led_on = false;
uint8_t current_display_page = 0;

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
unsigned long getCurrentTimestamp();
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

void showBootProgress(const String& message);

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Well Pump Monitor Starting ===");
    
    startup_time = millis();
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Initialize display first
    setupDisplay();
    showBootProgress("Initializing...");
    
    showBootProgress("Loading SPIFFS...");
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        showBootProgress("SPIFFS Failed!");
        return;
    }
    
    showBootProgress("Loading Config...");
    preferences.begin("pump-config", false);
    loadConfiguration();
    
    showBootProgress("Connecting WiFi...");
    setupWiFi();
    
    showBootProgress("Starting mDNS...");
    setupMDNS();
    
    showBootProgress("Starting Web...");
    setupWebServer();
    
    showBootProgress("Setting up OTA...");
    setupOTA();
    
    showBootProgress("Setting up NTP...");
    setupNTP();
    
    showBootProgress("Init Sensors...");
    setupSensors();
    
    showBootProgress("Init API Client...");
    setupAPI();
    
    // showBootProgress("Init LoRa...");
    // setupLoRa();
    
    current_led_state = LED_NORMAL;
    
    showBootProgress("Ready!");
    Serial.println("Setup complete!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Give user time to see "Ready!" message
    delay(1000);
}

void loop() {
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
    
    // Update NTP time more frequently
    static unsigned long lastNTPUpdate = 0;
    if (WiFi.status() == WL_CONNECTED) {
        unsigned long now = millis();
        if (now - lastNTPUpdate > 300000) { // Update every 5 minutes (more respectful)
            bool success = timeClient.update();
            lastNTPUpdate = now;
            if (success) {
                Serial.printf("NTP updated: %s (epoch: %lu)\n", 
                             timeClient.getFormattedTime().c_str(), 
                             timeClient.getEpochTime());
            } else {
                Serial.println("NTP update failed");
            }
        }
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
        showBootProgress("No WiFi config");
        delay(500);
        setupAP();
        return;
    }
    
    Serial.println("Connecting to WiFi: " + wifi_ssid);
    // Use AP+STA mode to keep config portal accessible
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(HOSTNAME);
    
    // Ensure AP stays active with same credentials
    WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4);
    
    // Now try to connect as station
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
        // Show WiFi connection progress
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("Well Pump Monitor");
        display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
        
        display.setCursor(0, 20);
        display.println("Connecting WiFi:");
        display.setCursor(0, 30);
        display.println(wifi_ssid);
        
        // Progress dots
        display.setCursor(0, 45);
        for(int i = 0; i < (attempts % 4) + 1; i++) {
            display.print(".");
        }
        
        display.setCursor(0, 55);
        display.print("Attempt ");
        display.print(attempts + 1);
        display.print("/20");
        
        display.display();
        
        delay(1000);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        
        // Show WiFi connected status
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("Well Pump Monitor");
        display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
        
        display.setCursor(0, 20);
        display.println("WiFi Connected!");
        display.setCursor(0, 35);
        display.print("IP: ");
        display.println(WiFi.localIP());
        display.setCursor(0, 50);
        display.print("RSSI: ");
        display.print(WiFi.RSSI());
        display.print(" dBm");
        
        display.display();
        delay(1500); // Show IP briefly
    } else {
        Serial.println("\nWiFi connection failed, starting AP mode");
        showBootProgress("WiFi Failed!");
        delay(1000);
        setupAP();
    }
}

void setupAP() {
    // Disconnect and clear any previous WiFi settings
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Configure AP+STA mode to allow both AP and station
    WiFi.mode(WIFI_AP_STA);
    
    // Set up AP with explicit configuration
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    // Configure AP settings before starting
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    // Start AP with channel 6 and show SSID
    bool ap_started = WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4);
    wifi_connected = false;
    
    if (!ap_started) {
        Serial.println("ERROR: Failed to start AP mode!");
        ESP.restart();
    }
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP Started Successfully!");
    Serial.print("\nAP IP address: ");
    Serial.println(IP);
    Serial.println("Connect to WiFi: " + String(AP_SSID));
    Serial.println("Password: " + String(AP_PASSWORD));
    
    // Show AP mode on display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Well Pump Monitor");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
    
    display.setCursor(0, 20);
    display.println("AP Mode Active");
    display.setCursor(0, 30);
    display.print("SSID: ");
    display.println(AP_SSID);
    display.setCursor(0, 40);
    display.print("Pass: ");
    display.println(AP_PASSWORD);
    display.setCursor(0, 50);
    display.print("IP: ");
    display.println(IP);
    
    display.display();
}

void setupOTA() {
    // ElegantOTA setup with the AsyncWebServer
    ElegantOTA.begin(&server, "admin", "pumphouse");
    
    Serial.println("ElegantOTA Ready");
    Serial.println("Access OTA updates at: http://" + WiFi.localIP().toString() + "/update");
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
    timeClient.setTimeOffset(0); // UTC time for proper Unix timestamps
    
    // Force initial sync if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Synchronizing time with NTP server...");
        Serial.println("Trying multiple NTP servers...");
        
        // Try multiple NTP servers
        const char* ntpServers[] = {
            "pool.ntp.org",
            "time.nist.gov", 
            "time.google.com"
        };
        
        bool success = false;
        for (int server = 0; server < 3 && !success; server++) {
            Serial.printf("Trying NTP server: %s\n", ntpServers[server]);
            timeClient.setPoolServerName(ntpServers[server]);
            
            for (int attempts = 0; attempts < 5; attempts++) {
                if (timeClient.forceUpdate()) {
                    success = true;
                    Serial.printf("NTP sync successful with %s!\n", ntpServers[server]);
                    Serial.printf("Current time: %s (epoch: %lu)\n", 
                                 timeClient.getFormattedTime().c_str(),
                                 timeClient.getEpochTime());
                    break;
                }
                Serial.print(".");
                delay(2000);
            }
        }
        
        if (!success) {
            Serial.println("\nWarning: All NTP servers failed!");
        }
    } else {
        Serial.println("WiFi not connected, skipping NTP sync");
    }
    
    Serial.println("NTP Client started");
}

unsigned long getCurrentTimestamp() {
    if (WiFi.status() == WL_CONNECTED && timeClient.isTimeSet()) {
        unsigned long timestamp = timeClient.getEpochTime();
        // Debug: Show timestamp details
        Serial.printf("NTP Time: %lu (%s)\n", timestamp, timeClient.getFormattedTime().c_str());
        return timestamp;
    }
    Serial.println("Warning: NTP time not available, using millis()");
    return 0; // Return 0 if time is not synchronized
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
        Serial.print("Connection test failed. HTTP status: ");
        Serial.println(apiClient->getLastHttpStatusCode());
        Serial.println("Check API URL, network connectivity, and server status");
        
        // Test basic connectivity
        Serial.println("Testing basic connectivity...");
        HTTPClient testHttp;
        testHttp.begin("http://httpbin.org/get");
        int testCode = testHttp.GET();
        Serial.print("HTTP test to httpbin.org: ");
        Serial.println(testCode);
        testHttp.end();
        
        // Test HTTPS connectivity
        WiFiClientSecure testSecure;
        testSecure.setInsecure();
        HTTPClient testHttps;
        testHttps.begin(testSecure, "https://httpbin.org/get");
        int testHttpsCode = testHttps.GET();
        Serial.print("HTTPS test to httpbin.org: ");
        Serial.println(testHttpsCode);
        testHttps.end();
        
        // Still keep the client for later retry attempts
        // Don't set apiClient = nullptr here so it can retry
    }
}

void setupWebServer() {
    server.on("/api/sensors", HTTP_GET, handleAPI_Sensors);
    server.on("/api/aggregated", HTTP_GET, handleAPI_Aggregated);
    server.on("/api/events", HTTP_GET, handleAPI_Events);
    server.on("/api/status", HTTP_GET, handleAPI_Status);
    server.on("/api/calibrate", HTTP_GET, handleAPI_Calibrate);
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
    if (!dataCollector) {
        return;
    }
    
    if (!apiClient) {
        // Update tracking variables to show API not configured
        static unsigned long last_no_api_log = 0;
        unsigned long now = millis();
        if (now - last_no_api_log > 60000) { // Log once per minute
            Serial.println("API client not configured - no data sending");
            last_no_api_log = now;
        }
        return;
    }
    
    unsigned long now = millis();
    if (now - data_log_timer < DATA_LOG_INTERVAL) {
        return;
    }
    data_log_timer = now;
    
    AggregatedData aggregated;
    Serial.println("=== ATTEMPTING DATA SEND ===");
    if (dataCollector->getAggregatedData(aggregated)) {
        Serial.println("Got aggregated data, attempting to send...");
        last_send_attempt_time = millis();
        if (apiClient->sendSensorData(aggregated)) {
            last_data_send_time = millis();
            last_send_success = true;
            last_http_status_code = apiClient->getLastHttpStatusCode();
            Serial.println("Data sent successfully!");
        } else {
            last_send_success = false;
            send_error_count++;
            last_http_status_code = apiClient->getLastHttpStatusCode();
            Serial.print("ERROR: Failed to send sensor data to server. HTTP code: ");
            Serial.println(last_http_status_code);
        }
    } else {
        Serial.println("WARNING: No aggregated data available to send");
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
    doc["timeSync"] = timeClient.isTimeSet();
    doc["epochTime"] = timeClient.getEpochTime();
    
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
    if (!sensorManager) {
        request->send(500, "application/json", "{\"error\":\"Sensor manager not initialized\"}");
        return;
    }
    
    JsonDocument doc;
    
    // If it's a GET request, return current raw values for calibration
    if (request->method() == HTTP_GET) {
        doc["pressureRaw"] = sensorManager->getRawPressureVoltage();
        doc["current1Raw"] = sensorManager->getRawCurrent1Voltage();
        doc["current2Raw"] = sensorManager->getRawCurrent2Voltage();
        doc["pressureCurrent"] = sensorManager->getPressure();
        doc["current1Current"] = sensorManager->getCurrent1();
        doc["current2Current"] = sensorManager->getCurrent2();
        doc["status"] = "success";
    }
    // If it's a POST request, perform calibration
    else if (request->method() == HTTP_POST) {
        String sensor = "";
        float value = 0.0;
        
        if (request->hasParam("sensor", true) && request->hasParam("value", true)) {
            sensor = request->getParam("sensor", true)->value();
            value = request->getParam("value", true)->value().toFloat();
            
            if (sensor == "pressure") {
                sensorManager->calibratePressureAtValue(value);
                doc["message"] = "Pressure calibrated to " + String(value) + " PSI";
            } else if (sensor == "current1") {
                sensorManager->calibrateCurrent1AtValue(value);
                doc["message"] = "Current1 calibrated to " + String(value) + " A";
            } else if (sensor == "current2") {
                sensorManager->calibrateCurrent2AtValue(value);
                doc["message"] = "Current2 calibrated to " + String(value) + " A";
            } else {
                doc["error"] = "Invalid sensor: " + sensor;
                doc["status"] = "error";
            }
            
            if (doc.containsKey("message")) {
                doc["status"] = "success";
            }
        } else {
            doc["error"] = "Missing sensor or value parameter";
            doc["status"] = "error";
        }
    }
    
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
    
    // Clear display and show boot screen
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Well Pump");
    display.println("Monitor");
    
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.println("Version 1.0");
    display.setCursor(0, 50);
    display.println("Booting...");
    display.display();
    
    Serial.println("OLED display initialized");
    delay(500); // Brief pause to show boot screen
}

void showBootProgress(const String& message) {
    display.clearDisplay();
    
    // Title
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Well Pump Monitor");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
    
    // Boot status
    display.setCursor(0, 20);
    display.println("Booting:");
    
    // Current operation
    display.setCursor(0, 35);
    display.setTextSize(1);
    display.println(message);
    
    // Progress indicator (animated dots)
    static int dots = 0;
    display.setCursor(0, 50);
    for(int i = 0; i < (dots % 4); i++) {
        display.print(".");
    }
    dots++;
    
    display.display();
    delay(100); // Small delay to make progress visible
}

void updateDisplay() {
    unsigned long now = millis();
    if (now - display_update_timer < DISPLAY_UPDATE_INTERVAL) {
        return;
    }
    display_update_timer = now;
    
    // Switch pages every PAGE_SWITCH_INTERVAL
    static unsigned long page_switch_timer = 0;
    if (now - page_switch_timer > PAGE_SWITCH_INTERVAL) {
        current_display_page = (current_display_page + 1) % 2;
        page_switch_timer = now;
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    // Title with page indicator
    display.print("Well Pump Monitor ");
    display.print(current_display_page + 1);
    display.println("/2");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
    
    if (current_display_page == 0) {
        // Page 1: Sensor Data and Status
        display.setCursor(0, 15);
        
        // Only show system status if not healthy
        if (!system_healthy) {
            display.println("Status: ERROR");
        }
        
        // Only show WiFi status if disconnected
        if (!wifi_connected) {
            display.println("WiFi: Disconnected");
        }
        
        // Current sensor data
        if (dataCollector) {
            SensorData data;
            if (dataCollector->getCurrentData(data)) {
                display.print("Temp: ");
                display.print(data.temperature, 1);
                display.println("C");
                
                display.print("Pressure: ");
                display.print(data.pressure, 1);
                display.println(" PSI");
                
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
    } else {
        // Page 2: System Info
        display.setCursor(0, 15);
        
        // Current date and time
        if (WiFi.status() == WL_CONNECTED && timeClient.isTimeSet()) {
            unsigned long epochTime = timeClient.getEpochTime();
            struct tm *ptm = gmtime((time_t *)&epochTime);
            
            display.printf("Date: %02d/%02d/%04d\n", 
                          ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_year + 1900);
            display.print("Time: ");
            display.println(timeClient.getFormattedTime());
        } else {
            display.println("Date: No sync");
            display.println("Time: No sync");
        }
        
        // Current IP address
        if (wifi_connected) {
            display.print("IP: ");
            display.println(WiFi.localIP());
        } else {
            display.println("IP: Not connected");
        }
        
        // Last data send status
        if (!apiClient) {
            display.println("Send: No API config");
        } else if (!apiClient->isInitialized() || !apiClient->isConnected()) {
            display.print("Send: API error (");
            display.print(apiClient->getLastHttpStatusCode());
            display.println(")");
        } else if (last_send_attempt_time > 0) {
            if (last_send_success && last_data_send_time > 0) {
                unsigned long time_since_send = (now - last_data_send_time) / 1000;
                display.print("Send: ");
                if (time_since_send < 60) {
                    display.print(time_since_send);
                    display.print("s ago OK");
                } else if (time_since_send < 3600) {
                    display.print(time_since_send / 60);
                    display.print("m ago OK");
                } else {
                    display.print(time_since_send / 3600);
                    display.print("h ago OK");
                }
                if (last_http_status_code > 0) {
                    display.print(" (");
                    display.print(last_http_status_code);
                    display.println(")");
                } else {
                    display.println();
                }
            } else {
                display.print("Send: FAILED");
                if (last_http_status_code > 0) {
                    display.print(" (");
                    display.print(last_http_status_code);
                    display.println(")");
                } else {
                    display.println();
                }
            }
            
            // Show error count if there are any errors
            if (send_error_count > 0) {
                display.print("Send errors: ");
                display.println(send_error_count);
            }
        } else {
            display.println("Last send: Never");
        }
        
        // Uptime
        unsigned long uptime_seconds = (now - startup_time) / 1000;
        display.print("Uptime: ");
        if (uptime_seconds < 60) {
            display.print(uptime_seconds);
            display.println("s");
        } else if (uptime_seconds < 3600) {
            display.print(uptime_seconds / 60);
            display.println("m");
        } else {
            display.print(uptime_seconds / 3600);
            display.print("h ");
            display.print((uptime_seconds % 3600) / 60);
            display.println("m");
        }
        
        // Free heap
        display.print("Free RAM: ");
        display.print(ESP.getFreeHeap() / 1024);
        display.println("KB");
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