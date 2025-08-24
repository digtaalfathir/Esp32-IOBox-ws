// ===================== Arduino WS Pipe =====================
#include <SPI.h>
#include <Ethernet.h>
#include <WebSocketsClient.h>

// ---------- Konfigurasi Jaringan ----------
byte mac[] = { 0xDE,0xAD,0xBE,0xEF,0xFE,0xED };

// DHCP → fallback ke IP statik jika gagal (opsional)
IPAddress fallbackIP(192,168,1,177);
IPAddress fallbackDNS(192,168,1,1);
IPAddress fallbackGW (192,168,1,1);
IPAddress fallbackSN (255,255,255,0);

// ---------- Konfigurasi WebSocket Server ----------
const char* WS_HOST = "192.168.1.100";  // GANTI
const uint16_t WS_PORT = 8080;          // GANTI
const char* WS_PATH = "/";              // GANTI, mis. "/ws"

// ---------- SPI / Ethernet ----------
const uint8_t ETH_CS = 10;  // UNO: D10 (MEGA: 53). Sesuaikan shield Anda.
WebSocketsClient webSocket;

bool linkUpPrev = false;
bool wsConnected = false;

// ---------- Serial Buffer ----------
static char rxBuf[256];
uint8_t rxLen = 0;

// ---------- Helper: kirim status link ke ESP32 ----------
void sendEthernetStatus(bool up) {
  if (up)  Serial.println("{\"ethernet\":1}");
  else     Serial.println("{\"ethernet\":0}");
}

// ---------- WS Events ----------
void wsEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      wsConnected = false;
      // Info ke ESP32 (opsional): {"ws":0}
      // Serial.println("{\"ws\":0}");
      break;

    case WStype_CONNECTED:
      wsConnected = true;
      // Info ke ESP32 (opsional): {"ws":1}
      // Serial.println("{\"ws\":1}");
      break;

    case WStype_TEXT:
      // Forward pesan server ke ESP32 (sudah berupa teks/JSON)
      // Tambah newline agar ESP32 gampang split
      Serial.write(payload, length);
      Serial.write('\n');
      break;

    default:
      break;
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);   // NOTE: UNO pins 0/1 ke ESP32, tidak bisa dipakai Serial Monitor bersamaan
  // while (!Serial) {}   // Untuk board dengan USB native; di UNO/Nano tidak wajib

  Ethernet.init(ETH_CS);
  if (Ethernet.begin(mac) == 0) {
    // DHCP gagal → fallback statik
    Ethernet.begin(mac, fallbackIP, fallbackDNS, fallbackGW, fallbackSN);
  }
  delay(1000);

  // Laporkan status awal
  bool up = (Ethernet.linkStatus() == LinkON);
  linkUpPrev = up;
  sendEthernetStatus(up);

  // Siapkan WS client (tidak apa-apa mulai, nanti akan auto retry saat link up)
  webSocket.begin(WS_HOST, WS_PORT, WS_PATH);
  webSocket.onEvent(wsEvent);
  webSocket.setReconnectInterval(3000); // ms
}

// ---------- loop non-blocking ----------
void loop() {
  // 1) Pantau link Ethernet
  bool linkUpNow = (Ethernet.linkStatus() == LinkON);
  if (linkUpNow != linkUpPrev) {
    linkUpPrev = linkUpNow;
    sendEthernetStatus(linkUpNow);

    if (!linkUpNow) {
      // Putus → pastikan WS akan reconnect saat link up lagi
      // (WebSocketsClient akan auto reconnect, tapi boleh juga paksa.)
    }
  }

  // 2) Jalankan WS client
  webSocket.loop();

  // 3) Terima JSON dari ESP32 via Serial → forward ke WS (jika connected)
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (wsConnected) {
        webSocket.sendTXT(rxBuf, rxLen);
      }
      rxLen = 0;
      rxBuf[0] = 0;
    } else {
      if (rxLen < sizeof(rxBuf) - 1) {
        rxBuf[rxLen++] = c;
        rxBuf[rxLen] = 0;
      } else {
        // overflow, reset buffer
        rxLen = 0;
        rxBuf[0] = 0;
      }
    }
  }
}
