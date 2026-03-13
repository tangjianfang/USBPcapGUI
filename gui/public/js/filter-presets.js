/**
 * USBPcapGUI - Filter Presets & Chip Manager
 *
 * Responsibilities:
 *   1. Quick-filter chips: multi-select, each chip = one independent AND condition
 *   2. Manual filter input: independent text expression, also AND-combined with chips
 *   3. Device Filter panel: VID / PID / Class / SubClass / Bus+Addr filter
 *   4. Persist all filter settings to localStorage (key: usbpcap_filter_state)
 *
 * Architecture:
 *   chipConditions  = conditions from active chips (array of filter tokens)
 *   manualConditions = conditions from the text input
 *   deviceConditions = conditions from the device filter panel
 *
 *   Final filter = chipConditions ∪ manualConditions ∪ deviceConditions  (all AND)
 */

const FilterPresets = {

    STORAGE_KEY: 'usbpcap_filter_state',

    // Active chip filter tokens — multi-select, each is a filter string token
    _chipTokens: new Set(),
    // Manual text input 
    _manualText: '',
    // Device panel filter tokens
    _deviceTokens: [],

    // Cached parsed conditions (performance: avoid re-parsing on every row)
    _combinedConditions: [],

    init() {
        this._bindChips();
        this._bindManualInput();
        this._bindDevicePanel();
        this._loadFromStorage();
    },

    // ── Chips ──────────────────────────────────────────────────────────────

    _bindChips() {
        document.querySelectorAll('.qf-btn').forEach(btn => {
            btn.addEventListener('click', () => this._toggleChip(btn));
        });
    },

    _toggleChip(btn) {
        const token = btn.dataset.filter || '';
        if (!token) return;

        if (this._chipTokens.has(token)) {
            this._chipTokens.delete(token);
            btn.classList.remove('active');
        } else {
            this._chipTokens.add(token);
            btn.classList.add('active');
        }
        this._apply();
    },

    clearChips() {
        this._chipTokens.clear();
        document.querySelectorAll('.qf-btn').forEach(b => b.classList.remove('active'));
        this._apply();
    },

    // ── Manual Input ───────────────────────────────────────────────────────

    _bindManualInput() {
        const input = document.getElementById('filter-input');
        const btnApply = document.getElementById('btn-filter-apply');
        const btnClear = document.getElementById('btn-filter-clear');
        if (!input) return;

        let debounce = null;
        input.addEventListener('input', () => {
            clearTimeout(debounce);
            debounce = setTimeout(() => {
                this._manualText = input.value.trim();
                this._apply();
            }, 150);
        });

        input.addEventListener('keydown', e => {
            if (e.key === 'Escape') {
                input.value = '';
                this._manualText = '';
                this._apply();
                input.blur();
            } else if (e.key === 'Enter') {
                clearTimeout(debounce);
                this._manualText = input.value.trim();
                this._apply();
            }
        });

        if (btnApply) {
            btnApply.addEventListener('click', () => {
                this._manualText = input.value.trim();
                this._apply();
            });
        }

        if (btnClear) {
            btnClear.addEventListener('click', () => {
                input.value = '';
                this._manualText = '';
                this.clearChips();
                this.clearDevicePanel();
                this._apply();
            });
        }
    },

    // ── Device Filter Panel ────────────────────────────────────────────────

    _bindDevicePanel() {
        const panel = document.getElementById('device-filter-panel');
        if (!panel) return;

        panel.addEventListener('input', () => this._collectDevicePanel());
        panel.addEventListener('change', () => this._collectDevicePanel());

        const btnApply = document.getElementById('btn-devfilter-apply');
        if (btnApply) btnApply.addEventListener('click', () => {
            this._collectDevicePanel();
            this._apply();
            this._saveToStorage();
        });

        const btnClear = document.getElementById('btn-devfilter-clear');
        if (btnClear) btnClear.addEventListener('click', () => {
            this.clearDevicePanel();
        });
    },

    _collectDevicePanel() {
        const tokens = [];

        const get = id => {
            const el = document.getElementById(id);
            return el ? el.value.trim() : '';
        };

        const vid = get('devf-vid');
        const pid = get('devf-pid');
        const cls = get('devf-class');
        const sub = get('devf-subclass');
        const bus = get('devf-bus');
        const addr = get('devf-addr');
        const dir = get('devf-dir');

        if (vid) tokens.push(`vid:${vid}`);
        if (pid) tokens.push(`pid:${pid}`);
        if (cls) tokens.push(`class:${cls}`);
        if (sub) tokens.push(`subclass:${sub}`);
        if (bus && addr) tokens.push(`addr:${bus}:${addr}`);
        else if (bus) tokens.push(`bus:${bus}`);
        else if (addr) tokens.push(`addr:${addr}`);
        if (dir && dir !== '') tokens.push(`direction:${dir}`);

        this._deviceTokens = tokens;
        this._apply();
    },

    clearDevicePanel() {
        this._deviceTokens = [];
        ['devf-vid','devf-pid','devf-class','devf-subclass','devf-bus','devf-addr','devf-dir']
            .forEach(id => {
                const el = document.getElementById(id);
                if (el) el.value = '';
            });
        this._apply();
    },

    // ── Combined Apply ─────────────────────────────────────────────────────

    _apply() {
        // Build combined filter string: chips ∪ manualText ∪ devicePanel
        const parts = [];

        for (const tok of this._chipTokens) parts.push(tok);
        for (const tok of this._deviceTokens) parts.push(tok);
        if (this._manualText) parts.push(this._manualText);

        const combined = parts.join(' ');
        this._combinedConditions = FilterEngine.parse(combined);

        if (window.CaptureTable) {
            CaptureTable.applyConditions(this._combinedConditions, combined);
        }

        this._updateFilterSummary(combined);
        this._saveToStorage();
    },

    getConditions() {
        return this._combinedConditions;
    },

    // ── Filter Summary Badge ───────────────────────────────────────────────

    _updateFilterSummary(combined) {
        const badge = document.getElementById('filter-count');
        if (!badge) return;
        const count = this._combinedConditions.length;
        if (count === 0) {
            badge.textContent = '';
            badge.title = '';
        } else {
            badge.textContent = `${count} filter${count > 1 ? 's' : ''}`;
            badge.title = FilterEngine.describe(this._combinedConditions);
        }
    },

    // ── Persist to localStorage ────────────────────────────────────────────

    _saveToStorage() {
        try {
            const state = {
                chips: Array.from(this._chipTokens),
                manual: this._manualText,
                device: {
                    vid:      (document.getElementById('devf-vid')      || {}).value || '',
                    pid:      (document.getElementById('devf-pid')      || {}).value || '',
                    cls:      (document.getElementById('devf-class')    || {}).value || '',
                    sub:      (document.getElementById('devf-subclass') || {}).value || '',
                    bus:      (document.getElementById('devf-bus')      || {}).value || '',
                    addr:     (document.getElementById('devf-addr')     || {}).value || '',
                    dir:      (document.getElementById('devf-dir')      || {}).value || '',
                }
            };
            localStorage.setItem(this.STORAGE_KEY, JSON.stringify(state));
        } catch (e) {
            // storage might be unavailable
        }
    },

    _loadFromStorage() {
        try {
            const raw = localStorage.getItem(this.STORAGE_KEY);
            if (!raw) return;
            const state = JSON.parse(raw);

            // Restore chips
            if (Array.isArray(state.chips)) {
                state.chips.forEach(token => {
                    const btn = document.querySelector(`.qf-btn[data-filter="${CSS.escape(token)}"]`);
                    if (btn) {
                        this._chipTokens.add(token);
                        btn.classList.add('active');
                    }
                });
            }

            // Restore manual input
            if (state.manual) {
                const input = document.getElementById('filter-input');
                if (input) input.value = state.manual;
                this._manualText = state.manual;
            }

            // Restore device panel
            if (state.device) {
                const set = (id, v) => { const e = document.getElementById(id); if (e && v) e.value = v; };
                set('devf-vid',      state.device.vid);
                set('devf-pid',      state.device.pid);
                set('devf-class',    state.device.cls);
                set('devf-subclass', state.device.sub);
                set('devf-bus',      state.device.bus);
                set('devf-addr',     state.device.addr);
                set('devf-dir',      state.device.dir);
                this._collectDevicePanel();
            }

            this._apply();
        } catch (e) {
            // corrupted storage, ignore
        }
    },

    // ── Populate Device Filter from Sidebar Selection ──────────────────────

    /**
     * Called by App when user selects + clicks "Filter by this device" in device tree.
     * Pre-fills the device filter panel with known VID/PID/class.
     */
    fillFromDevice(dev) {
        const set = (id, v) => { const e = document.getElementById(id); if (e && v !== undefined && v !== null) e.value = v; };
        if (dev.vid) set('devf-vid', dev.vidHex || dev.vid.toString(16).toUpperCase().padStart(4,'0'));
        if (dev.pid) set('devf-pid', dev.pidHex || dev.pid.toString(16).toUpperCase().padStart(4,'0'));
        if (dev.class) set('devf-class', dev.class);
        if (dev.subClass) set('devf-subclass', dev.subClass);
        if (dev.bus)  set('devf-bus',  dev.bus);

        // Expand panel if collapsed
        const panel = document.getElementById('device-filter-panel');
        if (panel) panel.classList.remove('collapsed');

        this._collectDevicePanel();
    }
};

window.FilterPresets = FilterPresets;
