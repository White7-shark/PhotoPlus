import { useIsFocused } from '@react-navigation/native';
import React, { useCallback, useEffect, useState } from 'react';
import { Image, Pressable, StyleSheet, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import {
  AppText,
  AuroraBackground,
  Badge,
  Button,
  ErrorState,
  GlassSurface,
  Icon,
  IconButton,
  LoadingState,
} from '../components';
import { CameraPreview } from '../components/CameraPreview';
import type { TabScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import { cameraFeatureSupport } from '../services/capabilities';
import { getCameraService, type FlashMode } from '../services/camera/CameraService';
import { createLogger } from '../services/logging/logger';
import { getPermissionService, type PermissionStatus } from '../services/permissions/PermissionService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';

const log = createLogger('camera');
const FLASH_CYCLE: FlashMode[] = ['off', 'auto', 'on'];

type SessionState = 'starting' | 'ready' | 'error';

export function CameraScreen({ navigation }: TabScreenProps<'Camera'>) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const focused = useIsFocused();
  const foreground = useAppStore(s => s.isForeground);
  const capabilities = useAppStore(s => s.capabilities);
  const repos = useAppStore(s => s.repos);
  const showGrid = useAppStore(s => s.settings.cameraGrid);
  const loadProjects = useAppStore(s => s.loadProjects);

  const [permission, setPermission] = useState<PermissionStatus | 'checking'>('checking');
  const [lens, setLens] = useState<'back' | 'front'>('back');
  const [flash, setFlash] = useState<FlashMode>('off');
  const [session, setSession] = useState<SessionState>('starting');
  const [sessionError, setSessionError] = useState<string | null>(null);
  const [capturing, setCapturing] = useState(false);
  const [lastShot, setLastShot] = useState<string | null>(null);
  const [toast, setToast] = useState<string | null>(null);
  const [previewKey, setPreviewKey] = useState(0);

  const support = capabilities ? cameraFeatureSupport(capabilities) : null;
  const permissions = getPermissionService();

  // Re-check whenever the screen regains focus or the app returns from
  // background — the user may have changed the permission in system settings.
  useEffect(() => {
    if (!focused || !foreground) {
      return;
    }
    let cancelled = false;
    permissions
      .ensure('camera')
      .then(status => {
        if (!cancelled) {
          setPermission(status);
        }
      })
      .catch(e => {
        log.error('permission check failed', e);
        if (!cancelled) {
          setPermission('unavailable');
        }
      });
    return () => {
      cancelled = true;
    };
  }, [focused, foreground, permissions]);

  useEffect(() => {
    if (!toast) {
      return;
    }
    const t = setTimeout(() => setToast(null), 2200);
    return () => clearTimeout(t);
  }, [toast]);

  // The camera only runs while visible and foregrounded: saves battery and
  // releases the hardware for other apps immediately.
  const active = focused && foreground && permission === 'granted';

  // Whenever the preview stops (tab change, backgrounding), the native session
  // is released; the shutter must wait for the next onCameraReady. Adjusted
  // during render (React's recommended pattern) rather than in an effect.
  const [wasActive, setWasActive] = useState(active);
  if (active !== wasActive) {
    setWasActive(active);
    if (!active) {
      setSession('starting');
    }
  }

  const capture = useCallback(async () => {
    if (capturing || session !== 'ready') {
      return;
    }
    setCapturing(true);
    try {
      const shot = await getCameraService().capture(flash);
      setLastShot(shot.path);
      if (repos) {
        const inbox = await repos.projects.ensureInbox();
        await repos.images.add({
          projectId: inbox.id,
          path: shot.path,
          source: 'camera',
          width: shot.width,
          height: shot.height,
          sizeBytes: shot.sizeBytes,
          mimeType: 'image/jpeg',
          capturedAt: shot.capturedAt,
        });
        void loadProjects();
      }
      setToast('Saved to Captures');
    } catch (e) {
      log.error('capture failed', e);
      setToast(`Capture failed: ${errorMessage(e)}`);
    } finally {
      setCapturing(false);
    }
  }, [capturing, session, flash, repos, loadProjects]);

  if (permission !== 'granted') {
    return (
      <View style={{ flex: 1, justifyContent: 'center', padding: 24, paddingTop: insets.top + 24 }}>
        <AuroraBackground variant="calm" />
        {permission === 'checking' ? (
          <LoadingState label="Preparing camera…" />
        ) : (
          <GlassSurface style={{ padding: 28, alignItems: 'center', gap: 14 }}>
            <View
              style={{
                width: 72,
                height: 72,
                borderRadius: 36,
                alignItems: 'center',
                justifyContent: 'center',
                backgroundImage: theme.gradients.spectrum,
              }}
            >
              <Icon name="camera" size={32} color="#FFFFFF" />
            </View>
            <AppText variant="title2" align="center" accessibilityRole="header">
              {permission === 'blocked' ? 'Camera access is off' : 'Allow camera access'}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {permission === 'blocked'
                ? 'Turn on camera access for Photo Plus in system settings to start shooting.'
                : 'Photo Plus needs the camera to take photos. Everything stays on your device.'}
            </AppText>
            {permission === 'blocked' ? (
              <Button label="Open settings" icon="gear" onPress={() => void permissions.openSettings()} />
            ) : (
              <Button
                label="Allow camera"
                icon="camera"
                onPress={() => void permissions.request('camera').then(setPermission)}
              />
            )}
            <Button label="Back to studio" variant="ghost" onPress={() => navigation.navigate('Studio')} />
          </GlassSurface>
        )}
      </View>
    );
  }

  return (
    <View style={{ flex: 1, backgroundColor: '#000' }}>
      <CameraPreview
        key={previewKey}
        lensFacing={lens}
        active={active}
        onReady={() => {
          setSession('ready');
          setSessionError(null);
        }}
        onError={(code, message) => {
          log.warn(`camera error ${code}`, message);
          setSession('error');
          setSessionError(message);
        }}
      />

      {showGrid ? (
        <View pointerEvents="none" style={StyleSheet.absoluteFill} importantForAccessibility="no-hide-descendants">
          {[1, 2].map(i => (
            <View
              key={`v${i}`}
              style={[
                styles.gridLine,
                { left: `${(i * 100) / 3}%`, top: 0, bottom: 0, width: StyleSheet.hairlineWidth },
              ]}
            />
          ))}
          {[1, 2].map(i => (
            <View
              key={`h${i}`}
              style={[
                styles.gridLine,
                { top: `${(i * 100) / 3}%`, left: 0, right: 0, height: StyleSheet.hairlineWidth },
              ]}
            />
          ))}
        </View>
      ) : null}

      {session === 'error' ? (
        <View style={[StyleSheet.absoluteFill, { justifyContent: 'center', padding: 24 }]}>
          <ErrorState
            title="Camera unavailable"
            message={sessionError ?? 'Another app may be using the camera.'}
            onRetry={() => {
              setSession('starting');
              setPreviewKey(k => k + 1); // remounts the native view, forcing a fresh bind
            }}
          />
        </View>
      ) : null}

      {/* Top controls */}
      <View style={[styles.topBar, { paddingTop: insets.top + 10 }]}>
        <IconButton icon="close" label="Close camera" onPress={() => navigation.navigate('Studio')} />
        <View style={{ flexDirection: 'row', gap: 10 }}>
          {support?.flash !== false ? (
            <IconButton
              icon={flash === 'off' ? 'flashOff' : 'flash'}
              label={`Flash ${flash}. Double tap to change`}
              active={flash !== 'off'}
              tint={flash === 'on' ? theme.colors.premium : undefined}
              onPress={() => setFlash(FLASH_CYCLE[(FLASH_CYCLE.indexOf(flash) + 1) % FLASH_CYCLE.length])}
            />
          ) : null}
          <IconButton
            icon="grid"
            label={showGrid ? 'Hide grid' : 'Show grid'}
            active={showGrid}
            onPress={() =>
              useAppStore
                .getState()
                .updateSetting('cameraGrid', !showGrid)
                .catch(() => undefined)
            }
          />
        </View>
      </View>

      {toast ? (
        <View
          style={{ position: 'absolute', top: insets.top + 80, alignSelf: 'center' }}
          accessibilityLiveRegion="polite"
        >
          <Badge
            label={toast}
            tone={toast.startsWith('Capture failed') ? 'danger' : 'success'}
            icon={toast.startsWith('Capture failed') ? 'alert' : 'check'}
          />
        </View>
      ) : null}

      {/* Bottom controls */}
      <View style={[styles.bottomBar, { paddingBottom: insets.bottom + 24 }]}>
        <GlassSurface intensity="strong" radius={36} style={styles.dock}>
          <View style={styles.thumbSlot}>
            {lastShot ? (
              <Image
                source={{ uri: `file://${lastShot}` }}
                style={styles.thumb}
                accessibilityLabel="Last captured photo"
              />
            ) : (
              <View
                style={[
                  styles.thumb,
                  { alignItems: 'center', justifyContent: 'center', backgroundColor: 'rgba(255,255,255,0.08)' },
                ]}
              >
                <Icon name="image" size={20} color="rgba(255,255,255,0.6)" />
              </View>
            )}
          </View>
          <Pressable
            testID="shutter"
            accessibilityRole="button"
            accessibilityLabel="Take photo"
            accessibilityState={{ disabled: session !== 'ready' || capturing, busy: capturing }}
            disabled={session !== 'ready' || capturing}
            onPress={capture}
            style={[styles.shutterOuter, { opacity: session === 'ready' ? 1 : 0.5 }]}
          >
            <View
              style={[
                styles.shutterInner,
                {
                  backgroundImage: capturing ? theme.gradients.spectrum : undefined,
                  transform: [{ scale: capturing ? 0.86 : 1 }],
                },
              ]}
            />
          </Pressable>
          <View style={styles.thumbSlot}>
            {support?.frontCamera !== false ? (
              <IconButton
                icon="flip"
                label={lens === 'back' ? 'Switch to front camera' : 'Switch to back camera'}
                onPress={() => {
                  setSession('starting');
                  setLens(lens === 'back' ? 'front' : 'back');
                }}
              />
            ) : null}
          </View>
        </GlassSurface>
        <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.75)', marginTop: 10 }}>
          {session === 'starting'
            ? 'Starting camera…'
            : lens === 'back'
              ? 'PHOTO · BACK CAMERA'
              : 'PHOTO · FRONT CAMERA'}
        </AppText>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  gridLine: { position: 'absolute', backgroundColor: 'rgba(255,255,255,0.35)' },
  topBar: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    paddingHorizontal: 16,
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  bottomBar: { position: 'absolute', left: 0, right: 0, bottom: 0, alignItems: 'center', paddingHorizontal: 16 },
  dock: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    width: '100%',
    maxWidth: 420,
    paddingHorizontal: 18,
    paddingVertical: 14,
  },
  thumbSlot: { width: 56, alignItems: 'center' },
  thumb: { width: 48, height: 48, borderRadius: 14, borderWidth: 1.5, borderColor: 'rgba(255,255,255,0.6)' },
  shutterOuter: {
    width: 82,
    height: 82,
    borderRadius: 41,
    borderWidth: 4,
    borderColor: '#FFFFFF',
    alignItems: 'center',
    justifyContent: 'center',
  },
  shutterInner: { width: 64, height: 64, borderRadius: 32, backgroundColor: '#FFFFFF' },
});
