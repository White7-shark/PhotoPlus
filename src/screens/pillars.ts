import type { IconName } from '../components/Icon';

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
    title: 'Camera Studio',
    tagline: 'Manual control, pro modes, composition guides',
    icon: 'aperture',
    gradient: 'linear-gradient(140deg, rgba(255,122,69,0.55) 0%, rgba(255,122,69,0.0) 70%)',
    premium: false,
    availability: 'Available now',
  },
  {
    id: 'assistant',
    title: 'AI Assistant',
    tagline: 'Pose matching, live guidance, light analysis',
    icon: 'sparkles',
    gradient: 'linear-gradient(140deg, rgba(192,77,255,0.55) 0%, rgba(192,77,255,0.0) 70%)',
    premium: true,
    availability: 'Coming soon',
  },
  {
    id: 'replication',
    title: 'Replication Studio',
    tagline: 'Scene Swap: you in a new place, clean cut-outs',
    icon: 'layers',
    gradient: 'linear-gradient(140deg, rgba(91,91,247,0.6) 0%, rgba(91,91,247,0.0) 70%)',
    premium: true,
    availability: 'Available now',
  },
  {
    id: 'editing',
    title: 'Editing Studio',
    tagline: 'Retouch, grade, mask, brand and export',
    icon: 'sliders',
    gradient: 'linear-gradient(140deg, rgba(45,212,191,0.5) 0%, rgba(45,212,191,0.0) 70%)',
    premium: false,
    availability: 'Coming soon',
  },
];

export function greetingFor(date: Date): { overline: string; title: string } {
  const h = date.getHours();
  if (h >= 5 && h < 8) {
    return { overline: 'Golden hour', title: 'The light is soft. Go shoot.' };
  }
  if (h >= 8 && h < 12) {
    return { overline: 'Good morning', title: 'What are we creating today?' };
  }
  if (h >= 12 && h < 17) {
    return { overline: 'Good afternoon', title: 'Find the light, frame the story.' };
  }
  if (h >= 17 && h < 19) {
    return { overline: 'Golden hour', title: 'The best light of the day is here.' };
  }
  if (h >= 19 && h < 21) {
    return { overline: 'Blue hour', title: 'Moody skies make cinematic frames.' };
  }
  return { overline: 'Good evening', title: 'Night shoots, neon and stars.' };
}
