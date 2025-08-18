// Simple test file
#include "GetShedual.h"

float getAMTemperature() {
    return 22.0;
}

void fetchScheduleDataFromFirebase() {
    // Empty for now
}

void handleScheduleUpdate(const char *topic, const String &message) {
    // Empty for now
}

float getPMTemperature() {
    return 18.0;
}

String getAMTime() {
    return "07:00";
}

String getPMTime() {
    return "19:00";
}
