/**
 * USBPcapGUI - Web GUI Server
 *
 * Express HTTP server + WebSocket for real-time event streaming.
 * Bridges the C++ bhplus-core service to the browser frontend.
 *
 * Usage:
 *   node server.js           # Start and open browser
 *   node server.js --dev     # Dev mode (no auto-open)
 *   node server.js --port N  # Custom port
 */

// Logger MUST be initialised before any other require so all console.* output
// (including from express/ws/etc.) is captured in the log file.
require('./logger').initLogger('server');

const express = require('express');
const http = require('http');
const path = require('path');
const fs = require('fs');
const { spawn, execSync } = require('child_process');
const { WebSocketServer } = require('ws');
const CoreBridge = require('./core-bridge');

// ---------------------------------------------------------------------------
// bhplus-core.exe auto-launcher
// ---------------------------------------------------------------------------

/** Candidate locations for bhplus-core.exe relative to this script's directory */
const CORE_EXE_CANDIDATES = [
    path.join(__dirname, '..', 'build_fresh', 'bin', 'Release', 'bhplus-core.exe'),
    path.join(__dirname, '..', 'build', 'bin', 'Release', 'bhplus-core.exe'),
    path.join(__dirname, '..', 'dist', 'USBPcapGUI', 'bhplus-core.exe'),
    path.join(path.dirname(process.execPath), 'bhplus-core.exe'),  // same dir as node.exe (packaged)
    path.join(__dirname, '..', 'bhplus-core.exe'),                 // parent of gui\ subfolder
    path.join(__dirname, 'bhplus-core.exe'),
];

let coreChildProcess = null;

function findCoreExe() {
    for (const p of CORE_EXE_CANDIDATES) {
        if (fs.existsSync(p)) return p;
    }
    return null;
}

function spawnCoreProcess() {
    if (coreChildProcess) return;   // already running
    const exePath = findCoreExe();
    if (!exePath) {
        console.warn('[Core] bhplus-core.exe not found. Searched:');
        CORE_EXE_CANDIDATES.forEach(p => console.warn('  -', p));
        console.warn('[Core] Continuing in DEMO mode. Build the C++ project to enable live capture.');
        return;
    }

    console.log(`[Core] Launching: ${exePath}`);
    coreChildProcess = spawn(exePath, [], {
        detached: false,
        stdio: ['ignore', 'pipe', 'pipe'],
        windowsHide: true,
    });

    coreChildProcess.stdout.on('data', d => process.stdout.write(`[bhplus-core] ${d}`.trimEnd() + '\n'));
    coreChildProcess.stderr.on('data', d => process.stderr.write(`[bhplus-core] ${d}`.trimEnd() + '\n'));

    coreChildProcess.on('exit', (code, signal) => {
        console.log(`[Core] bhplus-core.exe exited (code=${code}, signal=${signal})`);
        coreChildProcess = null;
    });

    coreChildProcess.on('error', err => {
        console.error(`[Core] Failed to spawn bhplus-core.exe: ${err.message}`);
        coreChildProcess = null;
    });
}

// --- Configuration ---
const DEFAULT_PORT = 17580;
const args = process.argv.slice(2);
const isDev = args.includes('--dev');
const noSpawnCore = args.includes('--no-spawn-core'); // set by launcher (it already started core)
const portIdx = args.indexOf('--port');
const PORT = portIdx >= 0 ? parseInt(args[portIdx + 1]) : DEFAULT_PORT;

// --- Express App ---
const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

const server = http.createServer(app);

// --- Core Bridge ---
const core = new CoreBridge();

// Capture state
let captureEvents = [];      // In-memory event buffer
const MAX_EVENTS = 100000;   // Max events kept in memory
let capturing = false;

// --- WebSocket Server ---
const wss = new WebSocketServer({ server, path: '/ws' });
const wsClients = new Set();

