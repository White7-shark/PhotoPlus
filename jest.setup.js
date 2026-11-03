/* global jest, globalThis */
// Keep test output clean; logs are still captured in the ring buffer.
globalThis.__PP_SILENT_LOGS__ = true;

jest.mock('react-native-safe-area-context', () => require('react-native-safe-area-context/jest/mock').default);
