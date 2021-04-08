/**
 * This esp32_mqtt_publish_subscribe.cpp implements a MQTT Publish/Subscribe
 * mechanism.
 *  
 * MIT License
 * 
 * ESP32 MQTT - Samples code
 * Copyright (c) 2021 Antonio Musarra's Blog - https://www.dontesta.it
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <ESP32Ping.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <PubSubClient.h>
#include "time.h"

// Macro to read build flags
#define ST(A) #A
#define STR(A) ST(A)

#ifdef WIFI_SSID
const char *ssid = STR(WIFI_SSID);
#endif

#ifdef WIFI_PASSWORD
const char *password = STR(WIFI_PASSWORD);
#endif

#ifdef MQTT_SERVER
const char *mqtt_server = STR(MQTT_SERVER);
#endif

#ifdef MQTT_PORT
const int mqtt_port = atoi(STR(MQTT_PORT));
#endif

#ifdef MQTT_USERNAME
const char *mqtt_username = STR(MQTT_USERNAME);
#endif

#ifdef MQTT_PASSWORD
const char *mqtt_password = STR(MQTT_PASSWORD);
#endif

#ifdef DEVICE_NAME
const char *device_name = STR(DEVICE_NAME);
#endif

#define ONBOARD_LED 2

// Relay pre-defined command
#define RELAY_COMMAND_ON "on"
#define RELAY_COMMAND_OFF "off"
#define RELAY_COMMAND_STATUS "status"

// Relay Identification
enum Relay
{
  Relay_00 = 0,
  Relay_01 = 1,
  Relay_02 = 2,
  Relay_03 = 3
};

// Relay Id to Pin
enum RelayPin
{
  Relay_00_Pin = 26,
  Relay_01_Pin = 25,
  Relay_02_Pin = 27,
  Relay_03_Pin = 14
};

// Setting for NTP Time
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

/**
 * MQTT Broker Connection details
 * 1. Defined the topic for telemetry data (temperature, humidity and pressure)
 * 2. Defined the topic about status of the relay 0
 * 3. Defined the topic about status of the relay 1
 * 4. Defined the topic about status of the relay 2
 * 5. Defined the topic about status of the relay 3
 * 6. Defined the topic where the action command for the relay will be received
 */
const char *topic_telemetry_data = "esp32/telemetry_data";
const char *topic_relay_00_status = "esp32/relay_00_status";
const char *topic_relay_01_status = "esp32/relay_01_status";
const char *topic_relay_02_status = "esp32/relay_02_status";
const char *topic_relay_03_status = "esp32/relay_03_status";
const char *topic_command = "esp32/command";

// Prefix for the MQTT Client Identification
String clientId = "esp32-client-";

// Defined the value for the status of the relay (active 1, 0 otherwise)
const int relay_status_on = 1;
const int relay_status_off = 0;

// BME280
Adafruit_BME280 bme;

// Initi variables for values from sensor BME280
float temperature = 0;
float humidity = 0;
int pressure = 0;

// Interval in ms of the reads
int counter = 0;
long interval = 5000;
long lastMessage = 0;

// Declare the custom functions
void callback(char *topic, byte *message, unsigned int length);
void setup_wifi();
void update_relay_status(int relayId, const int status);

// Init WiFi/WiFiUDP, NTP and MQTT Client
WiFiUDP ntpUDP;
WiFiClient espClient;
NTPClient timeClient(ntpUDP);
PubSubClient client(mqtt_server, mqtt_port, callback, espClient);

/**
  * MQTT Callback
  * 
  * If a message is received on the topic esp32/command (es. Relay off or on).
  * Format: {$device-name}:{relay;$relayId;$command}
  * Es: 
  *  esp32-zone-1:relay;3;off (switch off relay 3 of the specified device)
  *  esp32-zone-1:relay;2;on (switch on relay 2 of the specified device)
  *  esp32-zone-1:relay;3;status (get status of the relay 3 of the specified device) 
  */
