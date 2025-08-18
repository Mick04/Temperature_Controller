// ==================================================
// File: src/MQTTManager.cpp
// ==================================================

#include "MQTTManager.h"
#include "TemperatureSensors.h"
#include "TimeManager.h"
#include "GetShedual.h"
#include <WiFi.h>

// MQTT Client setup
WiFiClientSecure wifiClientSecure;
PubSubClient mqttClient(wifiClientSecure);

// Global MQTT status
MQTTState mqttStatus = MQTT_STATE_DISCONNECTED;

// Client ID for MQTT connection
String clientId = "ESP32-TemperatureController-";

// Previous temperature values for change detection
static float prevTempRed = NAN;
static float prevTempBlue = NAN;
static float prevTempGreen = NAN;
static bool firstReading = true;

void initMQTT()
{
    Serial.println("Initializing MQTT Manager...");

    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected, cannot initialize MQTT");
        mqttStatus = MQTT_STATE_DISCONNECTED;
        return;
    }

    // Generate unique client ID
    clientId += String(WiFi.macAddress());
    clientId.replace(":", "");

    Serial.print("MQTT Client ID: ");
    Serial.println(clientId);

    // Configure secure WiFi client for TLS connection
    wifiClientSecure.setInsecure(); // For testing - in production, use proper certificates

    // Set MQTT server and callback
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT_TLS);
    mqttClient.setCallback(onMQTTMessage);

    // Set buffer size for larger messages
    mqttClient.setBufferSize(512);

    Serial.println("MQTT Manager initialized");
}

void handleMQTT()
{
    // Don't proceed if WiFi is not connected
    if (WiFi.status() != WL_CONNECTED)
    {
        mqttStatus = MQTT_STATE_DISCONNECTED;
        return;
    }

    // Try to connect if not connected
    if (!mqttClient.connected())
    {
        mqttStatus = MQTT_STATE_CONNECTING;
        if (connectToMQTT())
        {
            mqttStatus = MQTT_STATE_CONNECTED;
        }
        else
        {
            mqttStatus = MQTT_STATE_ERROR;
            // Rate limit connection attempts
            static unsigned long lastConnectAttempt = 0;
            if (millis() - lastConnectAttempt > 30000) // Try every 30 seconds
            {
                Serial.println("MQTT connection failed, will retry in 30 seconds");
                lastConnectAttempt = millis();
            }
        }
    }
    else
    {
        mqttStatus = MQTT_STATE_CONNECTED;
        // Keep the connection alive
        mqttClient.loop();
    }
}

bool connectToMQTT()
{
    Serial.print("Connecting to MQTT broker: ");
    Serial.println(MQTT_SERVER);

    // Attempt to connect with credentials
    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD))
    {
        Serial.println("MQTT connected successfully!");

        // Subscribe to control topics (for receiving commands)
        mqttClient.subscribe("esp32/control/+");
        mqttClient.subscribe("esp32/commands/+");
        
        // Subscribe to new schedule structure topics
        mqttClient.subscribe("esp32/control/schedule/am/amTemperature");
        mqttClient.subscribe("esp32/control/schedule/pm/pmTemperature");
        mqttClient.subscribe("esp32/control/schedule/am/amHours");
        mqttClient.subscribe("esp32/control/schedule/am/amMinutes");
        mqttClient.subscribe("esp32/control/schedule/pm/pmHours");
        mqttClient.subscribe("esp32/control/schedule/pm/pmMinutes");

        // Publish connection status
        publishSingleValue(TOPIC_STATUS, "online");

        return true;
    }
    else
    {
        Serial.print("MQTT connection failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" retrying...");
        return false;
    }
}

void publishSensorData()
{
    if (mqttStatus != MQTT_STATE_CONNECTED)
    {
        Serial.println("MQTT not connected, cannot publish sensor data");
        return;
    }

    // Get temperature readings
    float tempRed = getTemperature(0);   // Red sensor
    float tempBlue = getTemperature(1);  // Blue sensor
    float tempGreen = getTemperature(2); // Green sensor

    // Check if any temperature has changed (or this is the first reading)
    bool hasChanged = firstReading ||
                      (abs(tempRed - prevTempRed) > 1.0 && !isnan(tempRed)) ||
                      (abs(tempBlue - prevTempBlue) > 1.0 && !isnan(tempBlue)) ||
                      (abs(tempGreen - prevTempGreen) > 1.0 && !isnan(tempGreen)) ||
                      (isnan(tempRed) != isnan(prevTempRed)) ||
                      (isnan(tempBlue) != isnan(prevTempBlue)) ||
                      (isnan(tempGreen) != isnan(prevTempGreen));

    if (!hasChanged)
    {
        Serial.println("No temperature changes detected, skipping MQTT publish");
        return;
    }

    Serial.println("Temperature change detected, publishing sensor data to MQTT...");

    // Update previous values
    prevTempRed = tempRed;
    prevTempBlue = tempBlue;
    prevTempGreen = tempGreen;
    firstReading = false;

    // Calculate average temperature
    float avgTemp = 0;
    int validSensors = 0;

    if (!isnan(tempRed))
    {
        publishSingleValue(TOPIC_TEMP_RED, round(tempRed));
        avgTemp += tempRed;
        validSensors++;
    }

    if (!isnan(tempBlue))
    {
        publishSingleValue(TOPIC_TEMP_BLUE, round(tempBlue));
        avgTemp += tempBlue;
        validSensors++;
    }

    if (!isnan(tempGreen))
    {
        publishSingleValue(TOPIC_TEMP_GREEN, round(tempGreen));
        avgTemp += tempGreen;
        validSensors++;
    }

    // Publish average temperature
    if (validSensors > 0)
    {
        avgTemp /= validSensors;
        publishSingleValue(TOPIC_TEMP_AVG, round(avgTemp));
    }

    // Publish dummy current data (until current sensor is implemented)
    float dummyCurrent = random(0, 100) / 10.0; // Random current 0-10A
    publishSingleValue(TOPIC_CURRENT, dummyCurrent);

    // Also publish time and system data when temperature changes
    publishTimeData();
    publishSystemData();
}

