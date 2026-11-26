# The FAQ and privacy policy, in English (the source Google Play translates).
# Run: python3 tools/i18n/content/faq_en.py   (merges into src/i18n/strings/en.json)
import json, os, re

FAQ = [
 ("start", "Getting started", "sparkles", [
  ("What is Photo Plus?", "Photo Plus turns your phone into a photo studio. Take photos and videos with a smarter camera, then retouch, relight, change the background, make product photos, film strips and collages — all on your phone, in seconds."),
  ("Is Photo Plus free?", "Yes. The camera, Clarity, Glide, Spotlight, basic editing, Film & Collage, Product Studio basics, Batch edit, Projects and many filters are free. Premium unlocks every tool, every filter and style, longer Glide videos, HD and 4K downloads and no watermark. You can also earn Premium time for free in Rewards."),
  ("Do I need an account?", "No. Photo Plus works without an account or sign-in. Your photos, projects and settings stay on your phone."),
  ("Do I need the internet?", "Almost everything works offline, because your photos are processed on your phone. Photo Plus goes online to check the time and for updates (at least once every 7 days), to show ads, and for Google Play purchases."),
  ("Which phones does Photo Plus work on?", "Android 10 or newer. Newer phones run the heavier tools (Scene Swap, Glide in a new scene, 4K downloads) faster; older phones run them at a smaller size so they never run out of memory."),
  ("What is Lucid?", "Lucid makes Photo Plus see-through: your phone’s wallpaper (live wallpapers too), a photo or a short looping clip shows behind the app, with glass cards over it. Lucid takes light or dark, colours and accents from the picture and adjusts them so every word stays easy to read. Choose it in Settings > Appearance > Theme."),
  ("How do I put my own photo or clip behind Lucid?", "Settings > Appearance > Lucid, then tap Photo or Video and pick one. Lucid with your phone’s wallpaper is free; your own photo or clip (a video up to 30 MB, playing silently on a loop) is part of Premium. Changed your wallpaper? Tap Phone wallpaper again."),
  ("Where are my photos saved?", "Photos you take or edit are kept in Photo Plus (Projects). When you save or download one, it also goes to your phone’s gallery. Your originals are always kept, untouched."),
 ]),
 ("camera", "Camera", "camera", [
  ("What do the camera modes do?", "Photo: bright, lively colour in one tap. Clarity: sharper, cleaner, brighter single photos. Ultra Detail: several shots combined for fine detail (Premium). Moonlight: clear photos in the dark from several shots (Premium). Glide: a short, steady pro video. Pro: full manual control of ISO, shutter, focus and white balance (Premium)."),
  ("What is Lively colour?", "Lively gives every photo warm, rich colour and bright light, like a flagship phone, while keeping the background natural. Turn it off in Settings > Camera & motion to keep your phone camera’s own look."),
  ("What is Auto Light?", "Auto Light reads the scene before you shoot. In a dark room it brightens and cleans the photo, in harsh light it balances it, and when needed it uses the flash or lights your face with the screen (Glow) for selfies. Turn it on or off in Settings."),
  ("How do I use Beauty in the camera?", "Tap Beauty in the camera and pick a filter. Natural Glow and Soft Radiance are free; 11 more are in Premium. Each filter has a strength slider and fine adjustments, and you see the result live before you shoot."),
  ("What is Spotlight?", "Spotlight makes portraits stand out: the background is softly blurred like a professional lens, with Glow skin retouch. You can tap a person to choose who stays sharp."),
  ("What are props in the camera?", "Props are hats, glasses, jewellery and outfit pieces that follow your face live in the camera, like a filter. Tap Props to choose one. Some props are free; the full collection is in Premium, and Premium can make props from your own photos."),
  ("What is Vibe Match?", "Vibe Match lets you shoot in the style of any photo you like: pick a photo and the camera matches its colour and mood. It is a Premium tool."),
  ("What is Transform?", "Transform gives a photo the look of a high-end camera lens: richer depth, smoother background and crisper subject. Free users can try it twice a day; Premium has no limit."),
  ("How do I change photo size or shape?", "Settings > Camera & motion: Capture resolution (Max, 12, 8 or 5 MP) and Aspect ratio (4:3, 16:9 or 1:1). Smaller photos save space."),
  ("Why are my selfies mirrored (or not)?", "Settings > Mirror selfies. On, front-camera photos are saved the way the preview shows them; off, they are saved the way others see you."),
 ]),
 ("pose", "Pose assistant", "figure", [
  ("What does the pose assistant do?", "It helps you get the shot: grid lines, a level, face position, simple pose demos and tips. With Premium you get the full pose library, poses taken from your own photos, live matching and auto capture."),
  ("How does auto capture work?", "Pick a pose (Premium). The camera shows it as a guide and checks your pose live; when you match it, the photo is taken for you — handy when the phone is on a stand."),
  ("Can I copy a pose from a photo?", "Yes, with Premium: choose a photo and Photo Plus turns the person’s pose into a guide you can follow in the camera."),
 ]),
 ("glide", "Glide video", "film", [
  ("What is Glide?", "Glide records a short pro video: steady, sharp and with Lively colour, Spotlight and Glow skin retouch on every frame."),
  ("How long can a Glide video be?", "Free Glide videos are 7 seconds and carry the Photo Plus watermark. Premium Glide videos can be up to 20 seconds with no watermark."),
  ("What are Fast and Slow?", "Fast plays your Glide at twice the speed. Slow turns it into cinematic slow motion."),
  ("Can I put myself in a new scene in a video?", "Yes, with Premium: Glide in a new scene moves you into any background, true to its size, frame after frame."),
 ]),
 ("editor", "Editing photos", "sliders", [
  ("How do I edit a photo?", "Open a photo from Projects or right after you take it and tap Edit. Choose a tool at the bottom; every change can be undone and redone, and your original is never changed."),
  ("What do the editing tools do?", "Adjust: exposure, brightness, contrast and colour. Crop: crop, rotate and straighten. Looks: colour filters. Beauty: beauty filters. Backdrop: a new background. Skin Tone: even, natural skin shades. Spotlight: background blur. Portrait: face, skin, eyes and lips retouch. Focus: blur parts of the photo. Transform: the high-end lens look. Props, Frames and Watermark."),
  ("What is the difference between basic and advanced Portrait?", "Basic Portrait retouches skin and evens tone in one slider. Advanced (Premium) gives precise control of face, eyes, lips, teeth, light and background."),
  ("What is Skin Tone?", "Skin Tone gives skin an even, natural shade without a filter look. Ivory, Honey, Caramel and Espresso are free; 9 more shades, Porcelain Rose to Ebony Glow, are in Premium."),
  ("What are Backdrops?", "Backdrops replace the background behind a person with a studio design, keeping hair edges natural. Signature, Soft Studio and Stage are free; 11 more designs are in Premium."),
  ("How do I remove the watermark?", "Premium saves without a watermark. On the free plan, you can watch 4 short ads to save that photo without the watermark."),
  ("Can I add my own logo or watermark?", "Yes. Watermark lets you choose the style and position of the Photo Plus mark; with Premium you can add your own brand, text or logo."),
  ("Can I make my own props?", "Yes, with Premium: Make a prop turns part of one of your photos (a hat, glasses, a necklace) into a prop you can wear in the camera and editor."),
  ("How do I compare before and after?", "Press and hold the photo, or use the compare button, to see the original; let go to see your edit."),
 ]),
 ("studio", "Photoshoot Studio", "layers", [
  ("What is the Photoshoot Studio?", "The studio holds the bigger tools: Scene Swap, Film & Collage, Relight, Product Studio and Batch edit. Tap the i on a tile to see what it does."),
  ("What is Scene Swap?", "Scene Swap cuts you out of your photo and places you in a new scene — a studio, a park at golden hour, a café, a solid colour or a background of your own — at the right size, with matching light and a natural shadow. It is a Premium tool."),
  ("The cut-out missed a part or kept too much. How do I fix it?", "Use the brush in Scene Swap: Keep paints back what was missed, Remove takes away what should go, and the hair brush finds fine hair, fur or lace by colour. Undo and redo are always there."),
  ("Can I change my hair colour or style?", "Yes, with Premium: 18 hair colours, 23 women’s styles and dreadlocks, rasta, afro or bald, with tools to shape, place and finish the hair so it looks real."),
  ("What are Pink lips and Lipstick?", "Pink lips adds a soft, natural pink that adapts to your lips; Lipstick lets you choose a colour and finish."),
  ("What is studio light?", "Studio light adds professional light to the scene: soft, Rembrandt, split, rim, neon or golden hour, light you can turn and soften, gradient backdrops, vignette and film looks. Soft and bright light, gradients, vignette, warmth and simple grades are free; the rest is Premium."),
  ("What is Film & Collage?", "Film & Collage turns your photos into a film strip or a collage. Every frame can have its own look, crop, colour and light, and Mix it up does it for you. Classic and photo-booth strips, ten looks and ten collage layouts are free; amber negative and cinema strips are Premium."),
  ("What is Relight?", "Relight puts studio lighting on your own photo, right where you stand — soft, Rembrandt, rim or neon light — with the background darkened, softened or coloured. It is a Premium tool."),
  ("What download sizes can I choose?", "Free downloads are a standard size. Premium can download at 720p, 1080p, HD and 4K."),
  ("What is Masters?", "Masters paints your photo the way twenty great artists really worked — Leonardo da Vinci, Rembrandt, Vermeer, Hokusai, Monet, Van Gogh, Klimt and more — with real brush strokes, each artist’s palette and light, and the finish of the real thing: canvas, varnish and cracks, red chalk on paper, an engraving or a woodblock print. Choose a movement, an artist and a finish (Study, Painting or Masterpiece). It’s part of Premium; the free plan can paint one picture a day."),
  ("Why does each artist look so different?", "Because each one is painted with that artist’s own method, not a filter: Leonardo’s soft sfumato and old varnish, Caravaggio’s single light from a black ground, Michelangelo’s red-chalk hatching, Dürer’s engraved lines, Hokusai’s flat woodblock colours, Seurat’s dots, Van Gogh’s thick swirling paint, Klimt’s gold leaf. A Masterpiece uses the finest brushes and keeps the face closest to you."),
  ("What is Recreate a shoot?", "Recreate a shoot will let you follow a pack’s scenes, poses and light, shot by shot. It is coming soon."),
 ]),
 ("product", "Product photos & business", "bag", [
  ("What is Product Studio?", "Product Studio makes catalogue shots of what you sell: the product is cut out onto white, paper or grey, with a natural shadow, true colour and the right size for online shops."),
  ("What is in Pro product setups?", "Premium adds tabletop, spotlit, scene and transparent backdrops, cast shadows, reflections, layout templates and packs."),
  ("How do I get the right size for my shop?", "Choose a size when you save: Square 1:1 for web shops and marketplaces, Post 4:5 for social feeds, Story 9:16 for stories and reel covers, or Catalogue 3:4 for print. With Premium you can also save a transparent PNG. Product Studio keeps the product centred with the right margin."),
  ("What is Batch edit?", "Batch edit puts one setup or look on many photos at once — up to 50 with Premium — and saves every result beside its untouched original."),
  ("Can I save my setup and use it again?", "Yes, with Premium: save a setup or look and reuse it in every project, or apply a pack across projects for a consistent brand look."),
  ("How do I add my brand to every photo?", "Use the Watermark tool with your own brand (Premium), and save it in your setup so Batch edit adds it to every photo."),
 ]),
 ("projects", "Projects & saving", "folder", [
  ("What are Projects?", "Projects keep a shoot together: its photos, edits and settings. Every original is kept, and you can compare before and after at any time."),
  ("How do I share a photo?", "Open the photo and tap Share to send it to any app on your phone."),
  ("Which format and quality should I save in?", "Settings > Export: JPEG is smallest and works everywhere, PNG is lossless and can be transparent, WebP is small with good quality. Quality: Compact, High or Max."),
  ("Photo Plus is using a lot of space. What can I do?", "Settings > Storage shows how much space captures, projects, packs and models use. Clear temporary cache removes temporary files; your photos are not affected."),
  ("Will I lose my photos if I uninstall Photo Plus?", "Photos you saved to your gallery stay. Projects kept inside Photo Plus are removed with the app, so save the ones you want to keep first."),
 ]),
 ("packs", "Packs", "image", [
  ("What are photoshoot packs?", "Packs are ready-made sets of scenes, looks and setups for a style of photoshoot. Browse them and see previews for free; unlocking, downloading and applying packs is Premium."),
 ]),
 ("premium", "Premium & payments", "crown", [
  ("What do I get with Premium?", "Every tool and every filter, style, backdrop and prop; Scene Swap, Relight and hair styles; Glide up to 20 seconds; HD and 4K downloads; no watermark; Batch edit for up to 50 photos; saved setups and packs."),
  ("What plans are there?", "Monthly, 3 months and yearly. The longer plans cost less per month. Prices are shown on the Premium page in your currency."),
  ("How does the free trial work?", "New subscribers get 7 days free. You are charged only when the trial ends, and you can cancel before then at no cost."),
  ("How do I cancel?", "Open Google Play > your profile > Payments & subscriptions > Subscriptions > Photo Plus > Cancel. Premium stays until the end of the period you paid for."),
  ("I changed phones. How do I get Premium back?", "Sign in to Google Play with the same Google account on the new phone and open Photo Plus: your subscription is restored from Google Play."),
  ("Who handles payments?", "Google Play. Photo Plus never sees your card or payment details."),
 ]),
 ("rewards", "Rewards", "target", [
  ("What are Rewards?", "On the free plan you can earn Premium time with points: daily check-ins, short tasks, ads and sharing Photo Plus. Points buy a tool for a day or Premium for 1, 3 or 7 days."),
  ("How do I earn points?", "Check in every day (10 to 60 points, more for a longer streak), finish tasks such as taking or editing photos, and watch short ads (20 points each, up to 8 a day)."),
  ("What can I get with points?", "One Premium tool for 24 hours (250 points), or all of Premium for 1 day (600), 3 days (1,500) or 7 days (3,200). You can redeem up to 10 days of Premium every 30 days."),
  ("How does the share reward work?", "Share Photo Plus 30 times to get 2 days of Premium. Each app counts once every 8 hours, and the campaign opens again every 30 days. Share as a post, a status or story, or a message to friends."),
  ("Why didn’t my share count?", "Each app counts once every 8 hours. Sharing to the same app again before then opens it, but doesn’t count. The share sheet shows when each app opens again."),
  ("Why can’t I watch an ad?", "Ads aren’t always available, and there is a short pause between ads and a daily limit. Check your internet connection and try again a little later."),
 ]),
 ("integrity", "Time, updates & connection", "shield", [
  ("Why does Photo Plus say my date and time are wrong?", "Photo Plus checks the time online to keep rewards and Premium time fair. If your phone’s clock is off, set Date & time to automatic in your phone’s settings and tap Check time."),
  ("Why do I need to go online every week?", "Photo Plus checks its integrity and the time online at least once every 7 days. Turn on Wi-Fi or mobile data and tap Check connection; it only takes a moment."),
  ("Why do I have to update?", "Updates keep your photos safe and the app working as it should. When an update is ready you have 7 days, with a reminder every 2 days; after that, Photo Plus asks you to update before you continue. Your photos and projects stay as they are."),
 ]),
 ("language", "Language", "globe", [
  ("How do I change the language?", "Settings > Language. Choose the phone’s language or one of English, Español, Português, Français, العربية, हिन्दी, Bahasa Indonesia, Русский or 简体中文. Photo Plus restarts in that language."),
  ("Why is the app right to left?", "Arabic is read right to left, so Photo Plus lays the app out that way when Arabic is chosen."),
 ]),
 ("privacy", "Privacy & permissions", "lock", [
  ("Are my photos uploaded anywhere?", "No. Photo Plus processes your photos and videos on your phone. They leave your phone only when you share or save them somewhere yourself."),
  ("Why does Photo Plus ask for the camera and microphone?", "The camera to take photos and videos, and the microphone to record sound in Glide videos. You can change this any time in your phone’s settings."),
  ("Why does Photo Plus ask for my photos?", "To open the photos you choose for editing. On newer Android versions you can allow only selected photos."),
  ("Why does Photo Plus send notifications?", "Only to remind you when an update is waiting. You can turn notifications off in your phone’s settings."),
  ("How do I change my ad choices?", "Settings > Privacy policy > Ad privacy choices (where the law requires it). You can also reset or delete your advertising ID in your phone’s Settings > Privacy > Ads."),
  ("How do I delete my data?", "Your data is on your phone: clear Photo Plus’s storage in your phone’s settings, or uninstall the app. Photos saved to your gallery stay until you delete them."),
 ]),
 ("trouble", "Troubleshooting", "alert", [
  ("The camera shows a black screen.", "Another app may be using the camera. Close other camera apps, then reopen Photo Plus. If camera access is off, allow it in your phone’s settings."),
  ("A tool is slow or stopped.", "Big photos and 4K take more memory. Close other apps, choose a smaller download size, or clear the temporary cache in Settings > Storage. Photo Plus automatically works at a smaller size on phones with less memory."),
  ("The background cut-out isn’t right.", "Photos with clear light and a person who stands apart from the background work best. Use the brush to keep or remove parts, and the hair brush for fine hair."),
  ("I can’t share to an app.", "Make sure the app is installed and up to date. Some apps take only the picture; the words are also copied, so you can paste them."),
  ("My purchase isn’t showing.", "Check that you’re signed in to Google Play with the account you paid with, connect to the internet and reopen Photo Plus."),
  ("How do I contact support?", "Use the developer contact on Photo Plus’s Google Play page. Tell us your phone model and what happened, and we’ll help."),
 ]),
]

