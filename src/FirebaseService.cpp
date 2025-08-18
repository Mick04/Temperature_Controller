// ==================================================
// File: src/FirebaseService.cpp
// ==================================================

#include "FirebaseService.h"
#include "config.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "TemperatureSensors.h"
#include "GetShedual.h"

// Provide the RTC and API key for RTDB
FirebaseData fbData; // Made non-static so other files can access it
static FirebaseConfig fbConfig;
static FirebaseAuth fbAuth;
static bool fbInitialized = false;

void initFirebase(SystemStatus &status)
{

    // Initialize only when WiFi connected
    if (WiFi.status() != WL_CONNECTED)
    {
        status.firebase = FB_CONNECTING;
        Serial.println("WiFi not connected, cannot initialize Firebase");
        return;
    }

    Serial.println("Initializing Firebase...");

    // Debug print credentials (remove in production)
    Serial.println("Firebase credentials:");
    Serial.print("API Key: ");
    Serial.println(FIREBASE_API_KEY);
    Serial.print("Database URL: ");
    Serial.println(FIREBASE_DATABASE_URL);

    // Clear any previous configuration
    fbConfig = FirebaseConfig();
    fbAuth = FirebaseAuth();

    // Set the API key and database URL
    fbConfig.api_key = FIREBASE_API_KEY;
    fbConfig.database_url = FIREBASE_DATABASE_URL;

    // Set the host explicitly (extract from database URL)
    fbConfig.host = "esp32-heater-controler-6d11f-default-rtdb.europe-west1.firebasedatabase.app";

    // Set timeout and retry settings
    fbConfig.timeout.serverResponse = 15 * 1000;   // 15 seconds
    fbConfig.timeout.socketConnection = 15 * 1000; // 15 seconds

    // For anonymous authentication, we need to sign in after initialization
    Serial.println("Attempting anonymous authentication...");

    // Initialize Firebase first
    Firebase.begin(&fbConfig, &fbAuth);
    Firebase.reconnectWiFi(true);

    // Wait for initialization
    delay(3000);

    // Try anonymous authentication
    Serial.println("Signing in anonymously...");
    if (Firebase.signUp(&fbConfig, &fbAuth, "", ""))
    {
        Serial.println("Anonymous sign-up successful");
    }
    else
    {
        Serial.print("Anonymous sign-up failed: ");
        Serial.println("Check Firebase project settings for anonymous auth");
    }

    // Wait longer for authentication
    delay(2000);

    // Test the connection immediately
    Serial.println("Testing Firebase connection...");

    // Try to write a simple test value instead of just checking ready()
    if (Firebase.RTDB.setString(&fbData, "/test/connection", "esp32_test"))
    {
        fbInitialized = true;
        status.firebase = FB_CONNECTED;
        Serial.println("Firebase initialized and connected successfully");
        Serial.println("Test write successful");

        // Now try to read back the data we just wrote
        Serial.println("Testing data retrieval...");
        if (Firebase.RTDB.getString(&fbData, "/test/connection"))
        {
            String retrievedValue = fbData.stringData();
            Serial.print("Retrieved value: ");
            Serial.println(retrievedValue);

            // Test reading a timestamp
            if (Firebase.RTDB.setTimestamp(&fbData, "/test/last_connection"))
            {
                Serial.println("Timestamp written successfully");
                if (Firebase.RTDB.getInt(&fbData, "/test/last_connection"))
                {
                    int timestamp = fbData.intData();
                    Serial.print("Connection timestamp: ");
                    Serial.println(timestamp);
                }
            }

            // Immediately fetch schedule data from Firebase - no defaults
            Serial.println("Fetching schedule data from Firebase...");
            fetchScheduleDataFromFirebase();
        }
        else
        {
            Serial.println("Read test failed:");
            Serial.print("Error: ");
            Serial.println(fbData.errorReason());
        }
    }
    else
    {
        // Try a different approach - check if we can read instead
        Serial.println("Write failed, trying read test...");
        if (Firebase.ready())
        {
            fbInitialized = true;
            status.firebase = FB_CONNECTED;
            Serial.println("Firebase ready - assuming connection is good");
        }
        else
        {
            // Don't set fbInitialized = false here - we'll try again later
            status.firebase = FB_ERROR;
            Serial.println("Firebase initialization failed - will retry later");
            Serial.print("Error: ");
            Serial.println(fbData.errorReason());
            Serial.print("HTTP Code: ");
            Serial.println(fbData.httpCode());
        }
    }
}

