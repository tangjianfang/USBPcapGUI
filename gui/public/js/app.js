/**
 * USBPcapGUI - Main Application Orchestrator
 * Wires up all modules, handles toolbar buttons, keyboard shortcuts,
 * WebSocket event routing, detail panel, export, and resize handles.
 */

const App = {

    _capturing: false,
    _connected: false,

    // ---- Initialization ----

    init() {
        // Init sub-modules
        CaptureTable.init();
        DeviceTree.init();

        this._bindToolbar();
        this._bindFilterBar();
        this._bindKeyboard();
        this._bindDetailTabs();
        this._bindResizeHandles();
        this._bindExportDialog();
        this._initWebSocket();

        // Initial UI state
        this._updateCaptureUI(false);
        document.getElementById('status-connection').textContent = 'Connecting...';
    },

    // ---- WebSocket ----

    _initWebSocket() {
        window.bhWs.on('open', () => {
            this._connected = true;
            document.getElementById('status-connection').textContent = 'Connected';
            document.getElementById('status-connection').classList.add('connected');
            DeviceTree.enumerate();
        });

        window.bhWs.on('close', () => {
            this._connected = false;
            document.getElementById('status-connection').textContent = 'Disconnected';
            document.getElementById('status-connection').classList.remove('connected');
        });

        window.bhWs.on('message', (msg) => {
            this._handleMessage(msg);
        });

        window.bhWs.connect();
    },

    _handleMessage(msg) {
        switch (msg.type) {
            case 'capture.event':
                CaptureTable.addEvent(msg.data);
                break;

            case 'capture.events':
                CaptureTable.addEvents(msg.data);
                break;

            case 'capture.started':
                this._capturing = true;
                this._updateCaptureUI(true);
                break;

            case 'capture.stopped':
                this._capturing = false;
                this._updateCaptureUI(false);
                break;

            case 'capture.cleared':
                CaptureTable.clear();
                break;

            case 'devices.list':
                DeviceTree.update(msg.data);
                break;

            case 'stats':
                this._updateStats(msg.data);
                break;

            case 'events.result':
                // Response to a query - replace table contents
                CaptureTable.clear();
                CaptureTable.addEvents(msg.data);
                break;

            case 'export.ready':
                this._downloadExport(msg.data);
                break;

            case 'error':
                this._showError(msg.message || 'Unknown error');
                break;

            default:
                console.log('[App] Unhandled message type:', msg.type, msg);
        }
    },

    // ---- Toolbar ----

    _bindToolbar() {
        document.getElementById('btn-start').addEventListener('click', () => this.startCapture());
        document.getElementById('btn-stop').addEventListener('click', () => this.stopCapture());
        document.getElementById('btn-clear').addEventListener('click', () => this.clearCapture());
        document.getElementById('btn-export').addEventListener('click', () => this.showExportDialog());

        const autoScrollBtn = document.getElementById('btn-autoscroll');
        if (autoScrollBtn) {
            autoScrollBtn.addEventListener('click', () => {
                CaptureTable.autoScroll = !CaptureTable.autoScroll;
                autoScrollBtn.classList.toggle('active', CaptureTable.autoScroll);
            });
        }
    },

    startCapture() {
        if (!this._connected) return;
        window.bhWs.send('capture.start', {
            deviceId: DeviceTree.getSelectedDeviceId()
        });
    },

    stopCapture() {
        if (!this._connected) return;
        window.bhWs.send('capture.stop');
    },

    clearCapture() {
        window.bhWs.send('capture.clear');
        CaptureTable.clear();
    },

    // ---- Filter Bar ----

    _bindFilterBar() {
        const filterInput = document.getElementById('filter-input');
        const filterClear = document.getElementById('filter-clear');

        let debounceTimer = null;
        filterInput.addEventListener('input', () => {
            clearTimeout(debounceTimer);
            debounceTimer = setTimeout(() => {
                CaptureTable.setFilter(filterInput.value);
            }, 150);
        });

        filterInput.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                filterInput.value = '';
                CaptureTable.setFilter('');
                filterInput.blur();
            }
        });

        if (filterClear) {
            filterClear.addEventListener('click', () => {
                filterInput.value = '';
                CaptureTable.setFilter('');
            });
        }
    },

    /**
     * Set filter from device tree double-click
     */
    filterByDevice(deviceId, deviceName) {
        const filterInput = document.getElementById('filter-input');
        filterInput.value = `device:"${deviceName}"`;
        CaptureTable.setFilter(filterInput.value);
    },

    // ---- Keyboard Shortcuts ----

    _bindKeyboard() {
        document.addEventListener('keydown', (e) => {
            // Don't intercept when typing in input fields
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;

            if (e.key === 'F5') {
                e.preventDefault();
                this.startCapture();
            } else if (e.key === 'F6') {
                e.preventDefault();
                this.stopCapture();
            } else if (e.ctrlKey && e.key === 'l') {
                e.preventDefault();
                this.clearCapture();
            } else if (e.ctrlKey && e.key === 'e') {
                e.preventDefault();
                this.showExportDialog();
            } else if (e.ctrlKey && e.key === 'f') {
                e.preventDefault();
                document.getElementById('filter-input').focus();
            } else if (e.key === 'Delete') {
                e.preventDefault();
                this.clearCapture();
            }
        });
    },

    // ---- Detail Panel ----

    _bindDetailTabs() {
        const tabs = document.querySelectorAll('.detail-tab');
        tabs.forEach(tab => {
            tab.addEventListener('click', () => {
                tabs.forEach(t => t.classList.remove('active'));
                tab.classList.add('active');

                const target = tab.dataset.tab;
                document.querySelectorAll('.detail-content').forEach(c => {
                    c.classList.remove('active');
                });
                const panel = document.getElementById('detail-' + target);
                if (panel) panel.classList.add('active');
            });
        });
    },

    /**
     * Called by CaptureTable when a row is selected
     */
    onEventSelected(event) {
        const hexPanel = document.getElementById('detail-hex');
        const decodePanel = document.getElementById('detail-decode');

        if (!event) {
            if (hexPanel) hexPanel.innerHTML = '<div class="detail-placeholder">Select an event to view details</div>';
            if (decodePanel) decodePanel.innerHTML = '<div class="detail-placeholder">Select an event to view decode</div>';
            return;
        }

        // Hex view
        if (hexPanel) {
            if (event.data) {
                hexPanel.innerHTML = HexView.render(event.data);
            } else {
                hexPanel.innerHTML = '<div class="detail-placeholder">No data payload</div>';
            }
        }

        // Protocol decode
        if (decodePanel) {
            decodePanel.innerHTML = this._renderDecode(event);
        }
    },

    _renderDecode(event) {
        const fields = [];

        fields.push(['Sequence', event.seq]);
        fields.push(['Timestamp', this._formatTimestamp(event.timestamp)]);
        fields.push(['Protocol', event.protocol]);
        fields.push(['Device', event.device]);
        if (event.deviceId !== undefined) fields.push(['Device ID', event.deviceId]);
        fields.push(['Phase', event.phase]);
        fields.push(['Direction', event.direction]);
        fields.push(['Command', event.command]);
        fields.push(['Status', event.status]);
        if (event.dataLength !== undefined) fields.push(['Data Length', event.dataLength + ' bytes']);
        if (event.duration !== undefined) fields.push(['Duration', event.duration + ' µs']);

        // Protocol-specific fields
        if (event.details) {
            fields.push(['---', '--- Protocol Details ---']);
            for (const [key, val] of Object.entries(event.details)) {
                fields.push([key, val]);
            }
        }

        let html = '<table class="decode-table">';
        for (const [key, val] of fields) {
            if (key === '---') {
                html += `<tr class="decode-separator"><td colspan="2">${this._esc(String(val))}</td></tr>`;
            } else {
                html += `<tr><td class="decode-key">${this._esc(String(key))}</td><td class="decode-value">${this._esc(String(val ?? ''))}</td></tr>`;
            }
        }
        html += '</table>';
        return html;
    },

    // ---- Resize Handles ----

    _bindResizeHandles() {
        this._makeResizable('device-panel', 'resize-handle-device', 'horizontal', 180, 500);
        this._makeResizable('detail-panel', 'resize-handle-detail', 'vertical', 100, 600);
    },

    _makeResizable(panelId, handleId, direction, minSize, maxSize) {
        const handle = document.getElementById(handleId);
        const panel = document.getElementById(panelId);
        if (!handle || !panel) return;

        let startPos, startSize;

        const onMouseMove = (e) => {
            let delta;
            if (direction === 'horizontal') {
                delta = e.clientX - startPos;
                const newWidth = Math.min(maxSize, Math.max(minSize, startSize + delta));
                panel.style.width = newWidth + 'px';
            } else {
                delta = startPos - e.clientY;
                const newHeight = Math.min(maxSize, Math.max(minSize, startSize + delta));
                panel.style.height = newHeight + 'px';
            }
        };

        const onMouseUp = () => {
            document.removeEventListener('mousemove', onMouseMove);
            document.removeEventListener('mouseup', onMouseUp);
            document.body.style.userSelect = '';
            document.body.style.cursor = '';
        };

        handle.addEventListener('mousedown', (e) => {
            e.preventDefault();
            startPos = direction === 'horizontal' ? e.clientX : e.clientY;
            startSize = direction === 'horizontal' ? panel.offsetWidth : panel.offsetHeight;
            document.body.style.userSelect = 'none';
            document.body.style.cursor = direction === 'horizontal' ? 'col-resize' : 'row-resize';
            document.addEventListener('mousemove', onMouseMove);
            document.addEventListener('mouseup', onMouseUp);
        });
    },

    // ---- Export ----

    _bindExportDialog() {
        const dialog = document.getElementById('export-dialog');
        if (!dialog) return;

        const overlay = document.getElementById('export-overlay');
        const closeBtn = document.getElementById('export-close');
        const exportBtn = document.getElementById('export-confirm');

        if (overlay) overlay.addEventListener('click', () => this.hideExportDialog());
        if (closeBtn) closeBtn.addEventListener('click', () => this.hideExportDialog());
        if (exportBtn) exportBtn.addEventListener('click', () => this._doExport());
    },

    showExportDialog() {
        const dialog = document.getElementById('export-dialog');
        const overlay = document.getElementById('export-overlay');
        if (dialog) dialog.style.display = 'block';
        if (overlay) overlay.style.display = 'block';

        // Update count
        const countEl = document.getElementById('export-event-count');
        if (countEl) {
            const filtered = CaptureTable.getFilteredEvents();
            countEl.textContent = `${filtered.length} events (filtered)`;
        }
    },

    hideExportDialog() {
        const dialog = document.getElementById('export-dialog');
        const overlay = document.getElementById('export-overlay');
        if (dialog) dialog.style.display = 'none';
        if (overlay) overlay.style.display = 'none';
    },

    _doExport() {
        const formatEl = document.querySelector('input[name="export-format"]:checked');
        const format = formatEl ? formatEl.value : 'json';

        window.bhWs.send('export', { format });
        this.hideExportDialog();
    },

    _downloadExport(data) {
        if (!data || !data.content) return;
        const blob = new Blob([data.content], { type: data.mimeType || 'application/octet-stream' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = data.filename || 'capture-export';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    },

    // ---- Device Selection ----

    onDeviceSelected(device) {
        // Show device info in status bar or elsewhere
        const statusDevice = document.getElementById('status-device');
        if (statusDevice && device) {
            statusDevice.textContent = device.name || `Device ${device.id}`;
        }
    },

    // ---- Stats ----

    _updateStats(stats) {
        if (stats.capturedEvents !== undefined) {
            const el = document.getElementById('event-count');
            if (el) el.textContent = stats.capturedEvents;
        }
        if (stats.bytesPerSec !== undefined) {
            const el = document.getElementById('status-rate');
            if (el) el.textContent = this._formatRate(stats.bytesPerSec);
        }
    },

    // ---- Capture UI State ----

    _updateCaptureUI(capturing) {
        const btnStart = document.getElementById('btn-start');
        const btnStop = document.getElementById('btn-stop');
        const statusCapture = document.getElementById('status-capture');

        if (btnStart) btnStart.disabled = capturing;
        if (btnStop) btnStop.disabled = !capturing;
        if (statusCapture) {
            statusCapture.textContent = capturing ? '● Capturing' : '○ Idle';
            statusCapture.classList.toggle('capturing', capturing);
        }
    },

    // ---- Utilities ----

    _showError(message) {
        console.error('[BHPlus]', message);
        // Simple toast notification
        const toast = document.createElement('div');
        toast.className = 'toast toast-error';
        toast.textContent = message;
        document.body.appendChild(toast);
        setTimeout(() => {
            toast.classList.add('show');
        }, 10);
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        }, 4000);
    },

    _esc(str) {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    },

    _formatTimestamp(ts) {
        if (!ts) return '';
        const d = new Date(typeof ts === 'number' ? ts : Date.parse(ts));
        return d.toLocaleTimeString('en-US', { hour12: false }) + '.' + String(d.getMilliseconds()).padStart(3, '0');
    },

    _formatRate(bytesPerSec) {
        if (bytesPerSec > 1048576) return (bytesPerSec / 1048576).toFixed(1) + ' MB/s';
        if (bytesPerSec > 1024) return (bytesPerSec / 1024).toFixed(1) + ' KB/s';
        return bytesPerSec + ' B/s';
    }
};

// ---- Boot ----
window.App = App;
document.addEventListener('DOMContentLoaded', () => {
    App.init();
});
