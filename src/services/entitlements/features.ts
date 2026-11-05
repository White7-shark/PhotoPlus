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
  shareTo: f('library.share', 'Share to other apps', 'editing', 'free'),
  cropRotate: f('editing.cropRotate', 'Crop and rotate', 'editing', 'free'),
  basicAdjustments: f('editing.basicAdjustments', 'Exposure, brightness & contrast', 'editing', 'free'),
  basicFilters: f('editing.basicFilters', 'Basic filters', 'editing', 'free'),
  basicRetouch: f('editing.basicRetouch', 'Basic retouching', 'editing', 'free'),
  basicPortrait: f('editing.basicPortrait', 'Basic focus & portrait effects', 'editing', 'free'),
  basicSharpen: f('editing.basicSharpen', 'Basic sharpening', 'editing', 'free'),
  watermarkStyles: f('editing.watermarkStyles', 'Watermark styles & positioning', 'editing', 'free'),
  marketplaceBrowse: f('marketplace.browse', 'Browse photoshoot packs', 'marketplace', 'free'),
  packPreview: f('marketplace.preview', 'Pack previews & descriptions', 'marketplace', 'free'),
  premiumDemos: f('marketplace.premiumDemos', 'Premium feature demonstrations', 'marketplace', 'free'),

  // Premium plan
  advancedCamera: f('camera.advanced', 'Advanced camera & manual controls', 'camera', 'premium'),
  multiShot: f('camera.multiShot', 'Moonlight & Ultra Detail multi-shot modes', 'camera', 'premium'),
  vibeMatch: f('camera.vibeMatch', 'Vibe Match: shoot in the style of any photo', 'camera', 'premium'),
  transform: f('camera.transform', 'Transform: the look of a high-end camera lens', 'camera', 'premium'),
  advancedEditing: f('editing.advanced', 'Advanced editing tools', 'editing', 'premium'),
  backgroundReplace: f('replication.background', 'Advanced background removal & replacement', 'replication', 'premium'),
  poseGuidance: f('assistant.pose', 'Pose guidance & matching', 'assistant', 'premium'),
  shootReplication: f('replication.simulate', 'Photoshoot simulation & replication', 'replication', 'premium'),
  lightingShadows: f('replication.lighting', 'Advanced lighting & shadow effects', 'replication', 'premium'),
  advancedRetouchGrade: f('editing.advancedRetouchGrade', 'Advanced retouching & colour grading', 'editing', 'premium'),
  premiumExport: f('editing.premiumExport', 'Premium export options', 'editing', 'premium'),
  customWatermark: f('editing.customWatermark', 'Custom watermark & branding', 'editing', 'premium'),
  packAccess: f('marketplace.packAccess', 'Unlock, download & apply photoshoot packs', 'marketplace', 'premium'),
  clothingTransform: f('replication.clothing', 'Clothing transformation (compatible packs)', 'replication', 'premium'),
} as const;

export type FeatureKey = keyof typeof FEATURES;

/**
 * Premium features the free plan may still try a few times a day (the count
 * resets at local midnight). Premium has no limit.
 */
export const FREE_DAILY_ALLOWANCE: Partial<Record<FeatureKey, number>> = {
  transform: 2,
};

export const PREMIUM_PRICE = { amount: 50, currency: 'GHS', period: 'month' } as const;
