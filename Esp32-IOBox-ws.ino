#include "time.h"              // Time library for NTP
#include <WiFi.h>              // WiFi support
#include <Wire.h>              // I2C communication
#include <vector>              // For dynamic array (buffer)
#include <PCF8574.h>           // PCF8574 library for I/O expansion
#include <LittleFS.h>          // File system
#include <WebServer.h>         // HTTP Web server
#include <ArduinoOTA.h>        // Over-The-Air update
#include <HTTPClient.h>        // 
#include <ArduinoJson.h>       // JSON serialization/deserialization
#include <PubSubClient.h>      // 
#include <WebSocketsClient.h>  // WebSocket client

const String program_version = "v1.1.0"; // Program version

//---------- Configuration structure ----------
struct Config {
  String hardwareId;
  String serverIP;
  String path;
  int monitoringPort;
  String commMode;
  String wifiSSID;
  String wifiPass;
};
Config config;
WebSocketsClient webSocket;  // WebSocket client
WebServer server(80);        // Web server on port 80
PCF8574 pcf8574(0x20);       // Address PCF8574

//---------- Time ----------
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;  // GMT+7 (WIB)
const int   daylightOffset_sec = 0;

//---------- Wifi & Access Point configuration ----------
String ap_ssid = "IoT-Node ";                    // SSID Access Point
const char* ap_password = "12345678";            // Password Access Point
bool isAPMode = true;                            // Status mode AP
bool readytoSend = false;
bool wifiReconnecting = false;
unsigned long apStartTime = 0;
unsigned long lastCountdownPrint = 0;
unsigned long wifiReconnectStart = 0;
const unsigned long apTimeout = 3 * 60 * 1000UL; // 3 minutes timeout for AP mode

//---------- Device configuration variables ----------
std::vector<String> monitoringBuffer;            // Buffer for monitoring data
const size_t MAX_BUFFER_SIZE = 100;              // Maximum buffer size for monitoring data

//---------- Input and output pins ----------
const int NUM_GPIO_INPUTS = 4;
const int NUM_PCF_INPUTS = 8;
const int INPUT_GPIO_PINS[] = {4, 34, 12, 13};
const uint8_t PCF_PIN_MAP[NUM_PCF_INPUTS] = {0, 1, 2, 3, 4, 5, 6, 7};
const int OUTPUT_PINS[] = {33, 25, 26, 27};
const int NUM_INPUTS = NUM_GPIO_INPUTS + NUM_PCF_INPUTS;                    // Sum of input pins
const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);       // Sum of output pins
uint8_t lastInputStates[NUM_INPUTS] = {0};                                  // Array to store last input states

//---------- Timing control ----------
unsigned long lastMonitoringTime = 0;  // Time of last monitoring data send
const long monitoringInterval = 200;   // Interval for sending monitoring data (in milliseconds)
unsigned long lastForceSendTime = 0;   // Time of last forced send
const unsigned long forceSendInterval = 60000;  // Interval for forced send (in milliseconds)

//---------- Default login credentials for web UI ----------
const char* default_username = "contoh";    // Username default
const char* default_password = "password";  // Password default
bool isAuthenticated = false;               // Authentication status

bool loadConfig() {
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("No config file found, using defaults");
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

//---------- Function to setup ws ----------
void setupWebSocket() {
  webSocket.begin(config.serverIP.c_str(), config.monitoringPort, config.path); // Connect to WebSocket server
  webSocket.onEvent(webSocketEvent);                                            // Set callback event
  webSocket.setReconnectInterval(500);                                          // Reconnect every 0.5s if disconnected
}

//---------- Function to setup httppost ----------
void setupHttppost() {
  Serial.println("HTTP POST Mode initialized");
  readytoSend = true;

  // === Send initial JSON with box_id ===
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
  Serial.println("Please connect to the Access Point and configure the device.");
  apStartTime = millis();  // Start timeout timer
}

//---------- Setup OTA update functionality ----------
void setupOTA() {
  ArduinoOTA.setPort(3232);                        // Set port OTA
  ArduinoOTA.setHostname(default_username);        // Set hostname
  ArduinoOTA.setPassword(default_password);        // Set password OTA

  // Callback when OTA starts
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) type = "sketch";
    else type = "filesystem";
    Serial.println("Start updating " + type);
  });

  // Callback when OTA ends
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  // Callback for progress OTA
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  // Callback for OTA error
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

  // Timeout for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) { // Timeout 60 seconds
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
  if (WiFi.status() == WL_CONNECTED) {
    wifiReconnecting = false;  // Flag to indicate not reconnecting
    return;
  }

  if (!wifiReconnecting) {
    Serial.println("WiFi disconnected. Starting reconnect...");
    WiFi.disconnect();
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
    wifiReconnectStart = millis();
    wifiReconnecting = true;
  } else {
    if (millis() - wifiReconnectStart > 10000) {
      Serial.println("Reconnect timeout. Failed to connect.");
      wifiReconnecting = false;  // Stop trying for now
      return;
    }

    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 500) {
      Serial.print(".");
      lastPrint = millis();
    }

    // Check if reconnected
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to WiFi!");
      Serial.println("IP: " + WiFi.localIP().toString());
      wifiReconnecting = false;
    }
  }
}

