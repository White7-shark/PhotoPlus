import { createBottomTabNavigator, type BottomTabBarProps } from '@react-navigation/bottom-tabs';
import { DarkTheme, DefaultTheme, NavigationContainer, type Theme as NavTheme } from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import React, { useMemo } from 'react';

import { ErrorBoundary } from '../components/ErrorBoundary';
import { CameraScreen } from '../screens/camera/CameraScreen';
import { CreatePropScreen } from '../screens/CreatePropScreen';
import { EditorScreen } from '../screens/editor/EditorScreen';
import { GlidePreviewScreen } from '../screens/GlidePreviewScreen';
import { PhotoPreviewScreen } from '../screens/PhotoPreviewScreen';
import { DeviceInfoScreen } from '../screens/DeviceInfoScreen';
import { DiagnosticsScreen } from '../screens/DiagnosticsScreen';
import { HomeScreen } from '../screens/HomeScreen';
import { MarketplaceScreen } from '../screens/MarketplaceScreen';
import { OnboardingScreen } from '../screens/OnboardingScreen';
import { ProjectsScreen } from '../screens/ProjectsScreen';
import { SettingsScreen } from '../screens/SettingsScreen';
import { PhotoshootStudioScreen } from '../screens/studio/PhotoshootStudioScreen';
import { FilmCollageScreen } from '../screens/studio/FilmCollageScreen';
import { RelightScreen } from '../screens/studio/RelightScreen';
import { MastersScreen } from '../screens/studio/MastersScreen';
import { BatchScreen } from '../screens/product/BatchScreen';
import { ProductStudioScreen } from '../screens/product/ProductStudioScreen';
import { ProjectScreen } from '../screens/product/ProjectScreen';
import { PremiumScreen } from '../screens/premium/PremiumScreen';
import { RewardsScreen } from '../screens/premium/RewardsScreen';
import { LanguageScreen } from '../screens/LanguageScreen';
import { HelpScreen } from '../screens/HelpScreen';
import { PrivacyScreen } from '../screens/PrivacyScreen';
import { navigationRef } from './ref';
import { SceneSwapScreen } from '../screens/studio/SceneSwapScreen';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { GlassTabBar } from './GlassTabBar';
import type { RootStackParamList, TabParamList } from './types';

const Stack = createNativeStackNavigator<RootStackParamList>();
const Tabs = createBottomTabNavigator<TabParamList>();

/** Wraps each screen in its own error boundary so one crash never takes down the app. */
function guarded<P extends object>(Component: React.ComponentType<P>, area: string) {
  const Guarded = (props: P) => (
    <ErrorBoundary area={area}>
      <Component {...props} />
    </ErrorBoundary>
  );
  Guarded.displayName = `Guarded(${area})`;
  return Guarded;
}

const Studio = guarded(HomeScreen, 'Studio');
const Projects = guarded(ProjectsScreen, 'Projects');
const Camera = guarded(CameraScreen, 'Camera');
const Packs = guarded(MarketplaceScreen, 'Marketplace');
const Settings = guarded(SettingsScreen, 'Settings');
const Onboarding = guarded(OnboardingScreen, 'Welcome');
const DeviceInfo = guarded(DeviceInfoScreen, 'Device');
const Diagnostics = guarded(DiagnosticsScreen, 'Engine Lab');
const PhotoPreview = guarded(PhotoPreviewScreen, 'Photo');
const Editor = guarded(EditorScreen, 'Edit');
const CreateProp = guarded(CreatePropScreen, 'Make a prop');
const GlidePreview = guarded(GlidePreviewScreen, 'Glide');
const PhotoshootStudio = guarded(PhotoshootStudioScreen, 'Photoshoot Studio');
const SceneSwap = guarded(SceneSwapScreen, 'Scene Swap');
const FilmCollage = guarded(FilmCollageScreen, 'Film & Collage');
const Relight = guarded(RelightScreen, 'Relight');
const Masters = guarded(MastersScreen, 'Masterstroke');
const ProductStudio = guarded(ProductStudioScreen, 'Product Studio');
const Batch = guarded(BatchScreen, 'Batch edit');
const Project = guarded(ProjectScreen, 'Project');
const Premium = guarded(PremiumScreen, 'Premium');
const Rewards = guarded(RewardsScreen, 'Rewards');
const Language = guarded(LanguageScreen, 'Language');
const Help = guarded(HelpScreen, 'Help');
const Privacy = guarded(PrivacyScreen, 'Privacy');

