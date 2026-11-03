import type { EditOperation, ProcessRequest } from './types';

export const MAX_OPERATIONS = 64;

/**
 * Client-side validation mirrors the native checks so the UI can explain
 * problems before a job is queued. Native code re-validates regardless.
 */
export function validateOperation(op: EditOperation, index: number): string | null {
  const finite = (...values: (number | undefined)[]) => values.every(v => v === undefined || Number.isFinite(v));
  switch (op.type) {
    case 'tone':
      return finite(op.exposure, op.brightness, op.contrast) ? null : `Operation #${index}: tone values must be finite`;
    case 'grayscale':
    case 'flip':
      return null;
    case 'rotate':
      return Number.isInteger(op.quarterTurns) ? null : `Operation #${index}: quarterTurns must be an integer`;
    case 'crop':
      if (!finite(op.x, op.y, op.width, op.height)) {
        return `Operation #${index}: crop must be finite`;
      }
      return op.width > 0 && op.height > 0 && op.x >= 0 && op.y >= 0 ? null : `Operation #${index}: invalid crop rectangle`;
    case 'resize':
      return op.width > 0 && op.height > 0 && op.width <= 16384 && op.height <= 16384
        ? null
        : `Operation #${index}: resize must be between 1 and 16384 px`;
    case 'sharpen':
      return finite(op.amount) && op.amount >= 0 ? null : `Operation #${index}: sharpen amount must be ≥ 0`;
    case 'saturation':
      return finite(op.amount) ? null : `Operation #${index}: saturation must be finite`;
    default:
      return `Operation #${index}: unknown type`;
  }
}

export function validateRequest(request: ProcessRequest): string | null {
  if (!request.inputPath || !request.outputPath) {
    return 'inputPath and outputPath are required';
  }
  if (request.operations.length > MAX_OPERATIONS) {
    return `Too many operations (max ${MAX_OPERATIONS})`;
  }
  if (request.quality !== undefined && (request.quality < 1 || request.quality > 100)) {
    return 'quality must be between 1 and 100';
  }
  for (let i = 0; i < request.operations.length; i++) {
    const error = validateOperation(request.operations[i], i);
    if (error) {
      return error;
    }
  }
  return null;
}
