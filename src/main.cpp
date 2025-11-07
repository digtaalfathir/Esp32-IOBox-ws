// C++ Standard Libraries
#include <vector>

// Core Arduino Libraries
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <time.h>

// Third-party Libraries
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <PCF8574.h>

// ============================================================
// CONDITIONAL COMPILATION - Firmware Type
// ============================================================
#ifdef FIRMWARE_WIFI_ONLY
// WiFi-Only Firmware
#include <WebSocketsClient.h>
const String program_version = VERSION_STRING;
#define NETWORK_MODE_NAME "WiFi"
#pragma message("═════════════════════════════════════════")
#pragma message("  BUILDING: WiFi-Only Firmware")
#pragma message("  Version: " VERSION_STRING)
#pragma message("  Network: WiFi STA → AP Fallback")
#pragma message("  WebSocket: links2004 (NETWORK_ESP32)")
#pragma message("  Transport: WiFiClient")
#pragma message("═════════════════════════════════════════")

#elif defined(FIRMWARE_ETHERNET_ONLY)
// Ethernet-Only Firmware
#include <Ethernet.h>
const String program_version = VERSION_STRING;
#define NETWORK_MODE_NAME "Ethernet"
#pragma message("═════════════════════════════════════════")
#pragma message("  BUILDING: Ethernet-Only Firmware")
#pragma message("  Version: " VERSION_STRING)
#pragma message("  Network: Ethernet W5500 → AP Fallback")
#pragma message("  WebSocket: links2004 (NETWORK_W5100)")
#pragma message("  Transport: EthernetClient")
#pragma message("═════════════════════════════════════════")

#else
#error "Must define FIRMWARE_WIFI_ONLY or FIRMWARE_ETHERNET_ONLY"
#endif

// =================================================================
// --- Hardware & Pin Definitions ---
// =================================================================
const int eth_cs_pin = 5;
const int eth_sclk_pin = 18;
const int eth_mosi_pin = 23;
const int eth_miso_pin = 19;
const int eth_int_pin = -1;
const int eth_rst_pin = -1;
const int eth_power_pin = -1;

const int NUM_PCF_INPUTS = 8;
const int NUM_GPIO_INPUTS = 8;
const uint8_t PCF_PIN_MAP[NUM_PCF_INPUTS] = {0, 1, 2, 3, 4, 5, 6, 7};
const int INPUT_GPIO_PINS[NUM_GPIO_INPUTS] = {32, 33, 25, 26, 27, 14, 12, 13};
const int OUTPUT_PINS[] = {17, 16, 4, 15};

const int NUM_INPUTS = NUM_PCF_INPUTS + NUM_GPIO_INPUTS;
const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);

// =================================================================
// --- Network & Server Configuration ---
// =================================================================
String ap_ssid = "IoT-Node ";
const char *ap_password = "12345678";
const char *default_username = "contoh";
const char *default_password = "password";
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// =================================================================
// --- EEPROM Configuration ---
// =================================================================
#define EEPROM_SIZE 512
#define HARDWARE_ID_ADDR 0
#define SERVER_IP_ADDR 32
#define WIFI_SSID_ADDR 64
#define WIFI_PASS_ADDR 128
#define SERVER_PORT_ADDR 192
#define PATH_ADDR 200
#define COMM_MODE_ADDR 232
#define NETWORK_MODE_ADDR 248
#define ACCESS_TOKEN_ADDR 280
#define IO_STATE_START_ADDR 350
#define OUTPUT_STATE_START_ADDR 366
#define TIMESTAMP_ADDR 370
#define IO_STATE_SIGNATURE_ADDR 374
#define IO_STATE_SIGNATURE 0xA5

// =================================================================
// --- Timing & Control ---
// =================================================================
#ifndef CONNECT_TIMEOUT
#define CONNECT_TIMEOUT 15000
#endif

#ifndef AP_TIMEOUT
#define AP_TIMEOUT 150000
#endif

const unsigned long connectTimeout = CONNECT_TIMEOUT;
const unsigned long apTimeout = AP_TIMEOUT;
const unsigned long reconnectInterval = 10 * 1000;
const unsigned long singleAttemptTimeout = 8000;
unsigned long mqttReconnectInterval = 1000;
const long monitoringInterval = 200;
const unsigned long forceSendInterval = 20 * 60 * 1000;
const size_t MAX_BUFFER_SIZE = 100;

int reconnectAttemptCount = 0;
const int maxReconnectAttempts = 2;

// =================================================================
// --- Configuration Structure ---
// =================================================================
struct Config
{
  String hardwareId, networkMode, serverIP, path, commMode, wifiSSID, wifiPass, accessTokenMQTT;
  int monitoringPort;
};
Config config;

// =================================================================
// --- Globals ---
// =================================================================
WebServer server(80);
PCF8574 pcf8574(0x20);

// Network client (conditional)
#ifdef FIRMWARE_WIFI_ONLY
WiFiClient netClient;
WebSocketsClient wsClient;
#define ACTIVE_CLIENT netClient
#else
EthernetClient netClient;
#define ACTIVE_CLIENT netClient
#endif

PubSubClient mqttClient;

// Simplified network status
static bool network_connected = false;
static bool network_ready = false;
bool isAPMode = false;
bool isAuthenticated = false;
bool isReconnecting = false;
bool readyToSend = false;
bool ws_connected = false;

unsigned long apStartTime = 0;
unsigned long lastCountdownPrint = 0;
unsigned long lastMonitoringTime = 0;
unsigned long lastForceSendTime = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectAttemptStartTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastHttpPollTime = 0;
const unsigned long httpPollInterval = 5000;
String httpSessionId = "";

uint8_t lastInputStates[NUM_INPUTS] = {0};
std::vector<String> monitoringBuffer;

// =================================================================
// --- Forward Declarations ---
// =================================================================
void publishStatus();
void publishTelemetry();
bool saveConfig();
void sendMonitoringData();
void setupCommunication();
void setupOTA();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void reconnectMQTT();
void processCommand(JsonDocument &doc);

// Web Server Handlers
void handleRoot();
void handleAuth();
void handleDashboard();
void handleConfigPage();
void handleControlPage();
void handleStatusPage();
void handleSaveConfig();
void handleToggle();
void handleGetOutputs();
void handleDownloadConfig();
void handleLogout();
void serveErrorPage(String errorMessage);

// Other functions
String readFromEEPROM(int address);
void writeToEEPROM(int address, const String &value);
void startAPMode();
void tryReconnect();
bool initEthernet();
void wsDisconnect();
bool wsConnect();
bool wsSendText(const String &s);
void wsPump();
bool loadConfig();
bool sendViaHTTP(const String &jsonData, String &resp);
bool ethHttpPost(const String &host, uint16_t port, const String &path, const String &body, String &response);

// =================================================================
// --- EEPROM Helpers ---
// =================================================================
String readFromEEPROM(int address)
{
  String value = "";
  char ch;
  int currentAddr = address;
  while (currentAddr < EEPROM_SIZE)
  {
    ch = EEPROM.read(currentAddr);
    if (ch == '\0')
      break;
    value += ch;
    currentAddr++;
    if (value.length() > 64)
      break;
  }
  return value;
}
void writeToEEPROM(int address, const String &value)
{
  int i = 0;
  for (; i < value.length() && (address + i) < (EEPROM_SIZE - 1); i++)
  {
    EEPROM.write(address + i, value[i]);
  }
  EEPROM.write(address + i, '\0');
}