/**
 * Main Firebase connection handler - manages Firebase initialization and connection monitoring
 * This function should be called regularly in the main loop to maintain Firebase connectivity
 *
 * @param status Reference to SystemStatus struct to update Firebase connection state
 *
 * Responsibilities:
 * 1. Attempts Firebase initialization when not yet initialized and WiFi is connected
 * 2. Monitors ongoing Firebase connection health
 * 3. Updates system status flags for Firebase connectivity
 * 4. Implements rate limiting to prevent excessive initialization attempts
 */
void handleFirebase(SystemStatus &status)
{
    // === FIREBASE INITIALIZATION PHASE ===
    // If Firebase hasn't been initialized yet, try to initialize it
    if (!fbInitialized)
    {
        // Only attempt initialization if WiFi is connected
        if (WiFi.status() == WL_CONNECTED)
        {
            // Rate limit initialization attempts to avoid spam (every 30 seconds)
            static unsigned long lastInitAttempt = 0;
            if (millis() - lastInitAttempt > 30000)
            {
                Serial.println("****WiFi connected, initializing Firebase...");
                initFirebase(status); // Attempt Firebase initialization
                lastInitAttempt = millis();
            }
        }
        else
        {
            // WiFi not connected - set status and wait
            status.firebase = FB_CONNECTING;

            // Only print this message occasionally to avoid spam (every 5 seconds)
            static unsigned long lastWiFiMessage = 0;
            if (millis() - lastWiFiMessage > 5000)
            {
                Serial.println("WiFi not connected, waiting for connection...");
                lastWiFiMessage = millis();
            }
        }
        return; // Exit early if not initialized
    }

    // === CONNECTION MONITORING PHASE ===
    // Firebase is initialized, now monitor its connection health

    // Rate limit Firebase status checks to avoid excessive polling (every 10 seconds)
    static unsigned long lastStatusCheck = 0;
    if (millis() - lastStatusCheck < 10000)
    {
        return; // Skip this check cycle
    }
    lastStatusCheck = millis();

    // Check current Firebase connection status
    if (Firebase.ready())
    {
        // Firebase is ready and connected
        if (status.firebase != FB_CONNECTED)
        {
            // Status changed from disconnected to connected - log the recovery
            Serial.println("Firebase connected successfully");
        }
        status.firebase = FB_CONNECTED;
    }
    else
    {
        // Firebase connection is down
        status.firebase = FB_ERROR;
        Serial.println("Firebase connection lost");
    }
}

void pushSensorValuesToFirebase()
{
    if (!fbInitialized)
    {
        Serial.println("Firebase not initialized, cannot push data");
        return;
    }

    Serial.println("Pushing sensor data to Firebase...");

    // Get real temperature readings from sensors
    float temp0 = getTemperature(0); // Red sensor
    float temp1 = getTemperature(1); // Blue sensor
    float temp2 = getTemperature(2); // Green sensor

    // Calculate average temperature from connected sensors
    /*
    // float avgTemp = 0;
    // int validSensors = 0;

    // if (!isnan(temp0))
    // {
    //     avgTemp += temp0;
    //     validSensors++;
    // }
    // if (!isnan(temp1))
    // {
    //     avgTemp += temp1;
    //     validSensors++;
    // }
    // if (!isnan(temp2))
    // {
    //     avgTemp += temp2;
    //     validSensors++;
    // }

    // if (validSensors > 0)
    // {
    //     avgTemp /= validSensors;
    // }
    // else
    // {
    //     avgTemp = NAN; // No valid sensors
    // }
*/
    // Push individual sensor temperatures
    if (!isnan(temp0))
    {
        int roundedTemp0 = round(temp0);
        if (Firebase.RTDB.setInt(&fbData, "/sensors/temperature_red", roundedTemp0))
        {
            Serial.print("Red temperature pushed: ");
            Serial.print(roundedTemp0);
            Serial.println("°C");
        }
    }

    if (!isnan(temp1))
    {
        int roundedTemp1 = round(temp1);
        if (Firebase.RTDB.setInt(&fbData, "/sensors/temperature_blue", roundedTemp1))
        {
            Serial.print("Blue temperature pushed: ");
            Serial.print(roundedTemp1);
            Serial.println("°C");
        }
    }

    if (!isnan(temp2))
    {
        int roundedTemp2 = round(temp2);
        if (Firebase.RTDB.setInt(&fbData, "/sensors/temperature_green", roundedTemp2))
        {
            Serial.print("Green temperature pushed: ");
            Serial.print(roundedTemp2);
            Serial.println("°C");
        }
    }

    // Push average temperature
    // if (!isnan(avgTemp))
    // {
    //     int roundedAvgTemp = round(avgTemp);
    //     if (Firebase.RTDB.setInt(&fbData, "/sensors/temperature", roundedAvgTemp))
    //     {
    //         Serial.print("Average temperature pushed: ");
    //         Serial.print(roundedAvgTemp);
    //         Serial.println("°C");
    //     }
    // }
    // else
    // {
    //     Serial.println("No valid temperature sensors available");
    // }

    // Push dummy current data (until current sensor is implemented)
    float dummyCurrent = random(0, 100) / 10.0; // Random current 0-10A
    if (Firebase.RTDB.setFloat(&fbData, "/sensors/current", dummyCurrent))
    {
        Serial.print("Current pushed: ");
        Serial.print(dummyCurrent);
        Serial.println("A");
    }

    // Push system status
    String statusPath = "/system/status";
    if (Firebase.RTDB.setString(&fbData, statusPath.c_str(), "online"))
    {
        Serial.println("System status pushed: online");
    }

    // Push timestamp
    String timestampPath = "/system/last_update";
    if (Firebase.RTDB.setTimestamp(&fbData, timestampPath.c_str()))
    {
        Serial.println("Timestamp updated");
    }
}