//---------- Detect changes in input pins ----------
bool checkInputChanges() {
  bool changed = false;

  // GPIO I1..I4
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    uint8_t current = digitalRead(INPUT_GPIO_PINS[i]) ? 1 : 0;
    if (current != lastInputStates[i])
    {
      lastInputStates[i] = current;
      changed = true;
    }
  }

  // PCF I5..I12 (urutan 4,5,6,7,0,1,2,3)
  for (int j = 0; j < NUM_PCF_INPUTS; j++)
  {
    uint8_t pin = PCF_PIN_MAP[j];
    uint8_t current = pcf8574.read(pin) ? 1 : 0;
    int idx = NUM_GPIO_INPUTS + j; // 4..11
    if (current != lastInputStates[idx])
    {
      lastInputStates[idx] = current;
      changed = true;
    }
  }
  return changed;
}

//---------- Function to bulit monitoring data  ----------
String buildMonitoringJSON(bool addTimestamp = false, bool defaultMode = false) {
  StaticJsonDocument<512> doc;

  doc["box_id"] = config.hardwareId;

  if (defaultMode) {
    // Semua input = 0
    for (int i = 0; i < NUM_INPUTS; i++) {
      String key = "I" + String(i + 1);
      doc[key] = 0;
    }
    // Semua output = 0
    for (int i = 0; i < NUM_OUTPUTS; i++) {
      String key = "O" + String(i + 1);
      doc[key] = 0;
    }
    // Current = 0
    doc["I"] = "0.00";
  } else {
    // I1..I4 dari GPIO
    for (int i = 0; i < NUM_GPIO_INPUTS; i++) {
      String key = "I" + String(i + 1);
      int rawVal = digitalRead(INPUT_GPIO_PINS[i]) ? 1 : 0;
      doc[key] = rawVal;
    }

    // I5..I12 dari PCF8574
    for (int j = 0; j < NUM_PCF_INPUTS; j++) {
      String key = "I" + String(NUM_GPIO_INPUTS + j + 1);
      uint8_t pin = PCF_PIN_MAP[j];
      int rawVal = pcf8574.read(pin) ? 1 : 0;
      doc[key] = rawVal;
    }

    // Status output
    for (int i = 0; i < NUM_OUTPUTS; i++) {
      String key = "O" + String(i + 1);
      doc[key] = digitalRead(OUTPUT_PINS[i]) ? 1 : 0;
    }
  }

  // Timestamp
  if (addTimestamp) {
    doc["timestamp"] = getTimestamp();
  }

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
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
  bool isWiFi = (WiFi.status() == WL_CONNECTED);
  bool isWS = (config.commMode == "ws" && webSocket.isConnected());
  bool isHTTP = (config.commMode == "httppost");
  bool isMQTT = (config.commMode == "mqtt");

  // Data JSON normal
  String data = buildMonitoringJSON();

  // === ONLINE ===
  if (isWiFi && (isWS || isHTTP || isMQTT)) {
    if (isWS) {
      // Send via WebSocket
      webSocket.sendTXT(data);
      Serial.println("Sending via WebSocket: " + data);
    } else if (isHTTP) {
      // Send via HTTP POST
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
    }
    //    } else if (isMQTT) {
    //      Serial.println("Sending via MQTT (not implemented yet): " + data);
    //    }
  }
  // === OFFLINE ===
  else {
    String newData = buildMonitoringJSON(true);
    if (monitoringBuffer.size() >= MAX_BUFFER_SIZE) {
      monitoringBuffer.erase(monitoringBuffer.begin());
    }
    monitoringBuffer.push_back(newData);
    Serial.println("Buffered (offline): " + newData);
  }
}