// =================================================================
// --- EEPROM I/O STATE ---
// =================================================================
void saveIOStateToEEPROM()
{
  for (int i = 0; i < NUM_INPUTS; i++)
    EEPROM.write(IO_STATE_START_ADDR + i, lastInputStates[i]);
  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    uint8_t outputState = digitalRead(OUTPUT_PINS[i]);
    EEPROM.write(OUTPUT_STATE_START_ADDR + i, outputState);
  }
  unsigned long timestamp = millis();
  EEPROM.write(TIMESTAMP_ADDR + 0, (timestamp >> 24) & 0xFF);
  EEPROM.write(TIMESTAMP_ADDR + 1, (timestamp >> 16) & 0xFF);
  EEPROM.write(TIMESTAMP_ADDR + 2, (timestamp >> 8) & 0xFF);
  EEPROM.write(TIMESTAMP_ADDR + 3, timestamp & 0xFF);
  EEPROM.write(IO_STATE_SIGNATURE_ADDR, IO_STATE_SIGNATURE);
  EEPROM.commit();
}
void loadIOStateFromEEPROM()
{
  if (EEPROM.read(IO_STATE_SIGNATURE_ADDR) != IO_STATE_SIGNATURE)
    return;
  for (int i = 0; i < NUM_INPUTS; i++)
    lastInputStates[i] = EEPROM.read(IO_STATE_START_ADDR + i);
  for (int i = 0; i < NUM_OUTPUTS; i++)
    digitalWrite(OUTPUT_PINS[i], EEPROM.read(OUTPUT_STATE_START_ADDR + i));
}

// =================================================================
// --- Ethernet (W5500) ---
// =================================================================
bool initEthernet()
{
#ifndef FIRMWARE_ETHERNET_ONLY
  return false;
#else
  Serial.println("\n[Ethernet] Initializing...");
  if (eth_rst_pin >= 0)
  {
    pinMode(eth_rst_pin, OUTPUT);
    digitalWrite(eth_rst_pin, LOW);
    delay(50);
    digitalWrite(eth_rst_pin, HIGH);
    delay(200);
  }

  SPI.begin(eth_sclk_pin, eth_miso_pin, eth_mosi_pin, eth_cs_pin);
  Ethernet.init(eth_cs_pin);

  uint64_t chipid = ESP.getEfuseMac();
  byte mac[6];
  mac[0] = 0xDE;
  mac[1] = 0xAD;
  mac[2] = (chipid >> 32) & 0xFF;
  mac[3] = (chipid >> 24) & 0xFF;
  mac[4] = (chipid >> 16) & 0xFF;
  mac[5] = (chipid >> 8) & 0xFF;

  Serial.print("  MAC: ");
  for (int i = 0; i < 6; i++)
  {
    if (mac[i] < 16)
      Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();

  Serial.print("  Trying DHCP... ");
  if (Ethernet.begin(mac, 5000) == 0) // ← Simplified timeout
  {
    Serial.println("FAILED");
    Serial.print("  Trying Static IP... ");
    IPAddress ip(192, 168, 0, 100);
    IPAddress dns(8, 8, 8, 8);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    Ethernet.begin(mac, ip, dns, gateway, subnet);
    delay(1000);
  }
  else
  {
    Serial.println("OK");
  }

  delay(500);
  if (Ethernet.linkStatus() == LinkOFF)
  {
    Serial.println("  No cable detected!");
    return false;
  }

  Serial.println("  Cable connected");
  Serial.print("  IP: ");
  Serial.println(Ethernet.localIP());
  Serial.print("  GW: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("  MASK: ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("  DNS: ");
  Serial.println(Ethernet.dnsServerIP());
  return true;
#endif
}

// =================================================================
// --- File System & Config ---
// =================================================================
bool loadConfig()
{
  bool loadedFromJson = false;
  File file = LittleFS.open("/config.json", "r");
  if (file)
  {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error)
    {
      config.hardwareId = doc["hardwareId"] | "unconfigured_json";
      JsonObject serverObj = doc["server"];
      config.serverIP = serverObj["serverIP"] | "";
      config.path = serverObj["path"] | "/ws";
      config.monitoringPort = doc["monitoringPort"] | 80;
      config.commMode = doc["commMode"] | "ws";
      JsonObject wifiObj = doc["wifi"];
      config.wifiSSID = wifiObj["ssid"] | "";
      config.wifiPass = wifiObj["password"] | "";
      config.accessTokenMQTT = doc["accessTokenMQTT"] | "";
      config.networkMode = doc["networkMode"] | "Hybrid";
      Serial.println("Configuration loaded from LittleFS (JSON).");
      loadedFromJson = true;
    }
    else
    {
      Serial.println("ERR: Parse JSON: " + String(error.c_str()) + ". Trying EEPROM...");
    }
  }
  else
  {
    Serial.println("config.json not found. Trying EEPROM...");
  }

  if (!loadedFromJson)
  {
    Serial.println("Loading from EEPROM...");
    config.hardwareId = readFromEEPROM(HARDWARE_ID_ADDR);
    config.serverIP = readFromEEPROM(SERVER_IP_ADDR);
    config.path = readFromEEPROM(PATH_ADDR);
    config.monitoringPort = readFromEEPROM(SERVER_PORT_ADDR).toInt();
    config.commMode = readFromEEPROM(COMM_MODE_ADDR);
    config.wifiSSID = readFromEEPROM(WIFI_SSID_ADDR);
    config.wifiPass = readFromEEPROM(WIFI_PASS_ADDR);
    config.networkMode = readFromEEPROM(NETWORK_MODE_ADDR);
    config.accessTokenMQTT = readFromEEPROM(ACCESS_TOKEN_ADDR);
    if (config.hardwareId.length() == 0)
      config.hardwareId = "unconfigured_eeprom";
    if (config.path.length() == 0)
      config.path = "/wsiot";
    if (config.monitoringPort <= 0)
      config.monitoringPort = 80;
    if (config.commMode.length() == 0)
      config.commMode = "ws";
    if (config.networkMode.length() == 0)
      config.networkMode = "Hybrid";
  }

  config.hardwareId.trim();
  config.networkMode.trim();
  config.serverIP.trim();
  config.path.trim();
  config.commMode.trim();
  config.wifiSSID.trim();
  config.wifiPass.trim();
  config.accessTokenMQTT.trim();

  if (!config.networkMode.equalsIgnoreCase("WiFi") &&
      !config.networkMode.equalsIgnoreCase("Ethernet") &&
      !config.networkMode.equalsIgnoreCase("Hybrid"))
  {
    Serial.println("[WARN] Invalid networkMode, setting to Hybrid");
    config.networkMode = "Hybrid";
  }

  Serial.println("Syncing initial config to LittleFS...");
  saveConfig();
  return true;
}

bool saveConfig()
{
  bool successJson = false;
  bool successEeprom = false;
  StaticJsonDocument<512> doc;
  doc["hardwareId"] = config.hardwareId;
  JsonObject serverObj = doc.createNestedObject("server");
  serverObj["serverIP"] = config.serverIP;
  serverObj["path"] = config.path;
  doc["monitoringPort"] = config.monitoringPort;
  doc["commMode"] = config.commMode;
  doc["networkMode"] = config.networkMode;
  doc["accessTokenMQTT"] = config.accessTokenMQTT;
  JsonObject wifiObj = doc.createNestedObject("wifi");
  wifiObj["ssid"] = config.wifiSSID;
  wifiObj["password"] = config.wifiPass;
  File file = LittleFS.open("/config.json", "w");
  if (file)
  {
    successJson = (serializeJson(doc, file) > 0);
    file.close();
  }
  writeToEEPROM(HARDWARE_ID_ADDR, config.hardwareId);
  writeToEEPROM(SERVER_IP_ADDR, config.serverIP);
  writeToEEPROM(WIFI_SSID_ADDR, config.wifiSSID);
  writeToEEPROM(WIFI_PASS_ADDR, config.wifiPass);
  writeToEEPROM(SERVER_PORT_ADDR, String(config.monitoringPort));
  writeToEEPROM(PATH_ADDR, config.path);
  writeToEEPROM(COMM_MODE_ADDR, config.commMode);
  writeToEEPROM(NETWORK_MODE_ADDR, config.networkMode);
  writeToEEPROM(ACCESS_TOKEN_ADDR, config.accessTokenMQTT);
  successEeprom = EEPROM.commit();
  return successJson && successEeprom;
}

String loadFile(const char *path)
{
  File file = LittleFS.open(path, "r");
  if (!file)
    return "";
  String content = file.readString();
  file.close();
  return content;
}

// =================================================================
// --- JSON Builders ---
// =================================================================
String buildStatusJSON()
{
  StaticJsonDocument<512> doc;
  doc["hardwareId"] = config.hardwareId;
  doc["firmware_version"] = program_version;
  doc["commMode"] = config.commMode;
  doc["serverIP"] = config.serverIP;
  doc["monitoringPort"] = config.monitoringPort;
  doc["networkMode"] = config.networkMode;
#ifdef FIRMWARE_WIFI_ONLY
  if (network_connected)
  {
    doc["connection_type"] = "WiFi";
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mac_address"] = WiFi.macAddress();
    doc["wifiSSID"] = config.wifiSSID;
  }
  else
  {
    doc["connection_type"] = "None";
  }
#else
  if (network_connected)
  {
    doc["connection_type"] = "Ethernet";
    doc["ip_address"] = Ethernet.localIP().toString();
  }
  else
  {
    doc["connection_type"] = "None";
  }
#endif
  // =============================================

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

String buildMonitoringJSON(bool forThingsBoard = false)
{
  StaticJsonDocument<512> doc;
  if (!forThingsBoard)
  {
    doc["box_id"] = config.hardwareId;
  }
  for (int i = 0; i < NUM_PCF_INPUTS; i++)
  {
    String kondisi_input = String(lastInputStates[i]);
    kondisi_input = (kondisi_input == "1") ? "0" : "1";
    doc["I" + String(i + 1)] = kondisi_input;
  }
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    String kondisi_input = String(lastInputStates[NUM_PCF_INPUTS + i]);
    kondisi_input = (kondisi_input == "1") ? "0" : "1";
    doc["I" + String(NUM_PCF_INPUTS + i + 1)] = kondisi_input;
  }
  for (int i = 0; i < NUM_OUTPUTS; i++)
    doc["Q" + String(i + 1)] = digitalRead(OUTPUT_PINS[i]);
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

// =================================================================
// --- MQTT ---
// =================================================================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("[MQTT] Message received on topic: ");
  Serial.println(topic);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error)
  {
    Serial.println("[MQTT] JSON parse error");
    return;
  }

  if (!doc.containsKey("box_id") || doc["box_id"].as<String>() != config.hardwareId)
  {
    Serial.println("[MQTT] box_id mismatch, ignoring");
    return;
  }

  if (doc.containsKey("cmd"))
  {
    String cmd = doc["cmd"].as<String>();

    if (cmd == "config")
    {
      StaticJsonDocument<400> resp;
      resp["box_id"] = config.hardwareId;
      resp["serverIP"] = config.serverIP;
      resp["port"] = config.monitoringPort;
      resp["wifiSSID"] = config.wifiSSID;
      resp["networkMode"] = config.networkMode;

#ifdef FIRMWARE_WIFI_ONLY
      if (network_connected)
      {
        resp["ip"] = WiFi.localIP().toString();
        resp["mac"] = WiFi.macAddress();
      }
#else
      if (network_connected)
      {
        resp["ip"] = Ethernet.localIP().toString();
      }
#endif

      String jsonOut;
      serializeJson(resp, jsonOut);
      String topic = "iot-node/" + config.hardwareId + "/config_response";
      mqttClient.publish(topic.c_str(), jsonOut.c_str());
    }
    else if (cmd == "restart")
    {
      StaticJsonDocument<100> resp;
      resp["box_id"] = config.hardwareId;
      resp["status"] = "restarting";
      String jsonOut;
      serializeJson(resp, jsonOut);
      String topic = "iot-node/" + config.hardwareId + "/status";
      mqttClient.publish(topic.c_str(), jsonOut.c_str());
      delay(500);
      ESP.restart();
    }
  }
  else if (doc.containsKey("serverIP") || doc.containsKey("wifiSSID") ||
           doc.containsKey("monitoringPort") || doc.containsKey("wifiPass") ||
           doc.containsKey("networkMode"))
  {

    bool updated = false;
    if (doc.containsKey("serverIP") && doc["serverIP"].as<String>() != config.serverIP)
    {
      config.serverIP = doc["serverIP"].as<String>();
      updated = true;
    }
    if (doc.containsKey("monitoringPort") && doc["monitoringPort"].as<int>() != config.monitoringPort)
    {
      config.monitoringPort = doc["monitoringPort"].as<int>();
      updated = true;
    }
    if (doc.containsKey("wifiSSID") && doc["wifiSSID"].as<String>() != config.wifiSSID)
    {
      config.wifiSSID = doc["wifiSSID"].as<String>();
      updated = true;
    }
    if (doc.containsKey("wifiPass") && doc["wifiPass"].as<String>().length() > 0)
    {
      config.wifiPass = doc["wifiPass"].as<String>();
      updated = true;
    }
    if (doc.containsKey("networkMode"))
    {
      String newMode = doc["networkMode"].as<String>();
      if ((newMode == "Ethernet" || newMode == "WiFi" || newMode == "Hybrid") &&
          newMode != config.networkMode)
      {
        config.networkMode = newMode;
        updated = true;
      }
    }

    if (updated)
    {
      saveConfig();
      StaticJsonDocument<100> resp;
      resp["box_id"] = config.hardwareId;
      resp["status"] = "config_updated";
      String jsonOut;
      serializeJson(resp, jsonOut);
      String topic = "iot-node/" + config.hardwareId + "/status";
      mqttClient.publish(topic.c_str(), jsonOut.c_str());
      delay(1000);
      ESP.restart();
    }
  }
  else if (doc.containsKey("O1") || doc.containsKey("O2") ||
           doc.containsKey("O3") || doc.containsKey("O4"))
  {
    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
      String key = "O" + String(i + 1);
      if (doc.containsKey(key))
      {
        int state = doc[key].as<int>();
        digitalWrite(OUTPUT_PINS[i], state == 1 ? HIGH : LOW);
      }
    }
    saveIOStateToEEPROM();
  }
}

