#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <MHZ.h>
#include <AsyncMqttClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>
#include <ESPAsyncWebServer.h>
#include <SoftwareSerial.h>
#include <tools.h>

#include "config.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
MHZ co2Sensor(&Serial2, MHZ::MHZ14A);
WiFiManager wifiManager;
WiFiClient espClient;
AsyncMqttClient mqttClient;
static AsyncWebServer server(80);

TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

// timers
unsigned long lastCo2Measurement = 0;

WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 40);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);

// default values for custom parameters
char mqtt_server[40];
char mqtt_port[6] = "8080";
char mqtt_user[20];
char mqtt_password[20];

byte appState = 0; // 0 = init; 1 = preheating; 2 = ready

int lastTemperature = 0;
int lastCo2Value = 0;

bool isWifiConnected = false;
bool isMqttConnected = false;

void publishHomeAssistantDiscovery()
{
    JsonDocument doc;
    doc["name"] = "CO2 Sensor";
    doc["state_topic"] = STATE_TOPIC;
    doc["unit_of_measurement"] = "ppm";
    doc["value_template"] = "{{ value_json.co2 }}";

    JsonObject device = doc.createNestedObject("device");
    device["identifiers"] = "co2_meter_" + getChipId();
    device["name"] = "CO2 Meter";
    device["model"] = "ESP32 CO2 Meter";
    device["manufacturer"] = "coding-lemur";

    // serialize JSON and send discover-topic
    String topic = "homeassistant/sensor/co2_meter/config";
    StringStream stream;
    size_t n = serializeJson(doc, stream);
    mqttClient.publish(topic.c_str(), 0, true, stream.str().c_str(), n);
}

void publishSensorState(int co2Value)
{
    // State-Topic für den CO2-Sensor

    // JSON-Daten für den aktuellen Zustand
    JsonDocument doc;
    doc["co2"] = co2Value;

    // Serialisiere das JSON und sende es an das State-Topic
    StringStream stream;
    size_t n = serializeJson(doc, stream);
    mqttClient.publish(STATE_TOPIC.c_str(), 0, false, stream.str().c_str(), n);
}

void connectToMqtt()
{
    uint16_t mqttPortValue = static_cast<uint16_t>(strtol(mqtt_port, nullptr, 10));
    mqttClient.setServer(mqtt_server, mqttPortValue);

    if (mqtt_user != "")
        mqttClient.setCredentials(mqtt_user, mqtt_password);

    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());

        isWifiConnected = true;
        connectToMqtt();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
        xTimerStart(wifiReconnectTimer, 0);
        break;
    }
}

void setupWifi()
{
    WiFi.setHostname(HOST_NAME);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiEvent);
    WiFi.begin();
}

void setupWifiManager()
{
    wifiManager.setDebugOutput(true);

    // custom parameters
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_password);

    connectToWifi();
}

void setupOTA()
{
    ArduinoOTA.setHostname("co2-meter-ota");

    ArduinoOTA.onStart([]()
                       { Serial.println("[OTA] starting"); });
    ArduinoOTA.onEnd([]()
                     { Serial.println("\n[OTA] finished"); });
    ArduinoOTA.onError([](ota_error_t error)
                       {
        Serial.printf("[OTA] error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth error");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Start error");
        else if (error == OTA_CONNECT_ERROR) Serial.println("connection error");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("receive error");
        else if (error == OTA_END_ERROR) Serial.println("Ende error"); });

    ArduinoOTA.begin();
    Serial.println("[INFO] OTA ready");
}

void setupDisplay()
{
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    delay(1000);

    display.clearDisplay();

    // init output
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(">> co2 meter <<");

    display.display();
}

JsonDocument getInfoJson()
{
    JsonDocument doc;
    doc["version"] = version;

    JsonObject system = doc.createNestedObject("system");
    system["deviceId"] = getChipId();
    system["freeHeap"] = ESP.getFreeHeap(); // in V

    // network
    JsonObject network = doc.createNestedObject("network");
    int8_t rssi = WiFi.RSSI();
    network["wifiRssi"] = rssi;
    network["wifiQuality"] = getRssiAsQuality(rssi);
    network["wifiSsid"] = WiFi.SSID();
    network["ip"] = WiFi.localIP().toString();
    network["mac"] = WiFi.macAddress();

    // CO2 meter
    JsonObject co2Meter = doc.createNestedObject("co2");
    co2Meter["isPreheating"] = co2Sensor.isPreHeating();
    co2Meter["temperature"] = lastTemperature;
    co2Meter["ppm"] = lastCo2Value > 0 ? lastCo2Value : 0;

    return doc;
}

void setupWebServer()
{
    server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *request)
              {
StringStream stream;
auto size = serializeJson(getInfoJson(), stream);

request->send(stream, "application/json", size); });

    server.begin();
}

void onMqttConnect(bool sessionPresent)
{
    isMqttConnected = true;

    Serial.println("mqtt connected");

    publishHomeAssistantDiscovery();

    /*const char *subscribeTopic = getMqttTopic("in/#");

    Serial.print("mqtt subscribe: ");
    Serial.println(subscribeTopic);

    mqttClient.subscribe(subscribeTopic, 1);
    mqttClient.publish(getMqttTopic("out/connected"), 1, false);

    sendInfo();*/
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    Serial.println("Disconnected from MQTT.");
    isMqttConnected = false;

    if (WiFi.isConnected())
        xTimerStart(mqttReconnectTimer, 0);
}

void setupMqtt()
{
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    // mqttClient.onSubscribe(onMqttSubscribe);
    // mqttClient.onUnsubscribe(onMqttUnsubscribe);
    // mqttClient.onMessage(onMqttMessage);
    // mqttClient.onPublish(onMqttPublish);
}

void connectToWifi()
{
    if (WiFi.isConnected())
    {
        Serial.println("Already connected to WiFi");
        return;
    }

    auto isConnected = wifiManager.autoConnect(AP_NAME, AP_PASSWORD);

    if (isConnected)
    {
        Serial.println("connected to wifi");

        strcpy(mqtt_server, custom_mqtt_server.getValue());
        strcpy(mqtt_port, custom_mqtt_port.getValue());
    }
    else
        Serial.println("config portal running");
}
void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600);
    LittleFS.begin(true);

    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

    setupMqtt();
    setupWifi();
    setupWifiManager();
    setupOTA();
    setupWebServer();

    co2Sensor.setDebug(true);
    setupDisplay();
}

void loop()
{
    ArduinoOTA.handle();

    if (co2Sensor.isPreHeating())
    {
        if (appState != 1)
        {
            appState = 1;

            display.clearDisplay();
            display.setTextSize(1);

            display.setCursor(0, 0);
            display.print("preheating...");

            display.display();
        }
    }
    else
    {
        appState = 2;

        if (lastCo2Measurement == 0 || millis() - lastCo2Measurement >= READ_SENSOR_INTERVAL)
        {
            lastTemperature = co2Sensor.getLastTemperature();
            lastCo2Value = co2Sensor.readCO2UART();

            Serial.print("temperature: ");
            Serial.println(lastTemperature);

            Serial.print("co2: ");
            Serial.println(lastCo2Value);

            publishSensorState(lastCo2Value);

            display.clearDisplay();
            display.setTextSize(2);

            display.setCursor(0, 0);

            display.setCursor(0, 10);
            display.print("CO2: ");
            display.print(lastCo2Value);

            if (lastCo2Value >= CO2_WARN_PPM)
            {
                display.setCursor(0, 40);
                display.setTextSize(2);
                display.print("VENTILATE");
            }

            display.display();

            lastCo2Measurement = millis();
        }
    }
}
