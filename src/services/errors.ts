/**
 * Plain-language messages for error codes from the native layer. Native
 * messages can be technical, so they are never shown to people directly.
 */
const MESSAGES: Record<string, string> = {
  NOT_READY: 'The camera is still getting ready. Try again in a moment.',
  BUSY: 'Still finishing your last photo — one moment.',
  CAMERA_UNAVAILABLE: 'The camera is busy or unavailable. Close other camera apps and try again.',
  CAPTURE_FAILED: 'That photo didn’t come through. Please try again.',
  RAW_UNAVAILABLE: 'RAW isn’t available on this camera.',
  CANCELLED: 'Cancelled.',
  OUT_OF_MEMORY: 'Your phone needs a little more free memory for this. Close some apps and try again.',
  BUDGET_EXCEEDED: 'Your phone needs a little more free memory for this. Close some apps and try again.',
  UNSUPPORTED_FORMAT: 'This photo’s format isn’t supported.',
  IO_ERROR: 'The photo couldn’t be saved or opened. Check your free space and try again.',
  INVALID_ARGUMENT: 'Something about that request wasn’t right. Please try again.',
  ENGINE_UNAVAILABLE: 'Photo enhancements aren’t available on this device.',
  NO_ACTIVITY: 'Please bring Photo Plus to the front and try again.',
  SHARE_FAILED: 'Sharing didn’t work this time. Please try again.',
  PERMISSION_DENIED: 'Photo Plus needs permission for that.',
  WATERMARK_MISSING: 'This photo couldn’t be prepared for sharing. Please try again.',
};

export function friendlyMessage(
  code: string | undefined,
  fallback = 'Something went wrong. Please try again.',
): string {
  return (code && MESSAGES[code]) || fallback;
}
