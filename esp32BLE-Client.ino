#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <RTClib.h>
#include <WiFi.h> 
#include "WiFiCreds.h"

#define DEBUG 1

#if DEBUG == 1
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#endif

const char* api_key = "bfe921e4-417c-4018-958b-7b099605abf3";
const char* device_type = "hub";


// non-volatile storage
Preferences preferences;

WiFiCreds wifiCreds; // Create an instance of WiFiCreds

// real time clock
RTC_DS3231 rtc;

// hold the config json object from api when called
// Specify the capacity of the JSON document. 
// The size depends on the expected size of your JSON data.
const size_t capacity = JSON_OBJECT_SIZE(10) + 500;
DynamicJsonDocument configJSON(capacity);

int timeslot_offset = 0; // Offset in minutes
const int cycleDuration = 2; // Duration of each cycle in minutes should be 15min, set to 1 for testing
esp_sleep_wakeup_cause_t wakeup_reason; // what cause the wake up of this device

int currentCycleNumber;
const int cycleNumber = 0;  // the cycle number that the system makes data available on


// Use the instance to access methods
const char* ssid = wifiCreds.getSSID(); 
const char* password = wifiCreds.getPassword(); 
// const char* ssid = wifiCreds.getOfficeSSID(); 
// const char* password = wifiCreds.getOfficePassword(); 


enum class programState: uint8_t {
  CONFIG_CHECK,
  SENSOR_CONFIG,
  BLE_CONNECT_READ_SAVE,
  MOBILE_CONNECT_TRANSMIT_DATA
};


DynamicJsonDocument parseJson(String jsonResponse) {
    // Create a JSON document object
    DynamicJsonDocument doc(1024); // Adjust size according to your response

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, jsonResponse);

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
    }
  return doc;
}

void constructEndpointUrl(char* buffer, size_t bufferLen, const char* baseEndPoint, const char* identifier, const char* action) {
  // debugln("constructEndpointUrl called");
    snprintf(buffer, bufferLen, "%s%s%s", baseEndPoint, identifier, action);
}

bool toggleConfigSensorsFlag() {
  // debugln("getHubConfig called");
    const char* baseConfigEndPoint = "https://beevibe-prod-7815d8f510b2.herokuapp.com/api/datacollection/hubconfig/";
    const char* action = "/toggle_sensor_config_flag/";

    char endPoint[256]; // Buffer to hold the complete endpoint URL
    constructEndpointUrl(endPoint, sizeof(endPoint), baseConfigEndPoint, api_key, action);
    debugln(endPoint);
    HTTPClient http;
    // Your HTTP GET request
    http.begin(endPoint);
    int httpResponseCode = http.PATCH(String(""));
    // check api actioned request
    if (httpResponseCode == 200) {
      http.end(); // Free resources
      return true;
    } else {
        debugln("HTTP Error: " + String(httpResponseCode));
    }
    
    http.end(); // Free resources
    
    return false;
}


String getHubConfig() {
  // debugln("getHubConfig called");
    const char* baseConfigEndPoint = "https://beevibe-prod-7815d8f510b2.herokuapp.com/api/datacollection/hubconfig/";
    // api_key = "bfe921e4-417c-4018-958b-7b099605abf3";
    const char* action = "/get_config/";

    char endPoint[256]; // Buffer to hold the complete endpoint URL
    constructEndpointUrl(endPoint, sizeof(endPoint), baseConfigEndPoint, api_key, action);
    debugln(endPoint);
    HTTPClient http;
    // Your HTTP GET request
    http.begin(endPoint);
    int httpResponseCode = http.GET();
    String response = http.getString(); // Get the response to the request
    
    http.end(); // Free resources
    
    return response;
}

int postErrorToAPI(const char* device_type, const char* device_id, const char* error_message) {
    // format json
    StaticJsonDocument<200> jsonDoc;
    jsonDoc["device_type"] = device_type; 
    jsonDoc["device_id"] = device_id;
    jsonDoc["error_message"] = error_message;
    jsonDoc["api_key"] = api_key;

    char jsonString[500];
    serializeJson(jsonDoc, jsonString);

    const char* deviceErrorReportsEndPoint = "https://beevibe-prod-7815d8f510b2.herokuapp.com/api/datacollection/deviceerrorreports/";

    // char endPoint[256]; // Buffer to hold the complete endpoint URL
    // constructEndpointUrl(endPoint, sizeof(endPoint), baseConfigEndPoint, "", "");
    // debugln(endPoint);

    HTTPClient http;
    http.begin(deviceErrorReportsEndPoint);

    // Set header for JSON content type
    http.addHeader("Content-Type", "application/json");

    // POST request with JSON payload
    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
        String response = http.getString(); // Get the response to the request
        debugln(response);
    } else {
        debugln("Error on sending POST: " + String(httpResponseCode));
    }

    http.end(); // Free resources
    return httpResponseCode;
}


