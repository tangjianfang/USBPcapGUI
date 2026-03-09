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
    _viewport: null,
    _statusCount: null,
    _statusFiltered: null,
    _renderedRange: { start: -1, end: -1 },
    _rowPool: [],

    /**
     * Initialize the capture table
     */
    init() {
        this._container = document.getElementById('capture-table-body');
        this._statusCount = document.getElementById('event-count');
        this._statusFiltered = document.getElementById('filtered-count');

        // Create virtual scroll structure
        this._viewport = document.createElement('div');
        this._viewport.className = 'virtual-viewport';
        this._viewport.style.position = 'relative';
        this._viewport.style.overflow = 'hidden';

        this._scrollContent = document.createElement('div');
        this._scrollContent.className = 'virtual-content';
        this._scrollContent.style.position = 'relative';

        this._viewport.appendChild(this._scrollContent);
        this._container.innerHTML = '';
        this._container.appendChild(this._viewport);

        // Scroll handler
        this._container.addEventListener('scroll', () => {
            this._onScroll();
        });

        // Click handler for row selection
        this._viewport.addEventListener('click', (e) => {
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
     * Set filter expression string
     */
    setFilter(filterStr) {
        this.filterConditions = FilterEngine.parse(filterStr);
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

        // Notify detail panel
        const event = this.filteredEvents[filteredIndex];
        if (window.App && window.App.onEventSelected) {
            window.App.onEventSelected(event);
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
        if (scrollBottom >= this._container.scrollHeight - 2) {
            this.autoScroll = true;
        }
    },

    _scrollToBottom() {
        const totalHeight = this.filteredEvents.length * this.ROW_HEIGHT;
        this._container.scrollTop = totalHeight - this._container.clientHeight;
        this.autoScroll = true;
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

        const proto = event.protocol || '';
        row.dataset.protocol = proto.toLowerCase();

        row.innerHTML = `
            <span class="cell cell-seq">${event.seq ?? ''}</span>
            <span class="cell cell-device">${this._esc(event.device || '')}</span>
            <span class="cell cell-protocol">${this._esc(proto)}</span>
            <span class="cell cell-phase">${this._esc(event.phase || '')}</span>
            <span class="cell cell-direction">${this._esc(event.direction || '')}</span>
            <span class="cell cell-data">${this._truncData(event.data)}</span>
            <span class="cell cell-command">${this._esc(event.command || '')}</span>
            <span class="cell cell-status">${this._esc(event.status || '')}</span>
            <span class="cell cell-timestamp">${this._formatTime(event.timestamp)}</span>
        `;

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
        if (typeof ts === 'number') {
            const d = new Date(ts);
            return d.toISOString().substring(11, 23); // HH:MM:SS.mmm
        }
        return String(ts);
    },

    _updateStatus() {
        if (this._statusCount) {
            this._statusCount.textContent = this.allEvents.length;
        }
        if (this._statusFiltered) {
            if (this.filterConditions.length > 0) {
                this._statusFiltered.textContent = `(${this.filteredEvents.length} shown)`;
                this._statusFiltered.style.display = '';
            } else {
                this._statusFiltered.style.display = 'none';
            }
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
