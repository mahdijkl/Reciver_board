#include <Arduino.h>

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"
#include <queue>

AsyncWebServer server(80);

uint8_t sensorNodeMacAddress[] = {0xD4, 0x8A, 0xFC, 0x9F, 0x6B, 0x74};
int retryEspNowCount = 0;

String ssid;
String pass;
String serverUrl;
String apiKey;

unsigned long previousMillis = 0;
const long interval = 10000; // interval to wait for Wi-Fi connection (milliseconds)

bool motionDetected = false;
bool motionStopped = false;

const int LED_PIN = 14;
const int BUTTON_PIN = 12;

typedef struct income_message
{
  bool motionDetected;
} income_message;

income_message incomingData;

typedef struct outcome_message
{
  int32_t wifiChannel;
} outcome_message;

outcome_message myData;

std::queue<income_message> messageQueue;
esp_now_peer_info_t peerInfo;

void initLittleFS()
{
  if (!LittleFS.begin(true))
  {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

String readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;

  while (file.available())
  {
    fileContent = file.readStringUntil('\n');
    break;
  }
  fileContent.trim();
  return fileContent;
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- file written");
  }
  else
  {
    Serial.println("- write failed");
  }
}

void deleteFile(fs::FS &fs, const char *path)
{
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path))
  {
    Serial.println("- file deleted");
  }
  else
  {
    Serial.println("- delete failed");
  }
}

void postMessage(String message)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    String postData = "{\"apiKey\":\"" + apiKey + "\",\"detected\":\"" + message + "\"}";

    int httpResponseCode = http.POST(postData);
    int retryCount = 0;
    while (httpResponseCode <= 0 && retryCount < 3)
    {
      retryCount++;
      httpResponseCode = http.POST(postData);
    }
    Serial.println("response code: " + String(httpResponseCode));

    http.end();
  }
}

bool isSent = false;
bool isFinished = false;
void sendCallback(const uint8_t *macAddr, esp_now_send_status_t status)
{
  Serial.print("Send Status: ");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  Serial.print(" on channel ");
  Serial.println(WiFi.channel());

  if (status == ESP_NOW_SEND_SUCCESS)
  {
    isSent = true;
  }
  isFinished = true;
}

void onReceive(const uint8_t *macAddr, const uint8_t *data, int len)
{
  memcpy(&incomingData, data, sizeof(incomingData));
  messageQueue.push(incomingData);
}

int getWiFiChannel(const char *ssidName)
{
  int channel = 0;
  int n = WiFi.scanNetworks(false, true);

  for (int i = 0; i < n; i++)
  {
    if (strcmp(ssidName, WiFi.SSID(i).c_str()) == 0)
    {
      channel = WiFi.channel(i);
      break;
    }
  }

  return channel;
}

int tmpChannel;

void sendChannelNumber()
{
  WiFi.mode(WIFI_AP);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(sendCallback);
  memcpy(peerInfo.peer_addr, sensorNodeMacAddress, 6);
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
    return;
  }

  myData.wifiChannel = getWiFiChannel(ssid.c_str());
  for (int i = 0; i < 65; i++)
  {
    if (isSent)
    {
      break;
    }
    tmpChannel = (i % 13) + 1;
    esp_wifi_set_channel(tmpChannel, WIFI_SECOND_CHAN_NONE);
    esp_err_t result = esp_now_send(sensorNodeMacAddress, (uint8_t *)&myData, sizeof(myData));
    if (result == ESP_OK)
    {
      Serial.println("Sent with success");
    }
    else
    {
      Serial.print("Error sending the data: ");
      Serial.println(result);
    }

    while (!isFinished)
    {
      delay(10);
    }
    isFinished = false;
  }
  esp_now_deinit();
}

void setupESPNow()
{
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onReceive);
}

bool initWiFi()
{
  if (ssid == "")
  {
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_AP);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED)
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      Serial.println("Failed to connect.");
      return false;
    }
  }
  Serial.println("Connected to WiFi.");
  Serial.println(WiFi.localIP());

  return true;
}

void initialSetup()
{

  Serial.println("Setting AP (Access Point)");
  WiFi.softAP("ESP-WIFI-MANAGER", "password");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/wifimanager.html", "text/html"); });

  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
            {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){

          if (p->name() == "ssid") {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            writeFile(LittleFS, "/ssid.txt", ssid.c_str());
          }

          if (p->name() == "pass") {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            writeFile(LittleFS, "/pass.txt", pass.c_str());
          }
          if (p->name() == "serverUrl") {
            serverUrl = p->value().c_str();
            Serial.print("serverUrl set to: ");
            Serial.println(serverUrl);
            writeFile(LittleFS, "/serverUrl.txt", serverUrl.c_str());
          }
          if (p->name() == "apikey") {
            apiKey = p->value().c_str();
            Serial.print("apiKey set to: ");
            Serial.println(apiKey);
            writeFile(LittleFS, "/apikey.txt", apiKey.c_str());
          }

        }
      }
      request->send(200, "text/plain", "Done. ESP will restart.");
      delay(3000);
      ESP.restart(); });
  server.begin();
}

void loadValue()
{
  ssid = readFile(LittleFS, "/ssid.txt");
  pass = readFile(LittleFS, "/pass.txt");
  serverUrl = readFile(LittleFS, "/serverUrl.txt");
  apiKey = readFile(LittleFS, "/apikey.txt");
}

void resetModule()
{
  deleteFile(LittleFS, "/ssid.txt");
  deleteFile(LittleFS, "/pass.txt");
  deleteFile(LittleFS, "/serverUrl.txt");
  deleteFile(LittleFS, "/apikey.txt");
  ESP.restart();
}

void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  initLittleFS();
  loadValue();
  Serial.print(ssid + " channel: ");
  Serial.println(getWiFiChannel(ssid.c_str()));

  sendChannelNumber();

  if (!initWiFi())
  {
    initialSetup();
  }
  else
  {
    setupESPNow();
  }
}

void loop()
{
  if (digitalRead(BUTTON_PIN) == HIGH)
  {
    resetModule();
  }
  while (!messageQueue.empty())
  {
    income_message msg = messageQueue.front();
    messageQueue.pop();

    if (msg.motionDetected)
    {
      digitalWrite(LED_PIN, HIGH);
      postMessage("1");
    }
    else
    {
      digitalWrite(LED_PIN, LOW);
      postMessage("0");
    }
    delay(1000);
  }
}
