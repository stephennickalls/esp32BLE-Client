#include <NimBLEDevice.h>
#include <HTTPClient.h>
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


WiFiCreds wifiCreds; // Create an instance of WiFiCreds

// Use the instance to access methods
const char* ssid = wifiCreds.getSSID(); 
const char* password = wifiCreds.getPassword(); 

enum class programState: uint8_t {
  TIME_CONFIG,
  SENSOR_CONFIG,
  CHECK_STATUS_SLEEP,
  BLE_CONNECT_READ_SAVE,
  MOBILE_CONNECT_TRANSMIT_DATA
};


void constructEndpointUrl(char* buffer, size_t bufferLen, const char* baseEndPoint, const char* identifier, const char* action) {
  debugln("constructEndpointUrl called");
    snprintf(buffer, bufferLen, "%s%s%s", baseEndPoint, identifier, action);
}


void getHubConfig() {
  debugln("fetchHubConfig called");
    const char* baseConfigEndPoint = "https://beevibe-prod-7815d8f510b2.herokuapp.com/api/hubconfig/";
    const char* api_key = "bfe921e4-417c-4018-958b-7b099605abf3";
    const char* action = "/get_config/";

    char endPoint[256]; // Buffer to hold the complete endpoint URL
    constructEndpointUrl(endPoint, sizeof(endPoint), baseConfigEndPoint, api_key, action);
    debugln(endPoint);
    // HTTPClient http;
    // // Your HTTP GET request
    // http.begin(endPoint);
    // int httpResponseCode = http.GET();
    // String response = http.getString(); // Get the response to the request

    // Serial.println(response); // Print the response to the serial monitor
    
    // http.end(); // Free resources
}


void setup (){
    Serial.begin(115200);
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
  debugln("loop called");
  getHubConfig();
  delay(2000);
}

//################### Finite state machine ############################
void programStateMachine() {

    static programState currentState = programState::CHECK_STATUS_SLEEP;

    switch (currentState) {
        case programState::CHECK_STATUS_SLEEP:
            debugln("CHECK_STATUS_SLEEP called");
            // Your code for this case...
            break; // 'break' inside the case block

        case programState::TIME_CONFIG:
            debugln("TIME_CONFIG called");
            // Your code for this case...
            break; // 'break' inside the case block

        case programState::SENSOR_CONFIG:
            debugln("SENSOR_CONFIG called");
            // Your code for this case...
            break; // 'break' inside the case block

        default:
            debugln("Switch case default called....something terrible has happened");
            // Your code for the default case...
            break; // 'break' inside the default block
    }

}


