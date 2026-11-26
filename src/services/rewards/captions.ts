/**
 * The share campaign's words in each of the app's languages, written by
 * hand (not machine translated): the headline, the captions for a post or
 * status, the notes to friends, and hashtags people use in that language.
 * Each caption is built the same way: the headline first, the link on a
 * line of its own (so it can be tapped) and, for posts, the hashtags last
 * (so the app recognises them).
 */
export type Caption = (link: string, days: number) => string;

export interface CampaignCopy {
  headline: string;
  posts: readonly Caption[];
  friends: readonly Caption[];
  /** Hashtags in the language, after the brand's own (#PhotoPlus ...). */
  hashtags: readonly string[];
}

const en: CampaignCopy = {
  headline: '🎉 You can now do a photoshoot right on your phone! 📸',
  posts: [
    (link, days) => `Meet Photo Plus ✨
✅ Be in a real studio, park or café without leaving home
✅ Pro studio lighting on any photo with Relight
✅ Clean product photos that sell, with real shadows
✅ Flawless beauty, dreamy Spotlight portraits and rich Lively colour
✅ Glide pro video, film strips and collages

No studio. No laptop. No experience needed.
🎁 Try Premium FREE for ${days} days!

📲 Download on Google Play:
${link}`,
    (
      link,
      days,
    ) => `My phone just became a photo studio 🤯 Photo Plus swaps your background for a real studio scene, adds pro lighting, retouches skin naturally and turns product snaps into catalogue shots — in seconds.

🎁 ${days} days FREE trial of Premium

👉 Get it on Google Play:
${link}`,
    (link, days) => `Stop paying for photoshoots 💸 — make yours with Photo Plus ✨
📸 Photoshoot Studio · 💡 Relight · 🛍️ Product Studio · 💄 Beauty & Spotlight · 🎬 Glide video

Perfect for creators, sellers and anyone who loves great photos.
🎁 Start with a ${days}-day free trial.

Download free 👉
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `Hey! 👋 You have to try Photo Plus. I use it to make my photos look like a real photoshoot — new backgrounds, studio lighting, natural beauty retouch, even clean product photos for selling. It all happens on your phone.

You can try Premium FREE for ${days} days 🎁
Get it here 👇
${link}`,
    (
      link,
      days,
    ) => `Look what my phone can do now 😍 Photo Plus puts you in a real studio, relights your photos like a pro and makes product shots look professional — in seconds.

🎁 ${days}-day free trial of Premium
Download it free on Google Play 👇
${link}`,
    (
      link,
      days,
    ) => `Found the best photo app 🔥 Photo Plus: studio backgrounds, pro lighting, beauty retouch, product photos and smooth Glide videos.

Try it with ${days} days free 🎁
${link}`,
  ],
  hashtags: [],
};

const es: CampaignCopy = {
  headline: '🎉 ¡Ya puedes hacer una sesión de fotos desde tu teléfono! 📸',
  posts: [
    (link, days) => `Te presento Photo Plus ✨
✅ Aparece en un estudio, un parque o un café de verdad sin salir de casa
✅ Iluminación de estudio profesional en cualquier foto con Relight
✅ Fotos de producto limpias que venden, con sombras reales
✅ Belleza impecable, retratos Spotlight de ensueño y el color intenso de Lively
✅ Vídeo profesional con Glide, tiras de película y collages

Sin estudio. Sin ordenador. Sin experiencia.
🎁 ¡Prueba Premium GRATIS durante ${days} días!

📲 Descárgala en Google Play:
${link}`,
    (
      link,
      days,
    ) => `Mi teléfono se acaba de convertir en un estudio de fotos 🤯 Photo Plus cambia tu fondo por un estudio real, añade luz profesional, retoca la piel con naturalidad y convierte tus productos en fotos de catálogo, en segundos.

🎁 ${days} días de prueba GRATIS de Premium

👉 Consíguela en Google Play:
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `¡Hola! 👋 Tienes que probar Photo Plus. Lo uso para que mis fotos parezcan de una sesión profesional: fondos nuevos, luz de estudio, retoque natural y hasta fotos de producto para vender. Todo desde el teléfono.

Puedes probar Premium GRATIS durante ${days} días 🎁
Descárgala aquí 👇
${link}`,
    (
      link,
      days,
    ) => `Mira lo que hace mi teléfono ahora 😍 Photo Plus te pone en un estudio de verdad y le da a tus fotos luz profesional en segundos.

🎁 ${days} días de prueba gratis de Premium
Descárgala gratis en Google Play 👇
${link}`,
  ],
  hashtags: ['#SesiónDeFotos', '#FotografíaMóvil', '#EditorDeFotos'],
};

