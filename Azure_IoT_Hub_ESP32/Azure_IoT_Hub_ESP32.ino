//// This code is adapted from Microsoft's sample code

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"

//// WiFi and Azure details\
#define HTTP_TRIGGER "" // ENTER HTTP TRIGGER SERVER-URL
#define IOT_CONFIG_WIFI_SSID "" // ENTER WIFI SSID
#define IOT_CONFIG_WIFI_PASSWORD "" // ENTER WIFI PASSWORD
#define IOT_CONFIG_IOTHUB_FQDN "{IOT-HUB-NAME}.azure-devices.net"
#define IOT_CONFIG_DEVICE_ID "" // ENTER DEVICE ID
#define IOT_CONFIG_DEVICE_KEY "" // ENTER DEVICE KEY
#define TELEMETRY_FREQUENCY_MILLISECS 3599500 // Duration (ms) - 500 (since `delay(500)` is used)
#define ALERT_FREQUENCY_MILLISECS 899500 // Alert timeout duration (ms) - 500 (since `delay(500)` is used)
#define UNIX_EPOCH_START_YEAR 1900

//// Libraries for the connected peripherals
#include <Wire.h>
#include <BH1750.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>

String patientName = ""; // ENTER THE PATIENT'S NAME THAT WILL BE USED ON THE SMS
HTTPClient http;

//// Set the thresholds for sending data
const int soundThreshold = 70;
const float lightThreshold = 400.0;

//// Initialize BH1750 and LCD
BH1750 lightMeter;
LiquidCrystal_I2C lcd(0x27, 16, 2);

//// Define microphone pin
#define MICROPHONE_AO 36 // Analog output for analogRead
#define MICROPHONE_DO 16 // Digital output for digitalRead

float lux = 0;
int analogValue = 0;
int digitalValue = 0;

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static unsigned long next_alert_send_time_ms = 0;
static char telemetry_topic[128];
static String telemetry_payload = "{}";

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
{
  Logger.Info("Received [");
  Logger.Info(topic);
  Logger.Info("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT event MQTT_EVENT_DATA");

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
      {
        incoming_data[i] = event->topic[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Topic: " + String(incoming_data));

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Data: " + String(incoming_data));

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT event UNKNOWN");
      break;
  }

  return ESP_OK;
}

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

static String writeTime()
{
  struct tm* ptm;
  time_t now = time(NULL);

  ptm = gmtime(&now);

  String total = "";

  total += String(ptm->tm_year + UNIX_EPOCH_START_YEAR);
  total += String("-");
  total += String(ptm->tm_mon + 1);
  total += String("-");
  total += String(ptm->tm_mday);
  total += String(" ");

  if (ptm->tm_hour < 10)
  {
    total += String(0);
  }

  total += String(ptm->tm_hour);
  total += String(":");

  if (ptm->tm_min < 10)
  {
    total += String(0);
  }

  total += String(ptm->tm_min);
  total += String(":");

  if (ptm->tm_sec < 10)
  {
    total += String(0);
  }

  total += String(ptm->tm_sec);
  return total;
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  (void)initializeMqttClient();
}

static void generateTelemetryPayload(float decibels, int lux)
{
  struct tm* ptm;
  time_t now = time(NULL);
  ptm = gmtime(&now);

  telemetry_payload = "{\"soundLevel\":" + String(decibels) + ",\"lightIntensity\":" + String(lux) + ",\"time\":" + writeTime() + "}";
}

static void sendTelemetry(float decibels, int lux)
{
  Logger.Info("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  generateTelemetryPayload(decibels,lux);

  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          (const char*)telemetry_payload.c_str(),
          telemetry_payload.length(),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}

// Arduino setup and loop main functions.

void setup() {
  //// Initializing
  Serial.begin(9600);
  Wire.begin(); // I2C SDA and SCL
  lcd.init(); // Initialize the LCD
  lcd.backlight(); // Turn on the LCD backlight
  lightMeter.begin(); // Initialize the BH1750

  pinMode(MICROPHONE_DO, INPUT); // Set the microphone's digital output to input

  establishConnection();
}

void loop()
{
  //// Read light level from the BH1750
  lux = lightMeter.readLightLevel();
 
  //// Read the microphone's analog output
  analogValue = analogRead(MICROPHONE_AO);
 
  //// Read the microphone's digital output
  digitalValue = digitalRead(MICROPHONE_DO);

  lcd.clear(); // Clear the LCD for a new message
  lcd.setCursor(0, 0); // Set cursor to the top-left corner
  String luxString = String(lux, 1);
  lcd.print("Light: " + luxString + " Lux"); // Display the light level
 
  lcd.setCursor(0, 1); // Set cursor to the second line
  lcd.print("Sound: ");
  float voltage = analogValue * (3.3 / 4095);
  float referenceVoltage = 0.00002; // Reference voltage for 0 dB
  float decibels = 20 * log10(voltage / referenceVoltage); // Calculate dB
  lcd.print(decibels); // Display the microphone's analog reading
  lcd.print(" dB");

  if ((decibels > soundThreshold || lux > lightThreshold) && (millis() > next_alert_send_time_ms)) {
  String serverUrl = HTTP_TRIGGER;
  String payload = "name=" + patientName; // Data to send

  // Create an HTTPClient object
  HTTPClient http;

  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Send the POST request
  int httpResponseCode = http.POST(payload);

  // If you get a response from the server, print it
  if (httpResponseCode > 0) {
    String response = http.getString();
    Logger.Info("Response code " + String(httpResponseCode));
    Logger.Info(response);
  }
  else {
    Logger.Error("Error on sending POST: " + httpResponseCode);
  }

    sendTelemetry(decibels, lux);
    next_alert_send_time_ms = millis() + ALERT_FREQUENCY_MILLISECS;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
  else if (millis() > next_telemetry_send_time_ms)
  {
    sendTelemetry(decibels, lux);
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }
  delay(500);
}
