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

// max number of services that we can be connected too
const int MAX_SENSOR_COUNT = 5; // Maximum number of sensors
const int MAX_SERVICE_COUNT = 5; // Maximum number of services
const int MAX_CHARACTERISTIC_COUNT = 5; // Maximum number of characteristics per service

// non-volatile storage
Preferences preferences;

WiFiCreds wifiCreds; // Create an instance of WiFiCreds

// real time clock
RTC_DS3231 rtc;

// global error messages
const size_t errorJsonCapacity = JSON_ARRAY_SIZE(10) + JSON_OBJECT_SIZE(5) * 10 + 500;
DynamicJsonDocument errorJsonDoc(errorJsonCapacity);
JsonArray errorMessages;

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

bool writeConfigSensorsFlag() {
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

void addErrorMessageToJSON(const char* errorMessage, const char* deviceType, const char* device_id) {
    char timestamp[25];
    DateTime now = rtc.now();
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    JsonObject errorObj = errorMessages.createNestedObject();
    errorObj["device_type"] = deviceType;
    errorObj["device_id"] = device_id; // uuid of the device
    errorObj["error_message"] = errorMessage;
    errorObj["timestamp"] = timestamp;
}

int postErrorsJSONToAPI() {

    if (errorMessages.size() == 0) {
        return -1; // No errors to send
    }

    StaticJsonDocument<1024> jsonDoc; 

    // Add the API key to the JSON document
    jsonDoc["api_key"] = api_key;

    // Add the error messages
    jsonDoc["errors"] = errorMessages;

    char jsonString[2048]; 
    serializeJson(jsonDoc, jsonString);

    // Print JSON for debugging
    Serial.println("Debug - JSON to be sent:");
    serializeJsonPretty(jsonDoc, Serial);
    Serial.println(); // New line after JSON output

    const char* endpoint = "https://beevibe-prod-7815d8f510b2.herokuapp.com/api/datacollection/deviceerrorreports/";

    HTTPClient http;
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println(response);
    } else {
        Serial.println("Error on sending POST: " + String(httpResponseCode));
    }

    http.end();
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
        addErrorMessageToJSON("Failed to open NVS storage", device_type, api_key);
        debug("Failed to open NVS storage");
        return false; // Exit if unable to open NVS
    }

    preferences.clear(); // Clear any previous entries
    int sensorIndex = 0;
    bool errorOccurred = false;

    for (JsonObject sensor : sensors) {
        const char* sensorUUID = sensor["uuid"]; // Extract the UUID
        // Save each UUID with a unique key
        String key = "sensor" + String(sensorIndex);
        success = preferences.putString(key.c_str(), String(sensorUUID));
        if (!success) {
            addErrorMessageToJSON("Failed to save UUID for a sensor", device_type, sensorUUID);
            debug("Failed to save UUID for sensor");
            debugln(sensorIndex);
            errorOccurred = true;
            break; // Exit the loop if error occurs
        }
        sensorIndex++;
        debug("Sensor UUID saved: ");
        debugln(sensorUUID);
    }

    if (!errorOccurred) {
        // Save the count of sensors only if no errors occurred
        success = preferences.putInt("sensorCount", sensorIndex);
        if (!success) {
            addErrorMessageToJSON("Failed to save sensor count", device_type, api_key);
            debugln("Failed to save sensor count!");
            return false;
        }
    }

    preferences.end(); // Close NVS storage

    if (errorOccurred) {
        debugln("Error occurred while configuring sensor UUIDs");
        return false;
    }

  return true;
}




void readSensorData() {
    // Initialize NVS
    preferences.begin("sensor_uuid_storage", true);
    int sensorCount = preferences.getInt("sensorCount", 0);

    NimBLEClient* pClient = NimBLEDevice::createClient();

    for (int sensorIdx = 0; sensorIdx < sensorCount && sensorIdx < MAX_SENSOR_COUNT; sensorIdx++) {
        // Retrieve each sensor UUID from NVS
        String key = "sensor" + String(sensorIdx);
        String sensorUUID = preferences.getString(key.c_str(), "");
        
        if (!sensorUUID.isEmpty()) {
            if (!pClient->connect(sensorUUID.c_str())) {
                debugln("Failed to connect, trying next sensor...");
                continue;
            }

            // Retrieve all services
            std::vector<NimBLERemoteService*> *services = pClient->getServices();
            for (auto pService : *services) {
                // Retrieve all characteristics for each service
                std::vector<NimBLERemoteCharacteristic*> *characteristics = pService->getCharacteristics();
                for (auto pCharacteristic : *characteristics) {
                    String value = pCharacteristic->readValue();
                    debugln(value);
                }
            }

            // Disconnect from the BLE Server after processing
            pClient->disconnect();
        }
    }

    // Clean up
    NimBLEDevice::deleteClient(pClient);
    preferences.end();
}