void reconnectMQTT()
{
  if (millis() - lastMqttReconnectAttempt < mqttReconnectInterval)
    return;
  lastMqttReconnectAttempt = millis();

  mqttClient.setClient(ACTIVE_CLIENT);
  mqttClient.setServer(config.serverIP.c_str(), config.monitoringPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);

  Serial.println("\n═══ MQTT CONNECTION ═══");
  Serial.println("Server: " + config.serverIP + ":" + String(config.monitoringPort));
  Serial.printf("Transport: %s\n", NETWORK_MODE_NAME);

  String clientId = "IoTNode-" + config.hardwareId;
  Serial.println("ClientID: " + clientId);
  Serial.print("Connecting... ");

  bool ok = mqttClient.connect(clientId.c_str());
  if (ok)
  {
    Serial.println("✓ SUCCESS!");
    String cmdTopic = "iot-node/" + config.hardwareId + "/cmd";
    mqttClient.subscribe(cmdTopic.c_str());
    publishStatus();
    readyToSend = true;
    mqttReconnectInterval = 5000;
  }
  else
  {
    Serial.print("✗ FAILED! RC=");
    Serial.println(mqttClient.state());
    readyToSend = false;
    mqttReconnectInterval = 1000;
  }
}

void publishTelemetry()
{
  if (!mqttClient.connected())
    return;

  bool isThingsBoard = (config.serverIP.indexOf("thingsboard") >= 0);
  String data = buildMonitoringJSON(isThingsBoard);

  String telemetryTopic = isThingsBoard ? "v1/devices/me/telemetry" : "iot-node/" + config.hardwareId + "/telemetry";

  if (mqttClient.publish(telemetryTopic.c_str(), data.c_str()))
  {
    Serial.println("[MQTT] Sent to: " + telemetryTopic);
    Serial.println("Sending : " + data);
  }
  else
  {
    Serial.println("[MQTT] Failed to publish");
  }
}

