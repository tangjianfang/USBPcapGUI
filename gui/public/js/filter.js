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
 *   dir:<<<                - Filter by direction
 *   dir:in                 - Alias: in = <<<, out = >>>
 *   len:>100               - Data length > 100
 *   len:<512               - Data length < 512
 *   len:64                 - Data length == 64
 *   data:ff01              - Match hex pattern in data
 *   deviceId:2             - Filter by device ID
 *   seq:>1000              - Filter by sequence number
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

    _textSearch(event, text) {
        const lower = text.toLowerCase();
        return (
            (event.protocol && event.protocol.toLowerCase().includes(lower)) ||
            (event.device && event.device.toLowerCase().includes(lower)) ||
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
