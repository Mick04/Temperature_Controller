// ==================================================
// File: src/FirebaseService.h
// ==================================================

#pragma once
#include <Arduino.h>
#include "config.h"

struct SystemStatus;
void initFirebase(SystemStatus &status);
void handleFirebase(SystemStatus &status);
void pushSensorValuesToFirebase();
void fetchControlValuesFromFirebase();
void setControlValue(const char *path, float value);
void setControlValue(const char *path, bool value);
void setControlValue(const char *path, const char *value);
