import type { BottomTabScreenProps } from '@react-navigation/bottom-tabs';
import type { CompositeScreenProps, NavigatorScreenParams } from '@react-navigation/native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import type { GlideClip } from '../services/camera/glide';
import type { StudioKind } from '../services/database/repositories/StudioProjectsRepository';

/**
 * A studio tool opened from a project: photo (the one to work with), projectId
 * (where finished works are kept), edit (a work's settings, to edit it again).
 */
export type StudioToolParams = { photo?: string; projectId?: string; edit?: Record<string, unknown> } | undefined;

export type TabParamList = {
  Studio: undefined;
  Projects: undefined;
  /** returnTo: a screen the photo goes straight to (the Photoshoot Studio). */
  Camera: { returnTo?: 'SceneSwap' | 'FilmCollage' | 'Relight' | 'ProductStudio' } | undefined;
  Packs: undefined;
  Settings: undefined;
};

export type RootStackParamList = {
  Onboarding: undefined;
  Main: NavigatorScreenParams<TabParamList> | undefined;
  DeviceInfo: undefined;
  Diagnostics: undefined;
  PhotoPreview: { imageId: string };
  Editor: { imageId: string };
  CreateProp: undefined;
  /** A finished Glide clip (before/after, share, save). */
  GlidePreview: { clip: GlideClip };
  /** The photoshoot studio's tools. */
  PhotoshootStudio: undefined;
  /** Scene Swap: a person cut out and placed in another scene. */
  /** photo: a photo just taken in the app's camera, opened straight away. */
  SceneSwap: StudioToolParams;
  /** Film & Collage: photos as a film strip or a collage; photo: one just taken in the camera. */
  /** Relight: studio light on the person in their own photo; photo: one just taken in the camera. */
  Relight: { photo?: string } | undefined;
  FilmCollage: { photo?: string } | undefined;
  /** Masters: the photo painted as one of twenty great artists worked; photo: one to open straight away. */
  Masters: StudioToolParams;
  /** Photo Motion: a photo as a living video loop; photo: one to open straight away. */
  Motion: StudioToolParams;
  /** A studio's projects (Photoshoot, Photo Arts, Photo Motion); photo: one just taken, to go into the project chosen. */
  StudioProjects: { studio: StudioKind; photo?: string };
  /** One studio project: its photos and its gallery of finished works. */
  StudioProject: { projectId: string; tab?: 'photos' | 'gallery' };
  /** A finished work: view, download, share, edit again, rename, delete (fresh: just made). */
  StudioItem: { itemId: string; fresh?: boolean };
  /** The live studio: the work drawn or painted stroke by stroke, then kept in the project. */
  ArtLive: { job: Record<string, unknown>; photo: string; projectId: string; title: string };
  /**
   * Product Studio: a product photo made a catalogue shot. photo: one just
   * taken in the camera; imageId: a library photo; projectId: the project it
   * is for (its pack and defaults; results are saved there).
   */
  ProductStudio: { photo?: string; imageId?: string; projectId?: string } | undefined;
  /** Batch editing: one setup or look on many photos (of a project, or those given). */
  Batch: { projectId?: string; imageIds?: string[] } | undefined;
  /** A project: its photos, pack and defaults. */
  Project: { projectId: string };
  /** Premium plans (monthly, 3 months, yearly; a 7-day free trial). */
  Premium: undefined;
  /** Rewards (free plan): points from check-ins, ads and tasks; the share campaign. */
  Rewards: undefined;
  /** The app's language (English and 8 more, or the phone's). */
  Language: undefined;
  /** Help & FAQ: every tool, plan and setting, by topic, with search. */
  Help: undefined;
  /** The privacy policy, with ad privacy choices. */
  Privacy: undefined;
};

export type RootScreenProps<T extends keyof RootStackParamList> = NativeStackScreenProps<RootStackParamList, T>;

export type TabScreenProps<T extends keyof TabParamList> = CompositeScreenProps<
  BottomTabScreenProps<TabParamList, T>,
  NativeStackScreenProps<RootStackParamList>
>;