//---------- Function to handle event WebSocket ----------
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnect from websocket!");
      readytoSend = false;
      break;
    case WStype_CONNECTED:
      Serial.println("Connect to websocket!");

      // Send initial JSON with box_id
      {
        StaticJsonDocument<100> doc;
        doc["box_id"] = config.hardwareId;
        String jsonString;
        serializeJson(doc, jsonString);
        webSocket.sendTXT(jsonString);
        Serial.println("Sent initial JSON: " + jsonString);
        delay(200);
      }

      // Send buffered data if any
      if (monitoringBuffer.size() > 0) {
        Serial.println("Reconnected: Sending buffered data...");
        for (String& bufferedData : monitoringBuffer) {
          webSocket.sendTXT(bufferedData);
          delay(200);  // delay to avoid flooding the server
        }
        monitoringBuffer.clear();  // Clear the buffer after sending
      }
      readytoSend = true;
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
            Serial.println("Send config JSON:");
            Serial.println(jsonOut);
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
          // Serial.println("No box_id in received data!");
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
    html.replace("%PATH%", config.path);
    html.replace("%WIFI_SSID%", config.wifiSSID);
    html.replace("%WIFI_PASS%", config.wifiPass);
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
  String newPath = server.arg("path");
  String newcommMode = server.arg("commMode");
  String newSSID = server.arg("ssid");
  String newPassword = server.arg("password");

  // Validate input data
  if (newHardwareId.length() > 0 && newServerIP.length() > 0 && newSSID.length() > 0 && newPassword.length() > 0) {

    config.hardwareId = newHardwareId;
    config.serverIP = newServerIP;
    config.path = newPath;
    config.commMode = newcommMode;
    config.wifiSSID = newSSID;
    config.wifiPass = newPassword;
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
        Serial.println("  help           - Show this help message");
        Serial.println("  config         - Show current configuration");
        Serial.println("  restartesp     - Restart the ESP32");
        Serial.println("  <JSON>         - Send new configuration in JSON format");
        Serial.println("");
        Serial.println("==> JSON format example:");
        Serial.println("{");
        Serial.println("  \"server_ip\": \"192.168.1.100\",  // Server IP address");
        Serial.println("  \"path\": \"/ws\",                 // Path Server");
        Serial.println("  \"port\": 80,                      // Port number for monitoring");
        Serial.println("  \"box_id\": \"001\",               // Unique hardware identifier");
        Serial.println("  \"commMode\": \"ws\",              // Static IP address for ESP32");
        Serial.println("  \"ssid\": \"YourWiFiSSID\",        // WiFi SSID");
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

// Inisialisasi GPIO lokal & PCF8574
void setupIO() {
  if (!pcf8574.begin()) Serial.println("PCF8574: Failed begin()");
  if (!pcf8574.isConnected()) Serial.println("PCF8574: not connected!");
  else Serial.println("PCF8574: Connected");

  pcf8574.write8(0xFF); // default HIGH

  for (int i = 0; i < NUM_GPIO_INPUTS; i++) pinMode(INPUT_GPIO_PINS[i], INPUT);
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
    digitalWrite(OUTPUT_PINS[i], LOW);
  }

  // Simpan state awal
  for (int i = 0; i < NUM_GPIO_INPUTS; i++) {
    lastInputStates[i] = digitalRead(INPUT_GPIO_PINS[i]) ? 1 : 0;
  }
  uint8_t s = pcf8574.read8();
  for (int j = 0; j < NUM_PCF_INPUTS; j++) {
    uint8_t pin = PCF_PIN_MAP[j];
    lastInputStates[NUM_GPIO_INPUTS + j] = (s >> pin) & 0x01;
  }
}

// Setup WiFi & mode AP
void setupWiFiMode() {
  if (config.wifiSSID.length() > 0 && config.wifiPass.length() > 0 && connectToWiFi()) {
    Serial.println("Try Connect Wifi :");
    isAPMode = false;
    setupOTA();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time synchronized from NTP.");
  } else {
    setupAP();
  }
}

// Setup komunikasi (WS / HTTP / MQTT)
void setupCommMode() {
  if (!isAPMode) {
    if (config.commMode == "ws") setupWebSocket();
    else if (config.commMode == "httppost") setupHttppost();
    //    else if (config.commMode == "mqtt") setupMQTTClient();
  }
}

// Setup route web server
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
  ArduinoOTA.handle();
  server.handleClient();
  handleSerialInput();

  if (!isAPMode) {
    ensureWiFiConnected();
    if (config.commMode == "ws") webSocket.loop();

    bool intervalPassed = millis() - lastMonitoringTime >= monitoringInterval;
    bool forceSendDue = millis() - lastForceSendTime >= forceSendInterval;

    if (intervalPassed && (checkInputChanges() || (forceSendDue && readytoSend))) {
      sendMonitoringData();
      lastMonitoringTime = millis();
      if (forceSendDue) lastForceSendTime = millis();
    }
  }
  else {
    handleAPModeTimeout();
  }
}
