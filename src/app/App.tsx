import React, { useEffect } from 'react';
import { StatusBar, View } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { AuroraBackground } from '../components/AuroraBackground';
import { ErrorBoundary } from '../components/ErrorBoundary';
import { ErrorState, SplashState } from '../components/States';
import { RootNavigator } from '../navigation/RootNavigator';
import { startLifecycleCoordinator } from '../services/lifecycle/LifecycleCoordinator';
import { createLogger } from '../services/logging/logger';
import { useAppStore } from '../state/store';
import { ThemeProvider, useTheme } from '../theme';

const log = createLogger('app');

function BootGate() {
  const theme = useTheme();
  const status = useAppStore(s => s.status);
  const bootError = useAppStore(s => s.bootError);
  const start = useAppStore(s => s.start);

  if (status === 'ready') {
    return <RootNavigator />;
  }
  return (
    <View style={{ flex: 1, backgroundColor: theme.colors.background }}>
      <AuroraBackground />
      {status === 'error' ? (
        <View style={{ flex: 1, justifyContent: 'center', padding: 24 }}>
          <ErrorState title="Something went wrong at startup" message={bootError ?? undefined} onRetry={() => start()} />
        </View>
      ) : (
        <SplashState label="Preparing your studio…" />
      )}
    </View>
  );
}

function ThemedApp() {
  const preference = useAppStore(s => s.settings.themePreference);
  return (
    <ThemeProvider preference={preference}>
      <ThemedStatusBar />
      <ErrorBoundary area="Photo Plus" onReset={() => useAppStore.getState().start()}>
        <BootGate />
      </ErrorBoundary>
    </ThemeProvider>
  );
}

function ThemedStatusBar() {
  const theme = useTheme();
  return <StatusBar barStyle={theme.mode === "dark" ? "light-content" : "dark-content"} />;
}

export default function App() {
  useEffect(() => {
    useAppStore.getState().start();
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