void publishTimeData()
{
    if (mqttStatus != MQTT_STATE_CONNECTED)
    {
        Serial.println("MQTT not connected, cannot publish time data");
        return;
    }

    Serial.println("Publishing time data to MQTT...");

    // Get formatted time and date
    String timeStr = getFormattedTime();
    String dateStr = getFormattedDate();

    publishSingleValue(TOPIC_TIME, timeStr.c_str());
    publishSingleValue(TOPIC_DATE, dateStr.c_str());
}

void publishSystemData()
{
    if (mqttStatus != MQTT_STATE_CONNECTED)
    {
        Serial.println("MQTT not connected, cannot publish system data");
        return;
    }

    Serial.println("Publishing system data to MQTT...");

    // Publish WiFi signal strength
    int rssi = WiFi.RSSI();
    publishSingleValue(TOPIC_WIFI_RSSI, rssi);

    // Publish uptime in seconds
    unsigned long uptime = millis() / 1000;
    publishSingleValue(TOPIC_UPTIME, (int)uptime);

    // Publish system status
    publishSingleValue(TOPIC_STATUS, "online");
}

void publishSingleValue(const char *topic, float value)
{
    if (mqttStatus != MQTT_STATE_CONNECTED)
        return;

    String payload = String(value, 2); // 2 decimal places
    if (mqttClient.publish(topic, payload.c_str()))
    {
        Serial.print("Published to ");
        Serial.print(topic);
        Serial.print(": ");
        Serial.println(payload);
    }
    else
    {
        Serial.print("Failed to publish to ");
        Serial.println(topic);
    }
}

void publishSingleValue(const char *topic, int value)
{
    if (mqttStatus != MQTT_STATE_CONNECTED)
        return;

    String payload = String(value);
    if (mqttClient.publish(topic, payload.c_str()))
    {
        Serial.print("Published to ");
        Serial.print(topic);
        Serial.print(": ");
        Serial.println(payload);
    }
    else
    {
        Serial.print("Failed to publish to ");
        Serial.println(topic);
    }
}

void publishSingleValue(const char *topic, const char *value)
{
    if (mqttStatus != MQTT_STATE_CONNECTED)
        return;

    if (mqttClient.publish(topic, value))
    {
        Serial.print("Published to ");
        Serial.print(topic);
        Serial.print(": ");
        Serial.println(value);
    }
    else
    {
        Serial.print("Failed to publish to ");
        Serial.println(topic);
    }
}

void onMQTTMessage(char *topic, byte *payload, unsigned int length)
{
    // Convert payload to string
    String message = "";
    for (int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }

    Serial.print("MQTT message received on topic: ");
    Serial.println(topic);
    Serial.print("*****************Message: ");
    Serial.println(message);

    // Handle incoming MQTT commands here
    String topicStr = String(topic);

    if (topicStr.startsWith("esp32/control/"))
    {
        // Handle schedule updates
        if (topicStr.indexOf("schedule/") >= 0 || topicStr.indexOf("shedual/") >= 0)
        {
            handleScheduleUpdate(topic, message);
        }
        // Handle control commands
        else if (topicStr.endsWith("target_temperature"))
        {
            float targetTemp = message.toFloat();
            Serial.print("Setting target temperature to: ");
            Serial.println(targetTemp);
            // You can integrate this with Firebase or local control
        }
        else if (topicStr.endsWith("heater_enable"))
        {
            bool enable = (message == "true" || message == "1");
            Serial.print("Setting heater enable to: ");
            Serial.println(enable);
            // You can integrate this with relay control
        }
    }
    else if (topicStr.startsWith("esp32/commands/"))
    {
        // Handle system commands
        if (topicStr.endsWith("restart"))
        {
            Serial.println("Restart command received");
            ESP.restart();
        }
        else if (topicStr.endsWith("status"))
        {
            Serial.println("Status request received");
            publishSystemData();
            publishSensorData();
            publishTimeData();
        }
    }
}

MQTTState getMQTTStatus()
{
    return mqttStatus;
}

bool checkTemperatureChanges()
{
    // Get current temperature readings
    float tempRed = getTemperature(0);   // Red sensor
    float tempBlue = getTemperature(1);  // Blue sensor
    float tempGreen = getTemperature(2); // Green sensor

    // Check if any temperature has changed (or this is the first reading)
    // Using 0.1°C as the minimum change threshold to avoid noise
    bool hasChanged = firstReading ||
                      (abs(tempRed - prevTempRed) > 0.1 && !isnan(tempRed)) ||
                      (abs(tempBlue - prevTempBlue) > 0.1 && !isnan(tempBlue)) ||
                      (abs(tempGreen - prevTempGreen) > 0.1 && !isnan(tempGreen)) ||
                      (isnan(tempRed) != isnan(prevTempRed)) ||
                      (isnan(tempBlue) != isnan(prevTempBlue)) ||
                      (isnan(tempGreen) != isnan(prevTempGreen));

    return hasChanged;
}
