#define AP_NAME "co2-meter AP"
#define AP_PASSWORD "co42meter"

#define HOST_NAME "co2-meter"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define READ_SENSOR_INTERVAL 60000 // in ms
#define MQTT_UPDATE_INTERVAL 45000 // in ms

int CO2_WARN_PPM = 1200;