void callback(char *topic, byte *message, unsigned int length)
{
  String messageTemp;

  for (int i = 0; i < length; i++)
  {
    messageTemp += (char)message[i];
  }

  Log.notice(F("Message arrived on topic: %s" CR), topic);
  Log.notice(F("Message Content: %s" CR), messageTemp.c_str());

  if ((String)topic == (String)topic_command)
  {

    /**
     * Parsing of the received command string. This piece of code 
     * could be written using regular expressions.
     * ([a-zA-Z,0-9,\-]{3,12}):(\w+);([0-3]);(off|on|status) this could be the 
     * regular expression which should be sufficient to satisfy the given format.
     */
    int indexOfDeviceSeparator = messageTemp.indexOf(":");
    
    String deviceName = messageTemp.substring(0, indexOfDeviceSeparator);
    String statement = messageTemp.substring(indexOfDeviceSeparator + 1,
                                             messageTemp.length());

    int indexOfStatementSeparator = statement.indexOf(";");
    int relayId = statement.substring(indexOfStatementSeparator + 1,
                                      indexOfStatementSeparator + 2)
                      .toInt();
    String command = statement.substring(statement.lastIndexOf(";") + 1,
                                         statement.length());

    if (!deviceName.isEmpty() && !statement.isEmpty() && !command.isEmpty() &&
        (String)device_name == deviceName)
    {
      Log.notice(F("Try to execute this statement (command %s): %s for relay %d on the device name: %s" CR),
                 command.c_str(), statement.c_str(), relayId, deviceName.c_str());

      /**
       * The following code block is responsible for executing the instructions 
       * received from the command topic. This block of code is purely 
       * educational and can be optimizing to avoid redundant code.
       */
      switch (relayId)
      {
      case Relay_00:
        if (command == RELAY_COMMAND_ON)
        {
          digitalWrite(Relay_00_Pin, LOW);
          update_relay_status(Relay_00, relay_status_on);

          Log.notice(F("Switch On relay 0" CR));
        }
        else if (command == RELAY_COMMAND_OFF)
        {
          digitalWrite(Relay_00_Pin, HIGH);
          update_relay_status(Relay_00, relay_status_off);

          Log.notice(F("Switch Off relay 0" CR));
        }
        else if (command == RELAY_COMMAND_STATUS)
        {
          digitalRead(Relay_00_Pin) == LOW ? update_relay_status(Relay_00, relay_status_on) : update_relay_status(Relay_00, relay_status_off);
        }
        break;
      case Relay_01:
        if (command == RELAY_COMMAND_ON)
        {
          digitalWrite(Relay_01_Pin, LOW);
          update_relay_status(Relay_01, relay_status_on);

          Log.notice(F("Switch On relay 1" CR));
        }
        else if (command == RELAY_COMMAND_OFF)
        {
          digitalWrite(Relay_01_Pin, HIGH);
          update_relay_status(Relay_01, relay_status_off);

          Log.notice(F("Switch Off relay 1" CR));
        }
        else if (command == RELAY_COMMAND_STATUS)
        {
          digitalRead(Relay_01_Pin) == LOW ? update_relay_status(Relay_01, relay_status_on) : update_relay_status(Relay_01, relay_status_off);
        }
        break;
      case Relay_02:
        if (command == RELAY_COMMAND_ON)
        {
          digitalWrite(Relay_02_Pin, LOW);
          update_relay_status(Relay_02, relay_status_on);

          Log.notice(F("Switch On relay 2" CR));
        }
        else if (command == RELAY_COMMAND_OFF)
        {
          digitalWrite(Relay_02_Pin, HIGH);
          update_relay_status(Relay_02, relay_status_off);

          Log.notice(F("Switch Off relay 2" CR));
        }
        else if (command == RELAY_COMMAND_STATUS)
        {
          digitalRead(Relay_02_Pin) == LOW ? update_relay_status(Relay_02, relay_status_on) : update_relay_status(Relay_02, relay_status_off);
        }
        break;
      case Relay_03:
        if (command == RELAY_COMMAND_ON)
        {
          digitalWrite(Relay_03_Pin, LOW);
          update_relay_status(Relay_03, relay_status_on);

          Log.notice(F("Switch On relay 3" CR));
        }
        else if (command == RELAY_COMMAND_OFF)
        {
          digitalWrite(Relay_03_Pin, HIGH);
          update_relay_status(Relay_03, relay_status_off);

          Log.notice(F("Switch Off relay 3" CR));
        }
        else if (command == RELAY_COMMAND_STATUS)
        {
          digitalRead(Relay_03_Pin) == LOW ? update_relay_status(Relay_03, relay_status_on) : update_relay_status(Relay_03, relay_status_off);
        }
        break;
      default:
        Log.warning(F("No relayId recognized" CR));
        break;
      }
    }
  }
}

