// server.js
const WebSocket = require("ws");
const PORT = 8080;

const wss = new WebSocket.Server({ port: PORT });

console.log(`WebSocket server running on ws://localhost:${PORT}`);

// Simpan daftar client
let clients = [];

// ID ESP target
const TARGET_BOX_ID = "001";

wss.on("connection", function connection(ws, req) {
  const clientIP = req.socket.remoteAddress;
  console.log(`New client connected from ${clientIP}`);
  clients.push(ws);

  ws.send(JSON.stringify({ message: "Welcome!" }));

  ws.on("message", function incoming(message) {
    console.log(`Received from ESP32: ${message}`);
  });

  ws.on("close", () => {
    console.log(`Client from ${clientIP} disconnected`);
    clients = clients.filter((c) => c !== ws);
  });

  ws.on("error", (error) => {
    console.error(`Error from ${clientIP}:`, error.message);
  });
});

// Fungsi kirim command ke semua client
function sendCommand(boxId, payload) {
  const jsonString = JSON.stringify({ box_id: boxId, ...payload });
  clients.forEach((ws) => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(jsonString);
      console.log(`Sent to ESP32: ${jsonString}`);
    }
  });
}

// === Contoh 1: kirim restart tiap 15 detik ===
// setInterval(() => {
//   sendCommand(TARGET_BOX_ID, { cmd: "restartesp" });
// }, 15000);

// === Contoh 2: kirim update config (ubah port, ip, wifi) ===
// setTimeout(() => {
//   sendCommand(TARGET_BOX_ID, {
//     port: 1883,
//   });
// }, 5000); // kirim 5 detik setelah konek
