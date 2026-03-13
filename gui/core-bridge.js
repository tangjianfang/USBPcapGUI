/**
 * USBPcapGUI - Core Bridge
 * Communicates with the C++ bhplus-core service via Named Pipe (JSON-RPC).
 *
 * Demo mode is NOT activated automatically — call enableDemoMode() explicitly.
 * When the core is not connected, requests return an error instead of fake data.
 */

const net = require('net');
const { EventEmitter } = require('events');
const path = require('path');

const PIPE_NAME = '\\\\.\\pipe\\bhplus-core';
const RECONNECT_INTERVAL = 5000; // ms between reconnect attempts

class CoreBridge extends EventEmitter {
    constructor() {
        super();
        this.client = null;
        this.connected = false;
        this.requestId = 0;
        this.pendingRequests = new Map();
        this.buffer = '';
        this.demoMode = false;
        this.demoInterval = null;
        this.demoSeq = 0;
        this.reconnectTimer = null;
    }

    /**
     * Connect to the C++ core service via Named Pipe
     */
    connect() {
        if (this.connected || this.demoMode) return;
        if (this.client) return; // already attempting

        this.client = net.createConnection(PIPE_NAME, () => {
            console.log('[CoreBridge] Connected to bhplus-core');
            this.connected = true;
            this.emit('connected');
        });

        this.client.on('data', (data) => {
            this.buffer += data.toString();
            this._processBuffer();
        });

        this.client.on('error', (err) => {
            if (err.code === 'ENOENT') {
                console.log('[CoreBridge] Named pipe not found — bhplus-core.exe is not running.');
            } else {
                console.log(`[CoreBridge] Connection error (${err.code}): ${err.message}`);
            }
            // 'close' fires next; let it handle cleanup and retry
        });

        this.client.on('close', () => {
            const wasConnected = this.connected;
            this.connected = false;
            this.client = null;

            if (wasConnected) {
                console.log('[CoreBridge] Disconnected from bhplus-core');
                this.emit('disconnected');
            }

            if (!this.demoMode) {
                this.reconnectTimer = setTimeout(() => {
                    this.reconnectTimer = null;
                    this.connect();
                }, RECONNECT_INTERVAL);
            }
        });
    }

    /**
     * Manually enable demo/mock mode.
     * Demo mode generates synthetic USB events without a real bhplus-core connection.
     */
    enableDemoMode() {
        if (this.demoMode) return;
        console.log('[CoreBridge] Demo mode ENABLED (manual)');
        this.demoMode = true;
        // Stop any pending live reconnect
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        this.emit('demo-mode');
    }

    /** Disable demo mode and resume trying to connect to bhplus-core. */
    disableDemoMode() {
        if (!this.demoMode) return;
        console.log('[CoreBridge] Demo mode DISABLED');
        this.demoMode = false;
        if (this.demoInterval) {
            clearInterval(this.demoInterval);
            this.demoInterval = null;
        }
        this.emit('demo-mode-off');
        this.connect();
    }

    /**
     * Send a JSON-RPC request to the core service.
     * Rejects with an error if the core is not connected (and demo mode is off).
     */
    async request(method, params = {}) {
        if (this.demoMode) {
            return this._handleDemoRequest(method, params);
        }

        if (!this.connected) {
            throw new Error('Core not connected. Start bhplus-core.exe or enable demo mode.');
        }

        return new Promise((resolve, reject) => {
            const id = ++this.requestId;
            const msg = JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n';

            this.pendingRequests.set(id, { resolve, reject, timer: setTimeout(() => {
                this.pendingRequests.delete(id);
                reject(new Error('Request timeout'));
            }, 10000)});

            this.client.write(msg);
        });
    }

    disconnect() {
        if (this.demoInterval) {
            clearInterval(this.demoInterval);
            this.demoInterval = null;
        }
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        if (this.client) {
            this.client.destroy();
            this.client = null;
        }
        this.connected = false;
        this.demoMode = false;
    }

    // --- Private ---

