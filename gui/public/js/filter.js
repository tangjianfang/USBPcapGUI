/**
 * USBPcapGUI - Filter Engine
 * Parses filter expressions and applies them to capture events.
 *
 * Filter syntax (space-separated, all conditions are AND):
 *   protocol:USB           - Filter by protocol
 *   device:Storage          - Filter by device name (substring match)
 *   command:GET_DESCRIPTOR  - Filter by command (substring / regex)
 *   status:OK              - Filter by status
 *   status:!STALL          - Negative filter (exclude STALL)
 *   dir:<<<                - Filter by direction (in/out/>>>/<<<)
 *   len:>100               - Data length > 100
 *   len:<512               - Data length < 512
 *   len:64                 - Data length == 64
 *   data:ff01              - Match hex pattern in data
 *   deviceId:2             - Filter by device ID
 *   seq:>1000              - Filter by sequence number
 *   vid:046D               - Filter by Vendor ID (hex, case-insensitive)
 *   pid:C52B               - Filter by Product ID (hex, case-insensitive)
 *   class:3                - Filter by USB device class (decimal or hex 0x03)
 *   subclass:1             - Filter by USB device subclass
 *   addr:5                 - Filter by USB device address (bus:addr or just addr)
 *   bus:1                  - Filter by USB bus number
 *   "free text"            - Search across all fields
 */