void printDateTime(const DateTime& dt) {
    char dateTimeStr[20];
    snprintf(dateTimeStr, sizeof(dateTimeStr), "%04d-%02d-%02dT%02d:%02d:%02dZ", 
             dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
    debugln(dateTimeStr);
}

bool calibrateRTCTime(const DateTime& rtcTime, const char* apiTime) {

    DateTime currentAPITime = DateTime(apiTime);

    long timeDiff = abs((long)rtcTime.unixtime() - (long)currentAPITime.unixtime());


    // If the time difference is greater than 60 seconds, adjust the RTC
    if (timeDiff > 60) {
        debugln("########### The RTC time was different ##################");
        debug("API Time: ");
        printDateTime(currentAPITime);
        debug("RTC Time: ");
        printDateTime(rtcTime);
      // adjuct time
        rtc.adjust(currentAPITime);
        debugln("TIME ADJUSTED");
        return true; // Indicate that the RTC was adjusted
    }

    return false; // Indicate no adjustment was needed
}

// save sensor uuids to non-volatile storage
bool configureSensorUUIDs(const JsonArray& sensors) {
    debugln("configureSensorUUIDs called");

    char errorMessage[50];

    bool success = preferences.begin("sensor_storage", false); // Open NVS in RW mode
    if (!success) {
        int responseCode = postErrorToAPI(device_type, api_key, "Failed to open NVS storage");
        debug("Failed to open NVS storage");
        return false; // Exit if unable to open NVS
    }

    preferences.clear(); // Clear any previous entries
    int sensorIndex = 0;
    bool errorOccurred = false;

    for (JsonObject sensor : sensors) {
        const char* uuid = sensor["uuid"]; // Extract the UUID
        // Save each UUID with a unique key
        String key = "sensor" + String(sensorIndex);
        success = preferences.putString(key.c_str(), String(uuid));
        if (!success) {
            strncpy(errorMessage, "Failed to save UUID for a sensor", sizeof(errorMessage));
            debug("Failed to save UUID for sensor");
            debugln(sensorIndex);
            errorOccurred = true;
            break; // Exit the loop if error occurs
        }
        sensorIndex++;
        debug("Sensor UUID saved: ");
        debugln(uuid);
    }

    if (!errorOccurred) {
        // Save the count of sensors only if no errors occurred
        success = preferences.putInt("sensorCount", sensorIndex);
        if (!success) {
            strncpy(errorMessage, "Failed to save sensor count", sizeof(errorMessage));
            debugln("Failed to save sensor count!");
            return false;
        }
    }

    preferences.end(); // Close NVS storage

    if (errorOccurred) {
        // Handle error, e.g., retry, log error, send error message, etc.
        int responseCode = postErrorToAPI(device_type, api_key, errorMessage);
        debugln("Error occurred while configuring sensor UUIDs");
        return false;
    }

  return true;
}






void setup (){
    Serial.begin(115200);
      // SETUP RTC MODULE
    if (! rtc.begin()) {
      debugln("RTC module is NOT found");
      Serial.flush();
      while (1);
    }
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.println("Connecting to WiFi...");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("Connected to WiFi with IP: ");
    Serial.println(WiFi.localIP());
    debugln("Starting NimBLE Client");


    debugln("setup complete");
}


void loop (){
  programStateMachine();
  delay(3000);
}

//################### Finite state machine ############################
void programStateMachine() {
  // CONFIG_CHECK,
  // SENSOR_CONFIG,
  // BLE_CONNECT_READ_SAVE,
  // MOBILE_CONNECT_TRANSMIT_DATA
  bool actionComplete = toggleConfigSensorsFlag();
  debugln("############ config sensors flag toggled at the begining of program state for debug reasons: remove this when tested");

    DateTime now = rtc.now();
    int calculatedCurrentCycleNumber = getCurrentCycleNumber(now);
    debug("calculatedCurrentCycleNumber: ");
    debugln(calculatedCurrentCycleNumber);

    if(calculatedCurrentCycleNumber == cycleNumber){
        static programState currentState = programState::BLE_CONNECT_READ_SAVE;
    }

    static programState currentState = programState::CONFIG_CHECK;

    switch (currentState) {
      case programState::CONFIG_CHECK: {
            debugln("CONFIG_CHECK called");
            // get cofig from api
            String configResponse = getHubConfig();
            configJSON  = parseJson(configResponse);
            // ##### check time and adjust is necessary
            const char* apiTime = configJSON["current_time"];
            DateTime rtcTime = rtc.now();
            if (apiTime != nullptr) {
                bool timeReset = calibrateRTCTime(rtcTime, apiTime);
            } else {
              // TODO: implement error codes and message back to api 
                debugln("Current time not found in JSON");
            }
            // end time calibration
            // ###### check for sensor config flag from api
            if(configJSON["config_sensors"] == true) {
              currentState = programState::SENSOR_CONFIG;
            }

            break;
        }
        case programState::SENSOR_CONFIG: {
            bool configSensorsResponse;
            debugln("SENSOR_CONFIG called");
            // look for and process sensor uuids
            if (configJSON.containsKey("sensors")) {
                JsonArray sensors = configJSON["sensors"];
                configSensorsResponse = configureSensorUUIDs(sensors);
                debug("configSensorsResponse: ");
                debugln(configSensorsResponse);
            } else {
                debugln("No sensors found in JSON");
            }
            if (!configSensorsResponse ){
              // TODO: toggle config flag in api to accept bool value
              debugln("Config sensors failed");
            }

            debugln("Sensor config called and completed without error");
            delay(2000);
            debugln("sleep now");
            esp_sleep_enable_timer_wakeup(15 * 1000000); 
            esp_deep_sleep_start();
            break;
        }
        case programState::BLE_CONNECT_READ_SAVE: {
            debugln("BLE_CONNECT_READ_SAVE called");
            break;
        }
        default:
            debugln("Switch case default called....something terrible has happened");
            break;
    }
}

int getCurrentCycleNumber(const DateTime& currentTime) {
    int currentMinute = currentTime.minute();
    int currentSecond = currentTime.second();

    // Calculate the remaining seconds in the current 8-minute window
    int totalSecondsRemaining = (4 - (currentMinute % 4)) * 60 - currentSecond;

    // If less than 10 seconds remain, treat as the beginning of the next cycle
    if (totalSecondsRemaining <= 10) {
        return 0;
    }

    // Otherwise, calculate the cycle number normally
    int minutesSinceHourStart = currentMinute % 4;
    return minutesSinceHourStart / 2;
}




