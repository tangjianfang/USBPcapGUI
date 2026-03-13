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
        InterfaceOptions.init();

        this._bindToolbar();
        this._bindUsbPcapBanner();
        this._bindFilterBar();
        this._bindKeyboard();
        this._bindDetailTabs();
        this._bindResizeHandles();
        this._bindExportDialog();
        this._bindCommandDialog();
        this._initWebSocket();

        // Initial UI state
        this._updateCaptureUI(false);
        this._updateConnectionBadge('Connecting...', 'badge-info');
        // Init filter presets (multi-select chips + device panel + localStorage restore)
        FilterPresets.init();
    },

    // ---- WebSocket ----

    _initWebSocket() {
        window.bhWs.on('open', () => {
            this._connected = true;
            this._updateConnectionBadge('LIVE', 'badge-success');
            DeviceTree.enumerate();
            window.bhWs.send('stats.get');
            // Refresh stats every 5 seconds so Dropped count stays up-to-date
            if (this._statsTimer) clearInterval(this._statsTimer);
            this._statsTimer = setInterval(() => {
                if (this._connected) window.bhWs.send('stats.get');
            }, 5000);
        });

        window.bhWs.on('close', () => {
            this._connected = false;
            this._updateConnectionBadge('DISCONNECTED', 'badge-warning');
            if (this._statsTimer) { clearInterval(this._statsTimer); this._statsTimer = null; }
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
                this._setStatusFrame('Capturing…');
                break;

            case 'capture.stopped':
                this._capturing = false;
                this._updateCaptureUI(false);
                this._setStatusFrame('Capture stopped');
                break;

            case 'capture.cleared':
                CaptureTable.clear();
                break;

            case 'devices.list':
                DeviceTree.update(msg.data);
                InterfaceOptions._onDevicesReceived(msg.data);
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
                    // Core just connected — re-enumerate devices (they were empty before core was ready)
                    DeviceTree.enumerate();
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

            case 'usbpcap.rescan':
                if (msg.data && (msg.data.interfacesAvailable > 0 || (msg.data.hubs && msg.data.hubs.length > 0))) {
                    this._showInfo('扫描完成，已检测到 USB 捕包接口');
                } else if (msg.data && msg.data.virtualUsbDetected) {
                    this._showError('当前环境为虚拟USB（云电脑/USB IP），USBPcap 不支持虚拟 USB 主控器，请在物理机器上运行');
                } else {
                    this._showError('重新扫描完成，仍未检到 USBPcap 接口。请重启电脑后重试');
                }
                break;

            case 'device.reset.result':
                this._showInfo((msg.data && msg.data.message) || '已提交设备重置请求');
                DeviceTree.enumerate();
                break;

            case 'command.send.result':
                if (msg.data && msg.data.ok) {
                    const transferred = msg.data.bytesTransferred || 0;
                    this._showInfo(`控制传输完成，传输 ${transferred} 字节`);
                    if (msg.data.dataHex) {
                        const hexPanel = document.getElementById('hex-view');
                        if (hexPanel) {
                            hexPanel.innerHTML = HexView.format(msg.data.dataHex);
                        }
                    }
                } else {
                    this._showError((msg.data && msg.data.message) || '控制传输失败');
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
        document.getElementById('btn-interface-options').addEventListener('click', () => this.showInterfaceOptions());
        document.getElementById('btn-clear').addEventListener('click', () => this.clearCapture());
        document.getElementById('btn-export').addEventListener('click', () => this.showExportDialog());
        document.getElementById('btn-reset-device').addEventListener('click', () => this.resetSelectedDevice());
        document.getElementById('btn-send-command').addEventListener('click', () => this.showCommandDialog());
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
                refreshBtn.disabled = true;
                refreshBtn.textContent = '扫描中...';
                window.bhWs.send('usbpcap.rescan');
                setTimeout(() => {
                    refreshBtn.disabled = false;
                    refreshBtn.textContent = '重新扫描';
                }, 3000);
            });
        }
    },

    startCapture() {
        if (!this._connected) {
            this._showError('Not connected to capture service. Start bhplus-core first.');
            return;
        }
        const selected = DeviceTree.getSelectedDevice();
        const settings = InterfaceOptions.getSavedSettings();
        const params = {
            ...settings,
            deviceIds: selected ? [selected.device || selected.id || 0] : [],
            filterBus: selected ? (selected.bus || 0) : 0,
        };
        window.bhWs.send('capture.start', params);
    },

    showInterfaceOptions() {
        const selected = DeviceTree.getSelectedDevice();
        const hubLabel = selected ? (selected.name || `Device ${selected.id}`) : '';
        InterfaceOptions.show(hubLabel, (params) => {
            if (!this._connected) {
                this._showError('Not connected to capture service. Start bhplus-core first.');
                return;
            }
            if (selected && params.deviceIds.length === 0) {
                params.deviceIds = [selected.device || selected.id || 0];
                params.filterBus = selected.bus || 0;
            }
            window.bhWs.send('capture.start', params);
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

    resetSelectedDevice() {
        const selected = DeviceTree.getSelectedDevice();
        if (!selected) {
            this._showError('请先在左侧选择一个设备');
            return;
        }
        window.bhWs.send('device.reset', { deviceId: selected.id });
    },

    // ---- Filter Bar (delegated to FilterPresets) ----

    _bindFilterBar() {
        // FilterPresets.init() handles all filter binding.
        // This stub is kept for call-site compatibility.
    },

    /**
     * Set filter from device tree click — pre-fills device filter panel
     */
    filterByDevice(deviceId, deviceName) {
        const dev = DeviceTree._devices && DeviceTree._devices.find(d => d.id === deviceId);
        if (dev) {
            FilterPresets.fillFromDevice(dev);
        } else {
            // Fallback: text filter
            const input = document.getElementById('filter-input');
            if (input) { input.value = `device:"${deviceName}"`; }
            FilterPresets._manualText = `device:"${deviceName}"`;
            FilterPresets._apply();
        }
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
        const decodeTree = document.getElementById('decode-tree');

        if (!event) {
            if (hexPanel) hexPanel.innerHTML = 'Select a packet to view details';
            if (decodeTree) decodeTree.innerHTML = '';
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

        // Protocol decode tree
        if (decodeTree) {
            decodeTree.innerHTML = '';
            this._buildDecodeTree(event).forEach(node => decodeTree.appendChild(node));
        }
    },

    _buildDecodeTree(event) {
        const nodes = [];

        // Frame section
        const isDown = (event.direction || '').toLowerCase() !== 'up';
        const fLen = event.dataLength ?? 0;
        nodes.push(this._dtSection(
            `Frame ${event.seq ?? ''}: ${fLen} bytes, ${isDown ? 'host → device' : 'device → host'}`,
            [
                this._dtLeaf('Sequence',   event.seq ?? ''),
                this._dtLeaf('Timestamp',  this._formatTimestamp(event.timestamp)),
                this._dtLeaf('Direction',  event.direction || ''),
                this._dtLeaf('Device',     event.device   || ''),
                this._dtLeaf('Length',     fLen + ' bytes'),
            ]
        ));

        // USB URB section
        const urbFields = [
            this._dtLeaf('Protocol',  event.protocol || 'USB'),
            this._dtLeaf('Phase',     event.phase || ''),
        ];
        if (event.status !== undefined) urbFields.push(this._dtLeaf('Status', event.status));
        if (event.duration !== undefined) urbFields.push(this._dtLeaf('Duration', event.duration + ' µs'));
        nodes.push(this._dtSection('USB URB', urbFields));

        // Protocol-specific decoded fields
        if (event.command || event.summary || (event.decodedFields && event.decodedFields.length)) {
            const protoFields = [];
            if (event.command) protoFields.push(this._dtLeaf('Command', event.command));
            if (event.summary) protoFields.push(this._dtLeaf('Summary', event.summary));
            for (const f of (event.decodedFields || [])) {
                protoFields.push(this._dtLeaf(f.name, f.value, f.description));
            }
            if (event.details) {
                for (const [k, v] of Object.entries(event.details)) {
                    protoFields.push(this._dtLeaf(k, v));
                }
            }
            nodes.push(this._dtSection(event.protocol || 'Protocol', protoFields));
        }

        return nodes;
    },

    _dtSection(title, children) {
        const li = document.createElement('li');
        li.className = 'has-children expanded';

        const row = document.createElement('div');
        row.className = 'dt-row';
        row.innerHTML = `<span class="dt-toggle">▾</span><span class="dt-section">${this._esc(title)}</span>`;

        const ul = document.createElement('ul');
        children.forEach(c => ul.appendChild(c));
        li.appendChild(row);
        li.appendChild(ul);

        row.addEventListener('click', () => {
            const exp = li.classList.toggle('expanded');
            row.querySelector('.dt-toggle').textContent = exp ? '▾' : '▸';
        });

        return li;
    },

    _dtLeaf(name, value, desc) {
        const li = document.createElement('li');
        const row = document.createElement('div');
        row.className = 'dt-row';
        const descHtml = desc ? `<span class="dt-desc">${this._esc(String(desc))}</span>` : '';
        row.innerHTML = `<span class="dt-toggle" style="visibility:hidden">▸</span><span class="dt-name">${this._esc(String(name))}</span><span class="dt-sep">:</span><span class="dt-value">${this._esc(String(value ?? ''))}</span>${descHtml}`;
        li.appendChild(row);
        return li;
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

    _bindCommandDialog() {
        const dialog = document.getElementById('command-dialog');
        if (!dialog) return;

        const cancelBtn = document.getElementById('btn-command-cancel');
        const sendBtn = document.getElementById('btn-command-send');
        if (cancelBtn) cancelBtn.addEventListener('click', () => this.hideCommandDialog());
        if (sendBtn) sendBtn.addEventListener('click', () => this._sendCommand());
    },

    showExportDialog() {
        const dialog = document.getElementById('export-dialog');
        if (dialog && dialog.showModal) dialog.showModal();
    },

    hideExportDialog() {
        const dialog = document.getElementById('export-dialog');
        if (dialog && dialog.close) dialog.close();
    },

    showCommandDialog() {
        const selected = DeviceTree.getSelectedDevice();
        if (!selected) {
            this._showError('请先在左侧选择一个设备');
            return;
        }

        const label = document.getElementById('command-device-label');
        if (label) {
            label.textContent = `${selected.name || 'USB Device'} (Bus ${selected.bus || 0} Dev ${selected.device || 0})`;
        }

        const dialog = document.getElementById('command-dialog');
        if (dialog && dialog.showModal) dialog.showModal();
    },

    hideCommandDialog() {
        const dialog = document.getElementById('command-dialog');
        if (dialog && dialog.close) dialog.close();
    },

    _sendCommand() {
        const selected = DeviceTree.getSelectedDevice();
        if (!selected) {
            this._showError('请先选择一个设备');
            return;
        }

        try {
            const data = {
                deviceId: selected.id,
                requestType: this._parseHexField('cmd-request-type', 0xff),
                request: this._parseHexField('cmd-request', 0xff),
                value: this._parseHexField('cmd-value', 0xffff),
                index: this._parseHexField('cmd-index', 0xffff),
                length: this._parseNumberField('cmd-length'),
                payloadHex: (document.getElementById('cmd-payload')?.value || '').trim()
            };

            window.bhWs.send('command.send', data);
            this.hideCommandDialog();
        } catch (e) {
            this._showError(e.message || '命令参数无效');
        }
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

    // ---- Status Bar ----

    updateStatusBar(event, selectedNo, total) {
        const frame = document.getElementById('sb-frame');
        if (frame && event) {
            const bytes = event.dataLength ?? 0;
            frame.textContent = `Frame ${event.seq ?? selectedNo}: ${bytes} bytes on wire`;
        }
        const packets = document.getElementById('sb-packets');
        if (packets) {
            packets.textContent = selectedNo
                ? `Selected: ${selectedNo} · Packets: ${total}`
                : `Packets: ${total}`;
        }
    },

    updatePacketCount(count, filtered) {
        const packets = document.getElementById('sb-packets');
        if (packets && this.selectedIndex === undefined) {
            packets.textContent = filtered ? `Filtered: ${count}` : `Packets: ${count}`;
        }
        const el = document.getElementById('event-count');
        if (el) el.textContent = `Events: ${count}`;
    },

    _setStatusFrame(text) {
        const frame = document.getElementById('sb-frame');
        if (frame) frame.textContent = text;
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
        const availableHubs = Array.isArray(status.hubs)
            ? status.hubs.filter(h => h.available).length : 0;

        // All checks pass — no warning needed
        if (status.installed && !status.accessDenied &&
            ((status.interfacesAvailable || 0) > 0 || availableHubs > 0)) {
            banner.classList.add('hidden');
            return;
        }

        if (status.accessDenied) {
            detail.textContent = '权限不足：USBPcap 设备无法打开（Access Denied）。请以管理员身份运行 USBPcapGUI.exe。';
            installBtn.disabled = true;
        } else if (!status.installed) {
            detail.textContent = status.installerFound
                ? '尚未检测到可用的 USBPcap 安装。请先安装 USBPcap，再点击刷新状态。'
                : '未找到随程序打包的 USBPcap 安装器，请手动安装后点击刷新状态。';
        } else if (status.virtualUsbDetected) {
            detail.textContent = '检测到虚拟 USB 环境（云电脑/USB over IP）。USBPcap 只支持真实物理 USB 主控器，无法在此环境中进行 USB 抓包。';
            installBtn.disabled = true;
        } else if (status.restartRecommended) {
            detail.textContent = 'USBPcap 已安装，但当前系统尚未暴露抓包接口。请先重启系统，或点击“重新扫描”尝试扫描 USB。';
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
        this._showToast(message, 'toast-error');
    },

    _showInfo(message) {
        this._showToast(message, 'toast-info');
    },

    _showToast(message, className) {
        const toast = document.createElement('div');
        toast.className = `toast ${className}`;
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

    _parseHexField(id, maxValue) {
        const value = (document.getElementById(id)?.value || '').trim();
        if (!value) return 0;
        const parsed = parseInt(value.replace(/^0x/i, ''), 16);
        if (!Number.isFinite(parsed) || parsed < 0 || parsed > maxValue) {
            throw new Error(`字段 ${id} 不是有效的十六进制值`);
        }
        return parsed;
    },

    _parseNumberField(id) {
        const value = Number(document.getElementById(id)?.value || 0);
        if (!Number.isFinite(value) || value < 0) {
            throw new Error(`字段 ${id} 不是有效数字`);
        }
        return value;
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
