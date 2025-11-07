const express = require('express');
const cors = require('cors');
const morgan = require('morgan');

const PORT = process.env.PORT || 8080;
const PATH = process.env.PATH_BASE || '/wsiot';

const app = express();
app.use(express.json({ limit: '256kb' }));
app.use(cors());
app.use(morgan('dev'));

// Antrian perintah: global + per-device (opsional)
const globalQueue = [];
const perDeviceQueues = new Map(); // key: box_id -> array

const lastSeen = new Map(); // simpan payload terakhir per device

function getDeviceId(req, body) {
  // WiFi firmware kirim header X-Device-ID, Ethernet tidak
  return req.get('X-Device-ID') || body?.box_id || body?.hardwareId || 'unknown';
}
function pushCommand(targetId, cmd) {
  if (targetId) {
    if (!perDeviceQueues.has(targetId)) perDeviceQueues.set(targetId, []);
    perDeviceQueues.get(targetId).push(cmd);
  } else {
    globalQueue.push(cmd);
  }
}
function popCommand(targetId) {
  if (targetId && perDeviceQueues.has(targetId)) {
    const q = perDeviceQueues.get(targetId);
    if (q.length) return q.shift();
  }
  if (globalQueue.length) return globalQueue.shift();
  return null;
}

// Health
app.get('/', (_req, res) => res.send('OK'));

// Device kirim status/telemetry ke sini
app.post(PATH, (req, res) => {
  const body = req.body || {};
  const devId = getDeviceId(req, body);
  const now = new Date().toISOString();

  console.log(`[${now}] POST ${PATH} from ${devId}`);
  console.dir(body, { depth: null });

  // Simpan last seen
  lastSeen.set(devId, { at: now, data: body });

  // OPTIONAL: balas perintah langsung via response POST
  // Contoh aktifkan command langsung (uncomment jika mau uji cepat):
  // return res.json({ box_id: devId, cmd: "get_io" });

  // Default: balas OK saja
  return res.send('OK');
});

// Device polling perintah tiap ~5 detik
app.get(`${PATH}/commands`, (req, res) => {
  // Firmware sekarang TIDAK mengirim box_id saat GET.
  // Kalau kamu patch firmware, bisa kirim ?box_id=HWID supaya per-device.
  const targetId = req.get('X-Device-ID') || req.query.box_id;
  const cmd = popCommand(targetId);
  if (cmd) return res.json(cmd);
  // Balas "null" string agar device skip parsing
  return res.type('application/json').send('null');
});

// Endpoint bantu untuk dorong perintah ke antrian
app.post('/push', (req, res) => {
  const cmd = req.body || {};
  // Jika ingin target device tertentu, kirimkan "box_id" di body (disarankan)
  const targetId = cmd.box_id || req.query.box_id; // fallback ke query
  pushCommand(targetId, cmd);
  return res.json({ queued: true, for: targetId || 'global', size: (targetId && perDeviceQueues.get(targetId)?.length) || globalQueue.length });
});

// Monitoring sederhana
app.get('/devices', (_req, res) => {
  const obj = {};
  for (const [id, info] of lastSeen.entries()) obj[id] = info;
  res.json(obj);
});

app.listen(PORT, '0.0.0.0', () => {
  console.log(`HTTP POST server listening on http://0.0.0.0:${PORT}${PATH}`);
  console.log(`Commands endpoint: GET ${PATH}/commands`);
  console.log(`Push command: POST /push {"cmd":"get_io","box_id":"YOUR_HWID"}`);
});