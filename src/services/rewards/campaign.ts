/**
 * The share campaign (Rewards, free plan): share Photo Plus 30 times, to as
 * many apps as you like, for two days of Premium. Each app counts once
 * every eight hours. Two ways to share, each with its own picture and words:
 *  - a post, status or update (a big picture with what Photo Plus does,
 *    hashtags and the Play Store link);
 *  - a message to friends (a picture and a personal note with the link).
 * Both say "7 days free trial".
 */
import { useI18n, type StringKey } from '../../i18n';
import { TRIAL_DAYS } from '../billing/plans';
import { CAMPAIGN } from './captions';

export const PLAY_URL = 'https://play.google.com/store/apps/details?id=com.photoplus';
/** The link with where it was shared from (Play's install referrer reads it). */
export const playLink = (medium: 'post' | 'friends') =>
  `${PLAY_URL}&referrer=${encodeURIComponent(`utm_source=photoplus_app&utm_medium=${medium}&utm_campaign=share_rewards`)}`;

export const HASHTAGS = [
  '#PhotoPlus',
  '#PhotoEditing',
  '#MobilePhotography',
  '#PhotoshootStudio',
  '#ProductPhotography',
  '#ContentCreator',
  '#SmallBusiness',
  '#PhotoEditor',
  '#ShotOnPhone',
  '#AndroidApps',
];

export type ShareForm = 'post' | 'status' | 'friends';

export interface ShareStyle {
  id: ShareForm;
  name: StringKey;
  about: StringKey;
  /** The picture's file name (android assets/promo, or assets/promo/<language>). */
  asset: string;
  aspect: number;
}

export const SHARE_STYLES: readonly ShareStyle[] = [
  { id: 'post', name: 'share_style_post', about: 'share_style_post_about', asset: 'post.jpg', aspect: 1080 / 1350 },
  {
    id: 'status',
    name: 'share_style_status',
    about: 'share_style_status_about',
    asset: 'status.jpg',
    aspect: 1080 / 1920,
  },
  { id: 'friends', name: 'share_style_friends', about: 'share_style_friends_about', asset: 'friends.jpg', aspect: 1 },
];
export const shareStyle = (id: ShareForm) => SHARE_STYLES.find(s => s.id === id) ?? SHARE_STYLES[0];

/** The picture folder of each language (assets/promo/<folder>; English is assets/promo itself). */
export const PROMO_FOLDERS: Readonly<Record<string, string>> = {
  en: '',
  es: 'es',
  'pt-BR': 'pt-br',
  fr: 'fr',
  ar: 'ar',
  hi: 'hi',
  id: 'id',
  ru: 'ru',
  'zh-CN': 'zh-cn',
};

const appLanguage = () => useI18n.getState().language.tag;
const copyFor = (lang: string) => CAMPAIGN[lang] ?? CAMPAIGN.en;

/** The picture to share, in the language (the asset path under assets/promo). */
export function promoAsset(form: ShareForm, lang: string = appLanguage()): string {
  const folder = PROMO_FOLDERS[lang] ?? '';
  return folder ? `${folder}/${shareStyle(form).asset}` : shareStyle(form).asset;
}

/** The headline of every share in the language, as on the pictures. */
export const headline = (lang: string = appLanguage()) => copyFor(lang).headline;
/** The English headline. */
export const HEADLINE = CAMPAIGN.en.headline;

/** The hashtags for a post in the language: the brand's own, then the language's. */
export const hashtags = (lang: string = appLanguage()) => [...HASHTAGS, ...copyFor(lang).hashtags];

/**
 * The words for a share, in the app's language (they change from share to
 * share, so posts don't look copied). Each caption is sent as the text
 * beside the picture: the headline first, the link on its own line (so it
 * can be tapped) and, for posts, the hashtags last (so the app recognises
 * them). Neither the link nor the hashtags are drawn on the pictures.
 */
export function shareText(form: ShareForm, turn: number, lang: string = appLanguage()): string {
  const copy = copyFor(lang);
  const list = form === 'friends' ? copy.friends : copy.posts;
  const caption = list[((turn % list.length) + list.length) % list.length];
  const body = `${copy.headline}\n\n${caption(playLink(form === 'friends' ? 'friends' : 'post'), TRIAL_DAYS)}`;
  return form === 'friends' ? body : `${body}\n\n${hashtags(lang).join(' ')}`;
}

/** Apps offered first (when they are on the phone); any other app comes from the share sheet. */
export const SHARE_APPS: readonly { pkg: string; name: string }[] = [
  { pkg: 'com.whatsapp', name: 'WhatsApp' },
  { pkg: 'com.whatsapp.w4b', name: 'WhatsApp Business' },
  { pkg: 'com.facebook.katana', name: 'Facebook' },
  { pkg: 'com.instagram.android', name: 'Instagram' },
  { pkg: 'com.facebook.orca', name: 'Messenger' },
  { pkg: 'com.zhiliaoapp.musically', name: 'TikTok' },
  { pkg: 'com.snapchat.android', name: 'Snapchat' },
  { pkg: 'org.telegram.messenger', name: 'Telegram' },
  { pkg: 'com.twitter.android', name: 'X' },
  { pkg: 'com.instagram.barcelona', name: 'Threads' },
  { pkg: 'com.linkedin.android', name: 'LinkedIn' },
  { pkg: 'com.pinterest', name: 'Pinterest' },
  { pkg: 'com.google.android.apps.messaging', name: 'Messages' },
];
export const appName = (pkg: string) => SHARE_APPS.find(a => a.pkg === pkg)?.name ?? pkg.split('.').slice(-1)[0];
