import type { IconName } from '../components/Icon';
import { t as tx } from '../i18n';

export interface PillarInfo {
  id: 'camera' | 'assistant' | 'replication' | 'editing';
  title: string;
  tagline: string;
  icon: IconName;
  gradient: string;
  premium: boolean;
  /** Shown to people: never internal milestones. */
  availability: 'Available now' | 'Coming soon';
}

/** The four pillars from the product spec, as presented on the Studio home. */
export const PILLARS: PillarInfo[] = [
  {
    id: 'camera',
    title: tx('pillars_camera_studio'),
    tagline: tx('pillars_manual_control_pro_modes_composition'),
    icon: 'aperture',
    gradient: 'linear-gradient(140deg, rgba(255,122,69,0.55) 0%, rgba(255,122,69,0.0) 70%)',
    premium: false,
    availability: 'Available now',
  },
  {
    id: 'assistant',
    title: tx('pillars_ai_assistant'),
    tagline: tx('pillars_pose_matching_live_guidance_light'),
    icon: 'sparkles',
    gradient: 'linear-gradient(140deg, rgba(192,77,255,0.55) 0%, rgba(192,77,255,0.0) 70%)',
    premium: true,
    availability: 'Coming soon',
  },
  {
    id: 'replication',
    title: 'Photoshoot Studio',
    tagline: tx('pillars_scene_swap_you_in_a'),
    icon: 'layers',
    gradient: 'linear-gradient(140deg, rgba(91,91,247,0.6) 0%, rgba(91,91,247,0.0) 70%)',
    premium: true,
    availability: 'Available now',
  },
  {
    id: 'editing',
    title: tx('pillars_editing_studio'),
    tagline: tx('pillars_retouch_grade_mask_brand_and'),
    icon: 'sliders',
    gradient: 'linear-gradient(140deg, rgba(45,212,191,0.5) 0%, rgba(45,212,191,0.0) 70%)',
    premium: false,
    availability: 'Coming soon',
  },
];

export function greetingFor(date: Date): { overline: string; title: string } {
  const h = date.getHours();
  if (h >= 5 && h < 8) {
    return { overline: tx('pillars_golden_hour'), title: tx('pillars_the_light_is_soft_go') };
  }
  if (h >= 8 && h < 12) {
    return { overline: tx('pillars_good_morning'), title: tx('pillars_what_are_we_creating_today') };
  }
  if (h >= 12 && h < 17) {
    return { overline: tx('pillars_good_afternoon'), title: tx('pillars_find_the_light_frame_the') };
  }
  if (h >= 17 && h < 19) {
    return { overline: tx('pillars_golden_hour'), title: tx('pillars_the_best_light_of_the') };
  }
  if (h >= 19 && h < 21) {
    return { overline: tx('pillars_blue_hour'), title: tx('pillars_moody_skies_make_cinematic_frames') };
  }
  return { overline: tx('pillars_good_evening'), title: tx('pillars_night_shoots_neon_and_stars') };
}
