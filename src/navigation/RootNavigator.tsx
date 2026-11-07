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
        background: theme.colors.background,
        card: theme.colors.backgroundRaised,
        text: theme.colors.textPrimary,
        primary: theme.colors.accent,
        border: theme.colors.divider,
      },
    };
  }, [theme]);

  return (
    <NavigationContainer theme={navTheme}>
      <Stack.Navigator
        initialRouteName={onboarded ? 'Main' : 'Onboarding'}
        screenOptions={{
          headerShown: false,
          animation: 'fade_from_bottom',
          contentStyle: { backgroundColor: theme.colors.background },
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
      </Stack.Navigator>
    </NavigationContainer>
  );
}
