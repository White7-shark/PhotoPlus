/**
 * The FAQ: topics and questions (their words are string resources, so
 * Google Play translates them), and a search over them that forgives case,
 * accents and word order.
 */
import { t, type StringKey } from '../../i18n';
import type { IconName } from '../../components/Icon';
import { FAQ_TOPICS, PRIVACY_SECTIONS } from './faqIndex';

export interface FaqItem {
  id: string;
  topic: string;
  question: string;
  answer: string;
}
export interface FaqTopic {
  id: string;
  icon: IconName;
  title: string;
  items: FaqItem[];
}

const word = (k: string) => t(k as StringKey);

/** Every topic with its questions, in the app's language. */
export function faqTopics(): FaqTopic[] {
  return FAQ_TOPICS.map(topic => ({
    id: topic.id,
    icon: topic.icon,
    title: word(`faq_cat_${topic.id}`),
    items: Array.from({ length: topic.count }, (_, i) => ({
      id: `${topic.id}_${i + 1}`,
      topic: topic.id,
      question: word(`faq_${topic.id}_${i + 1}_q`),
      answer: word(`faq_${topic.id}_${i + 1}_a`),
    })),
  }));
}

/** Lower case, no accents, single spaces (works for every script). */
export const fold = (s: string) =>
  s
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .toLocaleLowerCase()
    .replace(/[^\p{L}\p{N}\p{M}]+/gu, ' ')
    .trim();

/**
 * Questions that hold every word searched for (in the question, answer or
 * topic), best first: words in the question count most.
 */
export function searchFaq(topics: readonly FaqTopic[], query: string): FaqItem[] {
  const words = fold(query).split(' ').filter(Boolean);
  if (!words.length) return [];
  const scored: { item: FaqItem; score: number }[] = [];
  for (const topic of topics) {
    const title = fold(topic.title);
    for (const item of topic.items) {
      const q = fold(item.question);
      const a = fold(item.answer);
      let score = 0;
      let all = true;
      for (const w of words) {
        if (q.includes(w)) score += q.split(' ').includes(w) ? 4 : 3;
        else if (title.includes(w)) score += 2;
        else if (a.includes(w)) score += a.split(' ').includes(w) ? 1.5 : 1;
        else {
          all = false;
          break;
        }
      }
      if (all) scored.push({ item, score });
    }
  }
  return scored.sort((x, y) => y.score - x.score).map(s => s.item);
}

/** The privacy policy's sections, in the app's language. */
export function privacySections(): { title: string; body: string }[] {
  return Array.from({ length: PRIVACY_SECTIONS }, (_, i) => ({
    title: word(`privacy_${i + 1}_title`),
    body: word(`privacy_${i + 1}_body`),
  }));
}
