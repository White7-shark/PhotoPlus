import React, { useEffect } from 'react';
import { StatusBar, View } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { AuroraBackground } from '../components/AuroraBackground';
import { ErrorBoundary } from '../components/ErrorBoundary';
import { ErrorState, SplashState } from '../components/States';
import { RootNavigator } from '../navigation/RootNavigator';
import { IntegrityGate } from './IntegrityGate';
import { startLifecycleCoordinator } from '../services/lifecycle/LifecycleCoordinator';
import { createLogger } from '../services/logging/logger';
import { useAppStore } from '../state/store';
import { ThemeProvider, useTheme } from '../theme';
import { t as tx } from '../i18n';
import { applyLucidWindow, ensureLucid, useLucid } from '../services/lucid/LucidService';
import { getStorageService } from '../services/storage/StorageService';

const log = createLogger('app');

function BootGate() {
  const theme = useTheme();
  const status = useAppStore(s => s.status);
  const bootError = useAppStore(s => s.bootError);
  const start = useAppStore(s => s.start);

  if (status === 'ready') {
    return (
      <IntegrityGate>
        <RootNavigator />
      </IntegrityGate>
    );
  }
  return (
    <View style={{ flex: 1, backgroundColor: theme.lucid ? 'transparent' : theme.colors.background }}>
      <AuroraBackground />
      {status === 'error' ? (
        <View style={{ flex: 1, justifyContent: 'center', padding: 24 }}>
          <ErrorState
            title={tx('app_something_went_wrong_at_startup')}
            message={bootError ?? undefined}
            onRetry={() => void start()}
          />
        </View>
      ) : (
        <SplashState label={tx('app_preparing_your_studio')} />
      )}
    </View>
  );
}

function ThemedApp() {
  const preference = useAppStore(s => s.settings.themePreference);
  const status = useAppStore(s => s.status);
  const foreground = useAppStore(s => s.isForeground);
  const look = useLucid(s => s.look);
  const lucid = preference === 'lucid';
  // Lucid chosen: keep it showing (start-up and each return to the app).
  useEffect(() => {
    if (lucid && status === 'ready' && foreground) {
      void ensureLucid(path => getStorageService().exists(path)).catch(() => undefined);
    }
  }, [lucid, status, foreground]);
  // The see-through window only while Lucid shows the wallpaper itself.
  useEffect(() => {
    if (foreground) void applyLucidWindow(lucid && look != null);
  }, [lucid, look, foreground]);
  return (
    <ThemeProvider preference={preference} lucid={look}>
      <ThemedStatusBar />
      <ErrorBoundary area="Photo Plus" onReset={() => void useAppStore.getState().start()}>
        <BootGate />
      </ErrorBoundary>
    </ThemeProvider>
  );
}

function ThemedStatusBar() {
  const theme = useTheme();
  return <StatusBar barStyle={theme.mode === 'dark' ? 'light-content' : 'dark-content'} />;
}

export default function App() {
  useEffect(() => {
    void useAppStore.getState().start();
    return startLifecycleCoordinator({
      onForeground: () => useAppStore.getState().setForeground(true),
      onBackground: () => {
        const { setForeground } = useAppStore.getState();
        setForeground(false);
        // Long-running edits are resumable by the user; free resources now.
        try {
          require('../services/processing/ImageProcessingService').getProcessingService().cancelAll();
        } catch (e) {
          log.debug('No engine to cancel', e);
        }
      },
      onMemoryPressure: level => useAppStore.getState().setMemoryPressure(level),
    });
  }, []);

  return (
    <SafeAreaProvider>
      <ThemedApp />
    </SafeAreaProvider>
  );
}