    _processBuffer() {
        const lines = this.buffer.split('\n');
        this.buffer = lines.pop(); // keep incomplete line

        for (const line of lines) {
            if (!line.trim()) continue;
            try {
                const msg = JSON.parse(line);
                if (msg.id && this.pendingRequests.has(msg.id)) {
                    const { resolve, reject, timer } = this.pendingRequests.get(msg.id);
                    clearTimeout(timer);
                    this.pendingRequests.delete(msg.id);
                    if (msg.error) reject(new Error(msg.error.message));
                    else resolve(msg.result);
                } else if (msg.method === 'capture.events' && Array.isArray(msg.params)) {
                    // Batched events from core (primary path)
                    this.emit('capture-events', msg.params);
                } else if (msg.method === 'capture.event') {
                    // Single event (legacy / fallback)
                    this.emit('capture-event', msg.params);
                }
            } catch (e) {
                console.error('[CoreBridge] Parse error:', e.message);
            }
        }
    }

    /**
     * Generate demo/mock data when core is not available
     */
    _handleDemoRequest(method, params) {
        switch (method) {
            case 'devices.list':
            case 'devices.enumerate':
                return Promise.resolve(this._demoDevices());
            case 'capture.start':
                this._startDemoData();
                return Promise.resolve({ success: true });
            case 'capture.stop':
                if (this.demoInterval) {
                    clearInterval(this.demoInterval);
                    this.demoInterval = null;
                }
                return Promise.resolve({ success: true });
            case 'device.reset':
                return Promise.resolve({ ok: true, message: 'Demo mode: reset simulated' });
            case 'command.send': {
                const length = Number(params.length || 0);
                return Promise.resolve({
                    ok: true,
                    bytesTransferred: length,
                    dataHex: this._randomHex(length),
                    message: 'Demo mode: control transfer simulated'
                });
            }
            case 'stats.get':
            case 'capture.status':
                return Promise.resolve({
                    capturing: this.demoInterval !== null,
                    driverLoaded: false,
                    stats: {
                        totalEvents: this.demoSeq,
                        totalBytes: this.demoSeq * 64,
                        eventsDropped: 0,
                        activeDevices: 3
                    }
                });
            default:
                return Promise.resolve({});
        }
    }

    _demoDevices() {
        // Schema matches BHPLUS_USB_DEVICE_INFO serialized by DeviceInfoToJson
        return [
            { bus: 1, device: 2,  vid: 0x046D, pid: 0xC52B, class: 0x00, speed: 3, isHub: false, name: 'Logitech Unifying Receiver', serial: '' },
            { bus: 1, device: 3,  vid: 0x0781, pid: 0x5583, class: 0x08, speed: 3, isHub: false, name: 'SanDisk Ultra USB 3.0',      serial: '4C530001234' },
            { bus: 1, device: 4,  vid: 0x04F2, pid: 0x0112, class: 0x03, speed: 1, isHub: false, name: 'USB HID Keyboard',            serial: '' },
            { bus: 2, device: 2,  vid: 0x1A86, pid: 0x7523, class: 0xFF, speed: 1, isHub: false, name: 'USB-SERIAL CH340 (COM3)',     serial: '' },
            { bus: 2, device: 3,  vid: 0x05AC, pid: 0x12A8, class: 0x00, speed: 3, isHub: false, name: 'Apple iPhone',               serial: 'AABBCC001122' },
        ];
    }

