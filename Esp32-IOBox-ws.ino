#include <time.h>         // Time library for NTP
#include <WiFi.h>         // WiFi support
#include <Wire.h>         // I2C communication
#include <vector>         // For dynamic array (buffer)
#include <PCF8574.h>      // PCF8574 library for I/O expansion
#include <LittleFS.h>     // File system
#include <WebServer.h>    // HTTP Web server
#include <ArduinoOTA.h>   // Over-The-Air update
#include <HTTPClient.h>   // HTTP Post
#include <ArduinoJson.h>  // JSON serialization/deserialization
#include <PubSubClient.h>   // MQTT
#include <WebSocketsClient.h> // WebSocket client

const String program_version = "v1.1.2"; // Program version

//---------- Configuration structure ----------
struct Config {
  String hardwareId;
  String serverIP;
  String path;
  int monitoringPort;
  String commMode;
  String wifiSSID;
  String wifiPass;
  String accessToken;
};
Config config;

//---------- Object initialization ----------
WebServer server(80);
PCF8574 pcf8574(0x20);
WiFiClient wifiClient;
WebSocketsClient webSocket;
PubSubClient mqttClient(wifiClient);

//---------- Time ----------
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;  // GMT+7 (WIB)
const int   daylightOffset_sec = 0;

//---------- Wifi & Access Point configuration ----------
String ap_ssid = "IoT-Node ";
const char* ap_password = "12345678";
bool isAPMode = true;

bool readytoSend = false;
bool wifiReconnecting = false;
unsigned long apStartTime = 0;
unsigned long lastCountdownPrint = 0;
unsigned long wifiReconnectStart = 0;

const unsigned long apTimeout = 3 * 60 * 1000; // 3 minutes

//---------- Device configuration variables ----------
std::vector<String> monitoringBuffer;
const size_t MAX_BUFFER_SIZE = 100;

//---------- Input and output pins (UPDATED) ----------
const int NUM_PCF_INPUTS = 8;
const int NUM_GPIO_INPUTS = 8;
// Input 1-8: PCF8574 pins 0-7
const uint8_t PCF_PIN_MAP[NUM_PCF_INPUTS] = {0, 1, 2, 3, 4, 5, 6, 7};
// Input 9-16: ESP32 GPIO pins
const int INPUT_GPIO_PINS[NUM_GPIO_INPUTS] = {32, 33, 25, 26, 27, 14, 12, 13};
// Output 1-4: ESP32 GPIO pins
const int OUTPUT_PINS[] = {17, 16, 4, 15};

const int NUM_INPUTS = NUM_PCF_INPUTS + NUM_GPIO_INPUTS;
const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);
uint8_t lastInputStates[NUM_INPUTS] = {0}; // Array to store last input states

//---------- Timing control ----------
unsigned long lastMonitoringTime = 0;
const long monitoringInterval = 200;
unsigned long lastForceSendTime = 0;
const unsigned long forceSendInterval = 20 * 60 * 1000; // 20 minutes

//---------- Default login credentials for web UI ----------
const char* default_username = "contoh";  // Username default
const char* default_password = "password"; // Password default
bool isAuthenticated = false;       // Authentication status

bool loadConfig() {
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println(error.c_str());
    return false;
  }

  config.hardwareId = doc["hardwareId"].as<String>();
  config.serverIP = doc["server"]["serverIP"].as<String>();
  config.path = doc["server"]["path"].as<String>();
  config.monitoringPort = doc["monitoringPort"].as<int>();
  config.commMode = doc["commMode"].as<String>();
  config.wifiSSID = doc["wifi"]["ssid"].as<String>();
  config.wifiPass = doc["wifi"]["password"].as<String>();
  config.accessToken = doc["accessToken"].as<String>();

  Serial.println("Config loaded");
  return true;
}

bool saveConfig() {
  StaticJsonDocument<512> doc;
  doc["hardwareId"] = config.hardwareId;
  JsonObject server = doc.createNestedObject("server");
  server["serverIP"] = config.serverIP;
  server["path"] = config.path;
  doc["monitoringPort"] = config.monitoringPort;
  doc["commMode"] = config.commMode;
  doc["accessToken"] = config.accessToken;
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSSID;
  wifi["password"] = config.wifiPass;
  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write config");
    file.close();
    return false;
  }
  file.close();
  Serial.println("Config saved");
  return true;
}

String loadFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println(String("Failed to open file: ") + path);
    return "";
  }
  String content = file.readString();
  file.close();
  return content;
}

//---------- Data Building and Sending Logic (REWRITTEN) ----------
String buildStatusJSON() {
  StaticJsonDocument<512> doc;
  doc["type"] = "status";
  doc["hardwareId"] = config.hardwareId;
  doc["ip_address"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  doc["firmware_version"] = program_version;
  doc["commMode"] = config.commMode;
  doc["serverIP"] = config.serverIP;
  doc["monitoringPort"] = config.monitoringPort;
  doc["wifiSSID"] = config.wifiSSID;

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

//---------- data konfigurasi ----------
String buildConfigJSON() {
  StaticJsonDocument<512> doc;

  // Mengambil data dari struct Config, mirip dengan saveConfig()
  doc["hardwareId"] = config.hardwareId;

  JsonObject server = doc.createNestedObject("server");
  server["serverIP"] = config.serverIP;
  server["path"] = config.path;

  doc["monitoringPort"] = config.monitoringPort;
  doc["commMode"] = config.commMode;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSSID;

  // Menambahkan data status perangkat yang berguna
  doc["ip_address"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  doc["firmware_version"] = program_version;

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

//-------------- kirim data ke broker MQTT ---------------
void publishConfigMQTT() {
  String configData = buildConfigJSON();
  String configTopic = "iot-node/" + config.hardwareId + "/status";

  if (mqttClient.publish(configTopic.c_str(), configData.c_str())) {
    Serial.println("Topic: " + configTopic);
    Serial.println("Payload: " + configData);
  } else {
    Serial.println("Failed to publish MQTT.");
  }
}

//-------------- Callback pesan MQTT ---------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Pesan diterima MQTT: ");
  Serial.println(topic);
}

//-------------- Reconnect MQTT ---------------
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT Broker...");
    String clientId = "iot-node-" + config.hardwareId + "-" + String(random(0, 1000));
    bool isThingsBoard = (config.serverIP == "mqtt.thingsboard.cloud");

    // Use Access Token as username for ThingsBoard, otherwise connect anonymously.
    const char* user = isThingsBoard ? config.accessToken.c_str() : NULL;

    if (mqttClient.connect(clientId.c_str(), user, NULL)) {
      Serial.println(" Connected!");
      publishStatus(); // Send config on successful connection
    } else {
      Serial.print(" Failed, rc=" + String(mqttClient.state()));
      Serial.println(". Retrying in 5 seconds.");
      delay(5000);
    }
  }
}

//-------------- setup MQTT ---------------
void setupMQTTClient() {
  Serial.println("Communication Mode: MQTT");
  mqttClient.setServer(config.serverIP.c_str(), config.monitoringPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);//ukuran buffer

  readytoSend = true;
}

void setupWebSocket() {
  webSocket.begin(config.serverIP.c_str(), config.monitoringPort, config.path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(500);
}

void setupHttppost() {
  Serial.println("HTTP POST Mode initialized");
  readytoSend = true;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://" + config.serverIP + ":" + String(config.monitoringPort) + config.path;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<100> doc;
    doc["box_id"] = config.hardwareId;
    String jsonString;
    serializeJson(doc, jsonString);

    Serial.println("Sending initial HTTP POST: " + jsonString);

    int httpResponseCode = http.POST(jsonString);
    if (httpResponseCode > 0) {
      Serial.printf("Initial HTTP POST Success [%d]\n", httpResponseCode);
    } else {
      Serial.printf("Initial HTTP POST Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    delay(200);
  }
}

//---------- Function Access Point  ----------
void setupAP() {
  WiFi.mode(WIFI_AP);
  String apSSID = ap_ssid + config.hardwareId;
  const char* apSSID_c = apSSID.c_str();
  WiFi.softAP(apSSID_c, ap_password);
  Serial.println("\nAP Mode: " + WiFi.softAPIP().toString());
  apStartTime = millis();  // Start timeout timer
}

void setupOTA() {
  ArduinoOTA.setPort(3232);                    // Set port OTA
  ArduinoOTA.setHostname(default_username);      // Set hostname
  ArduinoOTA.setPassword(default_password);      // Set password OTA

  // Callback when OTA starts
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) type = "sketch";
    else type = "filesystem";
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

//---------- Function Connect to WiFi  ----------
bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // Check if connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.println("IP: " + WiFi.localIP().toString());
    return true;
  }
  return false;
}

//---------- Function to ensure WiFi is connected ----------
void ensureWiFiConnected() {
  static bool wasConnected = (WiFi.status() == WL_CONNECTED);

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      Serial.println("MONITOR:WIFI_RECONNECTED");
      Serial.println("IP: " + WiFi.localIP().toString());
      wasConnected = true;
    }
    wifiReconnecting = false;
    return;
  }
  if (wasConnected) {
    Serial.println("MONITOR:WIFI_DISCONNECTED");
    wasConnected = false;
    wifiReconnecting = true; // Langsung set true untuk memulai proses
    wifiReconnectStart = millis();
    WiFi.disconnect();
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
  } else {
    if (millis() - wifiReconnectStart > 10000) {
      Serial.println("Reconnect timeout. Failed to connect.");
      wifiReconnecting = false;
      return;
    }
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 500) {
      Serial.print(".");
      lastPrint = millis();
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to WiFi!");
      Serial.println("IP: " + WiFi.localIP().toString());
      wifiReconnecting = false;
    }
  }
}

