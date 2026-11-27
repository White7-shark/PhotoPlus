/**
 * Photo Arts: the twenty artists (engine order: pp_art_paint), grouped by
 * movement, each with their years, their technique and how they worked.
 * Names and words are string resources (translated); years are numbers.
 */
import { t, type StringKey } from '../../i18n';

export type Movement =
  'renaissance' | 'classical' | 'impressionism' | 'postimpressionism' | 'romanticism' | 'print' | 'drama';
export type MastersLevel = 0 | 1 | 2;

export interface Artist {
  /** The engine's index (0..19). */
  id: number;
  key: string;
  movement: Movement;
  years: string;
}

export const MOVEMENTS: readonly Movement[] = [
  'renaissance',
  'classical',
  'impressionism',
  'postimpressionism',
  'romanticism',
  'print',
  'drama',
];

export const ARTISTS: readonly Artist[] = [
  { id: 0, key: 'leonardo', movement: 'renaissance', years: '1452–1519' },
  { id: 1, key: 'michelangelo', movement: 'renaissance', years: '1475–1564' },
  { id: 2, key: 'raphael', movement: 'renaissance', years: '1483–1520' },
  { id: 3, key: 'durer', movement: 'print', years: '1471–1528' },
  { id: 4, key: 'titian', movement: 'classical', years: '1488–1576' },
  { id: 5, key: 'caravaggio', movement: 'drama', years: '1571–1610' },
  { id: 6, key: 'rembrandt', movement: 'classical', years: '1606–1669' },
  { id: 7, key: 'vermeer', movement: 'classical', years: '1632–1675' },
  { id: 8, key: 'velazquez', movement: 'classical', years: '1599–1660' },
  { id: 9, key: 'goya', movement: 'romanticism', years: '1746–1828' },
  { id: 10, key: 'hokusai', movement: 'print', years: '1760–1849' },
  { id: 11, key: 'turner', movement: 'romanticism', years: '1775–1851' },
  { id: 12, key: 'monet', movement: 'impressionism', years: '1840–1926' },
  { id: 13, key: 'degas', movement: 'impressionism', years: '1834–1917' },
  { id: 14, key: 'renoir', movement: 'impressionism', years: '1841–1919' },
  { id: 15, key: 'cezanne', movement: 'postimpressionism', years: '1839–1906' },
  { id: 16, key: 'seurat', movement: 'impressionism', years: '1859–1891' },
  { id: 17, key: 'vangogh', movement: 'postimpressionism', years: '1853–1890' },
  { id: 18, key: 'gauguin', movement: 'postimpressionism', years: '1848–1903' },
  { id: 19, key: 'klimt', movement: 'print', years: '1862–1918' },
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