wss.on('connection', (ws) => {
    wsClients.add(ws);
    console.log(`[WS] Client connected (${wsClients.size} total)`);

    void sendInitialState(ws);

    ws.on('message', async (raw) => {
        try {
            const msg = JSON.parse(raw);
            await handleWsMessage(ws, msg);
        } catch (e) {
            ws.send(JSON.stringify({ type: 'error', data: { message: e.message } }));
        }
    });

    ws.on('close', () => {
        wsClients.delete(ws);
        console.log(`[WS] Client disconnected (${wsClients.size} total)`);
    });
});

/**
 * Handle incoming WebSocket commands from browser
 */
async function handleWsMessage(ws, msg) {
    switch (msg.type) {
        case 'capture.start': {
            capturing = true;
            const result = await core.request('capture.start', msg.data || {});
            ws.send(JSON.stringify({ type: 'capture.started', data: result }));
            broadcast({ type: 'status', data: { capturing: true } });
            break;
        }
        case 'capture.stop': {
            capturing = false;
            const result = await core.request('capture.stop');
            ws.send(JSON.stringify({ type: 'capture.stopped', data: result }));
            broadcast({ type: 'status', data: { capturing: false } });
            break;
        }
        case 'capture.clear': {
            captureEvents = [];
            broadcast({ type: 'capture.cleared' });
            break;
        }
        case 'devices.enumerate': {
            const devices = await core.request('devices.list');
            ws.send(JSON.stringify({ type: 'devices.list', data: devices }));
            break;
        }
        case 'stats.get': {
            const stats = await core.request('stats.get');
            ws.send(JSON.stringify({ type: 'stats', data: stats }));
            break;
        }
        case 'usbpcap.status': {
            const status = await core.request('usbpcap.status');
            ws.send(JSON.stringify({ type: 'usbpcap.status', data: status }));
            break;
        }
        case 'usbpcap.install': {
            const result = await core.request('usbpcap.install');
            ws.send(JSON.stringify({ type: 'usbpcap.install', data: result }));
            const status = await core.request('usbpcap.status');
            broadcast({ type: 'usbpcap.status', data: status });
            break;
        }
        case 'usbpcap.rescan': {
            // Trigger PnP rescan (CM_Reenumerate_DevNode) and return updated status
            const result = await core.request('usbpcap.rescan');
            ws.send(JSON.stringify({ type: 'usbpcap.rescan', data: result }));
            // Also push fresh full status to all clients
            const status = await core.request('usbpcap.status');
            broadcast({ type: 'usbpcap.status', data: status });
            break;
        }
        case 'device.reset': {
            const result = await core.request('device.reset', msg.data || {});
            ws.send(JSON.stringify({ type: 'device.reset.result', data: result }));
            break;
        }
        case 'command.send': {
            const result = await core.request('command.send', msg.data || {});
            ws.send(JSON.stringify({ type: 'command.send.result', data: result }));
            break;
        }
        case 'events.query': {
            // Query events with filter
            const { offset = 0, limit = 1000, filter, filterText } = msg.data || {};
            const filtered = applyRequestedFilter(captureEvents, filter, filterText);
            const slice = filtered.slice(offset, offset + limit);
            ws.send(JSON.stringify({
                type: 'events.result',
                data: { events: slice, total: filtered.length, offset }
            }));
            break;
        }
        case 'export': {
            const { format = 'json', filter, filterText, filtered = false } = msg.data || {};
            const data = filtered ? applyRequestedFilter(captureEvents, filter, filterText) : captureEvents;
            const exported = exportData(data, format);
            ws.send(JSON.stringify({ type: 'export.result', data: exported }));
            break;
        }
        case 'demo.enable': {
            core.enableDemoMode();
            broadcast({ type: 'status', data: { demoMode: true, connected: false } });
            ws.send(JSON.stringify({ type: 'demo.enabled' }));
            break;
        }
        case 'demo.disable': {
            core.disableDemoMode();
            broadcast({ type: 'status', data: { demoMode: false, connected: core.connected } });
            ws.send(JSON.stringify({ type: 'demo.disabled' }));
            break;
        }
        default:
            ws.send(JSON.stringify({ type: 'error', data: { message: `Unknown command: ${msg.type}` } }));
    }
}