/**
 * Return the relays status
 */
int * get_relays_status()
{
  static int relaysStatus[4];

  digitalRead(Relay_00_Pin) == LOW ? relaysStatus[0] = HIGH : relaysStatus[0] = LOW;
  digitalRead(Relay_01_Pin) == LOW ? relaysStatus[1] = HIGH : relaysStatus[1] = LOW;
  digitalRead(Relay_02_Pin) == LOW ? relaysStatus[2] = HIGH : relaysStatus[2] = LOW;
  digitalRead(Relay_03_Pin) == LOW ? relaysStatus[3] = HIGH : relaysStatus[3] = LOW;

  return relaysStatus;
}

/**
 * Update Relay status on the topic
 * 
 * relayId: Identifiier of the relay
 * status: Status of the relay. (o or 1)
 */
void update_relay_status(int relayId, const int status)
{
  // Allocate the JSON document
  // Inside the brackets, 200 is the RAM allocated to this document.
  // Don't forget to change this value to match your requirement.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<200> relayStatus;

  relayStatus["clientId"] = clientId;
  relayStatus["deviceName"] = device_name;
  relayStatus["time"] = timeClient.getEpochTime();
  relayStatus["relayId"] = relayId;
  relayStatus["status"] = status;

  char relayStatusAsJson[200];
  serializeJson(relayStatus, relayStatusAsJson);

  switch (relayId)
  {
  case Relay_00:
    client.publish(topic_relay_00_status, relayStatusAsJson);
    break;
  case Relay_01:
    client.publish(topic_relay_01_status, relayStatusAsJson);
    break;
  case Relay_02:
    client.publish(topic_relay_02_status, relayStatusAsJson);
    break;
  case Relay_03:
    client.publish(topic_relay_03_status, relayStatusAsJson);
    break;
  }
}

/**
 * Setup lifecycle
 */
void setup()
{
  Serial.begin(115200);

  // Setup PIN Mode for On board led
  pinMode(ONBOARD_LED, OUTPUT);

  // Initialize with log level and log output.
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);

  // Log ESP Chip information
  Log.notice(F("ESP32 Chip model %s Rev %d" CR), ESP.getChipModel(),
             ESP.getChipRevision());
  Log.notice(F("This chip has %d cores" CR), ESP.getChipCores());

  // Start I2C communication
  if (!bme.begin(0x76))
  {
    Log.notice("Could not find a BME280 sensor, check wiring!");
    while (1)
      ;
  }

  clientId += String(random(0xffff), HEX);

  // Connect to WiFi
  setup_wifi();

  // Setup PIN Mode for Relay
  pinMode(Relay_00_Pin, OUTPUT);
  pinMode(Relay_01_Pin, OUTPUT);
  pinMode(Relay_02_Pin, OUTPUT);
  pinMode(Relay_03_Pin, OUTPUT);

  // Init Relay
  digitalWrite(Relay_00_Pin, HIGH);
  digitalWrite(Relay_01_Pin, HIGH);
  digitalWrite(Relay_02_Pin, HIGH);
  digitalWrite(Relay_03_Pin, HIGH);

  // Init NTP
  timeClient.begin();
  timeClient.setTimeOffset(0);
}

