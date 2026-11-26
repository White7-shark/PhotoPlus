/**
 * Masters: the twenty artists (engine order: pp_art_paint), grouped by
 * movement, each with their years, their technique and how they worked.
 * Names and words are string resources (translated); years are numbers.
 */
import { t, type StringKey } from '../../i18n';

export type Movement = 'renaissance' | 'baroque' | 'romantic' | 'impressionist' | 'modern';
export type MastersLevel = 0 | 1 | 2;

export interface Artist {
  /** The engine's index (0..19). */
  id: number;
  key: string;
  movement: Movement;
  years: string;
}

export const MOVEMENTS: readonly Movement[] = ['renaissance', 'baroque', 'romantic', 'impressionist', 'modern'];

export const ARTISTS: readonly Artist[] = [
  { id: 0, key: 'leonardo', movement: 'renaissance', years: '1452–1519' },
  { id: 1, key: 'michelangelo', movement: 'renaissance', years: '1475–1564' },
  { id: 2, key: 'raphael', movement: 'renaissance', years: '1483–1520' },
  { id: 3, key: 'durer', movement: 'renaissance', years: '1471–1528' },
  { id: 4, key: 'titian', movement: 'renaissance', years: '1488–1576' },
  { id: 5, key: 'caravaggio', movement: 'baroque', years: '1571–1610' },
  { id: 6, key: 'rembrandt', movement: 'baroque', years: '1606–1669' },
  { id: 7, key: 'vermeer', movement: 'baroque', years: '1632–1675' },
  { id: 8, key: 'velazquez', movement: 'baroque', years: '1599–1660' },
  { id: 9, key: 'goya', movement: 'romantic', years: '1746–1828' },
  { id: 10, key: 'hokusai', movement: 'romantic', years: '1760–1849' },
  { id: 11, key: 'turner', movement: 'romantic', years: '1775–1851' },
  { id: 12, key: 'monet', movement: 'impressionist', years: '1840–1926' },
  { id: 13, key: 'degas', movement: 'impressionist', years: '1834–1917' },
  { id: 14, key: 'renoir', movement: 'impressionist', years: '1841–1919' },
  { id: 15, key: 'cezanne', movement: 'modern', years: '1839–1906' },
  { id: 16, key: 'seurat', movement: 'modern', years: '1859–1891' },
  { id: 17, key: 'vangogh', movement: 'modern', years: '1853–1890' },
  { id: 18, key: 'gauguin', movement: 'modern', years: '1848–1903' },
  { id: 19, key: 'klimt', movement: 'modern', years: '1862–1918' },
];

const word = (k: string) => t(k as StringKey);
export const artistName = (a: Artist) => word(`masters_artist_${a.key}`);
export const artistTechnique = (a: Artist) => word(`masters_tech_${a.key}`);
export const artistAbout = (a: Artist) => word(`masters_about_${a.key}`);
export const movementName = (m: Movement) => word(`masters_movement_${m}`);
export const levelName = (l: MastersLevel) => word(`masters_level_${['study', 'painting', 'masterpiece'][l]}`);
export const levelAbout = (l: MastersLevel) => word(`masters_level_${['study', 'painting', 'masterpiece'][l]}_about`);
export const artistsOf = (m: Movement) => ARTISTS.filter(a => a.movement === m);
export const artistById = (id: number) => ARTISTS.find(a => a.id === id) ?? ARTISTS[0];
