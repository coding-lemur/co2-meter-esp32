#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <MHZ.h>
#include <PubSubClient.h>
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
PubSubClient client(espClient);
static AsyncWebServer server(80);

// timers
unsigned long lastCo2Measurement = 0;

byte appState = 0; // 0 = init; 1 = preheating; 2 = ready

int lastTemperature = 0;
int lastCo2Value = 0;

String getChipId()
{
    uint64_t chipId = ESP.getEfuseMac(); // 64-Bit MAC-Adresse
    String chipIdStr = String((uint32_t)(chipId >> 32), HEX) + String((uint32_t)chipId, HEX);
    return chipIdStr;
}

void setupWifi()
{
    WiFi.setHostname(HOST_NAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin();
}

void setupWifiManager()
{
    wifiManager.setDebugOutput(true);

    auto isConnected = wifiManager.autoConnect(AP_NAME, AP_PASSWORD);

    if (isConnected)
    {
        Serial.println("connected to wifi");
    }
    else
    {
        Serial.println("config portal running");
    }
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

void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600);
    LittleFS.begin(true);

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
