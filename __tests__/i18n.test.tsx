import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import en from '../src/i18n/strings/en.json';
import es from '../src/i18n/strings/es.json';
import { format, LANGUAGES, matchLanguage, setLanguage, t, useI18n } from '../src/i18n';
import { LanguageScreen } from '../src/screens/LanguageScreen';
import { CAMPAIGN } from '../src/services/rewards/captions';
import {
  HASHTAGS,
  hashtags,
  headline,
  PLAY_URL,
  PROMO_FOLDERS,
  promoAsset,
  SHARE_STYLES,
  shareText,
} from '../src/services/rewards/campaign';
import { skewWords } from '../src/services/integrity/IntegrityService';
import { ThemeProvider } from '../src/theme';

// Node's fs, path and child_process, loaded through Jest (the app's types don't include Node's).
const { existsSync, readFileSync } = jest.requireActual<{
  existsSync: (path: string) => boolean;
  readFileSync: (path: string, encoding: 'utf8') => string;
}>('fs');
const { join } = jest.requireActual<{ join: (...parts: string[]) => string }>('path');
const { execFileSync } = jest.requireActual<{
  execFileSync: (cmd: string, args: string[], opts: { cwd: string; stdio: 'pipe' }) => unknown;
}>('child_process');
const root = jest.requireActual<{ cwd: () => string }>('process').cwd();
const strings = (tag: string): Record<string, string> =>
  JSON.parse(readFileSync(join(root, `src/i18n/strings/${tag}.json`), 'utf8'));

// The native side: Android's resources, as the app's language picks them.
const mockI18n: {
  lang: string;
  getStrings: jest.Mock<Record<string, string>, [string]>;
  getLanguage: jest.Mock<string, []>;
  getSystemLanguage: jest.Mock<string, []>;
  setLanguage: jest.Mock<void, [string]>;
} = {
  lang: '',
  getStrings: jest.fn((tag: string) => ((tag || mockI18n.lang).startsWith('es') ? { ...en, ...es } : en)),
  getLanguage: jest.fn(() => mockI18n.lang),
  getSystemLanguage: jest.fn(() => 'fr-FR'),
  setLanguage: jest.fn((tag: string) => {
    mockI18n.lang = tag;
  }),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get i18n() {
        return mockI18n;
      },
    },
  };
});

beforeEach(() => {
  jest.clearAllMocks();
  setLanguage('');
});

