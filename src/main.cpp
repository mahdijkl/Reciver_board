#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"
#include <queue>

// const char *ssidChar = "Joshani";
// const char *passwordChar = "Arefi1400#";

uint8_t sensorNodeMacAddress[] = {0xD4, 0x8A, 0xFC, 0x9F, 0x6B, 0x74};
int retryEspNowCount = 0;

// const char *PARAM_INPUT_1 = "ssid";
// const char *PARAM_INPUT_2 = "pass";
// const char *PARAM_INPUT_3 = "ip";
// const char *PARAM_INPUT_4 = "gateway";
// const char *PARAM_INPUT_5 = "serverUrl";
// const char *PARAM_INPUT_6 = "apikey";
// const char *PARAM_INPUT_7 = "telegramApiToken";
// const char *PARAM_INPUT_8 = "telegramChatId";

// Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;
String serverUrl;
String apiKey;
String telegramApiToken;
String telegramChatId;
String telegramUrl;

// File paths to save input values permanently
const char *ssidPath = "/ssid.txt";
const char *passPath = "/pass.txt";
const char *ipPath = "/ip.txt";
const char *gatewayPath = "/gateway.txt";
const char *serverUrlPath = "/serverUrl.txt";
const char *apikeyPath = "/apikey.txt";
const char *telegramApiTokenPath = "/telegramApiToken.txt";
const char *telegramChatIdPath = "/telegramChatId.txt";

IPAddress localIP;
// IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
// IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 0, 0);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000; // interval to wait for Wi-Fi connection (milliseconds)

// String apiKey = "16d7a4fca7442dda3ad93c9a726597e4";
// String serverUrl = "http://188.213.196.180:8007/api/motion";

// String telegramApiToken = "188428767:AEhcC4SpIDyZbMHeqgU8OAWeaJGF4KqzGIFGDVVn";
// String telegramChatId = "1292108244";

bool motionDetected = false;
bool motionStopped = false;

const int LED_PIN = 19;

typedef struct struct_message
{
  bool motionDetected;
} struct_message;

struct_message incomingData;

typedef struct WifiChannel
{
  int32_t wifiChannel;
} WifiChannel;

WifiChannel myData;

std::queue<struct_message> messageQueue;
esp_now_peer_info_t peerInfo;

// Initialize LittleFS
void initLittleFS()
{
  if (!LittleFS.begin(true))
  {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

// Read File from LittleFS
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

// Write file to LittleFS
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

void postTelegramMessage(String message)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    telegramUrl = "https://tapi.bale.ai/bot" + telegramApiToken + "/sendMessage";
    http.begin(telegramUrl);
    http.addHeader("Content-Type", "application/json");

    String postData = "{\"chat_id\":\"" + telegramChatId + "\",\"text\":\"" + message + "\"}";

    int httpResponseCode = http.POST(postData);
    int retryCount = 0;
    while (httpResponseCode <= 0 && retryCount < 3)
    {
      retryCount++;
      Serial.print("Retrying POST request... Attempt: ");
      Serial.println(retryCount);
      httpResponseCode = http.POST(postData);
    }
    if (httpResponseCode > 0)
    {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    }
    else
    {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end();
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
      Serial.print("Retrying POST request... Attempt: ");
      Serial.println(retryCount);
      httpResponseCode = http.POST(postData);
    }

    if (httpResponseCode > 0)
    {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    }
    else
    {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
}

void sendCallback(const uint8_t *macAddr, esp_now_send_status_t status)
{
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  if ((!(status == ESP_NOW_SEND_SUCCESS)) && retryEspNowCount < 70)
  {
    retryEspNowCount++;
    esp_now_send(sensorNodeMacAddress, (uint8_t *)&myData, sizeof(myData));
  }
}

void onReceive(const uint8_t *macAddr, const uint8_t *data, int len)
{
  memcpy(&incomingData, data, sizeof(incomingData));
  messageQueue.push(incomingData);
}

void setupESPNow()
{
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(sendCallback);
  memcpy(peerInfo.peer_addr, sensorNodeMacAddress, 6);
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
    return;
  }

  myData.wifiChannel = WiFi.channel();
  esp_err_t result = esp_now_send(sensorNodeMacAddress, (uint8_t *)&myData, sizeof(myData));
  if (result == ESP_OK)
  {
    Serial.println("Sent with success");
    retryEspNowCount = 0;
  }
  else
  {
    Serial.print("Error sending the data: ");
    Serial.println(result);
  }
}

// Initialize WiFi
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

  Serial.println(WiFi.localIP());

  return true;
}

void initialSetup()
{
  AsyncWebServer server(80);

  Serial.println("Setting AP (Access Point)");

  WiFi.softAP("ESP-WIFI-MANAGER", "password");

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/wifimanager.html", "text/html"); });

  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
            {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == "ssid") {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(LittleFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == "pass") {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(LittleFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == "ip") {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            writeFile(LittleFS, ipPath, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == "gateway") {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Write file to save value
            writeFile(LittleFS, gatewayPath, gateway.c_str());
          }
          if (p->name() == "serverUrl") {
            serverUrl = p->value().c_str();
            Serial.print("serverUrl set to: ");
            Serial.println(serverUrl);
            // Write file to save value
            writeFile(LittleFS, serverUrlPath, serverUrl.c_str());
          }
          if (p->name() == "apikey") {
            apiKey = p->value().c_str();
            Serial.print("apiKey set to: ");
            Serial.println(apiKey);
            // Write file to save value
            writeFile(LittleFS, apikeyPath, apiKey.c_str());
          }
          if (p->name() == "telegramApiToken") {
            telegramApiToken = p->value().c_str();
            Serial.print("telegramApiToken set to: ");
            Serial.println(telegramApiToken);
            // Write file to save value
            writeFile(LittleFS, telegramApiTokenPath, telegramApiToken.c_str());
          }
          if (p->name() == "telegramChatId") {
            telegramChatId = p->value().c_str();
            Serial.print("telegramChatId set to: ");
            Serial.println(telegramChatId);
            // Write file to save value
            writeFile(LittleFS, telegramChatIdPath, telegramChatId.c_str());
          }
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart(); });
  server.begin();
}

void loadValue()
{
  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);
  ip = readFile(LittleFS, ipPath).c_str();
  gateway = readFile(LittleFS, gatewayPath).c_str();
  serverUrl = readFile(LittleFS, serverUrlPath).c_str();
  apiKey = readFile(LittleFS, apikeyPath).c_str();
  telegramApiToken = (String)readFile(LittleFS, telegramApiTokenPath);
  telegramChatId = (String)readFile(LittleFS, telegramChatIdPath);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(serverUrl);
  Serial.println(apiKey);
  Serial.println(telegramApiToken);
  Serial.println(telegramChatId);
}

void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  initLittleFS();
  loadValue();

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
  while (!messageQueue.empty())
  {
    struct_message msg = messageQueue.front();
    messageQueue.pop();

    if (msg.motionDetected)
    {
      digitalWrite(LED_PIN, HIGH);
      postTelegramMessage("Motion detected!");
      postMessage("1");
    }
    else
    {
      digitalWrite(LED_PIN, LOW);
      postTelegramMessage("Motion stopped!");
      postMessage("0");
    }
    delay(1000);
  }
}
