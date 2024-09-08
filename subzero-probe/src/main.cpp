#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <time.h>

#define WIFI_ENABLED true
#define AHT_ENABLED false
#define DEBUG true

const int MAX_BUFFER_SIZE = 5;
const int STATUS_LED_PIN = 2;

// WiFi credentials.
const char *WIFI_SSID = "SSID";
const char *WIFI_PASS = "PW";
const char *TIMEZONE = "Australia/Sydney";
const char *REPORT_ENDPOINT = "https://localhost:55155/v1/report";

typedef struct
{
  float temperature;
  float humidity;
  time_t time;
} SensorData;

void wifi_connect();
void setup_ntp();
void log_temperature(const SensorData *sensorData);
void led(bool on);
void report();
void print_debug();

Adafruit_AHTX0 aht;
SensorData sensor_buffer[MAX_BUFFER_SIZE];
int sensor_buffer_count = 0;

void setup()
{
  Serial.begin(9600);
  Serial.setTimeout(2000);
  pinMode(STATUS_LED_PIN, OUTPUT);
  led(true);

  // Wait for serial to initialize.
  // while (!Serial) {}

  Serial.println("Booting...");

  // Wait for AHT to start
  if (AHT_ENABLED)
  {
    while (!aht.begin())
    {
      Serial.println("Could not find AHT20. Waiting...");
      delay(5000);
    }
  }

  // Connect to wifi
  if (WIFI_ENABLED)
    wifi_connect();

  // Sync time
  if (WIFI_ENABLED)
    setup_ntp();

  // Turn off LED as we have finished booting
  Serial.println("Bootup complete");
  led(false);
}

void loop()
{
  auto *sensorData = &sensor_buffer[sensor_buffer_count];
  sensorData->time = time(nullptr);

  // Read sensor data
  if (AHT_ENABLED)
  {
    sensors_event_t humidity, temperature;
    aht.getEvent(&humidity, &temperature);

    sensorData->humidity = humidity.relative_humidity;
    sensorData->temperature = temperature.temperature;
  }

  // Push into buffer
  if (++sensor_buffer_count == MAX_BUFFER_SIZE)
  {
    Serial.println("Send data");

    led(true);
    sensor_buffer_count = 0;
    report();
    led(false);
  }

  // Log to serial
  if (DEBUG)
  {
    log_temperature(sensorData);
    print_debug();
  }

  // Sleep until next sensor read
  delay(5000);
}

void print_debug()
{
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
}

void setup_ntp()
{
  // Config time
  configTime(TIMEZONE, "pool.ntp.org");

  // Wait for time to sync
  while (time(nullptr) < 1500000000)
  {
    delay(500);
  }

  Serial.println("Time set");
}

void led(bool on)
{
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
}

void wifi_connect()
{
  // If Wifi is already connected, don't try to reconnect
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // WiFi fix: https://github.com/esp8266/Arduino/issues/2186
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    // Check to see if
    if (WiFi.status() == WL_CONNECT_FAILED)
    {
      Serial.println("Failed to connect to WiFi.");
      delay(10000);
    }

    delay(1000);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void report()
{
  JsonDocument doc;

  auto dataArray = doc["data"];
  doc["deviceAddress"] = WiFi.macAddress();

  Serial.println("Serialising json");

  // Populate JSON document
  for (SensorData &item : sensor_buffer)
  {
    auto jsonObj = dataArray.add<JsonObject>();
    jsonObj["temperature"] = item.temperature;
    jsonObj["humidity"] = item.humidity;

    auto timeInfo = *gmtime(&item.time);
    char timeBuf[21];
    strftime(timeBuf, sizeof(timeBuf), "%FT%TZ", &timeInfo);
    jsonObj["time"] = timeBuf;
  }

  // Serialize JSON to string
  String json;
  serializeJson(doc, json);

  // Setup HTTP client and send data
  if (DEBUG)
  {
    Serial.println("Posting data to " + String(REPORT_ENDPOINT));
    Serial.println(json);
  }

  WiFiClientSecure client;
  HTTPClient http;
  http.begin(client, REPORT_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  auto httpStatusCode = http.POST(json);
  http.end();

  if (DEBUG)
    Serial.println("Respose StatusCode: " + String(httpStatusCode));
}

void log_temperature(const SensorData *sensorData)
{
  // Only log if we have a serial to write too
  if (!Serial.availableForWrite())
    return;

  Serial.print('[');
  Serial.print(sensorData->time);
  Serial.print("] ");
  Serial.print(sensorData->temperature);
  Serial.print(" C | ");
  Serial.print(sensorData->temperature);
  Serial.print(" %");
  Serial.println();
}