const renderTabBar = (props: BottomTabBarProps) => <GlassTabBar {...props} />;

function MainTabs() {
  return (
    <Tabs.Navigator
      screenOptions={{ headerShown: false, lazy: true, sceneStyle: { backgroundColor: 'transparent' } }}
      tabBar={renderTabBar}
    >
      <Tabs.Screen name="Studio" component={Studio} />
      <Tabs.Screen name="Projects" component={Projects} />
      <Tabs.Screen name="Camera" component={Camera} />
      <Tabs.Screen name="Packs" component={Packs} />
      <Tabs.Screen name="Settings" component={Settings} />
    </Tabs.Navigator>
  );
}

export function RootNavigator() {
  const theme = useTheme();
  const onboarded = useAppStore(s => s.settings.onboardingCompleted);
  const navTheme: NavTheme = useMemo(() => {
    const base = theme.mode === 'dark' ? DarkTheme : DefaultTheme;
    return {
      ...base,
      colors: {
        ...base.colors,
        // Lucid: the picture (or the wallpaper itself) shows behind every screen.
        background: theme.lucid ? 'transparent' : theme.colors.background,
        card: theme.colors.backgroundRaised,
        text: theme.colors.textPrimary,
        primary: theme.colors.accent,
        border: theme.colors.divider,
      },
    };
  }, [theme]);

  return (
    <NavigationContainer ref={navigationRef} theme={navTheme}>
      <Stack.Navigator
        initialRouteName={onboarded ? 'Main' : 'Onboarding'}
        screenOptions={{
          headerShown: false,
          animation: 'fade_from_bottom',
          contentStyle: { backgroundColor: theme.lucid ? 'transparent' : theme.colors.background },
        }}
      >
        <Stack.Screen name="Onboarding" component={Onboarding} />
        <Stack.Screen name="Main" component={MainTabs} />
        <Stack.Screen name="DeviceInfo" component={DeviceInfo} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="Diagnostics" component={Diagnostics} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="PhotoPreview" component={PhotoPreview} options={{ animation: 'fade' }} />
        <Stack.Screen name="Editor" component={Editor} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="CreateProp" component={CreateProp} options={{ animation: 'slide_from_bottom' }} />
        <Stack.Screen name="GlidePreview" component={GlidePreview} options={{ animation: 'fade' }} />
        <Stack.Screen name="PhotoshootStudio" component={PhotoshootStudio} />
        <Stack.Screen name="SceneSwap" component={SceneSwap} />
        <Stack.Screen name="FilmCollage" component={FilmCollage} />
        <Stack.Screen name="Relight" component={Relight} />
        <Stack.Screen name="Masters" component={Masters} />
        <Stack.Screen name="ProductStudio" component={ProductStudio} />
        <Stack.Screen name="Batch" component={Batch} />
        <Stack.Screen name="Project" component={Project} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="Premium" component={Premium} options={{ animation: 'slide_from_bottom' }} />
        <Stack.Screen name="Rewards" component={Rewards} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="Language" component={Language} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="Help" component={Help} options={{ animation: 'slide_from_right' }} />
        <Stack.Screen name="Privacy" component={Privacy} options={{ animation: 'slide_from_right' }} />
      </Stack.Navigator>
    </NavigationContainer>
  );
}
