// server.js
const WebSocket = require("ws");
const PORT = 8080;

const wss = new WebSocket.Server({ port: PORT });

console.log(`✅ WebSocket server running on ws://localhost:${PORT}`);

// Ganti dengan ID alat ESP kamu
const TARGET_BOX_ID = "002";

// Simpan daftar client
let clients = [];

wss.on("connection", function connection(ws, req) {
  const clientIP = req.socket.remoteAddress;
  console.log(`🔗 New client connected from ${clientIP}`);
  clients.push(ws);

  ws.send(JSON.stringify({ message: "Welcome ESP32 Client!" }));

  ws.on("message", function incoming(message) {
    console.log(`📩 Received from ESP32: ${message}`);
  });

  ws.on("close", () => {
    console.log(`❌ Client from ${clientIP} disconnected`);
    clients = clients.filter((c) => c !== ws);
  });

  ws.on("error", (error) => {
    console.error(`⚠️ Error from ${clientIP}:`, error.message);
  });
});

// Fungsi kirim perintah ke semua client
function sendCommand(boxId, outputStates) {
  const payload = {
    box_id: boxId,
    ...outputStates,
  };
  const jsonString = JSON.stringify(payload);
  clients.forEach((ws) => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(jsonString);
      console.log(`📤 Sent to ESP32: ${jsonString}`);
    }
  });
}

// Contoh: kirim command setiap 5 detik
setInterval(() => {
  // Misal: O1 = ON, O2 = OFF, O3 = ON
  sendCommand(TARGET_BOX_ID, { O1: 1, O2: 0, O3: 1 });
}, 5000);