describe('the string resources', () => {
  it('are up to date with src/i18n (the generator check passes)', () => {
    expect(() =>
      execFileSync('node', ['tools/i18n/build-strings.mjs', '--check'], { cwd: root, stdio: 'pipe' }),
    ).not.toThrow();
  });

  it('no words the app shows are left in the code (they are all string resources)', () => {
    const out = String(execFileSync('node', ['tools/i18n/extract.mjs'], { cwd: root, stdio: 'pipe' }));
    expect(out).toMatch(/\n0 edits, 0 new strings/);
  }, 60_000);

  it('every language has a hand-translated file with only English keys and the same placeholders', () => {
    const holes = (s: string) => [...s.matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort();
    for (const l of LANGUAGES.filter(x => x.tag !== 'en')) {
      const table = strings(l.tag);
      const keys = Object.keys(table);
      expect(keys.length).toBeGreaterThan(40);
      for (const k of keys) {
        expect(en).toHaveProperty(k);
        expect(holes(table[k])).toEqual(holes((en as Record<string, string>)[k]));
        expect(table[k]).not.toBe('');
      }
      // The most-seen words are translated (not left in English).
      expect(table.tab_settings).not.toBe(en.tab_settings);
      expect(table.integrity_offline_title).not.toBe(en.integrity_offline_title);
    }
  });

  it('English is the full source: every key the app reads is in values/strings.xml', () => {
    const xml = readFileSync(join(root, 'android/app/src/main/res/values/strings.xml'), 'utf8');
    for (const k of Object.keys(en)) expect(xml).toContain(`name="${k}"`);
    // Placeholders are marked so translators keep them.
    expect(xml).toContain('<xliff:g id="days">{days}</xliff:g>');
    expect(xml).toContain('name="app_name" translatable="false"');
    for (const res of ['es', 'pt-rBR', 'fr', 'ar', 'hi', 'in', 'ru', 'zh-rCN'])
      expect(existsSync(join(root, `android/app/src/main/res/values-${res}/strings.xml`))).toBe(true);
  });
});

describe('the words in the app', () => {
  it('fills placeholders and falls back to English', () => {
    expect(format('{n} days left', { n: 3 })).toBe('3 days left');
    expect(format('{missing} stays', {})).toBe('{missing} stays');
    expect(t('settings_storage_free', { size: '2 GB' })).toBe('2 GB free');
  });

  it('matches phone languages to the offered ones', () => {
    expect(matchLanguage('pt').tag).toBe('pt-BR');
    expect(matchLanguage('pt-PT').tag).toBe('pt-BR');
    expect(matchLanguage('in-ID').tag).toBe('id');
    expect(matchLanguage('zh-Hans-CN').tag).toBe('zh-CN');
    expect(matchLanguage('es-MX').tag).toBe('es');
    expect(matchLanguage('de-DE').tag).toBe('en');
    expect(LANGUAGES).toHaveLength(9);
    expect(LANGUAGES.find(l => l.tag === 'ar')?.rtl).toBe(true);
  });

  it('follows the phone until a language is chosen; choosing one changes the words at once', () => {
    expect(useI18n.getState().choice).toBe('');
    expect(useI18n.getState().language.tag).toBe('fr'); // the phone's
    setLanguage('es');
    expect(mockI18n.setLanguage).toHaveBeenLastCalledWith('es');
    expect(t('tab_settings')).toBe('Ajustes');
    // A word without a hand translation stays English (until Play translates it).
    expect(t('settings_grid_title')).toBe(en.settings_grid_title);
    expect(skewWords(-180)).toBe('3 hours behind');
    expect(() => setLanguage('xx')).toThrow();
  });

  it('the language screen lists every language and changes it', async () => {
    await act(async () => {
      await render(
        <ThemeProvider preference="dark">
          <LanguageScreen navigation={{ goBack: jest.fn() } as never} route={{} as never} />
        </ThemeProvider>,
      );
    });
    for (const l of LANGUAGES) expect(screen.getByText(l.name)).toBeTruthy();
    expect(screen.getByText('Follows your phone: Français')).toBeTruthy();
    await act(async () => fireEvent.press(screen.getByText('Español')));
    expect(mockI18n.setLanguage).toHaveBeenLastCalledWith('es');
    // The screen redraws in Spanish.
    expect(screen.getByText('Idioma')).toBeTruthy();
    await act(async () => fireEvent.press(screen.getByText('Idioma del teléfono')));
    expect(mockI18n.setLanguage).toHaveBeenLastCalledWith('');
  });
});

describe('the share campaign in every language', () => {
  it('has a headline, captions with the link and the trial, and hashtags last on posts', () => {
    for (const l of LANGUAGES) {
      const copy = CAMPAIGN[l.tag];
      expect(copy).toBeDefined();
      expect(headline(l.tag)).toMatch(/^🎉 .+📸$/u);
      for (const form of ['post', 'status', 'friends'] as const) {
        const n = form === 'friends' ? copy.friends.length : copy.posts.length;
        const texts = Array.from({ length: n }, (_, i) => shareText(form, i, l.tag));
        expect(new Set(texts).size).toBe(n);
        for (const text of texts) {
          expect(text.startsWith(copy.headline)).toBe(true);
          expect(text.split('\n').some(line => line.startsWith(PLAY_URL))).toBe(true);
          expect(text).toContain('7');
          // Posts end with the hashtags; a note to friends carries none.
          const tags = hashtags(l.tag).join(' ');
          expect(text.trimEnd().endsWith(tags)).toBe(form !== 'friends');
          expect(text.includes('#PhotoPlus')).toBe(form !== 'friends');
        }
      }
      expect(hashtags(l.tag).slice(0, HASHTAGS.length)).toEqual([...HASHTAGS]);
    }
    expect(headline('en')).toBe('🎉 You can now do a photoshoot right on your phone! 📸');
  });

  it('shares the picture made in the language (every one is in the app)', () => {
    expect(promoAsset('post', 'en')).toBe('post.jpg');
    expect(promoAsset('status', 'pt-BR')).toBe('pt-br/status.jpg');
    expect(promoAsset('friends', 'xx')).toBe('friends.jpg');
    for (const l of LANGUAGES) {
      expect(PROMO_FOLDERS).toHaveProperty(l.tag);
      for (const s of SHARE_STYLES) {
        const file = join(root, 'android/app/src/main/assets/promo', promoAsset(s.id, l.tag));
        expect(existsSync(file)).toBe(true);
        // The native side accepts it (assets/promo/<name>.jpg or <language>/<name>.jpg).
        expect(promoAsset(s.id, l.tag)).toMatch(/^(?:[a-z]{2}(?:-[a-z]{2})?\/)?[a-z0-9-]{1,40}\.jpg$/);
      }
    }
  });
});
