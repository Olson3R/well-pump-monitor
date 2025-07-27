#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "DataCollector.h"
#include "EventDetector.h"

class WellPumpMongoClient {
private:
    HTTPClient* httpClient;
    
    String mongoURL;
    String apiKey;
    String dataSource;
    String database;
    String sensorCollection;
    String eventCollection;
    String deviceName;
    String location;
    
    bool connected;
    bool initialized;
    
    unsigned long lastConnectionTest;
    unsigned long lastRetryTime;
    
    uint16_t retryCount;
    uint16_t maxRetries;
    
    static const unsigned long CONNECTION_TEST_INTERVAL = 300000;
    static const unsigned long RETRY_DELAY = 30000;
    static const unsigned long MAX_RETRY_DELAY = 300000;
    
    struct DataBuffer {
        AggregatedData data;
        unsigned long timestamp;
        bool valid;
    };
    
    DataBuffer* buffer;
    uint8_t bufferSize;
    uint8_t bufferIndex;
    uint8_t bufferedCount;
    
    static const uint8_t BUFFER_SIZE = 10;
    
public:
    WellPumpMongoClient(const String& url, const String& key, 
                       const String& dataSource, const String& db,
                       const String& device, const String& loc);
    ~WellPumpMongoClient();
    
    bool begin();
    bool isConnected() const { return connected; }
    bool isInitialized() const { return initialized; }
    
    void setCredentials(const String& url, const String& key, 
                       const String& dataSource, const String& db);
    
    bool testConnection();
    bool writeAggregatedData(const AggregatedData& data);
    bool writeEvent(const Event& event);
    
    bool flushBuffer();
    uint8_t getBufferedCount() const { return bufferedCount; }
    
    void update();
    
    String getConnectionStatus() const;
    String getLastError() const;
    
private:
    bool connect();
    void disconnect();
    
    bool writeDataDocument(const AggregatedData& data);
    bool writeEventDocument(const Event& event);
    
    void addToBuffer(const AggregatedData& data);
    bool processBuffer();
    
    unsigned long getRetryDelay() const;
    void resetRetryCount() { retryCount = 0; }
    
    bool validateConfiguration() const;
    
    // MongoDB Data API helper methods
    bool insertDocument(const String& collection, const String& document);
    String createSensorDocument(const AggregatedData& data);
    String createEventDocument(const Event& event);
    String formatTimestamp(unsigned long timestamp);
};