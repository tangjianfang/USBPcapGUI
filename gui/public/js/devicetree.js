/**
 * USBPcapGUI - Device Tree Module
 * Renders a hierarchical device tree in the sidebar.
 * Groups devices by bus type (USB, NVMe, SCSI, ATA, Serial, etc.)
 */

const DeviceTree = {

    _container: null,
    _devices: [],
    _selectedDeviceId: null,

    /**
     * Initialize the device tree
     */
    init() {
        this._container = document.getElementById('device-tree');
        this._container.innerHTML = '<div class="device-placeholder">Connecting...</div>';
    },

    /**
     * Request device enumeration from core
     */
    enumerate() {
        if (window.bhWs && window.bhWs.isConnected()) {
            window.bhWs.send('devices.enumerate');
        }
    },

    /**
     * Update the tree with a new device list
     * @param {object[]} devices - Array of device objects { id, name, bus, vid, pid, serial, driver, path, children? }
     */
    update(devices) {
        this._devices = (devices || []).map(dev => ({
            ...dev,
            id: dev.id ?? (((dev.bus || 0) << 16) | (dev.device || 0)),
            busType: dev.busType || 'USB'
        }));
        this._render();
    },

    getSelectedDevice() {
        return this._devices.find(d => d.id === this._selectedDeviceId) || null;
    },

    /**
     * Get the currently selected device ID (for filtering)
     */
    getSelectedDeviceId() {
        return this._selectedDeviceId;
    },

    /**
     * Clear selection
     */
    clearSelection() {
        this._selectedDeviceId = null;
        this._container.querySelectorAll('.device-item.selected').forEach(el => {
            el.classList.remove('selected');
        });
    },

    /**
     * Render the device tree
     */
    _render() {
        this._container.innerHTML = '';

        if (this._devices.length === 0) {
            this._container.innerHTML = '<div class="device-placeholder">No devices found</div>';
            return;
        }

        // Group by bus type
        const groups = {};
        for (const dev of this._devices) {
            const bus = dev.busType || 'Other';
            if (!groups[bus]) groups[bus] = [];
            groups[bus].push(dev);
        }

        // Sort bus groups
        const busOrder = ['USB', 'NVMe', 'SCSI', 'SATA', 'ATA', 'Serial', 'Other'];
        const sortedBuses = Object.keys(groups).sort((a, b) => {
            const ia = busOrder.indexOf(a);
            const ib = busOrder.indexOf(b);
            return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
        });

        for (const bus of sortedBuses) {
            const group = this._createGroup(bus, groups[bus]);
            this._container.appendChild(group);
        }
    },

    _createGroup(busName, devices) {
        const group = document.createElement('div');
        group.className = 'device-group';

        const header = document.createElement('div');
        header.className = 'device-group-header';
        header.innerHTML = `
            <span class="device-group-toggle">&#9660;</span>
            <span class="device-group-icon">${this._busIcon(busName)}</span>
            <span class="device-group-name">${busName}</span>
            <span class="device-group-count">${devices.length}</span>
        `;

        let collapsed = false;
        header.addEventListener('click', () => {
            collapsed = !collapsed;
            list.style.display = collapsed ? 'none' : '';
            header.querySelector('.device-group-toggle').innerHTML = collapsed ? '&#9654;' : '&#9660;';
        });

        const list = document.createElement('div');
        list.className = 'device-group-list';

        for (const dev of devices) {
            const item = this._createItem(dev);
            list.appendChild(item);
        }

        group.appendChild(header);
        group.appendChild(list);
        return group;
    },

    _createItem(dev) {
        const item = document.createElement('div');
        item.className = 'device-item';
        item.dataset.deviceId = dev.id;

        if (dev.id === this._selectedDeviceId) {
            item.classList.add('selected');
        }

        const name = dev.name || `Device ${dev.id}`;
        const info = [];
        if (dev.vid && dev.pid) info.push(`VID:${dev.vid} PID:${dev.pid}`);
        if (dev.serial) info.push(`S/N:${dev.serial}`);
        if (dev.driver) info.push(dev.driver);

        item.innerHTML = `
            <span class="device-item-icon">${this._deviceIcon(dev)}</span>
            <span class="device-item-info">
                <span class="device-item-name" title="${this._esc(name)}">${this._esc(name)}</span>
                ${info.length ? '<span class="device-item-detail">' + this._esc(info.join(' | ')) + '</span>' : ''}
            </span>
        `;

        item.addEventListener('click', (e) => {
            e.stopPropagation();
            this._onSelect(dev.id, item);
        });

        item.addEventListener('dblclick', (e) => {
            e.stopPropagation();
            // Double-click to filter capture by this device
            if (window.App && window.App.filterByDevice) {
                window.App.filterByDevice(dev.id, dev.name);
            }
        });

        return item;
    },

    _onSelect(deviceId, itemEl) {
        // Toggle selection
        if (this._selectedDeviceId === deviceId) {
            this.clearSelection();
            return;
        }

        this.clearSelection();
        this._selectedDeviceId = deviceId;
        itemEl.classList.add('selected');

        if (window.App && window.App.onDeviceSelected) {
            const dev = this._devices.find(d => d.id === deviceId);
            window.App.onDeviceSelected(dev);
        }
    },

    _busIcon(bus) {
        const icons = {
            'USB': '🔌',
            'NVMe': '💾',
            'SCSI': '🗄️',
            'SATA': '💿',
            'ATA': '💿',
            'Serial': '📡',
        };
        return icons[bus] || '📟';
    },

    _deviceIcon(dev) {
        const bus = (dev.bus || '').toLowerCase();
        if (bus === 'usb') {
            if (dev.name && /storage|disk|mass/i.test(dev.name)) return '💾';
            if (dev.name && /keyboard|hid/i.test(dev.name)) return '⌨️';
            if (dev.name && /mouse/i.test(dev.name)) return '🖱️';
            if (dev.name && /audio|speaker/i.test(dev.name)) return '🔊';
            if (dev.name && /camera|video/i.test(dev.name)) return '📷';
            if (dev.name && /network|ethernet|wifi/i.test(dev.name)) return '🌐';
            return '🔌';
        }
        return '📟';
    },

    _esc(str) {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    }
};

window.DeviceTree = DeviceTree;
