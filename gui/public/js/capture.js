/**
 * USBPcapGUI - Capture Table Module
 * Virtual-scrolling high-performance capture table for 100K+ events.
 *
 * Architecture:
 *   - Maintains a flat array of ALL capture events (allEvents)
 *   - Maintains a filtered view (filteredEvents) via FilterEngine
 *   - Uses virtual scrolling: only renders visible rows
 *   - Row height is fixed (ROW_HEIGHT) for O(1) offset calculations
 */

const CaptureTable = {

    ROW_HEIGHT: 24,       // px per row
    OVERSCAN: 10,         // extra rows above/below viewport
    MAX_EVENTS: 200000,   // max events in memory

    allEvents: [],
    filteredEvents: [],
    filterConditions: [],
    selectedIndex: -1,    // index in filteredEvents
    autoScroll: true,
    capturing: false,

    // DOM references (set in init)
    _container: null,
    _scrollContent: null,
    _statusCount: null,
    _statusFiltered: null,
    _jumpBtn: null,
    _renderedRange: { start: -1, end: -1 },
    _rowPool: [],

    /**
     * Initialize the capture table
     */
    init() {
        this._container = document.getElementById('capture-table-body');
        this._statusCount = document.getElementById('event-count');
        this._statusFiltered = document.getElementById('filter-count');
        this._jumpBtn = document.getElementById('btn-jump-latest');

        // Jump-to-latest button
        if (this._jumpBtn) {
            this._jumpBtn.addEventListener('click', () => {
                this.autoScroll = true;
                this._jumpBtn.classList.add('hidden');
                this._scrollToBottom();
            });
        }

        // Create virtual scroll structure.
        // _scrollContent sits directly inside the scrollable _container — no
        // intermediate div, as any overflow:hidden wrapper would clip rows to 0.
        this._scrollContent = document.createElement('div');
        this._scrollContent.className = 'virtual-content';
        this._scrollContent.style.position = 'relative';

        this._container.innerHTML = '';
        this._container.appendChild(this._scrollContent);
        // Re-attach jump button (it was detached by innerHTML clear above)
        if (this._jumpBtn) this._container.appendChild(this._jumpBtn);

        // Scroll handler
        this._container.addEventListener('scroll', () => {
            this._onScroll();
        });

        // Click handler for row selection
        this._scrollContent.addEventListener('click', (e) => {
            const row = e.target.closest('.capture-row');
            if (row) {
                const idx = parseInt(row.dataset.index, 10);
                this.selectRow(idx);
            }
        });

        // Keyboard navigation
        this._container.addEventListener('keydown', (e) => {
            if (e.key === 'ArrowDown') { e.preventDefault(); this._moveSelection(1); }
            else if (e.key === 'ArrowUp') { e.preventDefault(); this._moveSelection(-1); }
            else if (e.key === 'PageDown') { e.preventDefault(); this._moveSelection(this._visibleCount()); }
            else if (e.key === 'PageUp') { e.preventDefault(); this._moveSelection(-this._visibleCount()); }
            else if (e.key === 'Home') { e.preventDefault(); this.selectRow(0); }
            else if (e.key === 'End') { e.preventDefault(); this.selectRow(this.filteredEvents.length - 1); }
        });

        this._container.tabIndex = 0;
        this._updateStatus();
    },

    /**
     * Add a single event (from WebSocket stream)
     */
    addEvent(event) {
        // Trim if over limit
        if (this.allEvents.length >= this.MAX_EVENTS) {
            const trimCount = Math.floor(this.MAX_EVENTS * 0.1);
            this.allEvents.splice(0, trimCount);
        }

        this.allEvents.push(event);

        // Check if it passes filter
        if (FilterEngine.matches(event, this.filterConditions)) {
            this.filteredEvents.push(event);
            this._updateScrollHeight();
            this._updateStatus();

            if (this.autoScroll) {
                this._scrollToBottom();
            } else {
                this._renderVisible();
            }
        }
    },

    /**
     * Add a batch of events
     */
    addEvents(events) {
        if (!events || events.length === 0) return;

        // Trim if over limit
        if (this.allEvents.length + events.length > this.MAX_EVENTS) {
            const trimCount = this.allEvents.length + events.length - this.MAX_EVENTS;
            this.allEvents.splice(0, trimCount);
        }

        this.allEvents.push(...events);

        const passing = events.filter(e => FilterEngine.matches(e, this.filterConditions));
        if (passing.length > 0) {
            this.filteredEvents.push(...passing);
            this._updateScrollHeight();
            this._updateStatus();

            if (this.autoScroll) {
                this._scrollToBottom();
            } else {
                this._renderVisible();
            }
        }
    },

    /**
     * Clear all events
     */
    clear() {
        this.allEvents = [];
        this.filteredEvents = [];
        this.selectedIndex = -1;
        this._renderedRange = { start: -1, end: -1 };
        this._scrollContent.innerHTML = '';
        this._scrollContent.style.height = '0px';
        this._rowPool = [];
        this._updateStatus();

        // Notify detail panel
        if (window.App && window.App.onEventSelected) {
            window.App.onEventSelected(null);
        }
    },

    /**
     * Set filter expression string (legacy / direct call)
     */
    setFilter(filterStr) {
        this.filterConditions = FilterEngine.parse(filterStr);
        this._refilter();
    },

    /**
     * Apply pre-parsed conditions directly (used by FilterPresets)
     */
    applyConditions(conditions, _rawStr) {
        this.filterConditions = conditions || [];
        this._refilter();
    },

    /**
     * Re-apply current filter to all events
     */
    _refilter() {
        this.filteredEvents = FilterEngine.apply(this.allEvents, this.filterConditions);
        this.selectedIndex = -1;
        this._renderedRange = { start: -1, end: -1 };
        this._scrollContent.innerHTML = '';
        this._rowPool = [];
        this._updateScrollHeight();
        this._renderVisible();
        this._updateStatus();
    },

    /**
     * Select a row by filtered index
     */
    selectRow(filteredIndex) {
        if (filteredIndex < 0 || filteredIndex >= this.filteredEvents.length) return;

        // Deselect previous
        if (this.selectedIndex >= 0) {
            const prevRow = this._scrollContent.querySelector(`[data-index="${this.selectedIndex}"]`);
            if (prevRow) prevRow.classList.remove('selected');
        }

        this.selectedIndex = filteredIndex;

        // Select new
        const newRow = this._scrollContent.querySelector(`[data-index="${filteredIndex}"]`);
        if (newRow) {
            newRow.classList.add('selected');
        }

        // Ensure visible
        this._scrollToRow(filteredIndex);

        // Notify detail panel and status bar
        const event = this.filteredEvents[filteredIndex];
        if (window.App && window.App.onEventSelected) {
            window.App.onEventSelected(event);
        }
        if (window.App && typeof App.updateStatusBar === 'function') {
            App.updateStatusBar(event, filteredIndex + 1, this.filteredEvents.length);
        }
    },

    // ---- Virtual Scroll Engine ----

    _visibleCount() {
        return Math.ceil(this._container.clientHeight / this.ROW_HEIGHT);
    },

    _updateScrollHeight() {
        const totalHeight = this.filteredEvents.length * this.ROW_HEIGHT;
        this._scrollContent.style.height = totalHeight + 'px';
    },

    _onScroll() {
        this.autoScroll = false;
        this._renderVisible();

        // Check if at bottom
        const scrollBottom = this._container.scrollTop + this._container.clientHeight;
        const atBottom = scrollBottom >= this._container.scrollHeight - 2;
        if (atBottom) this.autoScroll = true;

        // Show/hide jump-to-latest button
        if (this._jumpBtn) {
            this._jumpBtn.classList.toggle('hidden', atBottom || this.filteredEvents.length === 0);
        }
    },

    _scrollToBottom() {
        const totalHeight = this.filteredEvents.length * this.ROW_HEIGHT;
        this._container.scrollTop = totalHeight - this._container.clientHeight;
        this.autoScroll = true;
        if (this._jumpBtn) this._jumpBtn.classList.add('hidden');
        this._renderVisible();
    },

    _scrollToRow(index) {
        const rowTop = index * this.ROW_HEIGHT;
        const rowBottom = rowTop + this.ROW_HEIGHT;
        const viewTop = this._container.scrollTop;
        const viewBottom = viewTop + this._container.clientHeight;

        if (rowTop < viewTop) {
            this._container.scrollTop = rowTop;
        } else if (rowBottom > viewBottom) {
            this._container.scrollTop = rowBottom - this._container.clientHeight;
        }
        this._renderVisible();
    },

    _moveSelection(delta) {
        let newIdx = this.selectedIndex + delta;
        newIdx = Math.max(0, Math.min(newIdx, this.filteredEvents.length - 1));
        this.selectRow(newIdx);
    },

    _renderVisible() {
        const scrollTop = this._container.scrollTop;
        const containerHeight = this._container.clientHeight;

        let startIdx = Math.floor(scrollTop / this.ROW_HEIGHT) - this.OVERSCAN;
        let endIdx = Math.ceil((scrollTop + containerHeight) / this.ROW_HEIGHT) + this.OVERSCAN;

        startIdx = Math.max(0, startIdx);
        endIdx = Math.min(endIdx, this.filteredEvents.length - 1);

        if (startIdx === this._renderedRange.start && endIdx === this._renderedRange.end) {
            return; // No change
        }

        // Diff render: remove out-of-range rows, add new ones
        const frag = document.createDocumentFragment();
        const toRemove = [];

        // Remove rows outside new range
        for (const child of this._scrollContent.children) {
            const idx = parseInt(child.dataset.index, 10);
            if (isNaN(idx)) continue;
            if (idx < startIdx || idx > endIdx) {
                toRemove.push(child);
            }
        }
        for (const el of toRemove) {
            this._scrollContent.removeChild(el);
        }

        // Track which indices are rendered
        const rendered = new Set();
        for (const child of this._scrollContent.children) {
            const idx = parseInt(child.dataset.index, 10);
            if (!isNaN(idx)) rendered.add(idx);
        }

        // Create missing rows
        for (let i = startIdx; i <= endIdx; i++) {
            if (!rendered.has(i)) {
                const row = this._createRow(i);
                frag.appendChild(row);
            }
        }

        this._scrollContent.appendChild(frag);
        this._renderedRange = { start: startIdx, end: endIdx };
    },

    _createRow(index) {
        const event = this.filteredEvents[index];
        const row = document.createElement('div');
        row.className = 'capture-row';
        if (index === this.selectedIndex) row.classList.add('selected');
        row.dataset.index = index;
        row.style.position = 'absolute';
        row.style.top = (index * this.ROW_HEIGHT) + 'px';
        row.style.height = this.ROW_HEIGHT + 'px';
        row.style.width = '100%';
        row.style.display = 'flex';
        row.style.alignItems = 'center';

        // Direction-based Source / Destination
        // direction: '>>>' = host→device (DOWN/OUT), '<<<' = device→host (UP/IN)
        const isDown = (event.direction || '') === '>>>';
        // Prefer deviceName + VID:PID, fall back to event.device ("Bus N Dev M")
        let devName = event.device || '?';
        if (event.deviceName) {
            devName = event.vidHex && event.pidHex
                ? `${event.deviceName} (${event.vidHex}:${event.pidHex})`
                : event.deviceName;
        } else if (event.vidHex && event.pidHex) {
            devName = `${event.vidHex}:${event.pidHex}`;
        }
        const source = isDown ? 'host' : devName;
        const dest   = isDown ? devName : 'host';
        const arrow  = isDown
            ? '<span class="arrow-down">→</span>'
            : '<span class="arrow-up">←</span>';

        // Info column: command + summary + phase joined
        const infoParts = [event.command, event.summary].filter(Boolean);
        if (event.status && event.status !== 'OK') infoParts.push(event.status);
        const info = infoParts.join(' ');

        const proto = event.protocol || 'USB';
        const length = event.dataLength ?? '';
        const time = this._formatTime(event.timestamp);

        // Hex preview: no space between bytes, space every 8 bytes, max 32 bytes
        let hexPreview = '';
        if (event.data) {
            const raw = String(event.data).replace(/[^0-9a-fA-F]/g, '');
            if (raw.length) {
                const bytes = (raw.match(/.{1,2}/g) || []).map(b => b.toUpperCase());
                const preview = bytes.slice(0, 32);
                hexPreview = preview.reduce((acc, b, i) =>
                    acc + (i > 0 && i % 8 === 0 ? ' ' : '') + b, '');
                if (bytes.length > 32) hexPreview += '…';
            }
        }

        row.innerHTML = [
            `<span class="cell cell-seq">${this._esc(String(event.seq ?? index + 1))}</span>`,
            `<span class="cell cell-time">${this._esc(time)}</span>`,
            `<span class="cell cell-src">${arrow} ${this._esc(source)}</span>`,
            `<span class="cell cell-dst">${this._esc(dest)}</span>`,
            `<span class="cell cell-proto">${this._esc(proto)}</span>`,
            `<span class="cell cell-len">${this._esc(String(length))}</span>`,
            `<span class="cell cell-info">${this._esc(info)}</span>`,
            `<span class="cell cell-hex">${this._esc(hexPreview)}</span>`,
        ].join('');

        return row;
    },

    _esc(str) {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    },

    _truncData(data) {
        if (!data) return '';
        const clean = this._esc(data);
        if (clean.length > 48) return clean.substring(0, 48) + '...';
        return clean;
    },

    _formatTime(ts) {
        if (!ts) return '';
        if (typeof ts === 'number' && Number.isFinite(ts)) {
            const isMicroseconds = ts > 10_000_000_000_000;
            const millis = isMicroseconds ? Math.floor(ts / 1000) : ts;
            const d = new Date(millis);
            if (Number.isNaN(d.getTime())) return String(ts);
            const base = d.toISOString().substring(11, 23);
            if (!isMicroseconds) return base;
            return `${base}${String(ts % 1000).padStart(3, '0')}`;
        }
        return String(ts);
    },

    _updateStatus() {
        if (this._statusCount) {
            this._statusCount.textContent = `Events: ${this.allEvents.length}`;
        }
        if (this._statusFiltered) {
            if (this.filterConditions.length > 0) {
                this._statusFiltered.textContent = `Filtered: ${this.filteredEvents.length} / ${this.allEvents.length}`;
            } else {
                this._statusFiltered.textContent = '';
            }
        }
        // Update status bar packet counter
        if (window.App && typeof App.updatePacketCount === 'function') {
            App.updatePacketCount(this.filteredEvents.length, this.filterConditions.length > 0);
        }
    },

    /**
     * Get all filtered events (for export)
     */
    getFilteredEvents() {
        return this.filteredEvents;
    },

    /**
     * Get selected event
     */
    getSelectedEvent() {
        if (this.selectedIndex >= 0 && this.selectedIndex < this.filteredEvents.length) {
            return this.filteredEvents[this.selectedIndex];
        }
        return null;
    }
};

window.CaptureTable = CaptureTable;
