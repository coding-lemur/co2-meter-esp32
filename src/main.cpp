//#include <Wire.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFiSettings.h>
#include <SoftwareSerial.h>
#include <MHZ.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>

#include "config.h"

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })
#define DEVICE_ID (Sprintf("%06" PRIx64, ESP.getEfuseMac() >> 24)) // unique device ID
#define uS_TO_S_FACTOR 1000000                                     // Conversion factor for micro seconds to seconds

String version = "1.0.0";

AsyncMqttClient mqttClient;

// timer
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

MHZ co2Sensor(&Serial2, MHZ14A);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// states
bool isUpdating = false;
bool isWifiConnected = false;
bool isMqttConnected = false;
bool isPortalActive = false;
byte appState = 0; // 0 = init; 1 = preheating; 2 = ready

bool isPreheating = true;
bool isCo2SensorReady = false;
int lastTemperature = 0;
int lastCo2Value = 0;

// (old) timers
unsigned long lastInfoSend = 0;
unsigned long lastCo2Measurement = 0;

// settings
String mqtt_host;
uint16_t mqtt_port;
String mqtt_user;
String mqtt_password;

void connectToMqtt()
{
    isMqttConnected = false;

    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void onWiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\n", event);

    switch (event)
    {
    case SYSTEM_EVENT_STA_GOT_IP:
        isWifiConnected = true;

        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());

        if (!isPortalActive)
        {
            connectToMqtt();
        }

        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        isWifiConnected = false;

        Serial.println("WiFi disconnected");

        if (!isPortalActive)
        {
            xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
            xTimerStart(wifiReconnectTimer, 0);
        }

        break;

    default:
        break;
    }
}

int GetRssiAsQuality(int rssi)
{
    int quality = 0;

    if (rssi <= -100)
    {
        quality = 0;
    }
    else if (rssi >= -50)
    {
        quality = 100;
    }
    else
    {
        quality = 2 * (rssi + 100);
    }

    return quality;
}

const char *getMqttTopic(String part)
{
    String topic = "co2-meter/" + DEVICE_ID + "/" + part;

    return topic.c_str();
}

void sendInfo()
{
    DynamicJsonDocument doc(1024);
    doc["version"] = version;

    JsonObject system = doc.createNestedObject("system");
    system["deviceId"] = DEVICE_ID;
    system["freeHeap"] = ESP.getFreeHeap(); // in V

    // network
    JsonObject network = doc.createNestedObject("network");
    int8_t rssi = WiFi.RSSI();
    network["wifiRssi"] = rssi;
    network["wifiQuality"] = GetRssiAsQuality(rssi);
    network["wifiSsid"] = WiFi.SSID();
    network["ip"] = WiFi.localIP().toString();

    // CO2 meter
    JsonObject co2Meter = doc.createNestedObject("co2");
    co2Meter["isPreheating"] = isPreheating;
    co2Meter["isReady"] = isCo2SensorReady;
    co2Meter["temperature"] = lastTemperature;
    co2Meter["ppm"] = lastCo2Value;

    String JS;
    serializeJson(doc, JS);

    mqttClient.publish(getMqttTopic("out/info"), 1, false, JS.c_str());

    lastInfoSend = millis();
}

void onMqttConnect(bool sessionPresent)
{
    isMqttConnected = true;

    Serial.println("mqtt connected");

    const char *subscribeTopic = getMqttTopic("in/#");

    Serial.print("mqtt subscribe: ");
    Serial.println(subscribeTopic);

    mqttClient.subscribe(subscribeTopic, 1);
    mqttClient.publish(getMqttTopic("out/connected"), 1, false);

    sendInfo();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    isMqttConnected = false;

    Serial.println("mqtt disconnected");

    if (WiFi.isConnected() && !isPortalActive)
    {
        xTimerStart(mqttReconnectTimer, 0);
    }
}

void hardReset()
{
    Serial.println("starting hard-reset");

    SPIFFS.format();

    delay(1000);

    ESP.restart();

    //WiFiSettings.portal();
}

