/**
 * USBPcapGUI - Main Application Orchestrator
 * Wires up all modules, handles toolbar buttons, keyboard shortcuts,
 * WebSocket event routing, detail panel, export, and resize handles.
 */

const App = {

    _capturing: false,
    _connected: false,
    _usbpcapStatus: null,

    // ---- Initialization ----

    init() {
        // Init sub-modules
        CaptureTable.init();
        DeviceTree.init();

        this._bindToolbar();
        this._bindUsbPcapBanner();
        this._bindFilterBar();
        this._bindKeyboard();
        this._bindDetailTabs();
        this._bindResizeHandles();
        this._bindExportDialog();
        this._initWebSocket();

        // Initial UI state
        this._updateCaptureUI(false);
        this._updateConnectionBadge('Connecting...', 'badge-info');
    },

    // ---- WebSocket ----

    _initWebSocket() {
        window.bhWs.on('open', () => {
            this._connected = true;
            this._updateConnectionBadge('LIVE', 'badge-success');
            DeviceTree.enumerate();
            window.bhWs.send('stats.get');
        });

        window.bhWs.on('close', () => {
            this._connected = false;
            this._updateConnectionBadge('DISCONNECTED', 'badge-warning');
        });

        window.bhWs.on('message', (msg) => {
            this._handleMessage(msg);
        });

        window.bhWs.connect();
    },

    _handleMessage(msg) {
        switch (msg.type) {
            case 'init':
                this._capturing = !!msg.data.capturing;
                this._updateCaptureUI(this._capturing);
                if (Array.isArray(msg.data.events) && msg.data.events.length) {
                    CaptureTable.addEvents(msg.data.events);
                }
                this._updateUsbPcapStatus(msg.data.usbpcap || null);
                this._updateConnectionBadge(msg.data.demoMode ? 'DEMO' : 'LIVE', msg.data.demoMode ? 'badge-info' : 'badge-success');
                break;

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

            case 'status':
                if (msg.data && typeof msg.data.capturing === 'boolean') {
                    this._capturing = msg.data.capturing;
                    this._updateCaptureUI(this._capturing);
                }
                if (msg.data && msg.data.demoMode) {
                    this._updateConnectionBadge('DEMO', 'badge-info');
                } else if (msg.data && msg.data.coreConnected) {
                    this._updateConnectionBadge('LIVE', 'badge-success');
                }
                break;

            case 'usbpcap.status':
                this._updateUsbPcapStatus(msg.data);
                break;

            case 'usbpcap.install':
                if (msg.data && msg.data.ok) {
                    this._showError('USBPcap 安装程序已启动，请完成安装后点击刷新状态。');
                } else {
                    this._showError((msg.data && msg.data.message) || '无法启动 USBPcap 安装程序');
                }
                break;

            case 'events.result':
                // Response to a query - replace table contents
                CaptureTable.clear();
                CaptureTable.addEvents(msg.data.events || []);
                break;

            case 'export.result':
                this._downloadExport(msg.data);
                break;

            case 'error':
                this._showError((msg.data && msg.data.message) || msg.message || 'Unknown error');
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
        document.getElementById('btn-refresh-devices').addEventListener('click', () => DeviceTree.enumerate());

        const autoScrollBtn = document.getElementById('btn-autoscroll');
        if (autoScrollBtn) {
            autoScrollBtn.addEventListener('click', () => {
                CaptureTable.autoScroll = !CaptureTable.autoScroll;
                autoScrollBtn.classList.toggle('active', CaptureTable.autoScroll);
            });
        }
    },

    _bindUsbPcapBanner() {
        const installBtn = document.getElementById('btn-install-usbpcap');
        const refreshBtn = document.getElementById('btn-refresh-usbpcap');

        if (installBtn) {
            installBtn.addEventListener('click', () => {
                window.bhWs.send('usbpcap.install');
            });
        }

        if (refreshBtn) {
            refreshBtn.addEventListener('click', () => {
                window.bhWs.send('usbpcap.status');
                DeviceTree.enumerate();
            });
        }
    },

    startCapture() {
        if (!this._connected) return;
        const selected = DeviceTree.getSelectedDevice();
        window.bhWs.send('capture.start', {
            deviceIds: selected ? [selected.device] : [],
            filterBus: selected ? selected.bus : 0
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
        const filterClear = document.getElementById('btn-filter-clear');
        const filterApply = document.getElementById('btn-filter-apply');

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

        if (filterApply) {
            filterApply.addEventListener('click', () => {
                CaptureTable.setFilter(filterInput.value);
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
        const tabs = document.querySelectorAll('.tab');
        tabs.forEach(tab => {
            tab.addEventListener('click', () => {
                tabs.forEach(t => t.classList.remove('active'));
                tab.classList.add('active');

                const target = tab.dataset.tab;
                document.querySelectorAll('.tab-content').forEach(c => {
                    c.classList.remove('active');
                });
                const panel = document.getElementById(target);
                if (panel) panel.classList.add('active');
            });
        });
    },

    /**
     * Called by CaptureTable when a row is selected
     */
    onEventSelected(event) {
        const hexPanel = document.getElementById('hex-view');
        const decodePanel = document.getElementById('decode-body');

        if (!event) {
            if (hexPanel) hexPanel.innerHTML = 'Select an event to view details';
            if (decodePanel) decodePanel.innerHTML = '<tr><td colspan="3">Select an event to view decode</td></tr>';
            return;
        }

        // Hex view
        if (hexPanel) {
            if (event.data) {
                hexPanel.innerHTML = HexView.format(event.data);
            } else {
                hexPanel.innerHTML = 'No data payload';
            }
        }

        // Protocol decode
        if (decodePanel) {
            decodePanel.innerHTML = this._renderDecode(event);
        }
    },

    _renderDecode(event) {
        const fields = [];
        let html = '';

        fields.push(['Sequence', event.seq, '']);
        fields.push(['Timestamp', this._formatTimestamp(event.timestamp), '']);
        fields.push(['Protocol', event.protocol, '']);
        fields.push(['Device', event.device, '']);
        if (event.deviceId !== undefined) fields.push(['Device ID', event.deviceId, '']);
        fields.push(['Phase', event.phase, '']);
        fields.push(['Direction', event.direction, '']);
        fields.push(['Command', event.command, '']);
        fields.push(['Status', event.status, '']);
        if (event.dataLength !== undefined) fields.push(['Data Length', event.dataLength + ' bytes', '']);
        if (event.duration !== undefined) fields.push(['Duration', event.duration + ' µs', '']);
        if (event.summary) fields.push(['Summary', event.summary, '']);

        // Protocol-specific fields
        if (event.decodedFields && event.decodedFields.length) {
            fields.push(['---', '--- Decoded Fields ---', '']);
            for (const field of event.decodedFields) {
                fields.push([field.name, field.value, field.description || '']);
            }
        }
        if (event.details) {
            fields.push(['---', '--- Protocol Details ---', '']);
            for (const [key, val] of Object.entries(event.details)) {
                fields.push([key, val, '']);
            }
        }

        for (const [key, val, desc] of fields) {
            if (key === '---') {
                html += `<tr class="decode-separator"><td colspan="3">${this._esc(String(val))}</td></tr>`;
            } else {
                html += `<tr><td class="decode-key">${this._esc(String(key))}</td><td class="decode-value">${this._esc(String(val ?? ''))}</td><td>${this._esc(String(desc || ''))}</td></tr>`;
            }
        }
        return html;
    },

    // ---- Resize Handles ----

    _bindResizeHandles() {
        const handles = document.querySelectorAll('.resize-handle');
        handles.forEach(handle => {
            handle.addEventListener('mousedown', (e) => {
                e.preventDefault();
                const leftId = handle.dataset.left;
                const rightId = handle.dataset.right;
                const topId = handle.dataset.top;
                const bottomId = handle.dataset.bottom;

                if (leftId && rightId) {
                    const left = document.getElementById(leftId);
                    const startX = e.clientX;
                    const startWidth = left.offsetWidth;
                    const onMove = (ev) => {
                        left.style.width = Math.max(180, startWidth + (ev.clientX - startX)) + 'px';
                    };
                    const onUp = () => {
                        document.removeEventListener('mousemove', onMove);
                        document.removeEventListener('mouseup', onUp);
                    };
                    document.addEventListener('mousemove', onMove);
                    document.addEventListener('mouseup', onUp);
                } else if (topId && bottomId) {
                    const top = document.getElementById(topId);
                    const startY = e.clientY;
                    const startHeight = top.offsetHeight;
                    const onMove = (ev) => {
                        top.style.flex = 'none';
                        top.style.height = Math.max(120, startHeight + (ev.clientY - startY)) + 'px';
                    };
                    const onUp = () => {
                        document.removeEventListener('mousemove', onMove);
                        document.removeEventListener('mouseup', onUp);
                    };
                    document.addEventListener('mousemove', onMove);
                    document.addEventListener('mouseup', onUp);
                }
            });
        });
    },

    // ---- Export ----

    _bindExportDialog() {
        const dialog = document.getElementById('export-dialog');
        if (!dialog) return;

        const closeBtn = document.getElementById('btn-export-cancel');
        const exportBtn = document.getElementById('btn-export-ok');

        if (closeBtn) closeBtn.addEventListener('click', () => this.hideExportDialog());
        if (exportBtn) exportBtn.addEventListener('click', () => this._doExport());
    },

    showExportDialog() {
        const dialog = document.getElementById('export-dialog');
        if (dialog && dialog.showModal) dialog.showModal();
    },

    hideExportDialog() {
        const dialog = document.getElementById('export-dialog');
        if (dialog && dialog.close) dialog.close();
    },

    _doExport() {
        const formatEl = document.getElementById('export-format');
        const filteredEl = document.getElementById('export-filtered');
        const filterInput = document.getElementById('filter-input');
        const format = formatEl ? formatEl.value : 'json';
        const exportFiltered = !!(filteredEl && filteredEl.checked);
        const filterText = exportFiltered && filterInput ? filterInput.value.trim() : '';

        window.bhWs.send('export', {
            format,
            filtered: exportFiltered,
            filterText
        });
        this.hideExportDialog();
    },

    _downloadExport(data) {
        if (!data || !data.content) return;
        let blobContent = data.content;
        if (data.encoding === 'base64') {
            const binary = atob(data.content);
            const bytes = new Uint8Array(binary.length);
            for (let i = 0; i < binary.length; i++) {
                bytes[i] = binary.charCodeAt(i);
            }
            blobContent = bytes;
        }
        const blob = new Blob([blobContent], { type: data.mimeType || data.mime || 'application/octet-stream' });
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
        const normalized = stats && stats.stats ? stats.stats : stats;
        if (normalized.totalEvents !== undefined) {
            const el = document.getElementById('event-count');
            if (el) el.textContent = `Events: ${normalized.totalEvents}`;
        }
        if (normalized.eventsDropped !== undefined) {
            const el = document.getElementById('dropped-count');
            if (el) el.textContent = `Dropped: ${normalized.eventsDropped}`;
        }
    },

    // ---- Capture UI State ----

    _updateCaptureUI(capturing) {
        const btnStart = document.getElementById('btn-start');
        const btnStop = document.getElementById('btn-stop');

        if (btnStart) btnStart.disabled = capturing;
        if (btnStop) btnStop.disabled = !capturing;
    },

    _updateConnectionBadge(text, badgeClass) {
        const badge = document.getElementById('status-badge');
        if (!badge) return;
        badge.textContent = text;
        badge.className = `badge ${badgeClass}`;
    },

    _updateUsbPcapStatus(status) {
        this._usbpcapStatus = status;

        const banner = document.getElementById('usbpcap-banner');
        const detail = document.getElementById('usbpcap-banner-detail');
        const installBtn = document.getElementById('btn-install-usbpcap');

        if (!banner || !detail || !installBtn) return;

        if (!status) {
            banner.classList.add('hidden');
            return;
        }

        const hubCount = Array.isArray(status.hubs) ? status.hubs.length : 0;

        if (status.installed && ((status.interfacesAvailable || 0) > 0 || hubCount > 0)) {
            banner.classList.add('hidden');
            return;
        }

        if (!status.installed) {
            detail.textContent = status.installerFound
                ? '尚未检测到可用的 USBPcap 安装。请先安装 USBPcap，再点击刷新状态。'
                : '未找到随程序打包的 USBPcap 安装器，请手动安装后点击刷新状态。';
        } else if (status.restartRecommended) {
            detail.textContent = 'USBPcap 已安装，但当前系统尚未暴露抓包接口。请先重启系统，或完成 USB 设备重启后再点击刷新状态。';
        } else if (status.driverServiceInstalled && !status.driverServiceRunning) {
            detail.textContent = 'USBPcap 已安装，但驱动服务尚未运行。请以管理员身份启动后刷新状态。';
        } else {
            detail.textContent = `USBPcap 已安装，但当前未检测到可用实例（当前 ${hubCount} 个）。请刷新状态或重启系统后重试。`;
        }

        installBtn.disabled = !!status.installed || !status.installerFound;
        banner.classList.remove('hidden');
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
        if (typeof ts === 'number' && Number.isFinite(ts)) {
            const isMicroseconds = ts > 10_000_000_000_000;
            const millis = isMicroseconds ? Math.floor(ts / 1000) : ts;
            const d = new Date(millis);
            if (Number.isNaN(d.getTime())) return String(ts);
            const time = d.toLocaleTimeString('en-US', { hour12: false }) + '.' + String(d.getMilliseconds()).padStart(3, '0');
            return isMicroseconds ? `${time}${String(ts % 1000).padStart(3, '0')}` : time;
        }
        const d = new Date(Date.parse(ts));
        if (Number.isNaN(d.getTime())) return String(ts);
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
