/**
 * USBPcapGUI - WebSocket Client Module
 * Manages the real-time connection to the Node.js backend.
 */

class BHPlusWebSocket {
    constructor() {
        this.ws = null;
        this.connected = false;
        this.handlers = new Map();
        this.reconnectTimer = null;
    }

    /**
     * Register a handler for a specific message type
     */
    on(type, callback) {
        if (!this.handlers.has(type)) {
            this.handlers.set(type, []);
        }
        this.handlers.get(type).push(callback);
    }

    isConnected() {
        return this.connected;
    }

    /**
     * Connect to the WebSocket server
     */
    connect() {
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const url = `${proto}//${location.host}/ws`;

        this.ws = new WebSocket(url);

        this.ws.onopen = () => {
            console.log('[WS] Connected');
            this.connected = true;
            this._emit('open');
            this._emit('connected');
            if (this.reconnectTimer) {
                clearTimeout(this.reconnectTimer);
                this.reconnectTimer = null;
            }
        };

        this.ws.onmessage = (event) => {
            try {
                const msg = JSON.parse(event.data);
                this._emit('message', msg);
                this._emit(msg.type, msg.data);
            } catch (e) {
                console.error('[WS] Parse error:', e);
            }
        };

        this.ws.onclose = () => {
            console.log('[WS] Disconnected');
            this.connected = false;
            this._emit('close');
            this._emit('disconnected');
            // Auto-reconnect
            this.reconnectTimer = setTimeout(() => this.connect(), 2000);
        };

        this.ws.onerror = (err) => {
            console.error('[WS] Error:', err);
        };
    }

    /**
     * Send a message to the server
     */
    send(type, data = {}) {
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            console.warn('[WS] Not connected');
            return;
        }
        this.ws.send(JSON.stringify({ type, data }));
    }

    _emit(type, data) {
        const callbacks = this.handlers.get(type);
        if (callbacks) {
            for (const cb of callbacks) {
                try { cb(data); } catch (e) { console.error('[WS] Handler error:', e); }
            }
        }
    }
}

// Global instance
window.bhWs = new BHPlusWebSocket();
