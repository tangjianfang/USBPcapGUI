/**
 * USBPcapGUI - Interface Options Dialog
 *
 * Wireshark-style capture configuration dialog shown before each capture.
 * Allows configuring snapshot length, buffer size, device filter, and
 * capture options. Settings persist to localStorage.
 *
 * Usage:
 *   InterfaceOptions.show(hubLabel, onStart)
 *   InterfaceOptions._onDevicesReceived(devices) — called by app.js routing
 */

const InterfaceOptions = {

    _STORAGE_KEY: 'bhplus.captureSettings',

    _DEFAULTS: {
        snapshotLen: 65535,
        bufferLen: 1048576,
        captureAll: true,
        captureNew: true,
        injectDescriptors: true,
        saveOnStart: true,
    },

    _hubLabel: '',
    _onStart: null,
    _pendingDeviceCallback: null,   // set when we're waiting for devices.list
    _lastDevices: [],               // cache for re-use without re-querying
    _treeExpanded: {},              // collapse state for io-device-tree nodes

    // ── Public API ──────────────────────────────────────────────────────────

    /**
     * Return the persisted capture settings (from localStorage) without
     * opening the dialog. Used by startCapture() for one-click start.
     */
    getSavedSettings() {
        let settings = { ...this._DEFAULTS };
        try {
            const raw = localStorage.getItem(this._STORAGE_KEY);
            if (raw) Object.assign(settings, JSON.parse(raw));
        } catch (_) { /* use defaults */ }
        return settings;
    },

    /**
     * Open the dialog.
     * @param {string} hubLabel  e.g. "USBPcap2: \\.\USBPcap2" (optional)
     * @param {function} onStart callback(params) when Start is clicked
     */
    show(hubLabel, onStart) {
        this._hubLabel = hubLabel || '';
        this._onStart  = onStart  || null;

        const dlg = document.getElementById('interface-options-dialog');
        if (!dlg) return;

        // Update title
        const titleEl = document.getElementById('io-title-text');
        if (titleEl) {
            titleEl.textContent = hubLabel
                ? `USBPcapGUI \u00b7 Interface Options: ${hubLabel}`
                : 'USBPcapGUI \u00b7 Interface Options';
        }

        // Load saved settings into form
        this._loadSettings();

        // Request device list (shows loading state immediately)
        this._requestDevices();

        dlg.showModal();
    },

    /**
     * Called by app.js _handleMessage when 'devices.list' arrives.
     * Runs any pending one-shot callback (for dialog population).
     */
    _onDevicesReceived(devices) {
        this._lastDevices = devices || [];
        if (typeof this._pendingDeviceCallback === 'function') {
            this._pendingDeviceCallback(this._lastDevices);
            this._pendingDeviceCallback = null;
        }
    },

    // ── Initialization ───────────────────────────────────────────────────────

    init() {
        const dlg = document.getElementById('interface-options-dialog');
        if (!dlg) return;

        // Close button
        document.getElementById('io-btn-close')?.addEventListener('click', () => this._discard());

        // Tab switching (only Default tab for now)
        dlg.querySelectorAll('.io-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                dlg.querySelectorAll('.io-tab').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');
            });
        });

        // Reset buttons
        document.getElementById('io-reset-snaplen')?.addEventListener('click', () => {
            this._setField('io-snaplen', this._DEFAULTS.snapshotLen);
        });
        document.getElementById('io-reset-buflen')?.addEventListener('click', () => {
            this._setField('io-buflen', this._DEFAULTS.bufferLen);
        });

        // Refresh devices in tree
        document.getElementById('io-btn-refresh-devices')?.addEventListener('click', () => {
            this._requestDevices(true);
        });

        // Validate inputs on change (positive integers only)
        ['io-snaplen', 'io-buflen'].forEach(id => {
            document.getElementById(id)?.addEventListener('input', (e) => {
                this._validateInput(e.target);
            });
        });

        // Footer buttons
        document.getElementById('io-btn-restore')?.addEventListener('click',  () => this._restoreDefaults());
        document.getElementById('io-btn-start')?.addEventListener('click',    () => this._doStart());
        document.getElementById('io-btn-save')?.addEventListener('click',     () => this._saveSettings());
        document.getElementById('io-btn-discard')?.addEventListener('click',  () => this._discard());
        document.getElementById('io-btn-help')?.addEventListener('click',     () => this._showHelp());

        // Close on backdrop click
        dlg.addEventListener('click', (e) => {
            if (e.target === dlg) this._discard();
        });
    },

    // ── Settings persistence ─────────────────────────────────────────────────

    _loadSettings() {
        let settings = { ...this._DEFAULTS };
        try {
            const raw = localStorage.getItem(this._STORAGE_KEY);
            if (raw) Object.assign(settings, JSON.parse(raw));
        } catch (_) { /* use defaults */ }

        this._setField('io-snaplen', settings.snapshotLen);
        this._setField('io-buflen',  settings.bufferLen);
        this._setChecked('io-chk-capture-all',     settings.captureAll);
        this._setChecked('io-chk-capture-new',     settings.captureNew);
        this._setChecked('io-chk-inject-desc',     settings.injectDescriptors);
        this._setChecked('io-chk-save-on-start',   settings.saveOnStart);
    },

    _saveSettings() {
        if (!this._allValid()) return;
        const settings = this._collectSettings();
        try {
            localStorage.setItem(this._STORAGE_KEY, JSON.stringify(settings));
        } catch (_) {}
        App._showInfo('Capture settings saved.');
    },

    _restoreDefaults() {
        this._setField('io-snaplen', this._DEFAULTS.snapshotLen);
        this._setField('io-buflen',  this._DEFAULTS.bufferLen);
        this._setChecked('io-chk-capture-all',   this._DEFAULTS.captureAll);
        this._setChecked('io-chk-capture-new',   this._DEFAULTS.captureNew);
        this._setChecked('io-chk-inject-desc',   this._DEFAULTS.injectDescriptors);
        this._setChecked('io-chk-save-on-start', this._DEFAULTS.saveOnStart);
    },

    _collectSettings() {
        return {
            snapshotLen:       this._getInt('io-snaplen', this._DEFAULTS.snapshotLen),
            bufferLen:         this._getInt('io-buflen',  this._DEFAULTS.bufferLen),
            captureAll:        this._getChecked('io-chk-capture-all'),
            captureNew:        this._getChecked('io-chk-capture-new'),
            injectDescriptors: this._getChecked('io-chk-inject-desc'),
            saveOnStart:       this._getChecked('io-chk-save-on-start'),
        };
    },

    // ── Dialog actions ───────────────────────────────────────────────────────

    _doStart() {
        if (!this._allValid()) {
            App._showError('Please correct the highlighted fields before starting.');
            return;
        }
        const settings = this._collectSettings();

        if (settings.saveOnStart) {
            try { localStorage.setItem(this._STORAGE_KEY, JSON.stringify(settings)); } catch (_) {}
        }

        const dlg = document.getElementById('interface-options-dialog');
        if (dlg) dlg.close();

        if (typeof this._onStart === 'function') {
            // Build start params — deviceIds from selected/filtered devices
            const params = {
                snapshotLen:       settings.snapshotLen,
                bufferLen:         settings.bufferLen,
                captureAll:        settings.captureAll,
                captureNew:        settings.captureNew,
                injectDescriptors: settings.injectDescriptors,
                deviceIds: [],
                filterBus: 0,
            };
            this._onStart(params);
        }
    },

    _discard() {
        const dlg = document.getElementById('interface-options-dialog');
        if (dlg) dlg.close();
    },

    _showHelp() {
        App._showInfo('Snapshot length: max bytes per packet (65535 = capture full packet). Buffer length: kernel ring buffer size in bytes.');
    },

    // ── Device Tree ──────────────────────────────────────────────────────────

    _requestDevices(forceReFetch) {
        const container = document.getElementById('io-device-tree');
        if (!container) return;

        // Show loading state
        container.innerHTML = '<div class="io-devtree-loading">&#8635; Loading devices\u2026</div>';

        if (!forceReFetch && this._lastDevices.length > 0) {
            // Use cached data immediately
            this._renderDeviceTree(this._lastDevices);
            return;
        }

        // Register one-shot callback, then request
        this._pendingDeviceCallback = (devices) => {
            this._renderDeviceTree(devices);
        };

        if (window.bhWs && window.bhWs.isConnected()) {
            window.bhWs.send('devices.enumerate');
        } else {
            container.innerHTML = '<div class="io-devtree-loading">Not connected to capture service.</div>';
            this._pendingDeviceCallback = null;
        }
    },

    _renderDeviceTree(devices) {
        const container = document.getElementById('io-device-tree');
        if (!container) return;
        container.innerHTML = '';

        if (!devices || devices.length === 0) {
            container.innerHTML = '<div class="io-devtree-loading">No USB devices found.</div>';
            return;
        }

        // Group USB devices by bus/hub, non-USB first
        const usbDevices = devices.filter(d => !d.busType || d.busType === 'USB');
        const otherDevices = devices.filter(d => d.busType && d.busType !== 'USB');

        // USB group — show with hub grouping if hub index available
        const hubGroups = {};
        for (const dev of usbDevices) {
            const hub = dev.hub !== undefined ? `USBPcap${dev.hub}` : 'USB';
            if (!hubGroups[hub]) hubGroups[hub] = [];
            hubGroups[hub].push(dev);
        }

        // If no hub grouping possible, show flat
        const hubKeys = Object.keys(hubGroups).sort();
        if (hubKeys.length > 0) {
            for (const hubKey of hubKeys) {
                const group = this._makeTreeGroup('\ud83d\udcf6 ' + hubKey, hubGroups[hubKey]);
                container.appendChild(group);
            }
        }

        for (const dev of otherDevices) {
            const row = this._makeTreeLeaf('\ud83d\udcbb ' + (dev.name || `Device ${dev.id || ''}`));
            container.appendChild(row);
        }
    },

    _makeTreeGroup(label, devices) {
        const wrapper = document.createElement('div');

        const header = document.createElement('div');
        header.className = 'io-tree-node';

        const toggle = document.createElement('span');
        toggle.className = 'io-toggle';
        toggle.textContent = '\u25be';  // ▾ expanded

        const nameSpan = document.createElement('span');
        nameSpan.textContent = label;

        header.appendChild(toggle);
        header.appendChild(nameSpan);
        wrapper.appendChild(header);

        const children = document.createElement('div');
        children.className = 'io-tree-children';

        for (const dev of devices) {
            if (dev.children && dev.children.length > 0) {
                const sub = this._makeTreeGroup(
                    '\ud83d\udd0c ' + (dev.name || `Device ${dev.id || ''}`),
                    dev.children
                );
                children.appendChild(sub);
            } else {
                const leaf = this._makeTreeLeaf(
                    '\ud83d\udd0c ' + (dev.name || `Device ${dev.id || ''}`),
                    dev.vid, dev.pid
                );
                children.appendChild(leaf);
            }
        }

        wrapper.appendChild(children);

        toggle.addEventListener('click', (e) => {
            e.stopPropagation();
            const collapsed = children.classList.toggle('collapsed');
            toggle.textContent = collapsed ? '\u25b8' : '\u25be';  // ▸ / ▾
        });
        header.addEventListener('click', () => toggle.click());

        return wrapper;
    },

    _makeTreeLeaf(label, vid, pid) {
        const row = document.createElement('div');
        row.className = 'io-tree-node';
        row.style.paddingLeft = '24px';

        let text = label;
        if (vid !== undefined && pid !== undefined) {
            text += ` [${(vid >>> 0).toString(16).padStart(4,'0')}:${(pid >>> 0).toString(16).padStart(4,'0')}]`;
        }

        row.innerHTML = `<span class="io-toggle" style="visibility:hidden">&#x25be;</span>${this._esc(text)}`;
        return row;
    },

    // ── Validation helpers ───────────────────────────────────────────────────

    _validateInput(input) {
        const val = parseInt(input.value, 10);
        const valid = Number.isFinite(val) && val >= 1;
        input.classList.toggle('invalid', !valid);
        return valid;
    },

    _allValid() {
        let ok = true;
        ['io-snaplen', 'io-buflen'].forEach(id => {
            const el = document.getElementById(id);
            if (el && !this._validateInput(el)) ok = false;
        });
        return ok;
    },

    _getInt(id, fallback) {
        const val = parseInt(document.getElementById(id)?.value || '', 10);
        return Number.isFinite(val) && val >= 1 ? val : fallback;
    },

    _getChecked(id) {
        return !!(document.getElementById(id)?.checked);
    },

    _setField(id, value) {
        const el = document.getElementById(id);
        if (el) { el.value = value; el.classList.remove('invalid'); }
    },

    _setChecked(id, checked) {
        const el = document.getElementById(id);
        if (el) el.checked = !!checked;
    },

    _esc(str) {
        const d = document.createElement('div');
        d.textContent = String(str);
        return d.innerHTML;
    },
};

window.InterfaceOptions = InterfaceOptions;
