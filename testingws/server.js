// server.js

const WebSocket = require("ws");
const PORT = 8080;

// Buat WebSocket Server
const wss = new WebSocket.Server({ port: PORT });

console.log(`✅ WebSocket server running on ws://localhost:${PORT}`);

// Event saat client (ESP32) terhubung
wss.on("connection", function connection(ws, req) {
  const clientIP = req.socket.remoteAddress;
  console.log(`🔗 New client connected from ${clientIP}`);

  // Kirim pesan ke client
  ws.send(JSON.stringify({ message: "Welcome ESP32 Client!" }));

  // Terima pesan dari ESP32
  ws.on("message", function incoming(message) {
    console.log(`📩 Received from ESP32: ${message}`);

    // (Opsional) Balas kembali
    // ws.send(`Server received: ${message}`);
  });

  // Event saat client disconnect
  ws.on("close", () => {
    console.log(`❌ Client from ${clientIP} disconnected`);
  });

  // Tangani error
  ws.on("error", (error) => {
    console.error(`⚠️ Error from ${clientIP}:`, error.message);
  });
});
