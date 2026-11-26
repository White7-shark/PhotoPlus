/**
 * The app's words in the user's language.
 *
 * Every string is an Android resource (src/i18n/strings/en.json is the
 * English source; tools/i18n/build-strings.mjs writes res/values*). The
 * words come from Android, for the app's language, so translations Google
 * Play adds to the app bundle show up here too. English is the fallback
 * for any word not yet translated (and in tests, where there is no
 * native side).
 */
import { useCallback } from 'react';
import { create } from 'zustand';

import { nativeModules } from '../native';
import en from './strings/en.json';
import languages from './languages.json';

export type StringKey = keyof typeof en;
/** Placeholder values; an empty one (null, undefined) shows as nothing. */
export type Params = Record<string, string | number | null | undefined>;

export interface Language {
  /** BCP 47 tag, e.g. "pt-BR". */
  tag: string;
  /** Its own name, e.g. "Português (Brasil)". */
  name: string;
  english: string;
  rtl?: boolean;
}

/** The languages Photo Plus offers (English and 8 more). */
export const LANGUAGES: readonly Language[] = languages.map(({ tag, name, english, ...rest }) => ({
  tag,
  name,
  english,
  rtl: 'rtl' in rest ? Boolean((rest as { rtl?: boolean }).rtl) : undefined,
}));

const EN: Record<string, string> = en;

/** The offered language for a tag ("pt" → Português (Brasil), "zh-Hans-CN" → 简体中文, "in" → Indonesian). */
export function matchLanguage(tag: string): Language {
  const t = tag.replace('_', '-').toLowerCase();
  const exact = LANGUAGES.find(l => l.tag.toLowerCase() === t);
  if (exact) return exact;
  const base = t.split('-')[0] === 'in' ? 'id' : t.split('-')[0];
  return LANGUAGES.find(l => l.tag.split('-')[0].toLowerCase() === base) ?? LANGUAGES[0];
}

/** Fills {name} placeholders. */
export function format(text: string, params?: Params): string {
  if (!params) return text;
  return text.replace(/\{(\w+)\}/g, (all, k: string) => (k in params ? String(params[k] ?? '') : all));
}

function readStrings(tag: string): Record<string, string> {
  try {
    const got = nativeModules.i18n?.getStrings(tag) as Record<string, string> | undefined;
    return got && typeof got === 'object' ? got : EN;
  } catch {
    return EN;
  }
}
function readLanguage(): string {
  try {
    return nativeModules.i18n?.getLanguage() ?? '';
  } catch {
    return '';
  }
}
function readSystemLanguage(): string {
  try {
    return nativeModules.i18n?.getSystemLanguage() ?? 'en';
  } catch {
    return 'en';
  }
}

interface I18nState {
  /** The chosen language ("" follows the phone). */
  choice: string;
  /** The language shown. */
  language: Language;
  strings: Record<string, string>;
}

function load(choice: string): I18nState {
  return {
    choice,
    language: matchLanguage(choice || readSystemLanguage()),
    strings: readStrings(choice),
  };
}

export const useI18n = create<I18nState>(() => load(readLanguage()));

/** A word in the app's language (English when it has no translation yet). */
export function t(key: StringKey, params?: Params): string {
  const s = useI18n.getState().strings[key] ?? EN[key] ?? key;
  return format(s, params);
}

/** t, for components: they redraw when the language changes. */
export function useT(): typeof t {
  const strings = useI18n(s => s.strings);
  return useCallback((key: StringKey, params?: Params) => format(strings[key] ?? EN[key] ?? key, params), [strings]);
}

/** The phone's language, as offered by the app. */
export const systemLanguage = (): Language => matchLanguage(readSystemLanguage());

/**
 * Changes the app's language ("" to follow the phone). The words change at
 * once; Android also redraws the app in the language (right to left for
 * Arabic) and remembers it.
 */
export function setLanguage(tag: string): void {
  if (tag && !LANGUAGES.some(l => l.tag === tag)) throw new Error(`Unknown language: ${tag}`);
  nativeModules.i18n?.setLanguage(tag);
  useI18n.setState(load(tag));
}