/// =================================================================
// --- WebSocket Connect ---
// =================================================================
bool wsConnect()
{
#ifdef FIRMWARE_WIFI_ONLY
  Serial.printf("[WS] Connecting to ws://%s:%d%s\n",
                config.serverIP.c_str(),
                config.monitoringPort,
                config.path.c_str());
  Serial.printf("[WS] Transport: %s\n", NETWORK_MODE_NAME);

  wsClient.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                   {
    switch (type) {
    case WStype_DISCONNECTED:
      {
        Serial.println("[WS] Disconnected!");
        ws_connected = false;
      }
      break;

    case WStype_CONNECTED:
      {
        Serial.printf("[WS] ✓ Connected to: %s\n", payload);
        ws_connected = true;
        publishStatus();
      }
      break;

    case WStype_TEXT:
      {
        Serial.printf("[WS] ← Received: %s\n", payload);
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        
        if (!error) {
          processCommand(doc);
        } else {
          Serial.printf("[WS] JSON parse error: %s\n", error.c_str());
        }
      }
      break;

    case WStype_BIN:
      Serial.printf("[WS] Binary data (%u bytes)\n", length);
      break;

    case WStype_PING:
      Serial.println("[WS] Ping");
      break;

    case WStype_PONG:
      Serial.println("[WS] Pong");
      break;

    case WStype_ERROR:
      {
        Serial.printf("[WS]  Error: %s\n", payload);
        ws_connected = false;
      }
      break;
      
    default:
      break;
    } });

  wsClient.setReconnectInterval(5000);

  IPAddress serverIP;
  if (serverIP.fromString(config.serverIP))
  {
    wsClient.begin(serverIP, config.monitoringPort, config.path.c_str());
  }
  else
  {
    wsClient.begin(config.serverIP, config.monitoringPort, config.path);
  }

  Serial.println("[WS] Connection initiated...");
  return true;
#endif
}

void wsDisconnect()
{
#ifdef FIRMWARE_WIFI_ONLY
  Serial.println("[WS] Disconnecting...");
  wsClient.disconnect();
  ws_connected = false;
#endif
}

bool wsSendText(const String &s)
{
#ifdef FIRMWARE_WIFI_ONLY
  if (!ws_connected)
  {
    Serial.println("[WS] Not connected, cannot send");
    return false;
  }

  String payload = s;
  wsClient.sendTXT(payload);
  return true;
#endif
}

void wsPump()
{
#ifdef FIRMWARE_WIFI_ONLY
  wsClient.loop();
#endif
}

// =================================================================
// --- HTTP POST Helper ---
// =================================================================
bool ethHttpPost(const String &host, uint16_t port, const String &path, const String &body, String &response)
{
#ifndef FIRMWARE_ETHERNET_ONLY
  // Ethernet tidak tersedia di WiFi firmware
  Serial.println("[HTTP] Ethernet not available in WiFi firmware");
  return false;

#else
  // ============== ETHERNET IMPLEMENTATION ==============
  if (!netClient.connect(host.c_str(), port))
  {
    Serial.println("[HTTP] Ethernet connect failed");
    return false;
  }

  netClient.print("POST ");
  netClient.print(path);
  netClient.print(" HTTP/1.1\r\n");
  netClient.print("Host: ");
  netClient.print(host);
  netClient.print("\r\n");
  netClient.print("Content-Type: application/json\r\n");
  netClient.print("Content-Length: ");
  netClient.print(body.length());
  netClient.print("\r\n");
  netClient.print("Connection: close\r\n\r\n");
  netClient.print(body);

  unsigned long t0 = millis();
  while (netClient.connected() && !netClient.available() && millis() - t0 < 5000)
    delay(10);

  response = "";
  while (netClient.available())
    response += (char)netClient.read();
  netClient.stop();

  int status = -1;
  int sp1 = response.indexOf(' ');
  if (sp1 > 0)
  {
    int sp2 = response.indexOf(' ', sp1 + 1);
    if (sp2 > sp1)
      status = response.substring(sp1 + 1, sp2).toInt();
  }
  return status >= 200 && status < 300;
#endif
}

bool sendViaHTTP(const String &jsonData, String &resp)
{
#ifdef FIRMWARE_WIFI_ONLY
  // ============== WIFI FIRMWARE ==============
  HTTPClient http;
  String url = "http://" + config.serverIP + ":" +
               String(config.monitoringPort) + config.path;
  http.begin(netClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-ID", config.hardwareId);

  int code = http.POST(jsonData);
  resp = (code > 0) ? http.getString() : "";

  if (code <= 0)
  {
    Serial.printf("[HTTP] ✗ WiFi Error: %s\n", http.errorToString(code).c_str());
  }

  http.end();
  return code > 0;

#else
  // ============== ETHERNET FIRMWARE ==============
  return ethHttpPost(config.serverIP, config.monitoringPort,
                     config.path, jsonData, resp);
#endif
}

// =================================================================
// --- HTTP POST Command Polling ---
// =================================================================
void pollHTTPCommands()
{
  if (millis() - lastHttpPollTime < httpPollInterval)
    return;
  lastHttpPollTime = millis();

  String cmdUrl = config.path;
  if (!cmdUrl.endsWith("/"))
    cmdUrl += "/";
  cmdUrl += "commands";

#ifdef FIRMWARE_WIFI_ONLY
  HTTPClient http;
  String fullUrl = "http://" + config.serverIP + ":" + String(config.monitoringPort) + cmdUrl;
  http.begin(netClient, fullUrl); // ← netClient
  int code = http.GET();
  if (code == HTTP_CODE_OK)
  {
    String payload = http.getString();
    if (payload.length() && payload != "null" && payload != "[]")
    {
      StaticJsonDocument<512> doc;
      if (!deserializeJson(doc, payload))
        processCommand(doc);
    }
  }
  http.end();

#else
  String resp;
  if (netClient.connect(config.serverIP.c_str(), config.monitoringPort)) // ← netClient (bukan ethClient)
  {
    netClient.print("GET ");
    netClient.print(cmdUrl);
    netClient.print(" HTTP/1.1\r\n");
    netClient.print("Host: ");
    netClient.print(config.serverIP);
    netClient.print("\r\n");
    netClient.print("Connection: close\r\n\r\n");
    unsigned long t0 = millis();
    while (netClient.connected() && !netClient.available() && millis() - t0 < 5000)
      delay(10);
    while (netClient.available())
      resp += (char)netClient.read();
    netClient.stop();

    int sep = resp.indexOf("\r\n\r\n");
    String body = (sep >= 0) ? resp.substring(sep + 4) : resp;
    if (body.length() && body != "null" && body != "[]")
    {
      StaticJsonDocument<512> doc;
      if (!deserializeJson(doc, body))
        processCommand(doc);
    }
  }
#endif
}

// =================================================================
// --- Universal Command Processor ---
// =================================================================
void processCommand(JsonDocument &doc)
{
  // Validate box_id
  if (doc.containsKey("box_id") && doc["box_id"].as<String>() != config.hardwareId)
  {
    Serial.println("[CMD] box_id mismatch, ignoring");
    return;
  }

  // Handle system commands
  if (doc.containsKey("cmd"))
  {
    String cmd = doc["cmd"].as<String>();
    Serial.println("[CMD] Received: " + cmd);

    if (cmd == "config")
    {
      // Send configuration info
      publishStatus();
    }
    else if (cmd == "restart")
    {
      Serial.println("[CMD] Restart requested");

      // Send acknowledgment
      StaticJsonDocument<100> resp;
      resp["box_id"] = config.hardwareId;
      resp["status"] = "restarting";
      String jsonOut;
      serializeJson(resp, jsonOut);

      if (config.commMode == "ws")
      {
        wsSendText(jsonOut);
      }
      else if (config.commMode == "mqtt")
      {
        String topic = "iot-node/" + config.hardwareId + "/status";
        mqttClient.publish(topic.c_str(), jsonOut.c_str());
      }
      else if (config.commMode == "httppost")
      {
        String response;
        sendViaHTTP(jsonOut, response);
      }

      delay(500);
      ESP.restart();
    }
    else if (cmd == "get_status")
    {
      publishStatus();
    }
    else if (cmd == "get_io")
    {
      sendMonitoringData();
    }
  }

  // Handle configuration update
  else if (doc.containsKey("serverIP") || doc.containsKey("wifiSSID") ||
           doc.containsKey("monitoringPort") || doc.containsKey("wifiPass") ||
           doc.containsKey("networkMode"))
  {
    bool updated = false;

    if (doc.containsKey("serverIP") && doc["serverIP"].as<String>() != config.serverIP)
    {
      config.serverIP = doc["serverIP"].as<String>();
      updated = true;
    }
    if (doc.containsKey("monitoringPort") && doc["monitoringPort"].as<int>() != config.monitoringPort)
    {
      config.monitoringPort = doc["monitoringPort"].as<int>();
      updated = true;
    }
    if (doc.containsKey("wifiSSID") && doc["wifiSSID"].as<String>() != config.wifiSSID)
    {
      config.wifiSSID = doc["wifiSSID"].as<String>();
      updated = true;
    }
    if (doc.containsKey("wifiPass") && doc["wifiPass"].as<String>().length() > 0)
    {
      config.wifiPass = doc["wifiPass"].as<String>();
      updated = true;
    }
    if (doc.containsKey("networkMode"))
    {
      String newMode = doc["networkMode"].as<String>();
      if ((newMode == "Ethernet" || newMode == "WiFi" || newMode == "Hybrid") &&
          newMode != config.networkMode)
      {
        config.networkMode = newMode;
        updated = true;
      }
    }

    if (updated)
    {
      Serial.println("[CMD] Configuration updated");
      saveConfig();

      // Send acknowledgment
      StaticJsonDocument<100> resp;
      resp["box_id"] = config.hardwareId;
      resp["status"] = "config_updated";
      String jsonOut;
      serializeJson(resp, jsonOut);

      if (config.commMode == "ws")
      {
        wsSendText(jsonOut);
      }
      else if (config.commMode == "mqtt")
      {
        String topic = "iot-node/" + config.hardwareId + "/status";
        mqttClient.publish(topic.c_str(), jsonOut.c_str());
      }
      else if (config.commMode == "httppost")
      {
        String response;
        sendViaHTTP(jsonOut, response);
      }

      delay(1000);
      ESP.restart();
    }
  }

  // Handle output control
  else if (doc.containsKey("O1") || doc.containsKey("O2") ||
           doc.containsKey("O3") || doc.containsKey("O4"))
  {
    Serial.println("[CMD] Output control received");

    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
      String key = "O" + String(i + 1);
      if (doc.containsKey(key))
      {
        int state = doc[key].as<int>();
        digitalWrite(OUTPUT_PINS[i], state == 1 ? HIGH : LOW);
        Serial.printf("[CMD] O%d = %d\n", i + 1, state);
      }
    }

    saveIOStateToEEPROM();

    // Send acknowledgment with current I/O state
    delay(100);
    sendMonitoringData();
  }
}

