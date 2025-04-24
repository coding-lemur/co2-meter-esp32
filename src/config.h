#define HOST_NAME "co2-meter"
#define AP_PASSWORD "co42meter"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define READ_SENSOR_INTERVAL 60000 // in ms
#define MQTT_UPDATE_INTERVAL 45000 // in ms

const int CO2_WARN_PPM = 1200;
const char *CONFIG_FILE_PATH = "/config.json";

const String VERSION = "2.0.0-beta";
