#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "Arduino.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>

#define TRIGGER_PIN GPIO_NUM_12
#define ECHO_PIN GPIO_NUM_17
#define LED_GREEN_PIN GPIO_NUM_2
#define LED_YELLOW_PIN GPIO_NUM_4
#define LED_RED_PIN GPIO_NUM_5
#define OLED_ADDR 0x3C

static xQueueHandle distance_queue;
static SemaphoreHandle_t my_semaphore;
static Adafruit_SSD1306 display(128, 64, &Wire, -1);

volatile uint32_t ultrasonic_stack_highwater = 0;
volatile uint32_t led_stack_highwater = 0;

const char* ssid = "NARU - 2.4";
const char* password = "a1520Z81*";

WebServer server(80);

bool isGreenLEDOn = false;
bool isYellowLEDOn = false;
bool isRedLEDOn = false;

void connectToWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
  }
  Serial.println("Conectado ao WiFi!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
}

void hc_sr04_init() {
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
}

float hc_sr04_get_distance_cm() {
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  return (duration / 2.0) * 0.0343;
}

void ultrasonic_task(void *pvParameters) {
  hc_sr04_init();

  while (1) {
    float distance = hc_sr04_get_distance_cm();

    xQueueSend(distance_queue, &distance, portMAX_DELAY);
    xSemaphoreGive(my_semaphore);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Define a posição do texto
    int xPos = 0;
    int yPos = 0;

    display.setCursor(xPos, yPos);
    display.print("Distancia: ");
    display.print(distance);
    display.print(" cm");
    display.display();

    // Atualize o estado dos LEDs
    if (distance > 30) {
      isGreenLEDOn = true;
      isYellowLEDOn = false;
      isRedLEDOn = false;
    } else if (distance <= 30 && distance >= 15) {
      isGreenLEDOn = false;
      isYellowLEDOn = true;
      isRedLEDOn = false;
    } else {
      isGreenLEDOn = false;
      isYellowLEDOn = false;
      isRedLEDOn = true;
    }

    uint32_t stack_used = uxTaskGetStackHighWaterMark(NULL);
    if (stack_used > ultrasonic_stack_highwater) {
      ultrasonic_stack_highwater = stack_used;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void led_task(void *pvParameters) {
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  while (1) {
    float distance;

    if (xSemaphoreTake(my_semaphore, portMAX_DELAY) == pdTRUE) {
      if (xQueueReceive(distance_queue, &distance, 0) == pdTRUE) {
        if (isGreenLEDOn) {
          digitalWrite(LED_GREEN_PIN, HIGH);
          digitalWrite(LED_YELLOW_PIN, LOW);
          digitalWrite(LED_RED_PIN, LOW);
        } else if (isYellowLEDOn) {
          digitalWrite(LED_GREEN_PIN, LOW);
          digitalWrite(LED_YELLOW_PIN, HIGH);
          digitalWrite(LED_RED_PIN, LOW);
        } else {
          digitalWrite(LED_GREEN_PIN, LOW);
          digitalWrite(LED_YELLOW_PIN, LOW);
          digitalWrite(LED_RED_PIN, HIGH);
        }
      }
    }

    uint32_t stack_used = uxTaskGetStackHighWaterMark(NULL);
    if (stack_used > led_stack_highwater) {
      led_stack_highwater = stack_used;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void handleRoot() {
  String html = "<html><body style='text-align: center;'>";
  html += "<h1>Projeto Final - Curso de Extensão</h1>";
  html += "<div class='traffic-light-container'>";
  html += "<div class='traffic-light'>";
  html += "<div class='light red' id='red-light'></div>";
  html += "<div class='light yellow' id='yellow-light'></div>";
  html += "<div class='light green' id='green-light'></div>";
  html += "</div>";
  html += "</div>";
  html += "<p>Distância: <span id='distance'></span> cm</p>";
  html += "<style>";
  html += ".traffic-light-container { display: inline-block; vertical-align: middle; }";
  html += ".traffic-light { width: 60px; height: 180px; background: #333; margin: 10px auto; }";
  html += ".light { width: 50px; height: 50px; border-radius: 50%; margin: 10px auto; }";
  html += ".red { background-color: ";
  html += isRedLEDOn ? "red" : "white";
  html += "; }";
  html += ".yellow { background-color: ";
  html += isYellowLEDOn ? "yellow" : "white";
  html += "; }";
  html += ".green { background-color: ";
  html += isGreenLEDOn ? "green" : "white";
  html += "; }";
  html += "</style>";
  html += "<script>";
  html += "setInterval(function() {";
  html += "  fetch('/distance')";
  html += "    .then(response => response.text())";
  html += "    .then(data => {";
  html += "      document.getElementById('distance').textContent = data;";
  html += "    });";
  html += "  fetch('/leds')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      document.getElementById('red-light').style.backgroundColor = data.red ? 'red' : 'white';";
  html += "      document.getElementById('yellow-light').style.backgroundColor = data.yellow ? 'yellow' : 'white';";
  html += "      document.getElementById('green-light').style.backgroundColor = data.green ? 'green' : 'white';";
  html += "    });";
  html += "}, 1000);";
  html += "</script>";
  html += "</body></html>";
  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}




String getDistance() {
  return String(hc_sr04_get_distance_cm());
}

void handleLEDs() {
  String json = "{\"red\":" + String(isRedLEDOn ? "true" : "false") + ",";
  json += "\"yellow\":" + String(isYellowLEDOn ? "true" : "false") + ",";
  json += "\"green\":" + String(isGreenLEDOn ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void monitor_task(void *pvParameters) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    size_t free_heap = esp_get_free_heap_size();
    uint32_t ultrasonic_stack = ultrasonic_stack_highwater;
    uint32_t led_stack = led_stack_highwater;

    Serial.print("Heap Livre: ");
    Serial.print(free_heap);
    Serial.println(" bytes");
    Serial.print("Stack Ultrasonic: ");
    Serial.print(ultrasonic_stack);
    Serial.println(" bytes");
    Serial.print("Stack LED: ");
    Serial.print(led_stack);
    Serial.println(" bytes");

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  connectToWiFi();

  distance_queue = xQueueCreate(1, sizeof(float));
  my_semaphore = xSemaphoreCreateBinary();

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.display();
  delay(2000);
  display.clearDisplay();

  xTaskCreate(ultrasonic_task, "ultrasonic_task", 4096, NULL, 5, NULL);
  xTaskCreate(led_task, "led_task", 4096, NULL, 4, NULL);
  xTaskCreate(monitor_task, "monitor_task", 4096, NULL, 3, NULL);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/distance", HTTP_GET, []() {
    server.send(200, "text/plain", getDistance());
  });
  server.on("/leds", HTTP_GET, handleLEDs);
  server.begin();
}

void loop() {
  server.handleClient();
}
