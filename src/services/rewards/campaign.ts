/**
 * The share campaign (Rewards, free plan): share Photo Plus 30 times, to as
 * many apps as you like, for two days of Premium. Each app counts once
 * every eight hours. Two ways to share, each with its own picture and words:
 *  - a post, status or update (a big picture with what Photo Plus does,
 *    hashtags and the Play Store link);
 *  - a message to friends (a picture and a personal note with the link).
 * Both say "7 days free trial".
 */
import { TRIAL_DAYS } from '../billing/plans';

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
  name: string;
  about: string;
  /** The picture (android assets/promo). */
  asset: string;
  aspect: number;
}

export const SHARE_STYLES: readonly ShareStyle[] = [
  { id: 'post', name: 'Post', about: 'A post on your feed or page', asset: 'post.jpg', aspect: 1080 / 1350 },
  {
    id: 'status',
    name: 'Status or story',
    about: 'Your status, story or update',
    asset: 'status.jpg',
    aspect: 1080 / 1920,
  },
  { id: 'friends', name: 'To friends', about: 'A message to people and groups', asset: 'friends.jpg', aspect: 1 },
];
export const shareStyle = (id: ShareForm) => SHARE_STYLES.find(s => s.id === id) ?? SHARE_STYLES[0];

/** The headline of every share, as on the pictures. */
export const HEADLINE = '🎉 You can now photoshoot right on your phone! 📸';

// Each caption is sent as the text beside the picture, so the link (on its
// own line) can be tapped and the hashtags (the last line) are recognised
// by the app. Neither is drawn on the pictures.
const POST_TEXTS = [
  (link: string) =>
    `${HEADLINE}

Meet Photo Plus ✨
✅ Be in a real studio, park or café without leaving home
✅ Pro studio lighting on any photo with Relight
✅ Clean product photos that sell, with real shadows
✅ Flawless beauty, dreamy Spotlight portraits and rich Lively colour
✅ Glide pro video, film strips and collages

No studio. No laptop. No experience needed.
🎁 Try Premium FREE for ${TRIAL_DAYS} days!

📲 Download on Google Play:
${link}

${HASHTAGS.join(' ')}`,
  (link: string) =>
    `${HEADLINE}

My phone just became a photo studio 🤯 Photo Plus swaps your background for a real studio scene, adds pro lighting, retouches skin naturally and turns product snaps into catalogue shots — in seconds.

🎁 ${TRIAL_DAYS} days FREE trial of Premium

👉 Get it on Google Play:
${link}

${HASHTAGS.join(' ')}`,
  (link: string) =>
    `${HEADLINE}

Stop paying for photoshoots 💸 — make yours with Photo Plus ✨
📸 Photoshoot Studio · 💡 Relight · 🛍️ Product Studio · 💄 Beauty & Spotlight · 🎬 Glide video

Perfect for creators, sellers and anyone who loves great photos.
🎁 Start with a ${TRIAL_DAYS}-day free trial.

Download free 👉
${link}

${HASHTAGS.join(' ')}`,
];

const FRIEND_TEXTS = [
  (link: string) =>
    `${HEADLINE}

Hey! 👋 You have to try Photo Plus. I use it to make my photos look like a real photoshoot — new backgrounds, studio lighting, natural beauty retouch, even clean product photos for selling. It all happens on your phone.

You can try Premium FREE for ${TRIAL_DAYS} days 🎁
Get it here 👇
${link}`,
  (link: string) =>
    `${HEADLINE}

Look what my phone can do now 😍 Photo Plus puts you in a real studio, relights your photos like a pro and makes product shots look professional — in seconds.

🎁 ${TRIAL_DAYS}-day free trial of Premium
Download it free on Google Play 👇
${link}`,
  (link: string) =>
    `${HEADLINE}

Found the best photo app 🔥 Photo Plus: studio backgrounds, pro lighting, beauty retouch, product photos and smooth Glide videos.

Try it with ${TRIAL_DAYS} days free 🎁
${link}`,
];

/** The words for a share (they change from share to share, so posts don't look copied). */
export function shareText(form: ShareForm, turn: number): string {
  const list = form === 'friends' ? FRIEND_TEXTS : POST_TEXTS;
  const t = list[((turn % list.length) + list.length) % list.length];
  return t(playLink(form === 'friends' ? 'friends' : 'post'));
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
