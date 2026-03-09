/**
 * USBPcapGUI - Hex View Module
 * Formats binary data as a hex dump with offset, hex, and ASCII columns.
 */

const HexView = {
    BYTES_PER_LINE: 16,

    /**
     * Format hex data string into an HTML hex dump.
     * @param {string} hexString - Raw hex string (e.g., "48656c6c6f")
     * @returns {string} HTML content for the hex view
     */
    format(hexString) {
        if (!hexString) return '<span class="hex-offset">No data</span>';

        const bytes = [];
        for (let i = 0; i < hexString.length; i += 2) {
            bytes.push(parseInt(hexString.substr(i, 2), 16));
        }

        let html = '';
        for (let offset = 0; offset < bytes.length; offset += this.BYTES_PER_LINE) {
            // Offset column
            html += `<span class="hex-offset">${offset.toString(16).padStart(8, '0').toUpperCase()}</span>  `;

            // Hex bytes
            let hexPart = '';
            let asciiPart = '';

            for (let i = 0; i < this.BYTES_PER_LINE; i++) {
                if (offset + i < bytes.length) {
                    const byte = bytes[offset + i];
                    const isPrintable = byte >= 0x20 && byte < 0x7F;
                    const cls = isPrintable ? 'hex-byte' : 'hex-byte non-printable';

                    hexPart += `<span class="${cls}">${byte.toString(16).padStart(2, '0').toUpperCase()}</span> `;
                    asciiPart += isPrintable ? String.fromCharCode(byte) : '.';
                } else {
                    hexPart += '   ';
                    asciiPart += ' ';
                }

                // Gap between byte groups
                if (i === 7) hexPart += ' ';
            }

            html += hexPart;
            html += ` <span class="hex-ascii">|${this._escapeHtml(asciiPart)}|</span>\n`;
        }

        return html;
    },

    /**
     * Render hex view into a container element
     * @param {HTMLElement} container - Target element
     * @param {string} hexString - Raw hex data
     */
    render(container, hexString) {
        container.innerHTML = this.format(hexString);
    },

    /**
     * Clear the hex view
     */
    clear(container) {
        container.innerHTML = '<span class="hex-offset">Select an event to view data</span>';
    },

    _escapeHtml(str) {
        return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }
};

window.HexView = HexView;
