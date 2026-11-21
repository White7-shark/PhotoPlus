/**
 * Download sizes for studio photos, by their short edge: 360p and 480p on
 * every plan; 720p, 1080p, HD (1440p) and 4K (2160p) with Premium.
 */
export const EXPORT_SIZES = [
  { id: '360p', label: '360p', px: 360, premium: false },
  { id: '480p', label: '480p', px: 480, premium: false },
  { id: '720p', label: '720p', px: 720, premium: true },
  { id: '1080p', label: '1080p', px: 1080, premium: true },
  { id: 'hd', label: 'HD', px: 1440, premium: true },
  { id: '4k', label: '4K', px: 2160, premium: true },
] as const;
export type ExportSizeId = (typeof EXPORT_SIZES)[number]['id'];

export const exportSize = (id: ExportSizeId) => EXPORT_SIZES.find(s => s.id === id) ?? EXPORT_SIZES[1];
/** The size a save starts at: the best the plan allows short of 4K. */
export const defaultExportSize = (premium: boolean): ExportSizeId => (premium ? '1080p' : '480p');
/** What the device is asked for (the short edge in pixels). */
export const exportPx = (id: ExportSizeId) => exportSize(id).px;
