/**
 * Feature catalogue — the single place that says which plan unlocks what.
 * Derived verbatim from docs/PRODUCT_SPEC.md §2. Every feature built in
 * later phases must be registered here before it ships, so free/premium
 * boundaries cannot drift as development progresses.
 */

export type Plan = 'free' | 'premium';
export type Pillar = 'camera' | 'assistant' | 'replication' | 'editing' | 'marketplace';

export interface FeatureDefinition {
  id: string;
  title: string;
  pillar: Pillar;
  plan: Plan;
}

const f = (id: string, title: string, pillar: Pillar, plan: Plan): FeatureDefinition => ({ id, title, pillar, plan });

export const FEATURES = {
  // Free plan
  cameraCapture: f('camera.capture', 'Camera capture & basic settings', 'camera', 'free'),
  clarity: f('camera.clarity', 'Clarity: sharper, cleaner single photos', 'camera', 'free'),
  spotlight: f('camera.spotlight', 'Spotlight portraits & Glow skin retouch', 'camera', 'free'),
  beautyFilters: f('camera.beauty', 'Beauty mode: Natural Glow & Soft Radiance, live in the camera', 'camera', 'free'),
  poseBasic: f(
    'assistant.poseBasic',
    'Photo assistant: grids, level, face position, simple pose demos & tips',
    'assistant',
    'free',
  ),
  backdrops: f('editing.backdrops', 'Backdrops: Signature, Soft Studio & Stage backgrounds', 'editing', 'free'),
  skinTones: f('editing.skinTones', 'Skin Tone: Ivory, Honey, Caramel & Espresso', 'editing', 'free'),
  glide: f('camera.glide', 'Glide: 7-second pro video, steady and sharp, with Spotlight & Glow', 'camera', 'free'),
  glideLong: f('camera.glideLong', 'Glide up to 20 seconds, with no watermark', 'camera', 'premium'),
  glideScene: f('camera.glideScene', 'Glide in a new scene: you moved into any background, true to its size', 'camera', 'premium'),
  shareTo: f('library.share', 'Share to other apps', 'editing', 'free'),
  cropRotate: f('editing.cropRotate', 'Crop and rotate', 'editing', 'free'),
  basicAdjustments: f('editing.basicAdjustments', 'Exposure, brightness & contrast', 'editing', 'free'),
  basicFilters: f('editing.basicFilters', 'Basic filters', 'editing', 'free'),
  basicRetouch: f('editing.basicRetouch', 'Basic retouching', 'editing', 'free'),
  basicPortrait: f('editing.basicPortrait', 'Basic focus & portrait effects', 'editing', 'free'),
  basicSharpen: f('editing.basicSharpen', 'Basic sharpening', 'editing', 'free'),
  watermarkStyles: f('editing.watermarkStyles', 'Watermark styles & positioning', 'editing', 'free'),
  props: f('editing.props', 'Props: hats, glasses and outfit pieces', 'editing', 'free'),
  studioBasics: f('replication.studioBasics', 'Studio basics: soft and bright light, gradient backdrops, vignette, warmth and simple grades', 'replication', 'free'),
  frames: f('editing.frames', 'Photo frames', 'editing', 'free'),
  filmCollage: f('replication.filmCollage', 'Film & Collage: classic and photo-booth film strips, ten looks for every frame, ten collage layouts', 'replication', 'free'),
  productPhotos: f('studio.product', 'Product photos: cut out onto white, paper or grey, with natural shadows and colour correction', 'replication', 'free'),
  projects: f('library.projects', 'Projects: organise shoots, keep every original, compare before and after', 'editing', 'free'),
  marketplaceBrowse: f('marketplace.browse', 'Browse photoshoot packs', 'marketplace', 'free'),
  packPreview: f('marketplace.preview', 'Pack previews & descriptions', 'marketplace', 'free'),
  premiumDemos: f('marketplace.premiumDemos', 'Premium feature demonstrations', 'marketplace', 'free'),

  // Premium plan
  advancedCamera: f('camera.advanced', 'Advanced camera & manual controls', 'camera', 'premium'),
  multiShot: f('camera.multiShot', 'Moonlight & Ultra Detail multi-shot modes', 'camera', 'premium'),
  vibeMatch: f('camera.vibeMatch', 'Vibe Match: shoot in the style of any photo', 'camera', 'premium'),
  transform: f('camera.transform', 'Transform: the look of a high-end camera lens', 'camera', 'premium'),
  advancedEditing: f('editing.advanced', 'Advanced editing tools', 'editing', 'premium'),
  studioLight: f(
    'replication.light',
    'Studio light: Rembrandt, split, rim, neon and golden hour light, light you can turn and soften, film looks and composition templates',
    'replication',
    'premium',
  ),
  exportHd: f('studio.exportHd', 'Downloads at 720p, 1080p, HD and 4K', 'replication', 'premium'),
  relight: f('replication.relight', 'Relight: studio light on your own photo, with the background darkened, softened or coloured', 'replication', 'premium'),
  filmPremium: f('replication.filmPremium', 'Amber negative and cinema film strips in Film & Collage', 'replication', 'premium'),
  studioHair: f('replication.hair', 'Hair in the Photoshoot Studio: 18 colours, 23 women’s styles, and dreadlocks, rasta, afro or bald', 'replication', 'premium'),
  backgroundReplace: f(
    'replication.background',
    'Scene Swap: you in a new scene, cut-outs with clean edges, background libraries',
    'replication',
    'premium',
  ),
  poseGuidance: f(
    'assistant.pose',
    'Pose guidance: the full pose library, poses from your photos, live matching & auto capture',
    'assistant',
    'premium',
  ),
  shootReplication: f('replication.simulate', 'Photoshoot simulation & replication', 'replication', 'premium'),
  lightingShadows: f('replication.lighting', 'Advanced lighting & shadow effects', 'replication', 'premium'),
  advancedRetouchGrade: f('editing.advancedRetouchGrade', 'Advanced retouching & colour grading', 'editing', 'premium'),
  beautyPremium: f('camera.beautyPremium', 'The full beauty collection: 11 premium filters', 'camera', 'premium'),
  backdropsPremium: f(
    'editing.backdropsPremium',
    'The full Backdrops collection: 11 premium designs',
    'editing',
    'premium',
  ),
  skinTonesPremium: f(
    'editing.skinTonesPremium',
    'Every Skin Tone shade: 9 more, Porcelain Rose to Ebony Glow',
    'editing',
    'premium',
  ),
  advancedPortrait: f(
    'editing.advancedPortrait',
    'Advanced portrait: precise face, light and background controls',
    'editing',
    'premium',
  ),
  premiumExport: f('editing.premiumExport', 'Premium export options', 'editing', 'premium'),
  customWatermark: f('editing.customWatermark', 'Custom watermark & branding', 'editing', 'premium'),
  premiumProps: f('editing.premiumProps', 'The full props collection', 'editing', 'premium'),
  premiumFrames: f('editing.premiumFrames', 'Signature frame designs', 'editing', 'premium'),
  customProps: f('editing.customProps', 'Make your own props from your photos', 'editing', 'premium'),
  cameraProps: f('camera.props', 'Props in the camera', 'camera', 'free'),
  packAccess: f('marketplace.packAccess', 'Unlock, download & apply photoshoot packs', 'marketplace', 'premium'),
  productPro: f(
    'studio.productPro',
    'Pro product setups: tabletop, spotlit, scene and transparent backdrops, cast shadows, reflections, layout templates and packs',
    'replication',
    'premium',
  ),
  batchEditing: f('editing.batch', 'Batch editing: one setup or look on up to 50 photos at once', 'editing', 'premium'),
  savedSetups: f('editing.savedSetups', 'Save your setups and looks, and reuse them in every project', 'editing', 'premium'),
  clothingTransform: f('replication.clothing', 'Clothing transformation (compatible packs)', 'replication', 'premium'),
  masters: f('studio.masters', 'Photo Arts: your photo painted as twenty great artists worked', 'replication', 'premium'),
  lucidCustom: f('appearance.lucidCustom', 'Lucid with your own photo or video clip behind the glass', 'editing', 'premium'),
} as const;

export type FeatureKey = keyof typeof FEATURES;

/**
 * Premium features the free plan may still try a few times a day (the count
 * resets at local midnight). Premium has no limit.
 */
export const FREE_DAILY_ALLOWANCE: Partial<Record<FeatureKey, number>> = {
  transform: 2,
  masters: 1,
};

export const PREMIUM_PRICE = { amount: 50, currency: 'GHS', period: 'month' } as const;
