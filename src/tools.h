#include <WiFi.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

String getChipId()
{
    uint64_t chipId = ESP.getEfuseMac(); // 64-Bit MAC-Adresse
    String chipIdStr = String((uint32_t)(chipId >> 32), HEX) + String((uint32_t)chipId, HEX);
    return chipIdStr;
}

double round2(double value)
{
    return (int)(value * 100 + 0.5) / 100.0;
}

int getRssiAsQuality(int rssi)
{
    int quality = 0;

    if (rssi <= -100)
        quality = 0;
    else if (rssi >= -50)
        quality = 100;
    else
        quality = 2 * (rssi + 100);

    return quality;
}

unsigned long getUnixTime()
{
    time_t now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
        return 0;

    time(&now);
    return now; // unix time
}

void loadFilesToArray(File root, JsonArray list)
{
    File file = root.openNextFile();

    while (file)
    {
        list.add(file.name());
        file = root.openNextFile();
    }
}