const ptBR: CampaignCopy = {
  headline: '🎉 Agora você pode fazer um ensaio fotográfico direto no seu celular! 📸',
  posts: [
    (link, days) => `Conheça o Photo Plus ✨
✅ Apareça num estúdio, parque ou café de verdade sem sair de casa
✅ Iluminação de estúdio profissional em qualquer foto com o Relight
✅ Fotos de produto limpas que vendem, com sombras reais
✅ Beleza impecável, retratos Spotlight dos sonhos e a cor viva do Lively
✅ Vídeo profissional com o Glide, tiras de filme e colagens

Sem estúdio. Sem computador. Sem experiência.
🎁 Teste o Premium GRÁTIS por ${days} dias!

📲 Baixe no Google Play:
${link}`,
    (
      link,
      days,
    ) => `Meu celular virou um estúdio de fotos 🤯 O Photo Plus troca o seu fundo por um estúdio de verdade, coloca luz profissional, retoca a pele com naturalidade e transforma fotos de produto em fotos de catálogo, em segundos.

🎁 ${days} dias de teste GRÁTIS do Premium

👉 Baixe no Google Play:
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `Oi! 👋 Você precisa testar o Photo Plus. Eu uso pra deixar minhas fotos com cara de ensaio profissional: fundos novos, luz de estúdio, retoque natural e até fotos de produto pra vender. Tudo no celular.

Dá pra testar o Premium GRÁTIS por ${days} dias 🎁
Baixe aqui 👇
${link}`,
    (
      link,
      days,
    ) => `Olha o que meu celular faz agora 😍 O Photo Plus te coloca num estúdio de verdade e deixa suas fotos com luz profissional em segundos.

🎁 ${days} dias grátis de Premium
Baixe grátis no Google Play 👇
${link}`,
  ],
  hashtags: ['#EnsaioFotográfico', '#FotografiaComCelular', '#EditorDeFotos'],
};

const fr: CampaignCopy = {
  headline: '🎉 Vous pouvez maintenant faire un shooting photo directement sur votre téléphone ! 📸',
  posts: [
    (link, days) => `Découvrez Photo Plus ✨
✅ Posez dans un vrai studio, un parc ou un café sans quitter la maison
✅ Un éclairage de studio professionnel sur n’importe quelle photo avec Relight
✅ Des photos produit nettes qui font vendre, avec de vraies ombres
✅ Une beauté parfaite, des portraits Spotlight de rêve et les couleurs riches de Lively
✅ Des vidéos pro avec Glide, des bandes de film et des collages

Pas de studio. Pas d’ordinateur. Aucune expérience nécessaire.
🎁 Essayez Premium GRATUITEMENT pendant ${days} jours !

📲 Téléchargez-la sur Google Play :
${link}`,
    (
      link,
      days,
    ) => `Mon téléphone est devenu un studio photo 🤯 Photo Plus remplace votre arrière-plan par un vrai studio, ajoute une lumière professionnelle, retouche la peau naturellement et transforme vos produits en photos de catalogue, en quelques secondes.

🎁 ${days} jours d’essai GRATUIT de Premium

👉 Disponible sur Google Play :
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `Coucou ! 👋 Il faut que tu essaies Photo Plus. Je l’utilise pour que mes photos ressemblent à un vrai shooting : nouveaux fonds, lumière de studio, retouche naturelle et même des photos produit pour vendre. Tout se fait sur le téléphone.

Tu peux essayer Premium GRATUITEMENT pendant ${days} jours 🎁
C’est par ici 👇
${link}`,
    (
      link,
      days,
    ) => `Regarde ce que mon téléphone sait faire maintenant 😍 Photo Plus te place dans un vrai studio et donne à tes photos une lumière de pro en quelques secondes.

🎁 ${days} jours d’essai gratuit de Premium
À télécharger gratuitement sur Google Play 👇
${link}`,
  ],
  hashtags: ['#ShootingPhoto', '#PhotoMobile', '#RetouchePhoto'],
};

