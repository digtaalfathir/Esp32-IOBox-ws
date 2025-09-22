#include <FS.h>                // File system
#include "time.h"              // Time library for NTP
#include <WiFi.h>              // WiFi support
#include <Wire.h>              // I2C communication
#include <vector>              // For dynamic array (buffer)
#include "EmonLib.h"           // Energy monitoring library
#include <ESPmDNS.h>           // mDNS responder
#include <WiFiUdp.h>           // UDP for OTA
#include <PCF8574.h>           // PCF8574 library for I/O expansion
#include <LittleFS.h>          // File system
#include <WebServer.h>         // HTTP Web server
#include <ArduinoJson.h>       // JSON serialization/deserialization
#include <ArduinoOTA.h>        // Over-The-Air update
#include <WebSocketsClient.h>  // WebSocket client
#include <WebSocketsServer.h>  // WebSocket server
#include "login-page.h"        // Login page
#include "config-page.h"       // Configuration page
#include "success-page.h"      // Success page after saving configuration

const String program_version = "v1.0.5"; // Program version

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

//---------- Time ----------
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;  // GMT+7 (WIB)
const int   daylightOffset_sec = 0;

//---------- Access Point configuration ----------
String ap_ssid = "IoT-Node ";                    // SSID Access Point
const char* ap_password = "12345678";            // Password Access Point
bool isAPMode = true;                            // Status mode AP
bool wasOnline = true;                           // Last known online status
bool init2Ws = false;
bool readytoSend = false;
unsigned long apStartTime = 0;
unsigned long lastCountdownPrint = 0;
const unsigned long apTimeout = 3 * 60 * 1000UL; // 3 minutes timeout for AP mode

//---------- Configuration PCF8574 ----------
PCF8574 pcf8574(0x20);  // Address PCF8574

//---------- Monitoring server configuration ----------
int monitoring_port = 80;  // Port server monitoring

//---------- Device configuration variables ----------
String hardwareId = "001";                        // ID hardware
String serverIP = "192.168.1.2";                  // IP server monitoring
String path = "/ws";
String commMode = "ws";
std::vector<String> monitoringBuffer;             // Buffer for monitoring data
const size_t MAX_BUFFER_SIZE = 100;               // Maximum buffer size for monitoring data

//---------- Input and output pins ----------
//const int INPUT_PINS[] = {12, 13, 14, 25, 26, 27, 32, 33};                // Pin input
//const int OUTPUT_PINS[] = {4, 15, 16, 17};                                // Pin output
const int NUM_GPIO_INPUTS = 4;
const int NUM_PCF_INPUTS = 8;
const int INPUT_GPIO_PINS[] = {4, 34, 12, 13};
const uint8_t PCF_PIN_MAP[NUM_PCF_INPUTS] = {0, 1, 2, 3, 4, 5, 6, 7};
const int OUTPUT_PINS[] = {33, 25, 26, 27};
const int NUM_INPUTS = NUM_GPIO_INPUTS + NUM_PCF_INPUTS;                    // Sum of input pins
const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);       // Sum of output pins
uint8_t lastInputStates[NUM_INPUTS] = {0};                                  // Array to store last input states

//---------- Object initialization ----------
WebSocketsClient webSocket;  // WebSocket client
WebServer server(80);        // Web server on port 80
EnergyMonitor emon1;         // Energy monitoring object

//---------- WiFi credentials ----------
String storedSSID = "";      // SSID WiFi
String storedPassword = "";  // Password WiFi
bool wifiReconnecting = false;
unsigned long wifiReconnectStart = 0;

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
    Serial.print("Failed to parse config: ");
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