PRIVACY_UPDATED = "Last updated: 26 November 2026"
PRIVACY = [
 ("Who we are", "This policy explains how Photo Plus (the app) handles information. Photo Plus is a photo and video app that works on your phone; you don’t need an account to use it."),
 ("In short", "Your photos and videos are processed on your phone and are not uploaded by us. We don’t ask for your name, email or contacts, and we don’t sell personal information. Ads are shown by Google AdMob, and purchases are handled by Google Play."),
 ("Your photos and videos", "Photos and videos you take, open or edit are processed on your phone. Photo Plus keeps them in its own storage on your phone (Projects) and saves them to your gallery when you choose. They leave your phone only when you share them to another app or save them somewhere yourself."),
 ("Information stored on your phone", "Photo Plus stores your settings, language, projects, edits, saved setups, reward points and plan status on your phone. With the Lucid theme it also reads your wallpaper’s colours (and, where Android allows, the wallpaper picture) and keeps the photo or clip you choose, all on your phone. This information is not sent to us."),
 ("Permissions", "Camera: to take photos and videos. Microphone: to record sound in Glide videos. Photos and videos: to open the ones you choose (on newer Android versions you can allow only selected photos). Notifications: to remind you about updates. Internet: for ads, purchases, the time and update checks. You can change permissions at any time in your phone’s settings."),
 ("Advertising", "On the free plan, Photo Plus shows ads from Google AdMob, including rewarded ads you choose to watch. AdMob may collect your device’s advertising ID, IP address and information about your device and how you interact with ads, to show ads, measure them and prevent fraud. Where the law requires it (for example in the EEA and UK), we ask for your consent through Google’s consent form, and you can change your choice at any time under Ad privacy choices. You can also reset or delete your advertising ID in your phone’s settings. See Google’s privacy policy at policies.google.com/privacy."),
 ("Purchases", "Subscriptions are bought and managed through Google Play. Google processes your payment; Photo Plus receives only the status of your subscription, never your card or payment details."),
 ("Time, integrity and update checks", "To keep rewards and Premium time fair, Photo Plus checks the current time online by contacting Google and Cloudflare servers, and checks Google Play for updates. These requests carry only what every internet request carries, such as your IP address, and contain no photos or personal details."),
 ("Services from Google on your phone", "Some on-device features (face and person detection) use Google ML Kit and Google Play services, which may download models and send diagnostic and usage information to Google under Google’s terms. Your photos are processed on your phone and are not sent by these features."),
 ("Sharing Photo Plus", "When you share Photo Plus from Rewards, the app you choose receives the picture and words, and the words are copied to your clipboard so you can paste them. The link carries a tag that says it came from a share in the app; it does not identify you."),
 ("What we don’t do", "We don’t require an account, we don’t collect your name, email, contacts or location, we don’t use analytics or tracking tools of our own, and we don’t sell or rent personal information."),
 ("Children", "Photo Plus is not directed at children under 13, and we do not knowingly collect personal information from children."),
 ("Keeping and deleting your information", "Information on your phone stays until you delete it. Delete photos and projects in the app, clear Photo Plus’s storage in your phone’s settings, or uninstall the app to remove everything it stored. Photos you saved to your gallery stay until you delete them."),
 ("Security", "Your information stays on your phone, protected by Android’s app storage. Connections to the internet use encryption (HTTPS)."),
 ("Your rights", "Depending on where you live, you may have rights to access, correct or delete your personal information and to object to its use. Because Photo Plus keeps your information on your phone, you can exercise most of these rights directly in the app and your phone’s settings; for advertising, use Ad privacy choices or contact Google. For anything else, contact us."),
 ("Changes to this policy", "We may update this policy as Photo Plus changes. The date at the top shows the latest version; important changes will be shown in the app."),
 ("Contact us", "Questions about privacy? Contact us through the developer contact on Photo Plus’s Google Play page."),
]

