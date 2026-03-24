#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

// =============================
// Hardware
// =============================
#define LED_PIN 2
#define PIN_SENSOR 34

// =============================
// ADC e calibração
// =============================
const float VREF = 3.3;
const int ADC_RES = 4095;

float calibration = 597;
float offset = -46;

const int samples = 2000;

float filtered = 0;
float prevSample = 0;
float alpha = 0.995;

// =============================
// Constantes
// =============================
bool KEEP_ON = true;
bool KEEP_OFF = false;

// =============================
// WiFi
// =============================
const char* WIFI_SSID     = "SSID";
const char* WIFI_PASSWORD = "PWD";

const String API_URL = "http://your_server:8000/update/";

const unsigned long SENSORS_READ_INTERVAL = 5000;
const unsigned long LED_BLINK_INTERVAL = 100;

WiFiClient wifiClient;

// =============================
// Hardware
// =============================
void setupHardware() {

  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SENSOR, ADC_11db);

}

// =============================
// Leitura da tensão
// =============================
float readVoltageRMS()
{
  float sum = 0;

  for(int i=0;i<samples;i++)
  {
    int adc = analogRead(PIN_SENSOR);

    float voltage = (adc * VREF) / ADC_RES;

    filtered = alpha * (filtered + voltage - prevSample);

    prevSample = voltage;

    sum += filtered * filtered;

    delayMicroseconds(150);
  }

  float mean = sum / samples;

  float sensorRMS = sqrt(mean);

  float Vrms = (sensorRMS * calibration) + offset;

  if(Vrms < 0) Vrms = 0;

  return Vrms;
}

// =============================
// OTA
// =============================
void setupOTA() {

  ArduinoOTA.setHostname("ESP32");
  ArduinoOTA.setPassword("12345678");

  ArduinoOTA
    .onStart([]() {
      Serial.println("Iniciando atualização OTA");
      digitalWrite(LED_PIN, LOW);
    })
    .onEnd([]() {
      Serial.println("\nAtualização concluída");
      digitalWrite(LED_PIN, HIGH);
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progresso: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Erro[%u]\n", error);
      digitalWrite(LED_PIN, HIGH);
    });

  ArduinoOTA.begin();
}

// =============================
// WiFi
// =============================
bool connectToWiFi() {

  Serial.print("\nConectando ao WiFi");

  WiFi.disconnect(true);
  delay(100);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 10; i++) {

    if (WiFi.status() == WL_CONNECTED) {

      Serial.println(" Conectado!");

      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

      setupOTA();

      blinkLED(1, KEEP_ON);

      return true;
    }

    delay(500 * (i + 1));

    Serial.print(".");
  }

  Serial.println("\nFalha na conexão");

  return false;
}

// =============================
// JSON
// =============================
void addSensorData(JsonArray& array, const char* type, float value) {

  JsonObject sensor = array.createNestedObject();

  sensor["type"] = type;
  sensor["value"] = value;

}

// =============================
// Envio para servidor
// =============================
void sendVoltageToServer(float voltage) {

  HTTPClient http;

  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;

  // Identificação do dispositivo
  doc["device"] = "ESP32";
  doc["mac"] = WiFi.macAddress();

  // Dados do sensor (formato esperado pela API)
  JsonObject data = doc.createNestedObject("data");
  data["voltA"] = voltage;

  String json;
  serializeJson(doc, json);

  Serial.println("Enviando JSON:");
  Serial.println(json);

  int httpCode = http.POST(json);

  if (httpCode > 0) {
    Serial.printf("HTTP Code: %d\n", httpCode);
    blinkLED(2, KEEP_ON);
  } else {
    Serial.printf("Erro envio: %d\n", httpCode);
    blinkLED(5, KEEP_ON);
  }

  http.end();
}

// =============================
// LED
// =============================
void blinkLED(int times, bool keepOn) {

  for (int i = 0; i < times; i++) {

    digitalWrite(LED_PIN, keepOn ? LOW : HIGH);

    delay(LED_BLINK_INTERVAL);

    digitalWrite(LED_PIN, keepOn ? HIGH : LOW);

    delay(LED_BLINK_INTERVAL);

  }
}

// =============================
// Setup
// =============================
void setup() {

  setupHardware();

  connectToWiFi();

}

// =============================
// Loop
// =============================
void loop() {

  ArduinoOTA.handle();

  static unsigned long lastReadTime = 0;

  if (millis() - lastReadTime >= SENSORS_READ_INTERVAL) {

    if (WiFi.status() == WL_CONNECTED) {

      float voltage = readVoltageRMS();

      Serial.print("Tensão da rede: ");
      Serial.print(voltage,2);
      Serial.println(" V");

      sendVoltageToServer(voltage);

    } else {

      connectToWiFi();

    }

    lastReadTime = millis();

  }
}