// =================================================================
// --- HTTP/WS/MQTT Send Helpers ---
// =================================================================
void sendMonitoringData()
{
  if (!network_ready)
    return;

  bool isThingsBoard = (config.serverIP.indexOf("thingsboard") >= 0);
  String data = buildMonitoringJSON(isThingsBoard);

  Serial.println("Sending : " + data);

  if (config.commMode == "ws")
  {
    if (ws_connected)
    {
      wsSendText(data);
    }
    else
    {
      Serial.println("[WS] Not connected, skipping send");
    }
  }
  else if (config.commMode == "mqtt")
  {
    if (mqttClient.connected())
    {
      String topic = isThingsBoard ? "v1/devices/me/telemetry" : "iot-node/" + config.hardwareId + "/telemetry";
      mqttClient.publish(topic.c_str(), data.c_str());
    }
  }
  else if (config.commMode == "httppost")
  {
    String response;
    if (sendViaHTTP(data, response))
    {
      Serial.println("[HTTP] Telemetry sent successfully");

      // Check if server sent command in response
      if (response.length() > 0 && response != "OK" && response != "null")
      {
        Serial.println("[HTTP] Server response: " + response);

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, response) == DeserializationError::Ok)
        {
          processCommand(doc);
        }
      }
    }
    else
    {
      Serial.println("[HTTP] Failed to send telemetry");
    }
  }
}

void publishStatus()
{
  if (!network_ready)
    return;

  String statusData = buildStatusJSON();
  Serial.println("Publishing Status: " + statusData);

  if (config.commMode == "ws")
  {
    if (ws_connected)
    {
      wsSendText(statusData);
    }
  }
  else if (config.commMode == "mqtt")
  {
    if (mqttClient.connected())
    {
      String topic = "iot-node/" + config.hardwareId + "/status";
      mqttClient.publish(topic.c_str(), statusData.c_str());
    }
  }
  else if (config.commMode == "httppost")
  {
    String response;
    if (sendViaHTTP(statusData, response))
    {
      Serial.println("[HTTP] Status published successfully");
    }
  }
}

// =================================================================
// --- I/O & WEB SERVER (ringkas) ---
// =================================================================
bool checkInputChanges()
{
  bool changed = false;
  uint8_t s = pcf8574.read8();
  for (int i = 0; i < NUM_PCF_INPUTS; i++)
  {
    uint8_t current = (s >> PCF_PIN_MAP[i]) & 0x01;
    if (current != lastInputStates[i])
    {
      lastInputStates[i] = current;
      changed = true;
    }
  }
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    uint8_t current = digitalRead(INPUT_GPIO_PINS[i]);
    int index = NUM_PCF_INPUTS + i;
    if (current != lastInputStates[index])
    {
      lastInputStates[index] = current;
      changed = true;
    }
  }
  if (changed)
    saveIOStateToEEPROM();
  return changed;
}

String htmlPage(String body)
{
  return String("<html><body>") + body + "</body></html>";
}

void handleDashboard()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/dashboard.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholders
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%NETWORK_MODE%", config.networkMode);
    html.replace("%COMM_MODE%", config.commMode);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));

    server.send(200, "text/html", html);
  }
  else
  {
    serveErrorPage("Dashboard not found");
  }
}

// Handler untuk Config Page
void handleConfigPage()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/config.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholders
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));
    html.replace("%PATH%", config.path);
    html.replace("%WIFI_SSID%", config.wifiSSID);
    html.replace("%WIFI_PASS%", config.wifiPass);
    html.replace("%ACCESS_TOKEN%", config.accessTokenMQTT);

    // Network Mode
    html.replace("%NETWORK_WIFI%", config.networkMode == "WiFi" ? "selected" : "");
    html.replace("%NETWORK_ETH%", config.networkMode == "Ethernet" ? "selected" : "");
    html.replace("%NETWORK_HYBRID%", config.networkMode == "Hybrid" ? "selected" : "");

    // Comm Mode
    html.replace("%COMM_WS%", config.commMode == "ws" ? "selected" : "");
    html.replace("%COMM_HTTP%", config.commMode == "httppost" ? "selected" : "");
    html.replace("%COMM_MQTT%", config.commMode == "mqtt" ? "selected" : "");

    server.send(200, "text/html", html);
  }
  else
  {
    serveErrorPage("Config page not found");
  }
}

// Handler untuk Control Page
void handleControlPage()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/control.html", "r");
  if (file)
  {
    server.streamFile(file, "text/html");
    file.close();
  }
  else
  {
    serveErrorPage("Control page not found");
  }
}

// Handler untuk Status Page
void handleStatusPage()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/status.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholders
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%NETWORK_MODE%", config.networkMode);
    html.replace("%WIFI_SSID%", config.wifiSSID);
    html.replace("%COMM_MODE%", config.commMode);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));
    html.replace("%PATH%", config.path);

    // ✅ FIX: Conditional compilation
#ifdef FIRMWARE_WIFI_ONLY
    if (network_connected)
    {
      html.replace("%IP_ADDRESS%", WiFi.localIP().toString());
      html.replace("%MAC_ADDRESS%", WiFi.macAddress());
    }
    else
    {
      html.replace("%IP_ADDRESS%", "Not Connected");
      html.replace("%MAC_ADDRESS%", "N/A");
    }
