'use strict';
/**
 * USBPcapGUI - Rolling File Logger (Node.js)
 *
 * Intercepts console.log / console.warn / console.error and tees every message
 * to a rotating log file:
 *
 *   Logs/<name>.log          ← current file (≤ 5 MB)
 *   Logs/<name>.1.log        ← previous rotation
 *   Logs/<name>.2.log        ← oldest rotation
 *
 * Log directory: <package-root>/Logs/
 *   • When packaged  : <dist-folder>/Logs/  (server.js lives in <dist>/gui/)
 *   • In development : <repo-root>/Logs/    (server.js lives in <repo>/gui/)
 *
 * Usage (call once at server startup, before anything else):
 *   require('./logger').initLogger('server');
 */

const fs   = require('fs');
const path = require('path');

const MAX_BYTES = 5 * 1024 * 1024;  // 5 MB per file
const MAX_FILES = 3;                  // current + 2 rotations

// Log directory sits beside gui/ (one level up from __dirname)
const LOG_DIR = path.join(__dirname, '..', 'Logs');

let _logPath = null;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * Rotate log files if the current one has exceeded MAX_BYTES.
 *  name.log  → name.1.log  → name.2.log  (oldest is deleted)
 */
function _rotate(base) {
    try {
        if (fs.statSync(base).size < MAX_BYTES) return;
    } catch {
        return; // file doesn't exist yet — nothing to rotate
    }
    // Shift existing rotations upward: .2.log deleted, .1.log → .2.log, .log → .1.log
    for (let i = MAX_FILES - 1; i >= 1; i--) {
        const from = i === 1
            ? base
            : base.replace(/\.log$/, '.' + (i - 1) + '.log');
        const to = base.replace(/\.log$/, '.' + i + '.log');
        try { fs.renameSync(from, to); } catch { /* ignore */ }
    }
    // base is now gone (renamed to .1.log); a fresh .log will be created on next write
}

/**
 * Format a log line and append it to the current file (rotating first if needed).
 */
function _write(level, args) {
    if (!_logPath) return;
    try {
        _rotate(_logPath);
        const ts  = new Date().toISOString().replace('T', ' ').slice(0, 23);
        const msg = args
            .map(a => (a instanceof Error
                ? (a.stack || a.message)
                : typeof a === 'object' ? JSON.stringify(a) : String(a)))
            .join(' ');
        fs.appendFileSync(_logPath, `[${ts}] [${level.padEnd(5)}] ${msg}\n`, 'utf8');
    } catch { /* never crash on log failure */ }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * Initialise the file logger and patch console.log / .warn / .error.
 * @param {string} name  Base name for the log file, e.g. 'server'
 */
function initLogger(name) {
    try { fs.mkdirSync(LOG_DIR, { recursive: true }); } catch { /* ignore */ }

    _logPath = path.join(LOG_DIR, name + '.log');

    // Preserve original console methods
    const _origLog   = console.log.bind(console);
    const _origInfo  = console.info  ? console.info.bind(console)  : _origLog;
    const _origWarn  = console.warn.bind(console);
    const _origError = console.error.bind(console);

    // Tee: keep writing to stdout AND write to file
    console.log   = (...a) => { _origLog(...a);   _write('info',  a); };
    console.info  = (...a) => { _origInfo(...a);  _write('info',  a); };
    console.warn  = (...a) => { _origWarn(...a);  _write('warn',  a); };
    console.error = (...a) => { _origError(...a); _write('error', a); };

    // Capture uncaught errors so they land in the log file
    process.on('uncaughtException', (err) => {
        _write('fatal', [`Uncaught exception: ${err.stack || err.message}`]);
    });
    process.on('unhandledRejection', (reason) => {
        _write('error', [`Unhandled rejection: ${
            reason instanceof Error ? (reason.stack || reason.message) : String(reason)}`]);
    });

    _write('info', [`=== Logger initialised → ${_logPath} ===`]);
}

module.exports = { initLogger };
