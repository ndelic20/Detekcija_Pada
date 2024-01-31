#include <Arduino.h>
#include "SparkFun_MMA8452Q.h"
#include <AzIoTSasToken.h>
#include <SerialLogger.h>
#include <WiFi.h>
#include <az_core.h>
#include <azure_ca.h>
#include <ctime>
#include "WiFiClientSecure.h"

#include "PubSubClient.h"
#include "ArduinoJson.h"

/* Azure auth data */
char* deviceKey = "NnuLgf6Cf94qRhmDxRW+ftHxQpnvOh5vWAIoTDBFMpg=";	 // Azure Primary key for device
const char* iotHubHost = "HUB-RUS-ND.azure-devices.net";		 //[Azure IoT host name].azure-devices.net
const int tokenDuration = 60;

/* MQTT data for IoT Hub connection */
const char* mqttBroker = iotHubHost;  // MQTT host = IoT Hub link
const int mqttPort = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;	// Secure MQTT port
const char* mqttC2DTopic = AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC;	// Topic where we can receive cloud to device messages

// These three are just buffers - actual clientID/username/password is generated
// using the SDK functions in initIoTHub()
char mqttClientId[128];
char mqttUsername[128];
char mqttPasswordBuffer[200];
char publishTopic[200];

/* Auth token requirements */

uint8_t sasSignatureBuffer[256];  // Make sure it's of correct size, it will just freeze otherwise :/

az_iot_hub_client client;
AzIoTSasToken sasToken(
	&client, az_span_create_from_str(deviceKey),
	AZ_SPAN_FROM_BUFFER(sasSignatureBuffer),
	AZ_SPAN_FROM_BUFFER(
		mqttPasswordBuffer));	 // Authentication token for our specific device

const char* deviceId = "Device-ND";  // Device ID as specified in the list of devices on IoT Hub


MMA8452Q accel;
const int numReadings = 10;
float xReadings[numReadings];
float yReadings[numReadings];
float zReadings[numReadings];
int readingIndex = 0;


bool detectFall();

/* WiFi things */

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

const char* ssid = "Tele2 Pokucni Internet-F378";
const char* pass = "28YG40FYFML";
short timeoutCounter = 0;

void setupWiFi() {
	Logger.Info("Connecting to WiFi");

	wifiClient.setCACert((const char*)ca_pem); // We are using TLS to secure the connection, therefore we need to supply a certificate (in the SDK)

	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, pass);

	while (WiFi.status() != WL_CONNECTED) { // Wait until we connect...
		Serial.print(".");
		delay(500);

		timeoutCounter++;
		if (timeoutCounter >= 20) ESP.restart(); // Or restart if we waited for too long, not much else can you do
	}

	Logger.Info("WiFi connected");
}


// Use pool pool.ntp.org to get the current time
// Define a date on 1.1.2023. and wait until the current time has the same year (by default it's 1.1.1970.)
void initializeTime() {	 // MANDATORY or SAS tokens won't generate
  Logger.Info("Setting time using SNTP");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(NULL);
  std::tm tm{};
  tm.tm_year = 2023; // Define a date on 1.1.2023. and wait until the current time has the same year (by default it's 1.1.1970.)

  while (now < std::mktime(&tm)) // Since we are using an Internet clock, it may take a moment for clocks to sychronize
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
}


// MQTT is a publish-subscribe based, therefore a callback function is called whenever something is published on a topic that device is subscribed to
void callback(char *topic, byte *payload, unsigned int length) { 
  payload[length] = '\0'; // It's also a binary-safe protocol, therefore instead of transfering text, bytes are transfered and they aren't null terminated - so we need ot add \0 to terminate the string
  String message = String((char *)payload); // After it's been terminated, it can be converted to String

  Logger.Info("Callback:" + String(topic) + ": " + message);
}

bool connectMQTT()
{
  mqttClient.setBufferSize(1024); // The default size is defined in MQTT_MAX_PACKET_SIZE to be 256 bytes, which is too small for Azure MQTT messages, therefore needs to be increased or it will just crash without any info

  if (sasToken.Generate(tokenDuration) != 0) // SAS tokens need to be generated in order to generate a password for the connection
  {
    Logger.Error("Failed generating SAS token");
    return false;
  }
  else
    Logger.Info("SAS token generated");

  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(callback);

  return true;
}

