import type { BottomTabScreenProps } from '@react-navigation/bottom-tabs';
import type { CompositeScreenProps, NavigatorScreenParams } from '@react-navigation/native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import type { GlideClip } from '../services/camera/glide';

export type TabParamList = {
  Studio: undefined;
  Projects: undefined;
  Camera: undefined;
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
  SceneSwap: undefined;
};

export type RootScreenProps<T extends keyof RootStackParamList> = NativeStackScreenProps<RootStackParamList, T>;

export type TabScreenProps<T extends keyof TabParamList> = CompositeScreenProps<
  BottomTabScreenProps<TabParamList, T>,
  NativeStackScreenProps<RootStackParamList>
>;