//---------- Detect changes in input pins ----------
bool checkInputChanges()
{
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

//---------- Setup OTA update functionality ----------
void setupOTA() {
  ArduinoOTA.setPort(3232);                // Set port OTA
  ArduinoOTA.setHostname("contoh");        // Set hostname
  ArduinoOTA.setPassword("password");      // Set password OTA

  // Callback when OTA starts
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
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

//---------- Function to bulit monitoring data  ----------
String buildMonitoringJSON(bool addTimestamp = false) {
  StaticJsonDocument<512> doc;

  doc["box_id"] = hardwareId;

  // I1..I4 dari GPIO
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    String key = "I" + String(i + 1);                     // I1..I4
    int rawVal = digitalRead(INPUT_GPIO_PINS[i]) ? 1 : 0; // HIGH=1, LOW=0
    // int val = rawVal ? 0 : 1; // Inverted logic
    int val = rawVal;
    doc[key] = val;
  }

  // I5..I12 dari PCF8574 dengan urutan 4,5,6,7,0,1,2,3
  for (int j = 0; j < NUM_PCF_INPUTS; j++)
  {
    String key = "I" + String(NUM_GPIO_INPUTS + j + 1); // I5..I12
    uint8_t pin = PCF_PIN_MAP[j];
    int rawVal = pcf8574.read(pin) ? 1 : 0;
    // int val = rawVal ? 0 : 1; // Inverted logic
    int val = rawVal;
    doc[key] = val;
  }

  // STATUS OUTPUT
  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    String key = "O" + String(i + 1);
    doc[key] = digitalRead(OUTPUT_PINS[i]) ? 1 : 0;
  }

  // Add timestamp if requested
  if (addTimestamp) {
    doc["timestamp"] = getTimestamp();
  }

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

String buildDefaultJSON() {
  StaticJsonDocument<512> doc;

  doc["box_id"] = hardwareId;

  // inputs = 0
  for (int i = 0; i < NUM_INPUTS; i++) {
    String key = "I" + String(i + 1);
    doc[key] = 0;
  }

  // Outputs = 0
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    String key = "O" + String(i + 1);
    doc[key] = 0;
  }

  // Current = 0
  doc["I"] = "0.00";

  // Timestamp
  doc["timestamp"] = getTimestamp();

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
  bool isOnline = (WiFi.status() == WL_CONNECTED && webSocket.isConnected());

  // Check for transition from online to offline
  if (!isOnline && wasOnline) {
    String defaultData = buildDefaultJSON();
    if (monitoringBuffer.size() >= MAX_BUFFER_SIZE) {
      monitoringBuffer.erase(monitoringBuffer.begin());
    }
    monitoringBuffer.push_back(defaultData);
    Serial.println("Buffered (default snapshot on disconnect): " + defaultData);
  }

  String data = buildMonitoringJSON();  // Normal monitoring data

  if (isOnline) {
    // === ONLINE ===
    webSocket.sendTXT(data);
    Serial.println("Sending (live): " + data);
  } else {
    // === OFFLINE ===
    String newData = buildMonitoringJSON(true);
    if (monitoringBuffer.size() >= MAX_BUFFER_SIZE) {
      monitoringBuffer.erase(monitoringBuffer.begin());
    }
    monitoringBuffer.push_back(newData);
    Serial.println("Buffered (offline): " + newData);
  }

  // Send to Serial2 for PUSR
  Serial2.println(data);

  // Update status
  wasOnline = isOnline;
}

//---------- Function Access Point  ----------
void setupAP() {
  WiFi.mode(WIFI_AP);
  String apSSID = ap_ssid + hardwareId;
  const char* apSSID_c = apSSID.c_str();
  WiFi.softAP(apSSID_c, ap_password);
  Serial.println("\nAP Mode: " + WiFi.softAPIP().toString());
  Serial.println("Please connect to the Access Point and configure the device.");

  apStartTime = millis();  // Start timeout timer
}