#else
    if (network_connected)
    {
      html.replace("%IP_ADDRESS%", Ethernet.localIP().toString());
      html.replace("%MAC_ADDRESS%", "N/A");
    }
    else
    {
      html.replace("%IP_ADDRESS%", "Not Connected");
      html.replace("%MAC_ADDRESS%", "N/A");
    }
#endif

    server.send(200, "text/html", html);
  }
  else
  {
    serveErrorPage("Status page not found");
  }
}

// Update handleRoot - redirect to dashboard after login
void handleRoot()
{
  if (!isAuthenticated)
  {
    File file = LittleFS.open("/login.html", "r");
    if (file)
    {
      server.streamFile(file, "text/html");
      file.close();
    }
    else
    {
      server.send(404, "text/plain", "Login page not found");
    }
    return;
  }

  // Redirect to dashboard
  server.sendHeader("Location", "/dashboard");
  server.send(302, "text/plain", "");
}

// Update handleAuth - redirect to dashboard on success
void handleAuth()
{
  if (server.arg("username") == default_username &&
      server.arg("password") == default_password)
  {
    isAuthenticated = true;
    // Serial.println("[Auth] Login successful");
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  }
  else
  {
    // Serial.println("[Auth] Login failed - Invalid credentials");
    server.sendHeader("Location", "/?error=1");
    server.send(302, "text/plain", "");
  }
}
void handleSaveConfig()
{
  // Check authentication
  if (!isAuthenticated)
  {
    serveErrorPage("Unauthorized access. Please login first.");
    return;
  }

  // ===== VALIDATION =====

  // Validate Hardware ID
  if (!server.hasArg("hardwareId") || server.arg("hardwareId").length() == 0)
  {
    serveErrorPage("Hardware ID cannot be empty.");
    return;
  }

  // Validate Server IP
  if (!server.hasArg("serverIP") || server.arg("serverIP").length() == 0)
  {
    serveErrorPage("Server IP cannot be empty.");
    return;
  }

  // Validate Server Port
  if (!server.hasArg("serverPort") || server.arg("serverPort").toInt() <= 0 || server.arg("serverPort").toInt() > 65535)
  {
    serveErrorPage("Invalid server port number. Must be between 1-65535.");
    return;
  }

  // ===== CHECK FOR CHANGES =====
  bool changed = false;

  Serial.println("[Config] Checking for changes...");

  // Hardware ID
  if (server.hasArg("hardwareId") && server.arg("hardwareId") != config.hardwareId)
  {
    config.hardwareId = server.arg("hardwareId");
    config.hardwareId.trim();
    changed = true;
  }

  // Server IP
  if (server.hasArg("serverIP") && server.arg("serverIP") != config.serverIP)
  {
    config.serverIP = server.arg("serverIP");
    config.serverIP.trim();
    changed = true;
  }

  // Server Port
  if (server.hasArg("serverPort") && server.arg("serverPort").toInt() != config.monitoringPort)
  {
    config.monitoringPort = server.arg("serverPort").toInt();
    changed = true;
  }

  // Server Path
  if (server.hasArg("path") && server.arg("path") != config.path)
  {
    config.path = server.arg("path");
    config.path.trim();
    if (config.path.length() == 0)
    {
      config.path = "/wsiot";
    }
    changed = true;
  }

  // Communication Mode
  if (server.hasArg("commMode") && server.arg("commMode") != config.commMode)
  {
    config.commMode = server.arg("commMode");
    config.commMode.trim();
    changed = true;
  }

  // MQTT Access Token
  if (server.hasArg("accessToken") && server.arg("accessToken") != config.accessTokenMQTT)
  {
    config.accessTokenMQTT = server.arg("accessToken");
    config.accessTokenMQTT.trim();
    changed = true;
  }

  // WiFi SSID
  if (server.hasArg("ssid") && server.arg("ssid") != config.wifiSSID)
  {
    config.wifiSSID = server.arg("ssid");
    config.wifiSSID.trim();
    changed = true;
  }

  // WiFi Password
  if (server.hasArg("password") && server.arg("password").length() > 0 &&
      server.arg("password") != config.wifiPass)
  {
    config.wifiPass = server.arg("password");
    config.wifiPass.trim();
    changed = true;
  }

  // Network Mode
  if (server.hasArg("networkMode") && server.arg("networkMode") != config.networkMode)
  {
    config.networkMode = server.arg("networkMode");
    config.networkMode.trim();
    changed = true;
  }

  // ===== SAVE OR NO CHANGES =====

  if (!changed)
  {
    Serial.println("[Config] No changes detected");

    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<meta http-equiv='refresh' content='2;url=/config'>"
                "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap' rel='stylesheet'>"
                "<style>"
                "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);"
                "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
                ".box{background:white;padding:50px 40px;border-radius:24px;text-align:center;max-width:400px;"
                "box-shadow:0 25px 50px rgba(0,0,0,0.3);animation:slideUp 0.5s ease;}"
                "@keyframes slideUp{from{opacity:0;transform:translateY(30px)}to{opacity:1;transform:translateY(0)}}"
                "h2{color:#1e293b;margin-bottom:15px;font-size:24px;}"
                "p{color:#64748b;font-size:15px;line-height:1.6;}"
                ".icon{font-size:60px;margin-bottom:20px;}"
                "</style>"
                "</head><body>"
                "<div class='box'>"
                "<div class='icon'>ℹ️</div>"
                "<h2>No Changes Detected</h2>"
                "<p>Configuration is already up to date.<br>Redirecting back...</p>"
                "</div>"
                "</body></html>");
    return;
  }

  // ===== SAVE CONFIGURATION =====

  bool saveSuccess = saveConfig();

  if (!saveSuccess)
  {
    serveErrorPage("Failed to save configuration. Please try again.");
    return;
  }

  // ===== SERVE SUCCESS PAGE =====

  File file = LittleFS.open("/success.html", "r");
  if (file)
  {
    Serial.println("[Config] Serving success page");
    String html = file.readString();
    file.close();

    // Send response
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", html);

    // Ensure response is fully sent
    delay(100);

    for (int i = 5; i > 0; i--)
    {
      Serial.printf("[System] %d...\n", i);
      delay(1000);
    }
    delay(100);

    ESP.restart();
  }
  else
  {
    Serial.println("[Config] Warning: success.html not found, using enhanced fallback");

    String fallbackHtml =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap' rel='stylesheet'>"
        "<style>"
        "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);"
        "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
        ".box{background:white;padding:50px 40px;border-radius:24px;text-align:center;max-width:450px;"
        "box-shadow:0 25px 50px rgba(0,0,0,0.3);animation:slideUp 0.5s ease;}"
        "@keyframes slideUp{from{opacity:0;transform:translateY(30px)}to{opacity:1;transform:translateY(0)}}"
        "h2{color:#10b981;margin-bottom:15px;font-size:28px;font-weight:700;}"
        "p{color:#64748b;line-height:1.6;margin-bottom:25px;font-size:15px;}"
        ".countdown{font-size:18px;font-weight:600;color:#667eea;margin-bottom:15px;}"
        ".number{font-size:32px;font-weight:700;color:#667eea;display:inline-block;min-width:40px;}"
        ".progress{background:#e2e8f0;border-radius:100px;height:8px;overflow:hidden;margin-bottom:20px;}"
        ".bar{height:100%;background:linear-gradient(90deg,#667eea,#764ba2);border-radius:100px;animation:fill 5s linear;}"
        "@keyframes fill{from{width:0%}to{width:100%}}"
        ".check{font-size:70px;margin-bottom:20px;}"
        "</style>"
        "</head><body>"
        "<div class='box'>"
        "<div class='check'>✅</div>"
        "<h2>Configuration Saved!</h2>"
        "<p>Your settings have been saved successfully.<br>Device will restart to apply changes.</p>"
        "<div class='progress'><div class='bar'></div></div>"
        "<div class='countdown'>Restarting in <span class='number' id='timer'>5</span> seconds</div>"
        "</div>"
        "<script>"
        "let c=5;const t=setInterval(()=>{"
        "c--;document.getElementById('timer').textContent=c;"
        "if(c<=0){clearInterval(t);window.location.href='/';}},1000);"
        "</script>"
        "</body></html>";

    server.sendHeader("Connection", "close");
    server.send(200, "text/html", fallbackHtml);

    delay(5100);
    ESP.restart();
  }
}

