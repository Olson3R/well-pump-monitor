#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "DataCollector.h"
#include "EventDetector.h"

struct APIConfig {
    String baseURL;
    String apiKey;
    bool useHttps;
    bool verifyCertificate;
};

struct DataBuffer {
    AggregatedData data;
    unsigned long timestamp;
    bool valid;
};

class WellPumpAPIClient {
private:
    String baseURL;
    String apiKey;
    String deviceName;
    String location;
    bool useHttps;
    bool verifyCertificate;
    
    HTTPClient* httpClient;
    WiFiClientSecure* secureClient;
    
    bool connected;
    bool initialized;
    unsigned long lastConnectionTest;
    unsigned long lastRetryTime;
    uint16_t retryCount;
    uint16_t maxRetries;
    int lastHttpStatusCode;
    
    static const uint16_t BUFFER_SIZE = 20;
    static const unsigned long CONNECTION_TEST_INTERVAL = 30000;
    static const unsigned long RETRY_DELAY = 5000;
    static const unsigned long MAX_RETRY_DELAY = 300000;
    
    DataBuffer* buffer;
    uint8_t bufferIndex;
    uint8_t bufferedCount;
    uint8_t bufferSize;
    
    void resetRetryCount() { retryCount = 0; }
    unsigned long getRetryDelay() const;
    
public:
    WellPumpAPIClient(const APIConfig& config, const String& device, const String& loc);
    ~WellPumpAPIClient();
    
    bool begin();
    bool testConnection();
    bool connect();
    void disconnect();
    
    void setCredentials(const APIConfig& config);
    bool validateConfiguration() const;
    
    // Data sending methods
    bool sendSensorData(const AggregatedData& data);
    bool sendEvent(const Event& event);
    
    // Buffer management
    void addToBuffer(const AggregatedData& data);
    bool processBuffer();
    bool flushBuffer();
    
    // Status methods
    void update();
    String getConnectionStatus() const;
    String getLastError() const;
    uint8_t getBufferedCount() const { return bufferedCount; }
    bool isConnected() const { return connected; }
    bool isInitialized() const { return initialized; }
    int getLastHttpStatusCode() const { return lastHttpStatusCode; }
    
private:
    // HTTP request methods
    bool makeRequest(const String& endpoint, const String& method, const String& payload);
    bool sendSensorDataToAPI(const AggregatedData& data);
    bool sendEventToAPI(const Event& event);
    
    // JSON formatting
    String createSensorJSON(const AggregatedData& data);
    String createEventJSON(const Event& event);
    String formatTimestamp(unsigned long timestamp);
    
    // Connection management
    bool setupHTTPClient();
    void cleanupHTTPClient();
};