//---------- Detect changes in input pins (UPDATED) ----------
bool checkInputChanges() {
  bool changed = false;

  // PCF I1..I8
  for (int i = 0; i < NUM_PCF_INPUTS; i++)
  {
    uint8_t pin = PCF_PIN_MAP[i];
    uint8_t current = pcf8574.read(pin) ? 1 : 0;
    if (current != lastInputStates[i])
    {
      lastInputStates[i] = current;
      changed = true;
    }
  }

  // GPIO I9..I16
  for (int j = 0; j < NUM_GPIO_INPUTS; j++)
  {
    uint8_t current = digitalRead(INPUT_GPIO_PINS[j]) ? 1 : 0;
    int idx = NUM_PCF_INPUTS + j; // 8..15
    if (current != lastInputStates[idx])
    {
      lastInputStates[idx] = current;
      changed = true;
    }
  }
  return changed;
}


//---------- Function to bulit monitoring data (UPDATED) ----------
String buildMonitoringJSON(bool forThingsBoard = false) {
  StaticJsonDocument<512> doc;

  if (!forThingsBoard) {
    doc["type"] = "telemetry";
    doc["box_id"] = config.hardwareId;
  }

  for (int i = 0; i < NUM_PCF_INPUTS; i++) {
    doc["I" + String(i + 1)] = pcf8574.read(PCF_PIN_MAP[i]) ? 1 : 0;
  }
  for (int i = 0; i < NUM_GPIO_INPUTS; i++) {
    doc["I" + String(NUM_PCF_INPUTS + i + 1)] = digitalRead(INPUT_GPIO_PINS[i]) ? 1 : 0;
  }
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    doc["Q" + String(i + 1)] = digitalRead(OUTPUT_PINS[i]) ? 1 : 0;
  }

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

void publishStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  String statusData = buildStatusJSON();

  if (config.commMode == "mqtt" && mqttClient.connected()) {
    String topic = "iot-node/" + config.hardwareId + "/status";
    mqttClient.publish(topic.c_str(), statusData.c_str());
    Serial.println("Published Status to MQTT: " + topic);
  } else if (config.commMode == "ws" && webSocket.isConnected()) {
    webSocket.sendTXT(statusData);
    Serial.println("Sent Status via WebSocket");
  } else if (config.commMode == "httppost") {
    HTTPClient http;
    String url = "http://" + config.serverIP + ":" + String(config.monitoringPort) + config.path;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.POST(statusData);
    http.end();
    Serial.println("Sent Status via HTTP POST");
  }
  Serial.println("Payload: " + statusData);
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "2025-01-01 00:00:00";  // default timestamp
  }

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