void setup (){
    Serial.begin(115200);

    // initialize error messages
    errorMessages = errorJsonDoc.to<JsonArray>();

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

        // Initialize NimBLE
    NimBLEDevice::init(""); // Optionally, you can pass a device name

    debugln("NimBLE initialized");
    debugln("Setup complete");
   
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
  // writeConfigSensorsFlag(true); // REMOVE
  debugln("############ config sensors flag set to true at the begining of program state for debug reasons: remove this when tested");

    DateTime now = rtc.now();
    int calculatedCurrentCycleNumber = getCurrentCycleNumber(now);
    debug("calculatedCurrentCycleNumber: ");
    debugln(calculatedCurrentCycleNumber);

    // if(calculatedCurrentCycleNumber == cycleNumber){
    //     static programState currentState = programState::BLE_CONNECT_READ_SAVE;
    // }

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
                // postErrorToAPI(device_type, api_key, errorMessage);
                debugln("Current time not found in JSON");
            }
            // end time calibration
            // ###### check for sensor config flag from api
            if(configJSON["config_sensors"] == true) {
              currentState = programState::SENSOR_CONFIG;
            }
            // posting any errors to the api before going to sleep
            addErrorMessageToJSON("test error message 1", device_type, api_key);
            addErrorMessageToJSON("test error message 2", device_type, api_key);
            addErrorMessageToJSON("test error message 3", device_type, api_key);
            postErrorsJSONToAPI();
            debugln("Going to sleep");
            // sets sleep timer to however many seconds there are untill the next quater hour (timeslot offset to be included).
            esp_sleep_enable_timer_wakeup((uint64_t)calculateSleepDuration(rtc.now()) * 1000000); 
            esp_deep_sleep_start();

            break;
        }
        case programState::SENSOR_CONFIG: {
            bool configSensorsResponse;
            debugln("SENSOR_CONFIG called");
            // look for and process sensor uuids
            if (configJSON.containsKey("sensors")) {
                JsonArray sensors = configJSON["sensors"];
                configSensorsResponse = configureSensorUUIDs(sensors);
            } else {
                debugln("No sensors found in JSON");
            }
            if (!configSensorsResponse ){
              // TODO: toggle config flag in api to accept bool value
              // char errorMessage[50];
              // strncpy(errorMessage, "Failed to save UUID for a sensor", sizeof(errorMessage));
              // int responseCode = postErrorToAPI(device_type, api_key, errorMessage);
              debugln("Config sensors failed");
            }

            debugln("Sensor config called and completed without error");
            delay(2000);
            postErrorsJSONToAPI();
            debugln("Going to sleep");
            // sets sleep timer to however many seconds there are untill the next quater hour (timeslot offset to be included).
            esp_sleep_enable_timer_wakeup((uint64_t)calculateSleepDuration(rtc.now()) * 1000000); 
            esp_deep_sleep_start();
            break;
        }
        case programState::BLE_CONNECT_READ_SAVE: {
            debugln("BLE_CONNECT_READ_SAVE called");
            readSensorData();
            postErrorsJSONToAPI();
            debugln("Going to sleep");
            // sets sleep timer to however many seconds there are untill the next quater hour (timeslot offset to be included).
            esp_sleep_enable_timer_wakeup((uint64_t)calculateSleepDuration(rtc.now()) * 1000000); 
            esp_deep_sleep_start();
            break;
        }
        default:
            debugln("Switch case default called....something terrible has happened");
            break;
    }
}

// ################# Some Helpers ##################

int getCurrentCycleNumber(const DateTime& currentTime) {
    int minutesSinceHourStart = currentTime.minute() % 8; // Get the minutes within the current 8-minute window
    return minutesSinceHourStart / 2; // Divide by 2 to get the cycle number (0 to 3)
}

int calculateSleepDuration(const DateTime& currentTime) {
    int minutesSinceHourStart = currentTime.minute() % 8;
    int secondsSinceCycleStart = (minutesSinceHourStart % 2) * 60 + currentTime.second();
    int sleepDuration = (2 * 60) - secondsSinceCycleStart;

    // Add 5 seconds as a buffer
    sleepDuration += 5;

    // Ensure sleep duration does not exceed the cycle length
    if (sleepDuration > (2 * 60)) {
        sleepDuration = (2 * 60);
    }

    return sleepDuration;
}