void fetchControlValuesFromFirebase()
{
    if (!fbInitialized)
    {
        Serial.println("Firebase not initialized, cannot fetch data");
        return;
    }

    Serial.println("Fetching sensor data from Firebase for verification...");

    // Read current sensor data back for verification only
    if (Firebase.RTDB.getInt(&fbData, "/sensors/temperature"))
    {
        int currentTemp = fbData.intData();
        Serial.print("Average temperature reading: ");
        Serial.print(currentTemp);
        Serial.println("°C");
    }

    // Read individual sensor temperatures
    if (Firebase.RTDB.getInt(&fbData, "/sensors/temperature_red"))
    {
        int redTemp = fbData.intData();
        Serial.print("Red sensor reading: ");
        Serial.print(redTemp);
        Serial.println("°C");
    }

    if (Firebase.RTDB.getInt(&fbData, "/sensors/temperature_blue"))
    {
        int blueTemp = fbData.intData();
        Serial.print("Blue sensor reading: ");
        Serial.print(blueTemp);
        Serial.println("°C");
    }

    if (Firebase.RTDB.getInt(&fbData, "/sensors/temperature_green"))
    {
        int greenTemp = fbData.intData();
        Serial.print("Green sensor reading: ");
        Serial.print(greenTemp);
        Serial.println("°C");
    }

    if (Firebase.RTDB.getFloat(&fbData, "/sensors/current"))
    {
        float currentReading = fbData.floatData();
        Serial.print("Current sensor reading: ");
        Serial.print(currentReading);
        Serial.println("A");
    }

    // Read system status
    if (Firebase.RTDB.getString(&fbData, "/system/status"))
    {
        String systemStatus = fbData.stringData();
        Serial.print("System status: ");
        Serial.println(systemStatus);
    }

    // Read last update timestamp
    if (Firebase.RTDB.getInt(&fbData, "/system/last_update"))
    {
        int lastUpdate = fbData.intData();
        Serial.print("Last update timestamp: ");
        Serial.println(lastUpdate);
    }
}

// Helper functions to set control values
void setControlValue(const char *path, float value)
{
    if (!fbInitialized)
    {
        Serial.println("Firebase not initialized, cannot set control value");
        return;
    }

    if (Firebase.RTDB.setFloat(&fbData, path, value))
    {
        Serial.print("Control value set: ");
        Serial.print(path);
        Serial.print(" = ");
        Serial.println(value);
    }
    else
    {
        Serial.print("Failed to set control value: ");
        Serial.println(path);
    }
}

void setControlValue(const char *path, bool value)
{
    if (!fbInitialized)
    {
        Serial.println("Firebase not initialized, cannot set control value");
        return;
    }

    if (Firebase.RTDB.setBool(&fbData, path, value))
    {
        Serial.print("Control value set: ");
        Serial.print(path);
        Serial.print(" = ");
        Serial.println(value ? "true" : "false");
    }
    else
    {
        Serial.print("Failed to set control value: ");
        Serial.println(path);
    }
}

void setControlValue(const char *path, const char *value)
{
    if (!fbInitialized)
    {
        Serial.println("Firebase not initialized, cannot set control value");
        return;
    }

    if (Firebase.RTDB.setString(&fbData, path, value))
    {
        Serial.print("Control value set: ");
        Serial.print(path);
        Serial.print(" = ");
        Serial.println(value);
    }
    else
    {
        Serial.print("Failed to set control value: ");
        Serial.println(path);
    }
}