//---------- Function send monitoring data  ----------
void sendMonitoringData() {
  if (WiFi.status() != WL_CONNECTED) {
    return; // Add buffering logic here if needed
  }

  bool isThingsBoard = (config.serverIP == "mqtt.thingsboard.cloud");
  String data = buildMonitoringJSON(isThingsBoard);

  if (config.commMode == "mqtt" && mqttClient.connected()) {
    String topic = isThingsBoard ? "v1/devices/me/telemetry" : "iot-node/" + config.hardwareId + "/telemetry";
    if (mqttClient.publish(topic.c_str(), data.c_str())) {
      Serial.println("Data sent to MQTT topic: " + topic);
    } else {
      Serial.println("Failed to publish MQTT.");
    }
  } else if (config.commMode == "ws" && webSocket.isConnected()) {
    webSocket.sendTXT(data);
    Serial.println("Sending via WebSocket");
  } else if (config.commMode == "httppost") {
    HTTPClient http;
    String url = "http://" + config.serverIP + ":" + String(config.monitoringPort) + config.path;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(data);
    if (httpResponseCode > 0) {
      Serial.printf("HTTP POST Success [%d]\n", httpResponseCode);
    } else {
      Serial.printf("HTTP POST Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  } else {
    String newData = buildMonitoringJSON(true);
    if (monitoringBuffer.size() >= MAX_BUFFER_SIZE) {
      monitoringBuffer.erase(monitoringBuffer.begin());
    }
    monitoringBuffer.push_back(newData);
    Serial.println("Buffered (offline): " + newData);
  }
  Serial.println("Payload: " + data);
}


//---------- Function to handle event WebSocket ----------
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnect from websocket!");
      readytoSend = false;
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket Connected!");
      readytoSend = true;
      publishStatus(); // Send config on successful connection
      break;
    case WStype_TEXT: {
        String data = String((char*)payload);
        Serial.println("Data received: " + data);

        StaticJsonDocument<400> doc;
        DeserializationError error = deserializeJson(doc, data);

        if (error) {
          Serial.println("Error parsing JSON!");
          return;
        }

        if (doc.containsKey("box_id") && doc.containsKey("cmd")) {
          String receivedId = doc["box_id"].as<String>();
          String cmd = doc["cmd"].as<String>();

          // === CMD: config ===
          if (receivedId == config.hardwareId && cmd == "config") {
            StaticJsonDocument<400> resp;
            resp["server_ip"]   = config.serverIP;
            resp["path"]        = config.path;
            resp["hw_ip"]       = WiFi.localIP().toString();
            resp["box_id"]      = config.hardwareId;
            resp["port"]        = config.monitoringPort;
            resp["ssid"]        = config.wifiSSID;
            resp["pass"]        = config.wifiPass;
            resp["mac_address"] = WiFi.macAddress();

            String jsonOut;
            serializeJson(resp, jsonOut);

            webSocket.sendTXT(jsonOut);
            Serial.println("Send config JSON:" + jsonOut);
            return;
          }

          // === CMD: restartesp ===
          if (receivedId == config.hardwareId && cmd == "restartesp") {
            Serial.println("Received restart command. Sending confirmation...");

            StaticJsonDocument<200> resp;
            resp["box_id"] = config.hardwareId;
            resp["status"] = "restarting";

            String jsonOut;
            serializeJson(resp, jsonOut);
            webSocket.sendTXT(jsonOut);

            Serial.println("Sent confirmation JSON:");
            Serial.println(jsonOut);

            delay(1000);
            ESP.restart();
            return;
          }
        }

        if (doc.containsKey("box_id")) {
          String receivedId = doc["box_id"].as<String>();
          if (receivedId == config.hardwareId) {
            bool updated = false;

            if (doc.containsKey("server_ip")) {
              config.serverIP = doc["server_ip"].as<String>();
              updated = true;
            }
            if (doc.containsKey("path")) {
              config.path = doc["path"].as<String>();
              updated = true;
            }
            if (doc.containsKey("port")) {
              config.monitoringPort = doc["port"].as<int>();
              updated = true;
            }
            if (doc.containsKey("ssid")) {
              config.wifiSSID = doc["ssid"].as<String>();
              updated = true;
            }
            if (doc.containsKey("pass")) {
              config.wifiPass = doc["pass"].as<String>();
              updated = true;
            }

            if (updated) {
              // Commit supaya data benar-benar tersimpan
              saveConfig();

              // Konfirmasi ke server
              StaticJsonDocument<200> resp;
              resp["box_id"] = config.hardwareId;
              resp["status"] = "config_updated";

              String jsonOut;
              serializeJson(resp, jsonOut);
              webSocket.sendTXT(jsonOut);

              Serial.println("Sent config_updated JSON:");
              Serial.println(jsonOut);

              Serial.println("Box ID: " + config.hardwareId);
              Serial.println("Server IP: " + config.serverIP);
              Serial.println("Path: " + config.path);
              Serial.println("Monitoring Port: " + String(config.monitoringPort));
              Serial.println("Comm Mode: " + config.commMode);
              Serial.println("WiFi SSID: " + config.wifiSSID);
              Serial.println("WiFi Pass: " + config.wifiPass);

              delay(1000);
              ESP.restart(); // restart supaya config baru dipakai
              return;
            }

            for (int i = 0; i < NUM_OUTPUTS; i++) {
              digitalWrite(OUTPUT_PINS[i], doc["O" + String(i + 1)] | 0 ? HIGH : LOW);
            }
          } else {
            Serial.println("Ignore commands for ID: " + receivedId);
          }
        } else {
          Serial.println("No box_id in received data!");
        }
        break;
      }
  }
}