    _startDemoData() {
        if (this.demoInterval) return;

        // Transfer types matching BHPLUS_USB_TRANSFER_*
        const TRANSFER_CTRL = 'CTRL';
        const TRANSFER_BULK = 'BULK';
        const TRANSFER_INT  = 'INT';

        // Realistic USB event templates
        const usbTemplates = [
            // Control – enumeration
            { bus: 1, device: 2, ep: 0, type: TRANSFER_CTRL, fn: 'GET_DESCRIPTOR',     stage: 'SETUP', setup: '80060001000012 00', dir: '>>>' },
            { bus: 1, device: 2, ep: 0, type: TRANSFER_CTRL, fn: 'GET_DESCRIPTOR',     stage: 'COMPLETE', setup: '', dir: '<<<' },
            { bus: 1, device: 2, ep: 0, type: TRANSFER_CTRL, fn: 'SET_CONFIGURATION',  stage: 'SETUP', setup: '0009000100000000', dir: '>>>' },
            { bus: 1, device: 2, ep: 0, type: TRANSFER_CTRL, fn: 'SET_CONFIGURATION',  stage: 'COMPLETE', setup: '', dir: '<<<' },
            // Bulk – mass storage
            { bus: 1, device: 3, ep: 1, type: TRANSFER_BULK, fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '>>>' },
            { bus: 1, device: 3, ep: 1, type: TRANSFER_BULK, fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '<<<' },
            { bus: 1, device: 3, ep: 2, type: TRANSFER_BULK, fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '>>>' },
            { bus: 1, device: 3, ep: 2, type: TRANSFER_BULK, fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '<<<' },
            // Interrupt – HID keyboard
            { bus: 1, device: 4, ep: 1, type: TRANSFER_INT,  fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '<<<' },
            // Interrupt – serial USB
            { bus: 2, device: 2, ep: 1, type: TRANSFER_INT,  fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '>>>' },
            { bus: 2, device: 2, ep: 2, type: TRANSFER_INT,  fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '<<<' },
            // iPhone sync
            { bus: 2, device: 3, ep: 1, type: TRANSFER_BULK, fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '>>>' },
            { bus: 2, device: 3, ep: 2, type: TRANSFER_BULK, fn: 'BULK_OR_INTERRUPT_TRANSFER', dir: '<<<' },
        ];

        let irpCounter = 0x1000;
        const pendingIrp = new Map();

        this.demoInterval = setInterval(() => {
            const batchSize = Math.floor(Math.random() * 4) + 1;
            for (let i = 0; i < batchSize; i++) {
                const tmpl = usbTemplates[Math.floor(Math.random() * usbTemplates.length)];
                const isRequest = tmpl.dir === '>>>';
                const irpId = isRequest
                    ? (++irpCounter)
                    : (pendingIrp.get(`${tmpl.bus}-${tmpl.device}-${tmpl.ep}`) || ++irpCounter);

                if (isRequest) pendingIrp.set(`${tmpl.bus}-${tmpl.device}-${tmpl.ep}`, irpId);

                const dataLen = tmpl.type === TRANSFER_CTRL ? (tmpl.stage === 'COMPLETE' ? Math.floor(Math.random() * 18) + 2 : 0)
                              : tmpl.type === TRANSFER_BULK ? Math.floor(Math.random() * 512) + 1
                              : Math.floor(Math.random() * 8) + 1;

                const status = Math.random() > 0.03 ? 'OK' : 'STALL';
                const duration = isRequest ? 0 : Math.floor(Math.random() * 800) + 50;

                const details = {};
                if (tmpl.type === TRANSFER_CTRL) {
                    details.stage = tmpl.stage || 'COMPLETE';
                    if (tmpl.setup) details.setupPacket = tmpl.setup;
                }

                const event = {
                    seq:          ++this.demoSeq,
                    timestamp:    Date.now() * 1000,     // µs
                    type:         isRequest ? 'URB_DOWN' : 'URB_UP',
                    direction:    tmpl.dir,
                    protocol:     'USB',
                    bus:          tmpl.bus,
                    device:       tmpl.device,
                    endpoint:     tmpl.ep,
                    transferType: tmpl.type,
                    urbFunction:  tmpl.fn,
                    irpId:        irpId,
                    status:       status,
                    duration:     duration,
                    dataLength:   dataLen,
                    data:         this._randomHex(Math.min(dataLen, 64)),
                    details:      details,
                };

                this.emit('capture-event', event);
            }
        }, 200);
    }

    _randomHex(bytes) {
        let hex = '';
        for (let i = 0; i < bytes; i++) {
            hex += Math.floor(Math.random() * 256).toString(16).padStart(2, '0');
        }
        return hex;
    }
}

module.exports = CoreBridge;
