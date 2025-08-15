#include <Arduino.h>
#include "config.h"
#include <WiFi.h>

#include "WiFiManagerCustom.h"
#include "StatusLEDs.h"
#include "FirebaseService.h"
#include "TemperatureSensors.h"
#include "TimeManager.h"
#include "MQTTManager.h"

// put function declarations here:
int myFunction(int, int);
void updateHeaterControl();

// Global system status
SystemStatus systemStatus;

void setup()
{
  Serial.begin(115200);
  delay(1000); // Wait for Serial to initialize

  // Initialize system status
  systemStatus.heater = HEATER_OFF; // Start with heater off

  // Initialize relay pin for heater control
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Start with heater off

  // Initialize Status LEDs first
  initStatusLEDs();

  // Initialize temperature sensors
  initTemperatureSensors();

  // Initialize WiFi
  initWiFi(systemStatus);

  // Initialize MQTT, Time Manager (after WiFi)
  // Will be called in loop when WiFi is connected

  // Note: Firebase, MQTT and TimeManager will be initialized automatically when WiFi connects
  // via handleFirebase(), handleMQTT() and handleTimeManager() in the main loop
}

void loop()
{
  // Handle WiFi connection status
  handleWiFi(systemStatus);

  // Handle Firebase connection status (will initialize when WiFi is ready)
  handleFirebase(systemStatus);

  // Handle time management (will initialize when WiFi is ready)
  static bool timeManagerInitialized = false;
  if (systemStatus.wifi == CONNECTED && !timeManagerInitialized)
  {
    initTimeManager();
    timeManagerInitialized = true;
  }
  if (timeManagerInitialized)
  {
    handleTimeManager();
  }

  // Handle MQTT connection (will initialize when WiFi is ready)
  static bool mqttInitialized = false;
  if (systemStatus.wifi == CONNECTED && !mqttInitialized)
  {
    initMQTT();
    mqttInitialized = true;
  }
  if (mqttInitialized)
  {
    handleMQTT();
    systemStatus.mqtt = getMQTTStatus();
  }

  // Update LED status indicators
  updateLEDs(systemStatus);

  // If Firebase is connected, periodically push and fetch data
  static unsigned long lastDataSync = 0;
  if (systemStatus.firebase == FB_CONNECTED && millis() - lastDataSync > 15000) // Every 15 seconds
  {
    Serial.println("\n=== Data Sync ===");

    // Read all temperature sensors first
    readAllSensors();

    // Push sensor data to Firebase
    pushSensorValuesToFirebase();

    delay(1000); // Small delay between operations

    // Fetch control values from Firebase
    fetchControlValuesFromFirebase();

    // Update heater control based on temperature and Firebase settings
    updateHeaterControl();

    Serial.println("=== End Data Sync ===\n");
    lastDataSync = millis();
  }

  // If MQTT is connected, check for temperature changes and publish when needed
  static unsigned long lastMQTTCheck = 0;
  if (systemStatus.mqtt == MQTT_STATE_CONNECTED && millis() - lastMQTTCheck > 5000) // Check every 5 seconds
  {
    // Read all temperature sensors first
    readAllSensors();

    // Check if any temperature values have changed
    if (checkTemperatureChanges())
    {
      Serial.println("\n=== MQTT Publish (Temperature Change Detected) ===");

      // Publish sensor data (includes time and system data)
      publishSensorData();

      Serial.println("=== End MQTT Publish ===\n");
    }

    lastMQTTCheck = millis();
  } // Small delay to prevent excessive polling
  delay(100);
}

// put function definitions here:
int myFunction(int x, int y)
{
  return x + y; 
}

// Heater control function
void updateHeaterControl()
{
  // Get average temperature from sensors
  float tempRed = getTemperature(0);   // Red sensor
  float tempBlue = getTemperature(1);  // Blue sensor
  float tempGreen = getTemperature(2); // Green sensor

  float avgTemp = 0;
  int validSensors = 0;

  if (!isnan(tempRed))
  {
    avgTemp += tempRed;
    validSensors++;
  }
  if (!isnan(tempBlue))
  {
    avgTemp += tempBlue;
    validSensors++;
  }
  if (!isnan(tempGreen))
  {
    avgTemp += tempGreen;
    validSensors++;
  }

  if (validSensors > 0)
  {
    avgTemp /= validSensors;

    // Simple heater control logic (you can get target temp from Firebase)
    float targetTemp = 22.0; // Default target temperature
    float tolerance = 1.0;   // ±1°C tolerance

    if (avgTemp < (targetTemp - tolerance))
    {
      // Temperature too low, turn heater on
      digitalWrite(RELAY_PIN, HIGH);
      systemStatus.heater = HEATER_ON;
      Serial.print("Heater ON - Current: ");
      Serial.print(avgTemp);
      Serial.print("°C, Target: ");
      Serial.print(targetTemp);
      Serial.println("°C");
    }
    else if (avgTemp > (targetTemp + tolerance))
    {
      // Temperature too high, turn heater off
      digitalWrite(RELAY_PIN, LOW);
      systemStatus.heater = HEATER_OFF;
      Serial.print("Heater OFF - Current: ");
      Serial.print(avgTemp);
      Serial.print("°C, Target: ");
      Serial.print(targetTemp);
      Serial.println("°C");
    }
    // If within tolerance, maintain current state
  }
  else
  {
    // No valid sensors, turn heater off for safety
    digitalWrite(RELAY_PIN, LOW);
    systemStatus.heater = HEATER_ERROR;
    Serial.println("Heater ERROR - No valid temperature sensors");
  }
}