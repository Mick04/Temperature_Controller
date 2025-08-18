//==============================
// Heater Control
//==============================
#include <Arduino.h>
#include "HeaterControl.h"
#include "TemperatureSensors.h"
#include "GetShedual.h"
#include "config.h"
#include "TimeManager.h"
// External declarations
extern bool AmFlag;
extern SystemStatus systemStatus;

// Heater control function
void updateHeaterControl()
{
    // Serial.println("******************Updating Heater Control...**************");
    //float amTemp = getAMTemperature();
    float pmTemp = getPMTemperature();
    String amTime = getAMTime();
    String pmTime = getPMTime();

    float targetTemp = AmFlag ? amTemp : pmTemp;
    Serial.print("************* Target Temperature **************: ");
    Serial.println(targetTemp);
    Serial.print("pmTime ");
    Serial.println(pmTime);
    Serial.print("amTime ");
    Serial.println(amTime);
    Serial.print("pmTemp ");
    Serial.println(pmTemp);
    Serial.print("amTemp ");
    Serial.println(amTemp);
    Serial.println("*******************************");

    String targetTime = AmFlag ? amTime : pmTime;
    Serial.print("Target Time: ");
    Serial.println(targetTime);

    // ===== TEMPERATURE SENSOR READING =====
    // Read temperatures from all three DS18B20 sensors
    float tempRed = getTemperature(0);   // Red sensor (index 0)
    float tempBlue = getTemperature(1);  // Blue sensor (index 1)
    float tempGreen = getTemperature(2); // Green sensor (index 2)

    // ===== CALCULATE AVERAGE TEMPERATURE =====
    // We calculate the average from all working sensors for better accuracy
    // and redundancy in case one sensor fails
    //float avgTemp = 0;
    int validSensors = 0;

    // Check each sensor and add to average if reading is valid (not NaN)
    if (!isnan(tempRed))
    {
        amTemp += tempRed;
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

    // ===== HEATER CONTROL LOGIC =====
    if (validSensors > 0)
    {
        // Calculate the final average temperature
        avgTemp /= validSensors;

        // ===== HYSTERESIS CONTROL =====
        // Use a tolerance band to prevent rapid on/off switching (hysteresis)
        // This prevents the heater from constantly switching when near target temp
        float tolerance = 1.0; // ±1°C tolerance band around target temperature

        if (avgTemp < (targetTemp - tolerance))
        {
            // HEATER ON: Current temperature is below target minus tolerance
            // Example: Target=22°C, Tolerance=1°C → Turn ON when temp < 21°C
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
            // HEATER OFF: Current temperature is above target plus tolerance
            // Example: Target=22°C, Tolerance=1°C → Turn OFF when temp > 23°C
            digitalWrite(RELAY_PIN, LOW);
            systemStatus.heater = HEATER_OFF;
            Serial.print("Heater OFF - Current: ");
            Serial.print(avgTemp);
            Serial.print("°C, Target: ");
            Serial.print(targetTemp);
            Serial.println("°C");
        }
        // HYSTERESIS ZONE: If temperature is between (target-tolerance) and (target+tolerance),
        // maintain current heater state to prevent rapid switching
        // Example: Target=22°C, Tolerance=1°C → No change when temp is 21°C-23°C
    }
    else
    {
        // ===== SAFETY FALLBACK =====
        // No valid temperature sensors detected - turn heater off for safety
        // This prevents overheating if all sensors fail
        digitalWrite(RELAY_PIN, LOW);
        systemStatus.heater = HEATER_ERROR;
        Serial.println("Heater ERROR - No valid temperature sensors");
    }
}