const FilterEngine = {

    /**
     * Parse a filter string into structured conditions
     * @param {string} filterStr - Filter expression
     * @returns {object[]} Array of filter conditions
     */
    parse(filterStr) {
        if (!filterStr || !filterStr.trim()) return [];

        const conditions = [];
        // Match quoted strings and key:value pairs
        const tokens = filterStr.match(/(?:[^\s"]+|"[^"]*")+/g) || [];

        for (const token of tokens) {
            const colonIdx = token.indexOf(':');
            if (colonIdx > 0) {
                const key = token.substring(0, colonIdx).toLowerCase();
                let value = token.substring(colonIdx + 1);
                let negate = false;

                if (value.startsWith('!')) {
                    negate = true;
                    value = value.substring(1);
                }

                // Remove quotes
                value = value.replace(/^"|"$/g, '');

                // Numeric comparisons
                let op = '=';
                if (value.startsWith('>')) { op = '>'; value = value.substring(1); }
                else if (value.startsWith('<')) { op = '<'; value = value.substring(1); }

                conditions.push({ key, value, negate, op });
            } else {
                // Free text search
                const text = token.replace(/^"|"$/g, '');
                conditions.push({ key: '_text', value: text, negate: false, op: '=' });
            }
        }

        return conditions;
    },

    /**
     * Test if an event matches all conditions
     * @param {object} event - Capture event
     * @param {object[]} conditions - Parsed filter conditions
     * @returns {boolean}
     */
    matches(event, conditions) {
        if (!conditions || conditions.length === 0) return true;

        for (const cond of conditions) {
            let match;

            switch (cond.key) {
                case 'protocol':
                case 'proto':
                    match = this._strMatch(event.protocol, cond.value);
                    break;
                case 'device':
                    match = this._strMatch(event.device, cond.value);
                    break;
                case 'command':
                case 'cmd':
                    match = this._strMatch(event.command, cond.value);
                    break;
                case 'status':
                    match = this._strMatch(event.status, cond.value);
                    break;
                case 'dir':
                case 'direction':
                    match = this._dirMatch(event.direction, cond.value);
                    break;
                case 'len':
                case 'length':
                    match = this._numMatch(event.dataLength, cond.value, cond.op);
                    break;
                case 'data':
                    match = event.data && event.data.toLowerCase().includes(cond.value.toLowerCase());
                    break;
                case 'deviceid':
                    match = this._numMatch(event.deviceId, cond.value, cond.op);
                    break;
                case 'seq':
                    match = this._numMatch(event.seq, cond.value, cond.op);
                    break;

                // ── USB Device Identity Filters ──
                case 'vid':
                    match = this._vidPidMatch(event, 'vid', cond.value);
                    break;
                case 'pid':
                    match = this._vidPidMatch(event, 'pid', cond.value);
                    break;
                case 'class':
                case 'deviceclass':
                    match = this._deviceClassMatch(event, 'class', cond.value);
                    break;
                case 'subclass':
                case 'devicesubclass':
                    match = this._deviceClassMatch(event, 'subClass', cond.value);
                    break;
                case 'protocol_class':
                case 'deviceprotocol':
                    match = this._deviceClassMatch(event, 'protocol', cond.value);
                    break;
                case 'addr':
                case 'address':
                    // addr:BUS:ADDR or addr:ADDR
                    match = this._addrMatch(event, cond.value);
                    break;
                case 'bus':
                    match = this._numMatch(event.bus, cond.value, cond.op);
                    break;

                case '_text':
                    match = this._textSearch(event, cond.value);
                    break;
                default:
                    match = true; // Unknown field, skip
            }

            if (cond.negate) match = !match;
            if (!match) return false; // AND logic: all must match
        }

        return true;
    },

    /**
     * Apply parsed filter to an array of events
     */
    apply(events, conditions) {
        if (!conditions || conditions.length === 0) return events;
        return events.filter(e => this.matches(e, conditions));
    },

    // ── Internal helpers ──

    _strMatch(fieldValue, pattern) {
        if (!fieldValue) return false;
        try {
            const re = new RegExp(pattern, 'i');
            return re.test(fieldValue);
        } catch {
            return fieldValue.toLowerCase().includes(pattern.toLowerCase());
        }
    },

    _dirMatch(fieldValue, pattern) {
        const p = pattern.toLowerCase();
        if (p === 'in' || p === '<<<') return fieldValue === '<<<';
        if (p === 'out' || p === '>>>') return fieldValue === '>>>';
        return this._strMatch(fieldValue, pattern);
    },

    _numMatch(fieldValue, pattern, op) {
        const num = parseFloat(pattern);
        if (isNaN(num)) return false;
        if (op === '>') return fieldValue > num;
        if (op === '<') return fieldValue < num;
        return fieldValue === num;
    },

    /**
     * VID/PID match: first try event.vid/pid (direct), then lookup via DeviceTree by bus+device address.
     * Value can be decimal (1133) or hex (046D / 0x046D).
     */
    _vidPidMatch(event, field, value) {
        const target = this._parseHexOrDec(value);
        if (target === null) return false;

        // 1) Try direct fields on event (if C++ emits them)
        if (event[field] !== undefined && event[field] !== null) {
            return event[field] === target;
        }

        // 2) Resolve via DeviceTree: match bus + device address
        const devInfo = this._resolveDeviceInfo(event);
        if (!devInfo) return false;
        return devInfo[field] === target;
    },

    /**
     * DeviceClass/subClass/protocol match via event fields or DeviceTree lookup.
     */
    _deviceClassMatch(event, field, value) {
        const target = this._parseHexOrDec(value);
        if (target === null) return false;

        if (event[field] !== undefined && event[field] !== null) {
            return event[field] === target;
        }

        const devInfo = this._resolveDeviceInfo(event);
        if (!devInfo) return false;
        return devInfo[field] === target;
    },

    /**
     * addr:BUS:ADDR or addr:ADDR — match event.bus / event.device
     */
    _addrMatch(event, value) {
        if (value.includes(':')) {
            const parts = value.split(':');
            const bus = parseInt(parts[0], 10);
            const addr = parseInt(parts[1], 10);
            return event.bus === bus && (event.device === addr || event.device === String(addr));
        }
        const addr = parseInt(value, 10);
        if (isNaN(addr)) return false;
        return event.device === addr || event.device === String(addr);
    },

    /**
     * Parse a hex (046D / 0x046D) or decimal (1133) string to integer.
     */
    _parseHexOrDec(value) {
        if (!value) return null;
        const s = value.trim();
        if (/^0x/i.test(s)) return parseInt(s, 16);
        if (/^[0-9a-fA-F]{4}$/.test(s)) return parseInt(s, 16); // bare 4-digit hex
        const n = parseInt(s, 10);
        return isNaN(n) ? null : n;
    },

    /**
     * Find a device entry from DeviceTree that matches event.bus + event.device.
     * Returns the device object (with vid/pid/class/subClass/protocol) or null.
     */
    _resolveDeviceInfo(event) {
        if (!window.DeviceTree || !Array.isArray(DeviceTree._devices)) return null;
        const bus = typeof event.bus === 'number' ? event.bus : parseInt(event.bus, 10);
        const dev = typeof event.device === 'number' ? event.device : parseInt(event.device, 10);
        if (isNaN(bus) || isNaN(dev)) return null;
        return DeviceTree._devices.find(d => d.bus === bus && d.device === dev) || null;
    },

    _textSearch(event, text) {
        const lower = text.toLowerCase();
        return (
            (event.protocol && event.protocol.toLowerCase().includes(lower)) ||
            (event.device && String(event.device).toLowerCase().includes(lower)) ||
            (event.command && event.command.toLowerCase().includes(lower)) ||
            (event.status && event.status.toLowerCase().includes(lower)) ||
            (event.data && event.data.toLowerCase().includes(lower))
        );
    },

    /**
     * Get a human-readable description of the current filter
     */
    describe(conditions) {
        if (!conditions || conditions.length === 0) return '';
        return conditions.map(c => {
            const neg = c.negate ? 'NOT ' : '';
            if (c.key === '_text') return `"${c.value}"`;
            return `${neg}${c.key}${c.op === '>' ? '>' : c.op === '<' ? '<' : ':'}${c.value}`;
        }).join(' AND ');
    }
};

window.FilterEngine = FilterEngine;
