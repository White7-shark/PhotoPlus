/**
 * Collision-resistant, roughly time-ordered identifiers. Hermes has no
 * crypto.randomUUID, and ordering by id is handy for debugging, so we use
 * base36 time + random suffix (≈ 2^62 values per millisecond).
 */
export function createId(prefix: string, now: number = Date.now()): string {
  const time = now.toString(36).padStart(9, '0');
  let random = '';
  for (let i = 0; i < 12; i++) {
    random += Math.floor(Math.random() * 36).toString(36);
  }
  return `${prefix}_${time}${random}`;
}