//---------- Function handle main page ----------
void handleRoot() {
  if (!isAuthenticated) {
    server.send(200, "text/html", loadFile("/login-page.html"));  // Show login page if not authenticated
  } else {
    // Show configuration page if authenticated
    String html = String(loadFile("/config-page.html"));
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));
    html.replace("%PATH%", config.path);
    html.replace("%WIFI_SSID%", config.wifiSSID);
    html.replace("%WIFI_PASS%", config.wifiPass);
    html.replace("%ACCESS_TOKEN%", config.accessToken);
    server.send(200, "text/html", html.c_str());
  }
}

//---------- Function to check if the user is authenticated ----------
bool checkAuth(String username, String password) {
  return (username == default_username && password == default_password);
}

//---------- Function to handle login form submission ----------
void handleAuth() {
  String username = server.arg("username");  // Take username from form
  String password = server.arg("password");  // Take password from form

  // Check if the username and password are correct
  if (checkAuth(username, password)) {
    isAuthenticated = true;
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");      // Redirect to main page
  } else {
    server.send(401, "text/html", loadFile("/login-page.html"));// Show login page with error
  }
}

//---------- Function to save configuration ----------
void handleSaveConfig() {
  // Check if the user is authenticated
  if (!isAuthenticated) {
    server.send(401, "text/html", loadFile("/login-page.html"));
    return;
  }

  // Take configuration data from form
  String newHardwareId = server.arg("hardwareId");
  String newServerIP = server.arg("serverIP");
  int newMonitoringPort = server.arg("serverPort").toInt();
  String newPath = server.arg("path");
  String newcommMode = server.arg("commMode");
  String newSSID = server.arg("ssid");
  String newPassword = server.arg("password");
  String newAccessToken = server.arg("accessToken");

  // Validate input data
  if (newHardwareId.length() > 0 && newServerIP.length() > 0 && newSSID.length() > 0 && newPassword.length() > 0) {

    config.hardwareId = newHardwareId;
    config.serverIP = newServerIP;
    config.monitoringPort = newMonitoringPort;
    config.path = newPath;
    config.commMode = newcommMode;
    config.wifiSSID = newSSID;
    config.wifiPass = newPassword;
    config.accessToken = newAccessToken;
    saveConfig();

    // Show success page
    server.send(200, "text/html", loadFile("/success-page.html"));
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "Input tidak valid");
  }
}