p = os.path.join(os.path.dirname(__file__), '../../../src/i18n/strings/en.json')
d = json.load(open(p))
for k in [k for k in d if re.match(r'^(faq_(cat_\w+|\w+_\d+_[qa])|privacy_(\d+_(title|body)|updated))$', k)]:
    del d[k]
cats = []
for cid, title, icon, items in FAQ:
    d[f'faq_cat_{cid}'] = title
    for i, (q, a) in enumerate(items, 1):
        d[f'faq_{cid}_{i}_q'] = q
        d[f'faq_{cid}_{i}_a'] = a
    cats.append((cid, icon, len(items)))
d['privacy_updated'] = PRIVACY_UPDATED
for i, (h, b) in enumerate(PRIVACY, 1):
    d[f'privacy_{i}_title'] = h
    d[f'privacy_{i}_body'] = b
json.dump(d, open(p, 'w'), ensure_ascii=False, indent=2)
open(p, 'a').write('\n')

ts = os.path.join(os.path.dirname(__file__), '../../../src/services/help/faqIndex.ts')
os.makedirs(os.path.dirname(ts), exist_ok=True)
lines = ["// Generated by tools/i18n/content/faq_en.py. Do not edit: edit the FAQ there.",
         "import type { IconName } from '../../components/Icon';", "",
         "/** FAQ topics: id, icon and how many questions (keys faq_<id>_<n>_q / _a, title faq_cat_<id>). */",
         "export const FAQ_TOPICS: readonly { id: string; icon: IconName; count: number }[] = ["]
lines += [f"  {{ id: '{c}', icon: '{ic}', count: {n} }}," for c, ic, n in cats]
lines += ["];", "", f"/** Privacy policy sections (keys privacy_<n>_title / _body). */", f"export const PRIVACY_SECTIONS = {len(PRIVACY)};", ""]
open(ts, 'w').write("\n".join(lines))
md = os.path.join(os.path.dirname(__file__), '../../../docs/PRIVACY_POLICY.md')
with open(md, 'w') as out:
    out.write('# Photo Plus privacy policy\n\n')
    out.write(f'_{PRIVACY_UPDATED}_\n\n')
    out.write('<!-- Generated by tools/i18n/content/faq_en.py (the same words as the app\'s Privacy policy screen). Host this page and give its address in Play Console > App content > Privacy policy. -->\n\n')
    for i, (h, b) in enumerate(PRIVACY, 1):
        out.write(f'## {i}. {h}\n\n{b}\n\n')
print(sum(n for _, _, n in cats), 'questions in', len(cats), 'topics;', len(PRIVACY), 'privacy sections')