async function sendInitialState(ws) {
    let usbpcap = { installed: false, installerFound: false, hubs: [] };
    if (core.connected) {
        try {
            usbpcap = await core.request('usbpcap.status');
        } catch (e) {
            console.warn('[WS] Failed to query USBPcap status:', e.message);
        }
    }
    // else: core not yet connected — browser will receive a status update
    //        via the 'connected' event once the pipe is established.

    ws.send(JSON.stringify({
        type: 'init',
        data: {
            capturing,
            demoMode: core.demoMode,
            coreConnected: core.connected,
            eventCount: captureEvents.length,
            usbpcap,
            // Send last 1000 events as initial batch
            events: captureEvents.slice(-1000)
        }
    }));
}

/**
 * Apply filter criteria to events
 */
function applyFilter(events, filter) {
    return events.filter(e => {
        if (filter.deviceId && e.deviceId !== filter.deviceId) return false;
        if (filter.protocol && e.protocol !== filter.protocol) return false;
        if (filter.direction && e.direction !== filter.direction) return false;
        if (filter.status && e.status !== filter.status) return false;
        if (filter.command) {
            const re = new RegExp(filter.command, 'i');
            if (!re.test(e.command)) return false;
        }
        if (filter.dataPattern) {
            const re = new RegExp(filter.dataPattern, 'i');
            if (!re.test(e.data || '')) return false;
        }
        if (filter.minLength != null && e.dataLength < filter.minLength) return false;
        if (filter.maxLength != null && e.dataLength > filter.maxLength) return false;
        return true;
    });
}