void goSleep(unsigned long seconds)
{
    if (seconds <= 0)
    {
        return;
    }

    Serial.print("start deep-sleep for ");
    Serial.print(seconds);
    Serial.println(" seconds");

    StaticJsonDocument<200> doc;
    doc["duration"] = seconds;

    String JS;
    serializeJson(doc, JS);

    mqttClient.publish(getMqttTopic("out/sleep"), 1, false, JS.c_str());

    delay(500);

    esp_sleep_enable_timer_wakeup(seconds * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

void processingMessage(String channel, DynamicJsonDocument doc)
{
    if (channel.equals("info"))
    {
        sendInfo();
    }
    else if (channel.equals("sleep"))
    {
        unsigned long seconds = doc["duration"].as<unsigned long>();
        goSleep(seconds);
    }
    else if (channel.equals("display"))
    {
        bool turnOn = doc["on"].as<bool>();
        uint8_t command = turnOn ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF;

        display.ssd1306_command(command);
    }
    else if (channel.equals("hard-reset"))
    {
        hardReset();
    }
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    if (isUpdating || isPortalActive)
    {
        return;
    }

    try
    {
        // TODO add error handling (e.g. for illegal JSON format)

        String s_payload = String(payload);
        String s_topic = String(topic);
        int last = s_topic.lastIndexOf("/") + 1;
        String channel = s_topic.substring(last);

        DynamicJsonDocument doc(1024);
        deserializeJson(doc, s_payload);

        Serial.print("MQTT topic: " + s_topic);
        Serial.print("MQTT payload: " + s_payload);
        Serial.println("MQTT channel: " + channel);

        processingMessage(channel, doc);
    }
    catch (const std::exception &e)
    {
    }
}

void connectToWifi()
{
    WiFiSettings.connect(true, 30);
}

void setupTimers()
{
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)1, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
}

void setupDisplay()
{
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    delay(1000);

    display.clearDisplay();

    // demo output
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(">> co2 meter <<");

    display.display();
}

void setupOTA()
{
    ArduinoOTA
        .setHostname(WiFiSettings.hostname.c_str())
        .setPassword(WiFiSettings.password.c_str())
        .onStart([]() {
            isUpdating = true;

            String type;

            if (ArduinoOTA.getCommand() == U_FLASH)
            {
                type = "sketch";
            }
            else
            { // U_FS
                type = "filesystem";
            }

            // NOTE: if updating FS this would be the place to unmount FS using FS.end()
            Serial.println("Start updating " + type);

            display.clearDisplay();
            display.setTextSize(1);
            display.print("updating...");

            display.display();
        })
        .onEnd([]() {
            Serial.println("\nEnd");

            isUpdating = false;
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            unsigned int percentValue = progress / (total / 100);

            Serial.printf("Progress: %u%%\r", percentValue);

            display.clearDisplay();

            display.setCursor(0, 0);
            display.setTextSize(1);
            display.print("updating...");

            display.setCursor(0, 40);
            display.setTextSize(2);
            display.print(percentValue);
            display.print("%");

            display.display();
        })
        .onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);

            if (error == OTA_AUTH_ERROR)
            {
                Serial.println("Auth Failed");
            }
            else if (error == OTA_BEGIN_ERROR)
            {
                Serial.println("Begin Failed");
            }
            else if (error == OTA_CONNECT_ERROR)
            {
                Serial.println("Connect Failed");
            }
            else if (error == OTA_RECEIVE_ERROR)
            {
                Serial.println("Receive Failed");
            }
            else if (error == OTA_END_ERROR)
            {
                Serial.println("End Failed");
            }
        })
        .begin();
}

void detect_wakeup_reason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Wakeup caused by external signal using RTC_IO");
        break;

    case ESP_SLEEP_WAKEUP_EXT1:
        Serial.println("Wakeup caused by external signal using RTC_CNTL");
        break;

    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Wakeup caused by timer");

        mqttClient.publish(getMqttTopic("out/wakeup"), 1, false, "timer");

        break;

    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        Serial.println("Wakeup caused by touchpad");
        break;

    case ESP_SLEEP_WAKEUP_ULP:
        Serial.println("Wakeup caused by ULP program");
        break;

    default:
        Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600);

    //co2Sensor.setDebug(true);

    SPIFFS.begin(true); // On first run, will format after failing to mount
    //SPIFFS.format(); // TODO reset config on connection MQTT fail

    setupTimers();

    WiFi.onEvent(onWiFiEvent);

    WiFiSettings.secure = true;
    WiFiSettings.hostname = "co2-meter-"; // will auto add device ID
    WiFiSettings.password = PASSWORD;

    // Set callbacks to start OTA when the portal is active
    WiFiSettings.onPortal = []() {
        isPortalActive = true;

        Serial.println("WiFi config portal active");

        setupOTA();
    };
    WiFiSettings.onPortalWaitLoop = []() {
        ArduinoOTA.handle();
    };

    WiFiSettings.onConfigSaved = []() {
        ESP.restart();
    };

    // define custom settings
    mqtt_host = WiFiSettings.string("mqtt_host", "192.168.1.1");
    mqtt_port = WiFiSettings.integer("mqtt_port", 1883);
    mqtt_user = WiFiSettings.string("mqtt_user");
    mqtt_password = WiFiSettings.string("mqtt_password");

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqtt_host.c_str(), mqtt_port);

    if (mqtt_user != "")
    {
        mqttClient.setCredentials(mqtt_user.c_str(), mqtt_password.c_str());
    }

    if (!isPortalActive)
    {
        connectToWifi();
    }

    setupOTA();
    setupDisplay();

    detect_wakeup_reason();
}

void loop()
{
    ArduinoOTA.handle();

    if (!isPortalActive && !isUpdating)
    {
        isPreheating = co2Sensor.isPreHeating();
        isCo2SensorReady = co2Sensor.isReady();

        if (isPreheating)
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

            if (lastCo2Measurement == 0 || millis() - lastCo2Measurement >= 60000) // every minute
            {
                lastTemperature = co2Sensor.getLastTemperature();
                lastCo2Value = co2Sensor.readCO2UART();

                Serial.print("temperature: ");
                Serial.println(lastTemperature);

                Serial.print("co2: ");
                Serial.println(lastCo2Value);

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

        if (isWifiConnected && isMqttConnected)
        {
            if (lastInfoSend == 0 || millis() - lastInfoSend >= 45000) // every 45 seconds
            {
                sendInfo(); // TODO move to async timer
            }
        }
    }
}