const ar: CampaignCopy = {
  headline: '🎉 أصبح بإمكانك الآن إجراء جلسة تصوير على هاتفك مباشرةً! 📸',
  posts: [
    (link, days) => `تعرّف على Photo Plus ✨
✅ اظهر في استوديو أو حديقة أو مقهى حقيقي دون أن تغادر منزلك
✅ إضاءة استوديو احترافية على أي صورة مع Relight
✅ صور منتجات نظيفة تبيع، بظلال حقيقية
✅ جمال مثالي وصور Spotlight حالمة وألوان Lively الغنية
✅ فيديو احترافي مع Glide وشرائط أفلام وكولاج

بلا استوديو. بلا كمبيوتر. بلا خبرة.
🎁 جرّب Premium مجانًا لمدة ${days} أيام!

📲 نزّله من Google Play:
${link}`,
    (
      link,
      days,
    ) => `هاتفي أصبح استوديو تصوير 🤯 يستبدل Photo Plus خلفيتك باستوديو حقيقي، ويضيف إضاءة احترافية، ويحسّن البشرة بشكل طبيعي، ويحوّل صور المنتجات إلى صور كتالوج خلال ثوانٍ.

🎁 تجربة مجانية لـ Premium لمدة ${days} أيام

👉 احصل عليه من Google Play:
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `مرحبًا! 👋 لازم تجرّب Photo Plus. أستخدمه لتبدو صوري كأنها من جلسة تصوير حقيقية: خلفيات جديدة وإضاءة استوديو وتجميل طبيعي، وحتى صور منتجات للبيع. كل ذلك على الهاتف.

تقدر تجرّب Premium مجانًا لمدة ${days} أيام 🎁
حمّله من هنا 👇
${link}`,
    (link, days) => `شوف ماذا يفعل هاتفي الآن 😍 يضعك Photo Plus في استوديو حقيقي ويمنح صورك إضاءة احترافية خلال ثوانٍ.

🎁 تجربة مجانية لـ Premium لمدة ${days} أيام
نزّله مجانًا من Google Play 👇
${link}`,
  ],
  hashtags: ['#جلسة_تصوير', '#تصوير_بالجوال', '#تعديل_الصور'],
};

const hi: CampaignCopy = {
  headline: '🎉 अब आप सीधे अपने फ़ोन पर फ़ोटोशूट कर सकते हैं! 📸',
  posts: [
    (link, days) => `मिलिए Photo Plus से ✨
✅ घर से निकले बिना असली स्टूडियो, पार्क या कैफ़े में दिखें
✅ Relight के साथ किसी भी फ़ोटो पर प्रोफ़ेशनल स्टूडियो लाइट
✅ असली परछाइयों के साथ साफ़-सुथरी प्रोडक्ट फ़ोटो, जो बिकें
✅ बेदाग़ ब्यूटी, सपनों जैसे Spotlight पोर्ट्रेट और Lively के खिले-खिले रंग
✅ Glide प्रो वीडियो, फ़िल्म स्ट्रिप और कोलाज

न स्टूडियो, न लैपटॉप, न कोई अनुभव चाहिए।
🎁 Premium ${days} दिन तक मुफ़्त आज़माएँ!

📲 Google Play से डाउनलोड करें:
${link}`,
    (
      link,
      days,
    ) => `मेरा फ़ोन अब फ़ोटो स्टूडियो बन गया है 🤯 Photo Plus आपका बैकग्राउंड असली स्टूडियो में बदल देता है, प्रोफ़ेशनल लाइट जोड़ता है, स्किन को नैचुरल तरीके से निखारता है और प्रोडक्ट फ़ोटो को कैटलॉग जैसा बना देता है — कुछ ही सेकंड में।

🎁 Premium का ${days} दिन का मुफ़्त ट्रायल

👉 Google Play पर पाएँ:
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `हाय! 👋 तुम्हें Photo Plus ज़रूर आज़माना चाहिए। इससे मेरी फ़ोटो असली फ़ोटोशूट जैसी बन जाती हैं — नए बैकग्राउंड, स्टूडियो लाइट, नैचुरल ब्यूटी टच और बेचने के लिए प्रोडक्ट फ़ोटो भी। सब कुछ फ़ोन पर।

Premium ${days} दिन तक मुफ़्त है 🎁
यहाँ से डाउनलोड करो 👇
${link}`,
    (
      link,
      days,
    ) => `देखो मेरा फ़ोन अब क्या-क्या कर सकता है 😍 Photo Plus तुम्हें असली स्टूडियो में ले जाता है और फ़ोटो को सेकंडों में प्रो लाइट देता है।

🎁 Premium का ${days} दिन का मुफ़्त ट्रायल
Google Play से मुफ़्त डाउनलोड करो 👇
${link}`,
  ],
  hashtags: ['#फ़ोटोशूट', '#मोबाइलफ़ोटोग्राफ़ी', '#PhotoEditingIndia'],
};

