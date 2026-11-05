/**
 * Developer-only screens (device report, engine self-test) are hidden from
 * people using release builds, so the app never shows internal details.
 */
export const DEVELOPER_TOOLS: boolean = typeof __DEV__ !== 'undefined' && __DEV__;