// Function to handle serial input for configuration
void handleSerialInput() {
  static String inputString = "";

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      inputString.trim();

      if (inputString.equalsIgnoreCase("config")) {
        Serial.println("==> Current Configuration:");
        Serial.println("{");
        Serial.println("  \"server_ip\": \"" + config.serverIP + "\",");
        Serial.println("  \"path\": \"" + config.path + "\",");
        Serial.println("  \"box_id\": \"" + config.hardwareId + "\",");
        Serial.println("  \"port\": " + String(config.monitoringPort) + ",");
        Serial.println("  \"commMode\": \"" + config.commMode + "\",");
        Serial.println("  \"ssid\": \"" + config.wifiSSID + "\",");
        Serial.println("  \"pass\": \"" + config.wifiPass + "\"");
        Serial.println("  \"mac_address\": \"" + WiFi.macAddress() + "\"");
        int rssi = WiFi.RSSI();
        String signalQuality;
        if (rssi > -60) signalQuality = "Good";
        else if (rssi > -75)  signalQuality = "Fair";
        else signalQuality = "Weak";
        Serial.println("  \"wifi_rssi_dbm\": " + String(rssi) + ",");
        Serial.println("  \"wifi_quality\": \"" + signalQuality + "\",");
        Serial.println("}");
        inputString = "";
        return;
      } else if (inputString.equalsIgnoreCase("restartesp")) {
        Serial.println("==> Restart ESP32");
        ESP.restart();
        inputString = "";
        return;
      }  else if (inputString.equalsIgnoreCase("vers")) {
        Serial.println(program_version);
        inputString = "";
        return;
      } else if (inputString.equalsIgnoreCase("help")) {
        Serial.println("==> Available commands:");
        Serial.println("  help          - Show this help message");
        Serial.println("  config        - Show current configuration");
        Serial.println("  restartesp    - Restart the ESP32");
        Serial.println("  <JSON>        - Send new configuration in JSON format");
        Serial.println("");
        Serial.println("==> JSON format example:");
        Serial.println("{");
        Serial.println("  \"server_ip\": \"192.168.1.100\",  // Server IP address");
        Serial.println("  \"path\": \"/ws\",             // Path Server");
        Serial.println("  \"port\": 80,                  // Port number for monitoring");
        Serial.println("  \"box_id\": \"001\",               // Unique hardware identifier");
        Serial.println("  \"commMode\": \"ws\",            // Static IP address for ESP32");
        Serial.println("  \"ssid\": \"YourWiFiSSID\",      // WiFi SSID");
        Serial.println("  \"pass\": \"YourWiFiPassword\"     // WiFi Password");
        Serial.println("}");
        inputString = "";
        return;
      }

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, inputString);
      if (!error) {
        if (doc.containsKey("server_ip")) config.serverIP = doc["server_ip"].as<String>();
        if (doc.containsKey("path")) config.path = doc["path"].as<String>();
        if (doc.containsKey("box_id")) config.hardwareId = doc["box_id"].as<String>();
        if (doc.containsKey("port")) config.monitoringPort = doc["port"].as<int>();
        if (doc.containsKey("commMode")) config.commMode = doc["commMode"].as<String>();
        if (doc.containsKey("ssid")) config.wifiSSID = doc["ssid"].as<String>();
        if (doc.containsKey("pass")) config.wifiPass = doc["pass"].as<String>();

        saveConfig();
        Serial.println("==> Configuration saved:");
        serializeJsonPretty(doc, Serial);
        Serial.println("\n Restarting ESP32 . . .");
      } else {
        Serial.println("==> JSON parsing error: " + String(error.c_str()));
      }
      inputString = "";
    } else {
      inputString += c;
    }
  }
}