void mqttReconnect() {
  while (!mqttClient.connected()) {
    Logger.Info("Attempting MQTT connection...");
    const char *mqttPassword = (const char *)az_span_ptr(sasToken.Get()); // Just in case that the SAS token has been regenerated since the last MQTT connection, get it again
    
    if (mqttClient.connect(mqttClientId, mqttUsername, mqttPassword)) { // Either connect or wait for 5 seconds (we can block here since without IoT Hub connection we can't do much)
      Logger.Info("MQTT connected");
      mqttClient.subscribe(mqttC2DTopic); // If connected, (re)subscribe to the topic where we can receive messages sent from the IoT Hub 
    } else {
      Logger.Info("Trying again in 5 seconds");
      delay(5000);
    }
  }
}


String getTelemetryData() { // Get the data and pack it in a JSON message
  StaticJsonDocument<128> doc; // Create a JSON document we'll reuse to serialize our data into JSON
  String output = "";



	doc["DeviceId"] = (String)deviceId;
  doc["AccelerationX"] = accel.getCalculatedX();
  doc["AccelerationY"] = accel.getCalculatedY();
  doc["AccelerationZ"] = accel.getCalculatedZ();

	serializeJson(doc, output);

	Logger.Info(output);
  return output;
}

void sendTelemetryData() {
  String telemetryData = getTelemetryData();
  mqttClient.publish(publishTopic, telemetryData.c_str());
}

long lastTime, currentTime = 0;
int interval = 5000;
void checkTelemetry() { // Do not block using delay(), instead check if enough time has passed between two calls using millis() 
  currentTime = millis();

  if (currentTime - lastTime >= interval) { // Subtract the current elapsed time (since we started the device) from the last time we sent the telemetry, if the result is greater than the interval, send the data again
    Logger.Info("Sending telemetry...");
    sendTelemetryData();

    lastTime = currentTime;
  }
}

void sendTestMessageToIoTHub() {
  az_result res = az_iot_hub_client_telemetry_get_publish_topic(&client, NULL, publishTopic, 200, NULL ); // The receive topic isn't hardcoded and depends on chosen properties, therefore we need to use az_iot_hub_client_telemetry_get_publish_topic()
  Logger.Info(String(publishTopic));
  
  mqttClient.publish(publishTopic, deviceId); // Use https://github.com/Azure/azure-iot-explorer/releases to read the telemetry
}

bool initIoTHub() {
  az_iot_hub_client_options options = az_iot_hub_client_options_default(); // Get a default instance of IoT Hub client options

  if (az_result_failed(az_iot_hub_client_init( // Create an instnace of IoT Hub client for our IoT Hub's host and the current device
          &client,
          az_span_create((unsigned char *)iotHubHost, strlen(iotHubHost)),
          az_span_create((unsigned char *)deviceId, strlen(deviceId)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return false;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqttClientId, sizeof(mqttClientId) - 1, &client_id_length))) // Get the actual client ID (not our internal ID) for the device
  {
    Logger.Error("Failed getting client id");
    return false;
  }

  size_t mqttUsernameSize;
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqttUsername, sizeof(mqttUsername), &mqttUsernameSize))) // Get the MQTT username for our device
  {
    Logger.Error("Failed to get MQTT username ");
    return false;
  }

  Logger.Info("Great success");
  Logger.Info("Client ID: " + String(mqttClientId));
  Logger.Info("Username: " + String(mqttUsername));

  return true;
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

void setup() {
  Serial.begin(115200);
  setupWiFi();
  initializeTime();
  Wire.begin();

  if (accel.begin() == false) {
    Serial.println("Not Connected. Please check connections and read the hookup guide.");
    while (1);
  }

  if (initIoTHub()) {
    connectMQTT();
    mqttReconnect();
  }

  sendTestMessageToIoTHub();


  Logger.Info("Setup done");
}


void loop() { // No blocking in the loop, constantly check if we are connected and gather the data if necessary
  if (!mqttClient.connected()) mqttReconnect();
  if (sasToken.IsExpired()) {
    connectMQTT();
  }

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
    }
  }

  mqttClient.loop();

  checkTelemetry();
}