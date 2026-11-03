/**
 * Lightweight structured logger with an in-memory ring buffer. The buffer
 * feeds the diagnostics screen so users can share recent errors with support
 * without any data leaving the device automatically.
 */
export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export interface LogEntry {
  timestamp: number;
  level: LogLevel;
  scope: string;
  message: string;
  data?: unknown;
}

const MAX_ENTRIES = 200;
const buffer: LogEntry[] = [];
const listeners = new Set<(entry: LogEntry) => void>();

function write(level: LogLevel, scope: string, message: string, data?: unknown) {
  const entry: LogEntry = { timestamp: Date.now(), level, scope, message, data };
  buffer.push(entry);
  if (buffer.length > MAX_ENTRIES) {
    buffer.splice(0, buffer.length - MAX_ENTRIES);
  }
  listeners.forEach(listener => listener(entry));
  if (__DEV__) {
    const line = `[${scope}] ${message}`;
    const sink = level === 'error' ? console.error : level === 'warn' ? console.warn : console.log;
    // Tests set this flag to keep output clean; logs are still buffered.
    if (!(globalThis as { __PP_SILENT_LOGS__?: boolean }).__PP_SILENT_LOGS__) {
      if (data === undefined) {
        sink(line);
      } else {
        sink(line, data);
      }
    }
  }
}

export function createLogger(scope: string) {
  return {
    debug: (message: string, data?: unknown) => write('debug', scope, message, data),
    info: (message: string, data?: unknown) => write('info', scope, message, data),
    warn: (message: string, data?: unknown) => write('warn', scope, message, data),
    error: (message: string, data?: unknown) => write('error', scope, message, data),
  };
}

export function getRecentLogs(level?: LogLevel): LogEntry[] {
  return level ? buffer.filter(e => e.level === level) : buffer.slice();
}

export function subscribeToLogs(listener: (entry: LogEntry) => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function clearLogs() {
  buffer.length = 0;
}
