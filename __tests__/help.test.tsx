import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import en from '../src/i18n/strings/en.json';
import { HelpScreen } from '../src/screens/HelpScreen';
import { PrivacyScreen } from '../src/screens/PrivacyScreen';
import { faqTopics, fold, privacySections, searchFaq } from '../src/services/help/faq';
import { FAQ_TOPICS, PRIVACY_SECTIONS } from '../src/services/help/faqIndex';
import { ThemeProvider } from '../src/theme';

const mockAds = {
  initialize: jest.fn(async () => ({ canRequestAds: true })),
  privacyOptionsRequired: jest.fn(async () => true),
  showPrivacyOptions: jest.fn(async () => true),
};
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get ads() {
        return mockAds;
      },
    },
  };
});

const EN: Record<string, string> = en;
const nav = { goBack: jest.fn(), navigate: jest.fn() };
const show = async (ui: React.ReactElement) =>
  act(async () => {
    await render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
  });

describe('the FAQ', () => {
  it('has every question, answer and topic as a string resource', () => {
    expect(FAQ_TOPICS.length).toBeGreaterThanOrEqual(12);
    let n = 0;
    for (const topic of FAQ_TOPICS) {
      expect(EN[`faq_cat_${topic.id}`]).toBeTruthy();
      for (let i = 1; i <= topic.count; i++) {
        expect(EN[`faq_${topic.id}_${i}_q`]).toMatch(/\S/);
        expect(EN[`faq_${topic.id}_${i}_a`].length).toBeGreaterThan(40);
        n++;
      }
      expect(EN[`faq_${topic.id}_${topic.count + 1}_q`]).toBeUndefined();
    }
    expect(n).toBeGreaterThanOrEqual(80);
  });

  it('covers every tool by name', () => {
    const text = faqTopics()
      .flatMap(x => x.items.map(i => `${i.question} ${i.answer}`))
      .join(' ');
    for (const tool of [
      'Scene Swap',
      'Relight',
      'Film & Collage',
      'Product Studio',
      'Batch edit',
      'Glide',
      'Spotlight',
      'Lively',
      'Clarity',
      'Ultra Detail',
      'Moonlight',
      'Vibe Match',
      'Transform',
      'Auto Light',
      'Skin Tone',
      'Backdrops',
      'Props',
      'Frames',
      'Watermark',
      'Rewards',
      'Premium',
      'Projects',
      'Packs',
      'Language',
    ])
      expect(text).toContain(tool);
  });

  it('searches questions and answers, forgiving case, accents and word order', () => {
    const topics = faqTopics();
    expect(fold('  Café,  ÉCRAN! ')).toBe('cafe ecran');
    const watermark = searchFaq(topics, 'WATERMARK remove');
    expect(watermark[0].question).toBe('How do I remove the watermark?');
    expect(searchFaq(topics, 'cancel subscription')[0].topic).toBe('premium');
    expect(searchFaq(topics, 'hair colour').some(r => r.topic === 'studio')).toBe(true);
    expect(searchFaq(topics, 'zzzz qqqq')).toEqual([]);
    expect(searchFaq(topics, '   ')).toEqual([]);
    // A word in the question ranks above one only in an answer.
    const glide = searchFaq(topics, 'glide');
    expect(glide[0].question.toLowerCase()).toContain('glide');
  });

  it('the screen: topics, a topic’s questions that open, and search with a count', async () => {
    await show(<HelpScreen navigation={nav as never} route={{} as never} />);
    for (const topic of faqTopics()) expect(screen.getByTestId(`help-tile-${topic.id}`)).toBeTruthy();
    await act(async () => fireEvent.press(screen.getByTestId('help-tile-rewards')));
    expect(screen.getByText('How does the share reward work?')).toBeTruthy();
    expect(screen.queryByTestId('faq-rewards_4-answer')).toBeNull();
    await act(async () => fireEvent.press(screen.getByTestId('faq-rewards_4')));
    expect(screen.getByTestId('faq-rewards_4-answer').props.children).toMatch(/30 times/);
    await act(async () => fireEvent.changeText(screen.getByTestId('help-search'), 'watermark'));
    expect(screen.getByTestId('help-count').props.children).toMatch(/answers? found/);
    expect(screen.getByText('How do I remove the watermark?')).toBeTruthy();
    await act(async () => fireEvent.changeText(screen.getByTestId('help-search'), 'zzzz qqqq'));
    expect(screen.getByText('No answers found')).toBeTruthy();
    await act(async () => fireEvent.press(screen.getByTestId('help-clear')));
    expect(screen.getByTestId('help-topic-all')).toBeTruthy();
  });
});

describe('the privacy policy', () => {
  it('has every section as a string resource', () => {
    expect(PRIVACY_SECTIONS).toBeGreaterThanOrEqual(15);
    const sections = privacySections();
    expect(sections).toHaveLength(PRIVACY_SECTIONS);
    const all = sections.map(s => `${s.title} ${s.body}`).join(' ');
    for (const topic of ['AdMob', 'Google Play', 'camera', 'Children', 'advertising ID', 'Contact'])
      expect(all.toLowerCase()).toContain(topic.toLowerCase());
    expect(EN.privacy_updated).toMatch(/Last updated/);
  });

  it('the screen shows every section and opens ad privacy choices', async () => {
    await show(<PrivacyScreen navigation={nav as never} route={{} as never} />);
    for (let i = 1; i <= PRIVACY_SECTIONS; i++) expect(screen.getByTestId(`privacy-section-${i}`)).toBeTruthy();
    await act(async () => fireEvent.press(screen.getByText('Ad privacy choices')));
    expect(mockAds.showPrivacyOptions).toHaveBeenCalled();
    // Where no choices are needed, the screen says so.
    mockAds.privacyOptionsRequired.mockResolvedValue(false);
    await show(<PrivacyScreen navigation={nav as never} route={{} as never} />);
    await act(async () => fireEvent.press(screen.getAllByText('Ad privacy choices').at(-1)!));
    expect(screen.getByTestId('privacy-note').props.children).toMatch(/No ad choices are needed/);
  });
});
