#include <Arduino.h>
#include "SparkFun_MMA8452Q.h"
#include <azure_ca.h>
#include <WiFiClientSecure.h>
#include <logger.h>
#include <az_core.h>
#include <az_iot.h>


MMA8452Q accel;
WiFiClientSecure wifiClient;

void connectToWiFi(){
Serial.println("Connecting to WIFI SSID " + String("Tele2 Pokucni Internet-F378"));
wifiClient.setCACert((const char*)ca_pem);
WiFi.mode(WIFI_STA);
WiFi.begin("Tele2 Pokucni Internet-F378", "28YG40FYFML");
short timeoutCounter = 0;
while (WiFi.status() != WL_CONNECTED)
{
Serial.print(".");
delay(500);
timeoutCounter++;
if (timeoutCounter >= 20)
ESP.restart();
}
Serial.println("WiFi connected, IP address: " + WiFi.localIP().toString());
}

bool initIoTHub() {
az_iot_hub_client_options options = az_iot_hub_client_options_default();
options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);
if (az_result_failed(az_iot_hub_client_init(
&client,
az_span_create((uint8_t *)host, strlen(host)),
az_span_create((uint8_t *)deviceId, strlen(deviceId)),
&options)))
{
Logger.Error("Failed initializing Azure IoT Hub client");
return false;
}
size_t client_id_length;
if (az_result_failed(az_iot_hub_client_get_client_id(
&client, mqttClientId, sizeof(mqttClientId) - 1, &client_id_length)))
{
Logger.Error("Failed getting client id");
return false;
}
size_t mqttUsernameSize;
if (az_result_failed(az_iot_hub_client_get_user_name(
&client, mqttUsername, sizeof(mqttUsername), &mqttUsernameSize)))
{
Logger.Error("Failed to get MQTT username ");
return false;
}
Logger.Info("Client ID: " + String(mqttClientId));
Logger.Info("Username: " + String(mqttUsername));
return true;
}


const int numReadings = 10;
float xReadings[numReadings];
float yReadings[numReadings];
float zReadings[numReadings];
int readingIndex = 0;


bool detectFall();

void setup() {
  Serial.begin(115200);
  connectToWiFi();
/*initializeTime();
if (initIoTHub()) connectMQTT();*/
  Serial.println("Fall Detection Test Code!");
  Wire.begin();

  if (accel.begin() == false) {
    Serial.println("Not Connected. Please check connections and read the hookup guide.");
    while (1);
  }
}

void loop() {
  if (accel.available()) {
    float x = accel.getCalculatedX();
    float y = accel.getCalculatedY();
    float z = accel.getCalculatedZ();

    
    xReadings[readingIndex] = x;
    yReadings[readingIndex] = y;
    zReadings[readingIndex] = z;

    
    readingIndex = (readingIndex + 1) % numReadings;

    if (detectFall()) {
      Serial.println("Fall detected!");
      delay(1000);  
    }
  }

  delay(100);
}


bool detectFall() {
  
  float yThreshold = 0.05; 
  float xThreshold = 0.05; 
  float zThreshold = 0.05; 

  
  float deltaY = yReadings[readingIndex] - yReadings[(readingIndex + numReadings - 1) % numReadings];
  float deltaX = xReadings[readingIndex] - xReadings[(readingIndex + numReadings - 1) % numReadings];
  float deltaZ = zReadings[readingIndex] - zReadings[(readingIndex + numReadings - 1) % numReadings];

  return (yReadings[readingIndex] < 0 && yReadings[(readingIndex + numReadings - 1) % numReadings] >= 0) &&
         ((xReadings[readingIndex] < 0 && xReadings[(readingIndex + numReadings - 1) % numReadings] >= 0) ||
          (xReadings[readingIndex] > 0 && xReadings[(readingIndex + numReadings - 1) % numReadings] <= 0)) &&
         ((zReadings[readingIndex] < 0 && zReadings[(readingIndex + numReadings - 1) % numReadings] >= 0) ||
          (zReadings[readingIndex] > 0 && zReadings[(readingIndex + numReadings - 1) % numReadings] <= 0));
}