const id: CampaignCopy = {
  headline: '🎉 Sekarang kamu bisa photoshoot langsung di ponselmu! 📸',
  posts: [
    (link, days) => `Kenalan dengan Photo Plus ✨
✅ Tampil di studio, taman, atau kafe sungguhan tanpa keluar rumah
✅ Pencahayaan studio profesional di foto apa pun dengan Relight
✅ Foto produk yang bersih dan menjual, dengan bayangan asli
✅ Kecantikan sempurna, potret Spotlight yang memukau, dan warna Lively yang hidup
✅ Video pro dengan Glide, film strip, dan kolase

Tanpa studio. Tanpa laptop. Tanpa pengalaman.
🎁 Coba Premium GRATIS selama ${days} hari!

📲 Unduh di Google Play:
${link}`,
    (
      link,
      days,
    ) => `Ponselku sekarang jadi studio foto 🤯 Photo Plus mengganti latar belakangmu dengan studio sungguhan, menambahkan cahaya profesional, merapikan kulit secara alami, dan mengubah foto produk jadi foto katalog — dalam hitungan detik.

🎁 Uji coba GRATIS Premium ${days} hari

👉 Dapatkan di Google Play:
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `Hai! 👋 Kamu wajib coba Photo Plus. Aku pakai buat bikin fotoku kayak hasil photoshoot beneran — latar baru, cahaya studio, retouch alami, sampai foto produk buat jualan. Semuanya di ponsel.

Kamu bisa coba Premium GRATIS ${days} hari 🎁
Unduh di sini 👇
${link}`,
    (
      link,
      days,
    ) => `Lihat apa yang bisa dilakukan ponselku sekarang 😍 Photo Plus menaruhmu di studio sungguhan dan memberi fotomu cahaya profesional dalam hitungan detik.

🎁 Uji coba gratis Premium ${days} hari
Unduh gratis di Google Play 👇
${link}`,
  ],
  hashtags: ['#Photoshoot', '#FotografiPonsel', '#EditFoto'],
};

const ru: CampaignCopy = {
  headline: '🎉 Теперь фотосессию можно устроить прямо на телефоне! 📸',
  posts: [
    (link, days) => `Знакомьтесь: Photo Plus ✨
✅ Окажитесь в настоящей студии, парке или кафе, не выходя из дома
✅ Профессиональный студийный свет на любом фото с Relight
✅ Чистые фото товаров, которые продают, с настоящими тенями
✅ Безупречная красота, мечтательные портреты Spotlight и сочный цвет Lively
✅ Профессиональное видео Glide, киноленты и коллажи

Без студии. Без ноутбука. Без опыта.
🎁 Попробуйте Premium БЕСПЛАТНО ${days} дней!

📲 Скачайте в Google Play:
${link}`,
    (
      link,
      days,
    ) => `Мой телефон стал фотостудией 🤯 Photo Plus меняет фон на настоящую студию, добавляет профессиональный свет, естественно ретуширует кожу и превращает фото товаров в каталожные снимки — за секунды.

🎁 ${days} дней Premium БЕСПЛАТНО

👉 Скачайте в Google Play:
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `Привет! 👋 Попробуй Photo Plus. Я делаю в нём фото, как с настоящей фотосессии: новый фон, студийный свет, естественная ретушь и даже фото товаров для продажи. И всё это на телефоне.

Premium можно попробовать БЕСПЛАТНО ${days} дней 🎁
Скачать здесь 👇
${link}`,
    (
      link,
      days,
    ) => `Смотри, что теперь умеет мой телефон 😍 Photo Plus переносит тебя в настоящую студию и за секунды ставит на фото профессиональный свет.

🎁 ${days} дней Premium бесплатно
Скачай бесплатно в Google Play 👇
${link}`,
  ],
  hashtags: ['#Фотосессия', '#МобильнаяФотография', '#ОбработкаФото'],
};