void handleToggle() {
  if (server.hasArg("pin") && server.hasArg("state")) {
    int pin = server.arg("pin").toInt();
    String state = server.arg("state");

    if (pin >= 0 && pin < NUM_OUTPUTS) {
      if (state == "on") digitalWrite(OUTPUT_PINS[pin], HIGH);
      else digitalWrite(OUTPUT_PINS[pin], LOW);

      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid pin index");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSaveRules() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    File f = LittleFS.open("/rules.json", "w");
    if (f) {
      f.print(body);
      f.close();
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(500, "application/json", "{\"error\":\"write failed\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
  }
}

// Inisialisasi GPIO lokal & PCF8574 (UPDATED)
void setupIO() {
  if (!pcf8574.begin()) Serial.println("PCF8574: Failed begin()");
  if (!pcf8574.isConnected()) Serial.println("PCF8574: not connected!");
  else Serial.println("PCF8574: Connected");

  pcf8574.write8(0xFF); // default HIGH

  for (int i = 0; i < NUM_GPIO_INPUTS; i++) pinMode(INPUT_GPIO_PINS[i], INPUT_PULLUP);
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
    digitalWrite(OUTPUT_PINS[i], LOW);
  }

  // Simpan state awal
  // PCF (I1-I8)
  uint8_t s = pcf8574.read8();
  for (int i = 0; i < NUM_PCF_INPUTS; i++) {
    uint8_t pin = PCF_PIN_MAP[i];
    lastInputStates[i] = (s >> pin) & 0x01;
  }
  // GPIO (I9-I16)
  for (int j = 0; j < NUM_GPIO_INPUTS; j++) {
    lastInputStates[NUM_PCF_INPUTS + j] = digitalRead(INPUT_GPIO_PINS[j]) ? 1 : 0;
  }
}

// Setup WiFi & mode AP
void setupWiFiMode() {
  if (config.wifiSSID.length() > 0 && config.wifiPass.length() > 0 && connectToWiFi()) {
    Serial.println("Try Connect Wifi :");
    isAPMode = false;
    setupOTA();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  } else {
    setupAP();
  }
}

// Setup komunikasi (WS / HTTP / MQTT)
void setupCommMode() {
  if (!isAPMode) {
    if (config.commMode == "ws") setupWebSocket();
    else if (config.commMode == "httppost") setupHttppost();
    else if (config.commMode == "mqtt") setupMQTTClient(); // Diaktifkan
    readytoSend = true;
  }
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/save-config", HTTP_POST, handleSaveConfig);
  server.on("/download-config", HTTP_GET, []() {
    if (LittleFS.exists("/config.json")) {
      File file = LittleFS.open("/config.json", "r");
      server.streamFile(file, "application/json");
      file.close();
    } else {
      server.send(404, "application/json", "{\"error\":\"config.json not found\"}");
    }
  });
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/save-rules", HTTP_POST, handleSaveRules);
  server.begin();
}

// Timeout AP Mode with countdown
void handleAPModeTimeout() {
  unsigned long elapsed = millis() - apStartTime;
  unsigned long remaining = (apTimeout > elapsed) ? (apTimeout - elapsed) : 0;

  if (millis() - lastCountdownPrint >= 1000) {
    Serial.printf("AP Mode active, remaining time: %lu sec\n", remaining / 1000);
    lastCountdownPrint = millis();
  }

  if (elapsed > apTimeout) {
    Serial.println("AP Mode timeout, restarting...");
    ESP.restart();
  }
}

//---------- Main setup function ----------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  delay(1000);

  Serial.println("\n==> Starting IoT Node . . . \n");
  if (!LittleFS.begin()) Serial.println("Failed mount config");
  else Serial.println("Config mounted");
  if (!loadConfig()) Serial.println("Config file not found");;

  Serial.println("Box ID: " + config.hardwareId);
  Serial.println("Server IP: " + config.serverIP);
  Serial.println("Path: " + config.path);
  Serial.println("Monitoring Port: " + String(config.monitoringPort));
  Serial.println("Comm Mode: " + config.commMode);
  Serial.println("WiFi SSID: " + config.wifiSSID);
  Serial.println("WiFi Pass: " + config.wifiPass);

  setupIO();
  setupWiFiMode();
  setupCommMode();
  setupRoutes();
}

//---------- Main loop function  ----------
void loop() {
  if (!isAPMode) {
    ensureWiFiConnected();
    if (config.commMode == "ws") webSocket.loop();
    if (config.commMode == "mqtt") {
      if (!mqttClient.connected()) {
        reconnectMQTT();
      }
      mqttClient.loop();
    }
    if (millis() - lastMonitoringTime >= monitoringInterval) {
      bool hasChanged = checkInputChanges();
      bool forceSend = (millis() - lastForceSendTime >= forceSendInterval);

      if (hasChanged || forceSend) {
        sendMonitoringData();
        lastForceSendTime = millis();
      }
      lastMonitoringTime = millis();
    }
    if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();
  } else {
    handleAPModeTimeout();
  }
  server.handleClient();
  handleSerialInput();
}