//---------- Function Connect to WiFi  ----------
bool connectToWiFi() {
  WiFi.mode(WIFI_STA);  // Set mode Wifi

  // Start connecting to WiFi
  WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

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
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
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

//---------- Function to setup ws ----------
void setupWebSocket() {
  webSocket.begin(serverIP.c_str(), monitoring_port, path); // Connect to WebSocket server
  webSocket.onEvent(webSocketEvent);                         // Set callback event
  webSocket.setReconnectInterval(500);                       // Reconnect every 0.5s if disconnected
  Serial.println("WS Client started");
  Serial.println("Connecting to ws server: " + serverIP);
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
        doc["box_id"] = hardwareId;
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
      init2Ws = true;
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
          if (receivedId == hardwareId && cmd == "config") {
            StaticJsonDocument<400> resp;
            resp["server_ip"]   = serverIP;
            resp["path"]        = path;
            resp["hw_ip"]       = WiFi.localIP().toString();
            resp["box_id"]      = hardwareId;
            resp["port"]        = monitoring_port;
            resp["ssid"]        = storedSSID;
            resp["pass"]        = storedPassword;
            resp["mac_address"] = WiFi.macAddress();

            String jsonOut;
            serializeJson(resp, jsonOut);

            webSocket.sendTXT(jsonOut);
            Serial.println("Send config JSON:");
            Serial.println(jsonOut);
            return;
          }

          // === CMD: restartesp ===
          if (receivedId == hardwareId && cmd == "restartesp") {
            Serial.println("Received restart command. Sending confirmation...");

            StaticJsonDocument<200> resp;
            resp["box_id"] = hardwareId;
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
          if (receivedId == hardwareId) {
            bool updated = false;

            if (doc.containsKey("server_ip")) {
              serverIP = doc["server_ip"].as<String>();
              config.serverIP = serverIP;
              updated = true;
            }
            if (doc.containsKey("path")) {
              serverIP = doc["path"].as<String>();
              config.path = path;
              updated = true;
            }
            if (doc.containsKey("port")) {
              monitoring_port = doc["port"].as<int>();
              config.monitoringPort = monitoring_port;
              updated = true;
            }
            if (doc.containsKey("ssid")) {
              storedSSID = doc["ssid"].as<String>();
              config.wifiSSID  = storedSSID;
              updated = true;
            }
            if (doc.containsKey("pass")) {
              storedPassword = doc["pass"].as<String>();
              config.wifiPass  = storedPassword;
              updated = true;
            }

            if (updated) {
              // Commit supaya data benar-benar tersimpan
              saveConfig();

              // Konfirmasi ke server
              StaticJsonDocument<200> resp;
              resp["box_id"] = hardwareId;
              resp["status"] = "config_updated";

              String jsonOut;
              serializeJson(resp, jsonOut);
              webSocket.sendTXT(jsonOut);

              Serial.println("Sent config_updated JSON:");
              Serial.println(jsonOut);

              delay(1000);
              ESP.restart(); // restart supaya config baru dipakai
              return;
            }

            for (int i = 0; i < NUM_OUTPUTS; i++) {
              String key = "O" + String(i + 1);
              if (doc.containsKey(key)) {
                int state = doc[key].as<int>();
                digitalWrite(OUTPUT_PINS[i], state == 1 ? HIGH : LOW);
              }
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
    server.send(200, "text/html", loginHTML);  // Show login page if not authenticated
  } else {
    // Show configuration page if authenticated
    String html = String(configHTML);
    html.replace("%HARDWARE_ID%", hardwareId);
    html.replace("%SERVER_IP%", serverIP);
    html.replace("%PATH%", path);
    html.replace("%WIFI_SSID%", storedSSID);
    html.replace("%WIFI_PASS%", storedPassword);
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
    server.send(401, "text/html", loginHTML);// Show login page with error
  }
}

//---------- Function to save configuration ----------
void handleSaveConfig() {
  // Check if the user is authenticated
  if (!isAuthenticated) {
    server.send(401, "text/html", loginHTML);
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
    server.send(200, "text/html", successHTML);
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
        Serial.println("  \"server_ip\": \"" + serverIP + "\",");
        Serial.println("  \"path\": \"" + path + "\",");
        Serial.println("  \"box_id\": \"" + hardwareId + "\",");
        Serial.println("  \"port\": " + String(monitoring_port) + ",");
        Serial.println("  \"commMode\": \"" + commMode + "\",");
        Serial.println("  \"ssid\": \"" + storedSSID + "\",");
        Serial.println("  \"pass\": \"" + storedPassword + "\"");
        Serial.println("  \"mac_address\": \"" + WiFi.macAddress() + "\"");
        int rssi = WiFi.RSSI();
        String signalQuality;
        if (rssi > -60) {
          signalQuality = "Good";
        } else if (rssi > -75) {
          signalQuality = "Fair";
        } else {
          signalQuality = "Weak";
        }
        Serial.println("  \"wifi_rssi_dbm\": " + String(rssi) + ",");
        Serial.println("  \"wifi_quality\": \"" + signalQuality + "\",");
        int estimatedSpeed = 0;
        if (rssi > -60) {
          estimatedSpeed = 72;
        } else if (rssi > -70) {
          estimatedSpeed = 36;
        } else if (rssi > -80) {
          estimatedSpeed = 12;
        } else {
          estimatedSpeed = 1;
        }
        Serial.println("  \"wifi_speed_mbps\": " + String(estimatedSpeed));
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
        Serial.println("  \"commMode\": \"ws\",             // Static IP address for ESP32");
        Serial.println("  \"ssid\": \"YourWiFiSSID\",        // WiFi SSID");
        Serial.println("  \"pass\": \"YourWiFiPassword\"     // WiFi Password");
        Serial.println("}");
        inputString = "";
        return;
      }

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, inputString);
      if (!error) {
        if (doc.containsKey("server_ip")) {
          serverIP = doc["server_ip"].as<String>();
          config.serverIP = serverIP;
        }
        if (doc.containsKey("path")) {
          serverIP = doc["path"].as<String>();
          config.path = path;
        }
        if (doc.containsKey("box_id")) {
          hardwareId = doc["box_id"].as<String>();
          config.hardwareId = hardwareId;
        }
        if (doc.containsKey("port")) {
          monitoring_port = doc["port"].as<int>();
          config.monitoringPort = monitoring_port;
        }
        if (doc.containsKey("commMode")) {
          commMode = doc["commMode"].as<String>();
          config.commMode = commMode;
        }
        if (doc.containsKey("ssid")) {
          storedSSID = doc["ssid"].as<String>();
          config.wifiSSID = storedSSID;
        }
        if (doc.containsKey("pass")) {
          storedPassword = doc["pass"].as<String>();
          config.wifiPass = storedPassword;
        }

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

//---------- Main setup function ----------
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // PUSR
  Wire.begin();
  delay(1000);
  Serial.println("\n==> Starting IoT Node . . . \n");

  if (!LittleFS.begin()) Serial.println("Failed mount config");
  else Serial.println("Config mounted");

  if (!loadConfig()) {
    // Default config if no config file
    Serial.println("Using default configuration");
    config.hardwareId = "001";
    config.serverIP = "192.168.1.2";
    config.serverIP = "/ws";
    config.monitoringPort = 8080;
    config.commMode = "ws";
    config.wifiSSID = "BayMax";
    config.wifiPass = "11111111";
    saveConfig();
  }

  Serial.println("Box ID: " + config.hardwareId);
  Serial.println("Server IP: " + config.serverIP);
  Serial.println("Path: " + config.path);
  Serial.println("Monitoring Port: " + String(config.monitoringPort));
  Serial.println("Comm Mode: " + config.commMode);
  Serial.println("WiFi SSID: " + config.wifiSSID);
  Serial.println("WiFi Pass: " + config.wifiPass);

  // Copy config to variables
  hardwareId = config.hardwareId;
  serverIP = config.serverIP;
  path = config.path;
  String portStr = String(config.monitoringPort);
  if (portStr.length() == 0) monitoring_port = 80;
  else monitoring_port = portStr.toInt();
  commMode = config.commMode;
  Serial.println("Comm Mode Active: " + config.commMode);
  storedSSID = config.wifiSSID;
  storedPassword = config.wifiPass;

  // Initialize PCF8574
  if (!pcf8574.begin()) Serial.println("PCF8574: Failed begin()");
  if (!pcf8574.isConnected()) Serial.println("PCF8574: not connected!");
  else Serial.println("PCF8574: Connected");

  pcf8574.write8(0xFF); // Set all pins HIGH
  for (int i = 0; i < NUM_GPIO_INPUTS; i++) // Input GPIO
  {
    pinMode(INPUT_GPIO_PINS[i], INPUT);
  }

  for (int i = 0; i < NUM_OUTPUTS; i++) // Output GPIO
  {
    pinMode(OUTPUT_PINS[i], OUTPUT);
    digitalWrite(OUTPUT_PINS[i], LOW);
  }

  // Initial State
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    lastInputStates[i] = digitalRead(INPUT_GPIO_PINS[i]) ? 1 : 0;
  }
  uint8_t s = pcf8574.read8();
  for (int j = 0; j < NUM_PCF_INPUTS; j++)
  {
    uint8_t pin = PCF_PIN_MAP[j];
    lastInputStates[NUM_GPIO_INPUTS + j] = (s >> pin) & 0x01;
  }

  // Try to connect to WiFi if credentials are available
  if (storedSSID.length() > 0 && storedPassword.length() > 0 && connectToWiFi()) {
    Serial.println("Try Connect Wifi :");
    isAPMode = false;
    setupOTA();
    Serial.println("Port: " + portStr);
    Serial.println("MAC Address: " + WiFi.macAddress());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time synchronized from NTP.");
  } else {
    setupAP();
  }

  if (!isAPMode) {
    if (commMode == "ws") setupWebSocket();
    //    else if (commMode == "httppost") setupHTTPClient();
    //    else if (commMode == "mqtt") setupMQTTClient();
  }

  // Setup route web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/save-config", HTTP_POST, handleSaveConfig);
  server.begin();
}

//---------- Main loop function  ----------
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  handleSerialInput();

  if (!isAPMode) {
    ensureWiFiConnected();
    webSocket.loop();
    if (commMode == "ws") {
      if (init2Ws) {
        bool intervalPassed = millis() - lastMonitoringTime >= monitoringInterval;
        bool forceSendDue = millis() - lastForceSendTime >= forceSendInterval;

        if (intervalPassed && (checkInputChanges() || (forceSendDue && readytoSend))) {
          sendMonitoringData();
          lastMonitoringTime = millis();
          if (forceSendDue) lastForceSendTime = millis();
        }
      }
    }
    else if (commMode == "httppost") {
      Serial.println("Mode HTTP");
      }
    else if (commMode == "mqtt") {
      Serial.println("Mode MQTT");
      }
  } else {
    // === Timeout AP Mode with countdown ===
    unsigned long elapsed = millis() - apStartTime;
    unsigned long remaining = (apTimeout > elapsed) ? (apTimeout - elapsed) : 0;

    // Print remaining time
    if (millis() - lastCountdownPrint >= 1000) {
      Serial.print("AP Mode active, remaining time: ");
      Serial.print(remaining / 1000);
      Serial.println(" sec");
      lastCountdownPrint = millis();
    }

    if (elapsed > apTimeout) {
      Serial.println("AP Mode timeout, restarting...");
      ESP.restart();   // Restart device to try reconnecting to WiFi
    }
  }
}
