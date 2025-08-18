#include <Arduino.h>
#include "config.h"
#include <WiFi.h>

#include "WiFiManagerCustom.h"
#include "StatusLEDs.h"
#include "FirebaseService.h"
#include "TemperatureSensors.h"
#include "TimeManager.h"
#include "MQTTManager.h"
#include "GetShedual.h"
#include "HeaterControl.h"

// put function declarations here:
int myFunction(int, int);
void updateHeaterControl();
bool AmFlag;
bool firstRun = true;
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

  // Initialize schedule manager
  //initScheduleManager();

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
  static bool initialScheduleFetched = false; // Track if we've done the initial schedule fetch
  
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

    // Fetch schedule data from Firebase ONLY once on startup
    if (!initialScheduleFetched)
    {
      Serial.println("*********======================**********");
      Serial.println("");
      Serial.println("Initial schedule data fetch from Firebase...");
      Serial.println("");
      Serial.println("*********======================**********");
      fetchScheduleDataFromFirebase();
      initialScheduleFetched = true;
      Serial.println("✅ Initial schedule fetch completed. Future updates will come via MQTT.");
    }

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
  // delay(100);
  // long rssi = WiFi.RSSI();
  // Serial.print("Signal strength (RSSI): ");
  // Serial.print(rssi);
  // Serial.println(" dBm");
}

// put function definitions here:
int myFunction(int x, int y)
{
  return x + y;
}