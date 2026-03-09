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
const express = require('express');
const http = require('http');
const path = require('path');
const { WebSocketServer } = require('ws');
const CoreBridge = require('./core-bridge');

// --- Configuration ---
const DEFAULT_PORT = 17580;
const args = process.argv.slice(2);
const isDev = args.includes('--dev');
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

    // Send current state
    ws.send(JSON.stringify({
        type: 'init',
        data: {
            capturing,
            demoMode: core.demoMode,
            eventCount: captureEvents.length,
            // Send last 1000 events as initial batch
            events: captureEvents.slice(-1000)
        }
    }));

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
        case 'events.query': {
            // Query events with filter
            const { offset = 0, limit = 1000, filter } = msg.data || {};
            let filtered = captureEvents;
            if (filter) {
                filtered = applyFilter(captureEvents, filter);
            }
            const slice = filtered.slice(offset, offset + limit);
            ws.send(JSON.stringify({
                type: 'events.result',
                data: { events: slice, total: filtered.length, offset }
            }));
            break;
        }
        case 'export': {
            const { format = 'json', filter } = msg.data || {};
            let data = captureEvents;
            if (filter) data = applyFilter(captureEvents, filter);
            const exported = exportData(data, format);
            ws.send(JSON.stringify({ type: 'export.result', data: exported }));
            break;
        }
        default:
            ws.send(JSON.stringify({ type: 'error', data: { message: `Unknown command: ${msg.type}` } }));
    }
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

/**
 * Export data in various formats
 */
function exportData(events, format) {
    switch (format) {
        case 'csv': {
            const header = 'Seq,Time,Direction,Device,Protocol,Command,Status,Length\n';
            const rows = events.map(e =>
                `${e.seq},${e.timestamp},${e.direction},"${e.device}",${e.protocol},"${e.command}",${e.status},${e.dataLength}`
            ).join('\n');
            return { content: header + rows, filename: 'capture.csv', mime: 'text/csv' };
        }
        case 'txt': {
            const lines = events.map(e =>
                `${String(e.seq).padStart(8)}  ${new Date(e.timestamp).toISOString()}  ${e.direction}  ${e.protocol.padEnd(6)}  ${e.command.padEnd(30)}  ${e.status.padEnd(8)}  ${e.dataLength}B`
            ).join('\n');
            return { content: lines, filename: 'capture.txt', mime: 'text/plain' };
        }
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

// --- REST API (supplementary) ---

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
    const result = exportData(captureEvents, req.params.format);
    res.setHeader('Content-Disposition', `attachment; filename="${result.filename}"`);
    res.setHeader('Content-Type', result.mime);
    res.send(result.content);
});

// --- Core Bridge Events ---

core.on('capture-event', (event) => {
    captureEvents.push(event);

    // Enforce max buffer size
    if (captureEvents.length > MAX_EVENTS) {
        captureEvents = captureEvents.slice(-MAX_EVENTS);
    }

    // Push to all WebSocket clients
    broadcast({ type: 'capture.event', data: event });
});

core.on('connected', () => {
    broadcast({ type: 'status', data: { coreConnected: true, demoMode: false } });
});

core.on('demo-mode', () => {
    broadcast({ type: 'status', data: { coreConnected: false, demoMode: true } });
});

// --- Start Server ---

server.listen(PORT, async () => {
    console.log(`  ╔══════════════════════════════════════════╗`);
    console.log(`  ║           USBPcapGUI Web UI             ║`);
    console.log(`  ╠══════════════════════════════════════════╣`);
    console.log(`  ║  http://localhost:${PORT}                 ║`);
    console.log(`  ║  Press Ctrl+C to stop                   ║`);
    console.log(`  ╚══════════════════════════════════════════╝\n`);

    // Connect to C++ core
    core.connect();

    // Auto-open browser (unless in dev mode)
    if (!isDev) {
        try {
            const open = (await import('open')).default;
            await open(`http://localhost:${PORT}`);
        } catch (e) {
            console.log(`[Server] Open browser manually: http://localhost:${PORT}`);
        }
    }
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\n[Server] Shutting down...');
    core.disconnect();
    wss.close();
    server.close();
    process.exit(0);
});