// =================================================================
// --- Toggle Output Handler ---
// =================================================================
void handleToggle()
{
  if (!isAuthenticated)
  {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  if (!server.hasArg("pin") || !server.hasArg("state"))
  {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }

  int pin = server.arg("pin").toInt();
  String state = server.arg("state");

  if (pin < 0 || pin >= NUM_OUTPUTS)
  {
    server.send(400, "text/plain", "Invalid pin number");
    return;
  }

  if (state != "on" && state != "off")
  {
    server.send(400, "text/plain", "Invalid state value");
    return;
  }

  digitalWrite(OUTPUT_PINS[pin], state == "on" ? HIGH : LOW);
  saveIOStateToEEPROM();

  Serial.printf("[Control] Output O%d set to %s\n", pin + 1, state.c_str());
  server.send(200, "text/plain", "OK");
}

// =================================================================
// --- Get Outputs Handler ---
// =================================================================
void handleGetOutputs()
{
  if (!isAuthenticated)
  {
    server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  StaticJsonDocument<200> doc;
  JsonArray outputs = doc.createNestedArray("outputs");

  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    outputs.add(digitalRead(OUTPUT_PINS[i]));
  }

  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
}

// =================================================================
// --- Download Config Handler ---
// =================================================================
void handleDownloadConfig()
{
  if (!isAuthenticated)
  {
    serveErrorPage("Unauthorized access. Please login first.");
    return;
  }

  StaticJsonDocument<512> doc;
  doc["hardwareId"] = config.hardwareId;
  doc["serverIP"] = config.serverIP;
  doc["monitoringPort"] = config.monitoringPort;
  doc["path"] = config.path;
  doc["commMode"] = config.commMode;
  doc["networkMode"] = config.networkMode;
  doc["wifiSSID"] = config.wifiSSID;
  doc["wifiPass"] = config.wifiPass;
  doc["accessTokenMQTT"] = config.accessTokenMQTT;

  String jsonString;
  serializeJsonPretty(doc, jsonString);

  if (jsonString.length() == 0)
  {
    serveErrorPage("Failed to generate configuration file.");
    return;
  }

  String filename = "config_" + config.hardwareId + ".json";
  server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
  server.send(200, "application/json", jsonString);

  Serial.println("[Config] Configuration downloaded: " + filename);
}

// =================================================================
// --- Logout Handler ---
// =================================================================
void handleLogout()
{
  isAuthenticated = false;
  Serial.println("[Auth] User logged out");
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// =================================================================
// --- Error Page Handler ---
// =================================================================
void serveErrorPage(String errorMessage)
{
  Serial.println("[Error] " + errorMessage);

  File file = LittleFS.open("/error.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholder dengan pesan error
    html.replace("%ERROR_MESSAGE%", errorMessage);

    server.send(400, "text/html", html);
  }
  else
  {
    // Fallback jika error.html tidak ditemukan
    server.send(400, "text/html",
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<style>"
                "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);"
                "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
                ".box{background:white;padding:40px;border-radius:20px;text-align:center;max-width:400px;box-shadow:0 20px 60px rgba(0,0,0,0.3);}"
                "h2{color:#ef4444;margin-bottom:15px;font-size:24px;}"
                "p{color:#64748b;line-height:1.6;}"
                "button{margin-top:20px;padding:12px 24px;background:#667eea;color:white;border:none;border-radius:10px;font-weight:600;cursor:pointer;}"
                "</style>"
                "</head><body>"
                "<div class='box'>"
                "<div style='font-size:60px;margin-bottom:20px;'>⚠️</div>"
                "<h2>Error Occurred</h2>"
                "<p>" +
                    errorMessage + "</p>"
                                   "<button onclick='history.back()'>← Go Back</button>"
                                   "</div>"
                                   "</body></html>");
  }
}

// =================================================================
// --- OTA ---
// =================================================================
void setupOTA()
{
#ifdef FIRMWARE_WIFI_ONLY
  if (!network_connected)
  {
    Serial.println("[OTA] Skipped - WiFi not connected");
    return;
  }

  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(config.hardwareId.c_str());
  ArduinoOTA.setPassword(default_password);
  ArduinoOTA.begin();
  Serial.println("[OTA] Service started (WiFi mode)");
#else
  Serial.println("[OTA] Not available in Ethernet firmware");
#endif
}

// =================================================================
// --- NETWORK SETUP (WiFi/Ethernet/Hybrid + AP Fallback) ---
// =================================================================
void setupNetwork()
{
  isAPMode = false;
  network_connected = false;
  network_ready = false;

  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.printf("║ NETWORK INITIALIZATION - %s Mode%*s║\n",
                NETWORK_MODE_NAME,
                24 - strlen(NETWORK_MODE_NAME), "");
  Serial.println("╠════════════════════════════════════════════════════╣");

#ifdef FIRMWARE_WIFI_ONLY
  Serial.printf("║ Timeout: %d seconds%*s║\n",
                CONNECT_TIMEOUT / 1000, 31, "");
  Serial.println("╚════════════════════════════════════════════════════╝");

  if (config.wifiSSID.length() == 0)
  {
    Serial.println("[WiFi] ✗ SSID not configured!");
    Serial.println("[WiFi] Starting AP Mode...");
    startAPMode();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  Serial.printf("[WiFi] Connecting to: %s\n", config.wifiSSID.c_str());
  WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());

  unsigned long startTime = millis();
  int dots = 0;

  while (WiFi.status() != WL_CONNECTED &&
         (millis() - startTime < connectTimeout))
  {
    delay(500);
    Serial.print(".");
    dots++;
    if (dots % 40 == 0)
      Serial.println();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("[WiFi] Connected!");
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
    network_connected = true;
    network_ready = true;
    setupCommunication();
  }
  else
  {
    Serial.println("[WiFi] Connection FAILED!");
    Serial.println("[WiFi] Starting AP Mode...");
    startAPMode();
  }

#else // FIRMWARE_ETHERNET_ONLY
  Serial.printf("║ Timeout: %d seconds%*s║\n",
                CONNECT_TIMEOUT / 1000, 31, "");
  Serial.println("╚════════════════════════════════════════════════════╝");

  WiFi.mode(WIFI_OFF);

  if (initEthernet())
  {
    unsigned long startTime = millis();
    Serial.print("[Ethernet] Waiting for link");

    while (Ethernet.linkStatus() == LinkOFF &&
           (millis() - startTime < connectTimeout))
    {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (Ethernet.linkStatus() == LinkON)
    {
      Serial.println("[Ethernet] Connected!");
      Serial.printf("[Ethernet] IP: %s\n", Ethernet.localIP().toString().c_str());
      Serial.printf("[Ethernet] Gateway: %s\n", Ethernet.gatewayIP().toString().c_str());
      Serial.printf("[Ethernet] Subnet: %s\n", Ethernet.subnetMask().toString().c_str());
      network_connected = true;
      network_ready = true;
      setupCommunication();
    }
    else
    {
      Serial.println("[Ethernet] No cable detected!");
      Serial.println("[Ethernet] Starting AP Mode...");
      startAPMode();
    }
  }
  else
  {
    Serial.println("[Ethernet] Initialization FAILED!");
    Serial.println("[Ethernet] Starting AP Mode...");
    startAPMode();
  }
#endif
}

void setupCommunication()
{
  if (isAPMode || readyToSend)
    return;

  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.printf("║ COMMUNICATION SETUP - %s Mode%*s║\n",
                config.commMode.c_str(),
                21 - config.commMode.length(), "");
  Serial.println("╚════════════════════════════════════════════════════╝");

  if (config.commMode == "ws")
  {
#ifndef FIRMWARE_WIFI_ONLY
    wsConnect();
    readyToSend = true;
#endif
  }
  else if (config.commMode == "mqtt")
  {
    mqttClient.setClient(ACTIVE_CLIENT); // ← Macro yang resolve ke netClient
    mqttClient.setServer(config.serverIP.c_str(), config.monitoringPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(15);

    Serial.printf("[MQTT] Initialized via %s\n", NETWORK_MODE_NAME); // ← Macro
  }
  else if (config.commMode == "httppost")
  {
    String statusData = buildStatusJSON();
    String resp;

    if (sendViaHTTP(statusData, resp))
    {
      Serial.println("[HTTP] Initial status sent");
      if (resp.length())
        Serial.println("Response: " + resp);
    }
    else
    {
      Serial.println("[HTTP] Failed to send initial status");
    }

    readyToSend = true;
  }
}

void startAPMode()
{
  isAPMode = true;
  network_connected = false;
  network_ready = false;

  String finalApSsid = ap_ssid + config.hardwareId;

  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║              AP MODE ACTIVATED                     ║");
  Serial.println("╠════════════════════════════════════════════════════╣");
  Serial.printf("║ SSID:     %-40s ║\n", finalApSsid.c_str());
  Serial.printf("║ Password: %-40s ║\n", ap_password);
  Serial.printf("║ Timeout:  %d seconds%*s║\n",
                AP_TIMEOUT / 1000, 29, "");
  Serial.println("╠════════════════════════════════════════════════════╣");
  Serial.println("║ 1. Connect to WiFi AP above                        ║");
  Serial.println("║ 2. Open browser: http://192.168.4.1                ║");
  Serial.println("║ 3. Configure network settings                      ║");
  Serial.println("║ 4. Device will restart automatically               ║");
  Serial.println("╚════════════════════════════════════════════════════╝\n");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(finalApSsid.c_str(), ap_password);

  apStartTime = millis();

  Serial.printf("[AP] IP Address: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("[AP] Waiting for configuration...\n");

  if (ws_connected)
    wsDisconnect();
  if (mqttClient.connected())
    mqttClient.disconnect();
  readyToSend = false;
}

void tryReconnect()
{
#ifdef FIRMWARE_WIFI_ONLY
  if (config.wifiSSID.length() > 0)
  {
    Serial.println("[Reconnect] Attempting WiFi...");
    WiFi.disconnect(true);
    delay(300);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
  }
  else
  {
    startAPMode();
    return;
  }
#else // FIRMWARE_ETHERNET_ONLY
  Serial.println("[Reconnect] Re-initializing Ethernet...");
  if (initEthernet() && Ethernet.linkStatus() == LinkON)
  {
    network_connected = true;
  }
#endif

  isReconnecting = true;
  reconnectAttemptStartTime = millis();
}

// =================================================================
// --- SETUP ---
// =================================================================
void setupIO()
{
  if (pcf8574.begin())
    Serial.println("PCF8574: Connected.");
  else
    Serial.println("PCF8574: Failed!");
  pcf8574.write8(0xFF);
  for (int pin : INPUT_GPIO_PINS)
    pinMode(pin, INPUT_PULLUP);
  for (int pin : OUTPUT_PINS)
  {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  loadIOStateFromEEPROM();
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Serial.println("\n\n==> Starting IoT Node " + program_version + " <==");
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("[FATAL] EEPROM init fail");
    delay(1000);
    ESP.restart();
  }
  if (!LittleFS.begin())
  {
    Serial.println("[FATAL] LittleFS mount failed.");
    while (1)
    {
      delay(1000);
    }
  }

  loadConfig();

  Serial.println("box_id: " + config.hardwareId);
  Serial.println("serverIP: " + config.serverIP);
  Serial.println("path: " + config.path);
  Serial.println("monitoringPort: " + String(config.monitoringPort));
  Serial.println("commMode: " + config.commMode);
  Serial.println("wifiSSID: " + config.wifiSSID);

  setupIO();
  setupNetwork();

  if (!isAPMode)
  {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

#ifdef FIRMWARE_WIFI_ONLY
    if (network_connected)
    {
      setupOTA();
    }
#endif
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/control", HTTP_GET, handleControlPage);
  server.on("/status", HTTP_GET, handleStatusPage);
  server.on("/save-config", HTTP_POST, handleSaveConfig);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/get-outputs", HTTP_GET, handleGetOutputs);
  server.on("/download-config", HTTP_GET, handleDownloadConfig);
  server.on("/logout", HTTP_GET, handleLogout);

  // 404 handler
  server.onNotFound([]()
                    {
  Serial.println("[Web] 404 - Page not found: " + server.uri());
  serveErrorPage("Page not found: " + server.uri()); });

  server.begin();
}

// =================================================================
// --- LOOP ---
// =================================================================
void loop()
{
  server.handleClient();

  if (isAPMode)
  {
    unsigned long now = millis();
    unsigned long elapsed = now - apStartTime;

    if (apStartTime > 0 && now - lastCountdownPrint >= 1000)
    {
      lastCountdownPrint = now;
      unsigned long remaining = (apTimeout > elapsed) ? (apTimeout - elapsed) / 1000 : 0;
      Serial.printf("[AP MODE] Active. Restart in %lu seconds...\n", remaining);
    }

    if (elapsed >= apTimeout)
    {
      Serial.println("[AP MODE] Timeout! Restarting...");
      delay(200);
      ESP.restart();
    }
    return;
  }

#ifdef FIRMWARE_WIFI_ONLY
  bool isConnected = (WiFi.status() == WL_CONNECTED);
#else
  Ethernet.maintain();
  bool isConnected = (Ethernet.linkStatus() == LinkON);
#endif

  if (isConnected)
  {
    if (!network_connected)
    {
      network_connected = true;
      network_ready = true;
      Serial.printf("[%s] Reconnected!\n", NETWORK_MODE_NAME);
    }

    if (isReconnecting)
    {
      isReconnecting = false;
      reconnectAttemptCount = 0;
      reconnectAttemptStartTime = 0;
    }

    if (!readyToSend)
    {
      setupCommunication();
    }

#ifdef FIRMWARE_WIFI_ONLY
    ArduinoOTA.handle();
#endif

    // Communication handlers
    if (config.commMode == "ws")
    {
      wsPump();
    }
    else if (config.commMode == "mqtt")
    {
      if (!mqttClient.connected())
        reconnectMQTT();
      mqttClient.loop();
    }
    else if (config.commMode == "httppost")
    {
      pollHTTPCommands();
    }

    // I/O monitoring
    unsigned long currentTime = millis();
    if (currentTime - lastMonitoringTime >= monitoringInterval)
    {
      lastMonitoringTime = currentTime;
      bool hasChanged = checkInputChanges();
      bool forceSend = (currentTime - lastForceSendTime >= forceSendInterval);

      if (hasChanged || forceSend)
      {
        sendMonitoringData();
        publishTelemetry();
        lastForceSendTime = currentTime;
      }
    }
  }
  else
  {
    // Disconnected handling
    if (network_connected)
    {
      network_connected = false;
      network_ready = false;
      readyToSend = false;
      Serial.printf("[%s] Disconnected!\n", NETWORK_MODE_NAME);

      if (ws_connected)
        wsDisconnect();
      if (mqttClient.connected())
        mqttClient.disconnect();

      isReconnecting = false;
      reconnectAttemptCount = 0;
      reconnectAttemptStartTime = 0;
    }

    if (!isReconnecting)
    {
      if (reconnectAttemptCount >= maxReconnectAttempts)
      {
        Serial.println("\n[Reconnect] Max attempts reached. Starting AP Mode...");
        startAPMode();
        return;
      }

      if (reconnectAttemptCount > 0)
        delay(2000);
      tryReconnect();
      reconnectAttemptCount++;
    }
    else
    {
      unsigned long attemptElapsed = millis() - reconnectAttemptStartTime;
      if (attemptElapsed >= singleAttemptTimeout)
      {
        Serial.println("\n[Reconnect] Attempt timeout");
        isReconnecting = false;
      }
      else
      {
        static unsigned long lastDot = 0;
        if (millis() - lastDot >= 500)
        {
          Serial.print(".");
          lastDot = millis();
        }
        delay(100);
      }
    }
  }
}