const zhCN: CampaignCopy = {
  headline: '🎉 现在，直接在手机上就能拍一场写真！📸',
  posts: [
    (link, days) => `认识一下 Photo Plus ✨
✅ 足不出户，就能置身真实的影棚、公园或咖啡馆
✅ 用 Relight 为任何照片打上专业影棚光
✅ 干净利落、带真实阴影的产品图，更好卖
✅ 完美美颜、梦幻的 Spotlight 人像和 Lively 的鲜活色彩
✅ Glide 专业视频、胶片条和拼图

无需影棚，无需电脑，无需经验。
🎁 免费试用 Premium ${days} 天！

📲 在 Google Play 下载：
${link}`,
    (
      link,
      days,
    ) => `我的手机变成了摄影棚 🤯 Photo Plus 能把背景换成真实影棚，加上专业灯光，自然地美化肌肤，还能把产品随手拍变成画册级大片——只需几秒。

🎁 Premium 免费试用 ${days} 天

👉 在 Google Play 获取：
${link}`,
  ],
  friends: [
    (
      link,
      days,
    ) => `嗨！👋 你一定要试试 Photo Plus。我用它把照片拍得像真正的写真：换新背景、影棚灯光、自然美颜，连卖货用的产品图都能搞定。全在手机上完成。

Premium 可以免费试用 ${days} 天 🎁
点这里下载 👇
${link}`,
    (link, days) => `看看我的手机现在能做什么 😍 Photo Plus 让你置身真实影棚，几秒钟就给照片打上专业灯光。

🎁 Premium 免费试用 ${days} 天
在 Google Play 免费下载 👇
${link}`,
  ],
  hashtags: ['#写真', '#手机摄影', '#修图'],
};

/** By the app's language tag (src/i18n/languages.json). */
export const CAMPAIGN: Readonly<Record<string, CampaignCopy>> = {
  en,
  es,
  'pt-BR': ptBR,
  fr,
  ar,
  hi,
  id,
  ru,
  'zh-CN': zhCN,
};