/**
 * Setup the WiFi Connection
 */
void setup_wifi()
{
  // We start by connecting to a WiFi network
  Log.notice(F("Connecting to WiFi network: %s (password: %s)" CR),
             ssid, password);

  WiFi.begin(ssid, password);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(clientId.c_str());

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Serial.println("WiFi connected :-)");
  Serial.print("IP Address: ");
  Serial.print(WiFi.localIP());
  Serial.println("");
  Serial.print("Mac Address: ");
  Serial.print(WiFi.macAddress());
  Serial.println("");
  Serial.print("Hostname: ");
  Serial.print(WiFi.getHostname());
  Serial.println("");
  Serial.print("Gateway: ");
  Serial.print(WiFi.gatewayIP());
  Serial.println("");

  bool success = Ping.ping(mqtt_server, 3);

  if (!success)
  {
    Log.error(F("Ping failed to %s" CR), mqtt_server);
    return;
  }

  Log.notice(F("Ping OK to %s" CR), mqtt_server);

  // When setup wifi ok turn on led board
  digitalWrite(ONBOARD_LED, HIGH);
}

/**
 * Reconnect to MQTT Broker
 */
void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Log.notice(F("Attempting MQTT connection to %s" CR), mqtt_server);

    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password))
    {
      Log.notice(F("Connected as clientId %s :-)" CR), clientId.c_str());

      // Subscribe
      client.subscribe(topic_command, 1);
      Log.notice(F("Subscribe to the topic command %s " CR), topic_command);

      // Init Status topic for Relay
      update_relay_status(Relay_00, relay_status_off);
      update_relay_status(Relay_01, relay_status_off);
      update_relay_status(Relay_02, relay_status_off);
      update_relay_status(Relay_03, relay_status_off);

      // Turn on led board
      digitalWrite(ONBOARD_LED, HIGH);
    }
    else
    {
      Log.error(F("{failed, rc=%d try again in 5 seconds}" CR), client.state());
      // Turn off led board and wait 5 seconds before retrying
      digitalWrite(ONBOARD_LED, LOW);
      delay(5000);
    }
  }
}

/**
 * Loop lifecycle
 */
void loop()
{
  while (!timeClient.update())
  {
    timeClient.forceUpdate();
  }

  long now = millis();
  
  if (!client.connected())
  {
    reconnect();
  }
  
  client.loop();

  if (now - lastMessage > interval)
  {
    lastMessage = now;

    // Allocate the JSON document
    // Inside the brackets, 200 is the RAM allocated to this document.
    // Don't forget to change this value to match your requirement.
    // Use arduinojson.org/v6/assistant to compute the capacity.
    StaticJsonDocument<256> telemetry;

    /**
     * Reading humidity, temperature and pressure
     * Temperature is always a floating point, in Centigrade. Pressure is a 
     * 32 bit integer with the pressure in Pascals. You may need to convert 
     * to a different value to match it with your weather report. Humidity is 
     * in % Relative Humidity
     */
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure();

    telemetry["clientId"] = clientId.c_str();
    telemetry["deviceName"] = device_name;
    telemetry["time"] = timeClient.getEpochTime();
    telemetry["temperature"] = temperature;
    telemetry["humidity"] = humidity;
    telemetry["pressure"] = pressure;
    telemetry["interval"] = interval;
    telemetry["counter"] = ++counter;

    JsonArray relaysStatusJsonArray = telemetry.createNestedArray("relaysStatus");

    int * relaysStatus = get_relays_status();

    for (int i = 0; i <= 3; i++)
    {
        relaysStatusJsonArray.add(relaysStatus[i]);
    }
    
    char telemetryAsJson[256];
    serializeJson(telemetry, telemetryAsJson);

    client.publish(topic_telemetry_data, telemetryAsJson);

    serializeJsonPretty(telemetry, Serial);
    Serial.println();
  }
}