function parseFilterText(filterText) {
    if (!filterText || !filterText.trim()) return [];

    const conditions = [];
    const tokens = filterText.match(/(?:[^\s"]+|"[^"]*")+/g) || [];

    for (const token of tokens) {
        const colonIdx = token.indexOf(':');
        if (colonIdx > 0) {
            const key = token.substring(0, colonIdx).toLowerCase();
            let value = token.substring(colonIdx + 1);
            let negate = false;
            let op = '=';

            if (value.startsWith('!')) {
                negate = true;
                value = value.substring(1);
            }
            if (value.startsWith('>')) {
                op = '>';
                value = value.substring(1);
            } else if (value.startsWith('<')) {
                op = '<';
                value = value.substring(1);
            }

            conditions.push({
                key,
                value: value.replace(/^"|"$/g, ''),
                negate,
                op
            });
        } else {
            conditions.push({
                key: '_text',
                value: token.replace(/^"|"$/g, ''),
                negate: false,
                op: '='
            });
        }
    }

    return conditions;
}

function applyTextFilter(events, filterText) {
    const conditions = parseFilterText(filterText);
    if (!conditions.length) return events;

    return events.filter(event => conditions.every(cond => {
        let match = true;

        switch (cond.key) {
            case 'protocol':
            case 'proto':
                match = matchString(event.protocol, cond.value);
                break;
            case 'device':
                match = matchString(event.device, cond.value);
                break;
            case 'command':
            case 'cmd':
                match = matchString(event.command, cond.value);
                break;
            case 'status':
                match = matchString(event.status, cond.value);
                break;
            case 'phase':
                match = matchString(event.phase, cond.value);
                break;
            case 'dir':
            case 'direction':
                match = matchDirection(event.direction, cond.value);
                break;
            case 'len':
            case 'length':
                match = matchNumber(event.dataLength, cond.value, cond.op);
                break;
            case 'deviceid':
                match = matchNumber(event.deviceId, cond.value, cond.op);
                break;
            case 'seq':
                match = matchNumber(event.seq, cond.value, cond.op);
                break;
            case 'endpoint':
            case 'ep':
                match = matchNumber(event.endpoint, cond.value, cond.op);
                break;
            case 'data':
                match = matchString(event.data, cond.value);
                break;
            case '_text':
                match = [event.protocol, event.device, event.command, event.status, event.phase, event.data]
                    .some(value => matchString(value, cond.value));
                break;
            default:
                match = true;
                break;
        }

        return cond.negate ? !match : match;
    }));
}

function applyRequestedFilter(events, filter, filterText) {
    let filtered = events;
    if (filter) {
        filtered = applyFilter(filtered, filter);
    }
    if (filterText) {
        filtered = applyTextFilter(filtered, filterText);
    }
    return filtered;
}

function matchString(value, pattern) {
    if (!value && value !== 0) return false;
    const text = String(value);
    try {
        return new RegExp(pattern, 'i').test(text);
    } catch {
        return text.toLowerCase().includes(String(pattern).toLowerCase());
    }
}

function matchDirection(value, pattern) {
    const normalized = String(pattern).toLowerCase();
    if (normalized === 'in' || normalized === '<<<') return value === '<<<';
    if (normalized === 'out' || normalized === '>>>') return value === '>>>';
    return matchString(value, pattern);
}

function matchNumber(value, raw, op) {
    const actual = Number(value);
    const expected = Number(raw);
    if (!Number.isFinite(actual) || !Number.isFinite(expected)) return false;
    if (op === '>') return actual > expected;
    if (op === '<') return actual < expected;
    return actual === expected;
}

function formatTimestamp(timestamp) {
    if (typeof timestamp !== 'number' || !Number.isFinite(timestamp)) {
        return String(timestamp || '');
    }
    const isMicroseconds = timestamp > 10_000_000_000_000;
    const millis = isMicroseconds ? Math.floor(timestamp / 1000) : timestamp;
    const date = new Date(millis);
    if (Number.isNaN(date.getTime())) {
        return String(timestamp);
    }
    const base = date.toISOString();
    if (!isMicroseconds) return base;
    return `${base.slice(0, -1)}${String(timestamp % 1000).padStart(3, '0')}Z`;
}

function hexToBuffer(hex) {
    if (!hex) return Buffer.alloc(0);
    const normalized = String(hex).replace(/[^0-9a-f]/gi, '');
    if (!normalized) return Buffer.alloc(0);
    return Buffer.from(normalized.length % 2 === 0 ? normalized : `0${normalized}`, 'hex');
}

function mapTransferType(value) {
    const key = String(value || '').toLowerCase();
    switch (key) {
        case 'isochronous': return 0;
        case 'interrupt': return 1;
        case 'control': return 2;
        case 'bulk': return 3;
        default: return 3;
    }
}

function toBigInt(value) {
    if (typeof value === 'bigint') return value;
    if (typeof value === 'number' && Number.isFinite(value)) return BigInt(Math.trunc(value));
    if (typeof value === 'string' && value.trim()) {
        try {
            return BigInt(value.trim());
        } catch {
            return 0n;
        }
    }
    return 0n;
}

function buildUsbPcapPayload(event) {
    const payload = hexToBuffer(event.data);
    const transfer = mapTransferType(event.transferType);
    const details = event.details || {};
    const headerLen = transfer === 2 ? 28 : (transfer === 0 ? 39 : 27);
    const buffer = Buffer.alloc(headerLen + payload.length);

    buffer.writeUInt16LE(headerLen, 0);
    buffer.writeBigUInt64LE(toBigInt(event.irpId), 2);
    buffer.writeUInt32LE((event.statusCode || 0) >>> 0, 10);
    buffer.writeUInt16LE((event.urbFunction || 0) & 0xffff, 14);
    buffer.writeUInt8(event.direction === '<<<' ? 1 : 0, 16);
    buffer.writeUInt16LE((event.bus || 0) & 0xffff, 17);
    buffer.writeUInt16LE((event.deviceAddress || 0) & 0xffff, 19);
    buffer.writeUInt8((event.endpoint || 0) & 0xff, 21);
    buffer.writeUInt8(transfer & 0xff, 22);
    buffer.writeUInt32LE(payload.length >>> 0, 23);

    if (transfer === 2) {
        buffer.writeUInt8((details.stage || 0) & 0xff, 27);
    } else if (transfer === 0) {
        buffer.writeUInt32LE((details.startFrame || 0) >>> 0, 27);
        buffer.writeUInt32LE((details.numPackets || 0) >>> 0, 31);
        buffer.writeUInt32LE((details.errorCount || 0) >>> 0, 35);
    }

    payload.copy(buffer, headerLen);
    return buffer;
}

function exportPcap(events) {
    const globalHeader = Buffer.alloc(24);
    globalHeader.writeUInt32LE(0xa1b2c3d4, 0);
    globalHeader.writeUInt16LE(2, 4);
    globalHeader.writeUInt16LE(4, 6);
    globalHeader.writeInt32LE(0, 8);
    globalHeader.writeUInt32LE(0, 12);
    globalHeader.writeUInt32LE(65535, 16);
    globalHeader.writeUInt32LE(249, 20);

    const records = [];
    for (const event of events) {
        const usbPayload = buildUsbPcapPayload(event);
        const timestamp = Number(event.timestamp || 0);
        const tsSec = Math.floor(timestamp / 1_000_000);
        const tsUsec = timestamp % 1_000_000;
        const pcapHeader = Buffer.alloc(16);
        pcapHeader.writeUInt32LE(tsSec >>> 0, 0);
        pcapHeader.writeUInt32LE(tsUsec >>> 0, 4);
        pcapHeader.writeUInt32LE(usbPayload.length >>> 0, 8);
        pcapHeader.writeUInt32LE(usbPayload.length >>> 0, 12);
        records.push(pcapHeader, usbPayload);
    }

    const content = Buffer.concat([globalHeader, ...records]);
    return {
        content: content.toString('base64'),
        encoding: 'base64',
        filename: 'capture.pcap',
        mime: 'application/vnd.tcpdump.pcap'
    };
}

/**
 * Export data in various formats
 */
function exportData(events, format) {
    switch (format) {
        case 'csv': {
            const header = 'Seq,Timestamp,Direction,Device,Protocol,Phase,Command,Status,Length\n';
            const rows = events.map(e =>
                `${e.seq},${formatTimestamp(e.timestamp)},${e.direction},"${e.device}",${e.protocol},${e.phase || ''},"${e.command}",${e.status},${e.dataLength}`
            ).join('\n');
            return { content: header + rows, filename: 'capture.csv', mime: 'text/csv' };
        }
        case 'txt': {
            const lines = events.map(e => {
                const parts = [];

                // ── Summary ─────────────────────────────────────────────────
                parts.push(`No.${String(e.seq).padStart(6)}`);
                parts.push(formatTimestamp(e.timestamp));
                parts.push(e.direction);
                parts.push((e.protocol || '').padEnd(6));
                parts.push((e.command || '').padEnd(30));
                parts.push((e.status || '').padEnd(10));
                parts.push(`${e.dataLength}B`);

                // ── URB fields ───────────────────────────────────────────────
                const dir = (e.direction || '') === '<<<' ? 'dev→host' : 'host→dev';
                parts.push(dir);
                if (e.device)          parts.push(`dev:${e.device}`);
                if (e.phase)           parts.push(`phase:${e.phase}`);
                if (e.duration != null) parts.push(`dur:${e.duration}µs`);

                // ── Protocol decoded fields ──────────────────────────────────
                if (e.summary)         parts.push(`summary:${e.summary}`);
                for (const f of (e.decodedFields || [])) {
                    parts.push(`${f.name}:${f.value}`);
                }
                if (e.details && typeof e.details === 'object') {
                    for (const [k, v] of Object.entries(e.details)) {
                        parts.push(`${k}:${v}`);
                    }
                }

                // ── Hex (all bytes on same line) ─────────────────────────────
                if (e.data) {
                    const hex = String(e.data).replace(/[^0-9a-fA-F]/g, '');
                    if (hex.length) {
                        const bytes = (hex.match(/.{1,2}/g) || []).map(b => b.toUpperCase()).join(' ');
                        parts.push(`hex:${bytes}`);
                    }
                }

                return parts.join(' | ');
            });
            return { content: lines.join('\n'), filename: 'capture.txt', mime: 'text/plain' };
        }
        case 'pcap':
            return exportPcap(events);
        case 'json':
        default:
            return { content: JSON.stringify(events, null, 2), filename: 'capture.json', mime: 'application/json' };
    }
}

/**
 * Broadcast a message to all connected WebSocket clients
 */
function broadcast(msg) {
    const data = JSON.stringify(msg);
    for (const ws of wsClients) {
        if (ws.readyState === 1) { // OPEN
            ws.send(data);
        }
    }
}

// --- REST API ---

app.post('/api/demo/enable', (_req, res) => {
    core.enableDemoMode();
    broadcast({ type: 'status', data: { demoMode: true, coreConnected: false } });
    res.json({ ok: true, demoMode: true });
});

app.post('/api/demo/disable', (_req, res) => {
    core.disableDemoMode();
    broadcast({ type: 'status', data: { demoMode: false, coreConnected: core.connected } });
    res.json({ ok: true, demoMode: false });
});

app.get('/api/demo/status', (_req, res) => {
    res.json({ demoMode: core.demoMode, coreConnected: core.connected });
});

app.get('/api/devices', async (req, res) => {
    try {
        const devices = await core.request('devices.list');
        res.json(devices);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

app.get('/api/stats', async (req, res) => {
    try {
        const status = await core.request('capture.status');
        // C++ core returns stats nested under capture.status
        const stats = status && status.stats ? status.stats : status;
        res.json(stats);
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

app.get('/api/events', (req, res) => {
    const offset = parseInt(req.query.offset) || 0;
    const limit = Math.min(parseInt(req.query.limit) || 1000, 10000);
    const slice = captureEvents.slice(offset, offset + limit);
    res.json({ events: slice, total: captureEvents.length, offset });
});

app.get('/api/export/:format', (req, res) => {
    const filtered = req.query.filtered === '1' || req.query.filtered === 'true';
    const filterText = typeof req.query.filterText === 'string' ? req.query.filterText : '';
    const events = filtered ? applyRequestedFilter(captureEvents, null, filterText) : captureEvents;
    const result = exportData(events, req.params.format);
    res.setHeader('Content-Disposition', `attachment; filename="${result.filename}"`);
    res.setHeader('Content-Type', result.mime);
    if (result.encoding === 'base64') {
        res.send(Buffer.from(result.content, 'base64'));
    } else {
        res.send(result.content);
    }
});

// --- Core Bridge Events ---

// --- Event batching for WebSocket broadcast ---
let wsBatchBuffer = [];
let wsBatchTimer = null;
const WS_BATCH_INTERVAL = 50; // ms

function flushWsBatch() {
    wsBatchTimer = null;
    if (wsBatchBuffer.length === 0) return;
    const batch = wsBatchBuffer;
    wsBatchBuffer = [];
    broadcast({ type: 'capture.events', data: batch });
}

function pushEvents(events) {
    for (const event of events) {
        captureEvents.push(event);
    }
    if (captureEvents.length > MAX_EVENTS) {
        captureEvents = captureEvents.slice(-MAX_EVENTS);
    }
    wsBatchBuffer.push(...events);
    if (!wsBatchTimer) {
        wsBatchTimer = setTimeout(flushWsBatch, WS_BATCH_INTERVAL);
    }
}

core.on('capture-events', (events) => {
    pushEvents(events);
});

core.on('capture-event', (event) => {
    pushEvents([event]);
});

core.on('connected', async () => {
    broadcast({ type: 'status', data: { coreConnected: true, demoMode: false } });
    // Push full USBPcap status now that the core is reachable
    try {
        const usbpcap = await core.request('usbpcap.status');
        broadcast({ type: 'usbpcap.status', data: usbpcap });
    } catch (_) {}
    // Push device list so the browser refreshes the device tree automatically
    try {
        const devices = await core.request('devices.list');
        broadcast({ type: 'devices.list', data: devices });
    } catch (_) {}
});

core.on('disconnected', () => {
    broadcast({ type: 'status', data: { coreConnected: false, demoMode: false } });
});

core.on('demo-mode', () => {
    broadcast({ type: 'status', data: { coreConnected: false, demoMode: true } });
});

core.on('demo-mode-off', () => {
    broadcast({ type: 'status', data: { coreConnected: false, demoMode: false } });
});

// --- Start Server ---

function handlePortInUse() {
    console.warn(`[Server] Port ${PORT} in use — freeing port and retrying...`);
    try {
        // Find the PID holding our port and kill it (exclude ourselves)
        execSync(
            `powershell -NoProfile -Command "` +
            `$p = (Get-NetTCPConnection -LocalPort ${PORT} -State Listen -ErrorAction SilentlyContinue | ` +
            `Select-Object -First 1 -ExpandProperty OwningProcess); ` +
            `if ($p -and $p -ne ${process.pid}) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue }; ` +
            `Stop-Process -Name bhplus-core -Force -ErrorAction SilentlyContinue"`,
            { stdio: 'ignore', timeout: 5000 }
        );
    } catch (_) { /* ignore */ }
    // Poll until port is free, then re-listen (max 5 seconds)
    let waited = 0;
    const poll = setInterval(() => {
        waited += 200;
        try {
            const testSrv = require('net').createServer();
            testSrv.once('error', () => {
                testSrv.close();
                if (waited >= 5000) {
                    clearInterval(poll);
                    console.error(`[Server] ERROR: Port ${PORT} still in use after 5s. Run: Stop-Process -Name "node","bhplus-core" -Force`);
                    process.exit(1);
                }
            });
            testSrv.once('listening', () => {
                testSrv.close(() => {
                    clearInterval(poll);
                    server.listen(PORT, onListening);
                });
            });
            testSrv.listen(PORT);
        } catch (_) { /* keep polling */ }
    }, 200);
}

let _retried = false;
let _started = false;
function onListenError(err) {
    if (err.code === 'EADDRINUSE') {
        if (!_retried) {
            _retried = true;
            handlePortInUse();
        }
        // else: duplicate event (server + wss both fire) — ignore while retry is in flight
    } else {
        throw err;
    }
}

server.on('error', onListenError);
wss.on('error', onListenError);

async function onListening() {
    if (_started) return;
    _started = true;
    console.log(`  ╔══════════════════════════════════════════╗`);
    console.log(`  ║           USBPcapGUI Web UI             ║`);
    console.log(`  ╠══════════════════════════════════════════╣`);
    console.log(`  ║  http://localhost:${PORT}                 ║`);
    console.log(`  ║  Press Ctrl+C to stop                   ║`);
    console.log(`  ╚══════════════════════════════════════════╝\n`);

    // Launch bhplus-core.exe (if not already running), then connect
    if (!noSpawnCore) {
        spawnCoreProcess();
    } else {
        console.log('[Core] --no-spawn-core set: skipping auto-launch (launcher already started bhplus-core.exe)');
    }
    // Give the process ~800 ms to bind the named pipe before connecting
    setTimeout(() => core.connect(), 800);

    // Auto-open browser (unless in dev mode)
    if (!isDev) {
        try {
            const open = (await import('open')).default;
            await open(`http://localhost:${PORT}`);
        } catch (e) {
            console.log(`[Server] Open browser manually: http://localhost:${PORT}`);
        }
    }
}

server.listen(PORT, onListening);

// Graceful shutdown
function shutdown() {
    console.log('\n[Server] Shutting down...');
    core.disconnect();
    if (coreChildProcess) {
        coreChildProcess.kill();
        coreChildProcess = null;
    }
    wss.close();
    server.close();
    process.exit(0);
}

process.on('SIGINT',   shutdown);
process.on('SIGTERM',  shutdown);
process.on('SIGBREAK', shutdown);  // Windows CTRL_BREAK_EVENT from launcher
