import { useIsFocused } from '@react-navigation/native';
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { StyleSheet, View, useWindowDimensions } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import {
  AppText,
  AuroraBackground,
  Badge,
  Button,
  ErrorState,
  GlassSurface,
  Icon,
  LoadingState,
} from '../../components';
import { CameraPreview } from '../../components/CameraPreview';
import type { TabScreenProps } from '../../navigation/types';
import { PremiumSheet } from '../../components/PremiumSheet';
import { useFeatureCheck } from '../../hooks/useFeature';
import { errorCode } from '../../native';
import {
  getCameraService,
  type AspectRatio,
  type CameraCapabilities,
  type CaptureOptions,
  type CaptureResult,
  type FlashMode,
  type LensFacing,
  type ManualSettings,
} from '../../services/camera/CameraService';
import {
  availableModes,
  captureModeFor,
  DEFAULT_MODE,
  flattenPoints,
  lookCameraSettings,
  modeLabel,
  spotlightSettings,
  toggleSpotlightPoint,
  type BlurLevel,
  type GlowLevel,
  exposureIndices,
  formatEv,
  formatFocusDistance,
  formatShutter,
  isManualActive,
  portraitAspect,
  progressLabel,
  resolutionLabel,
  resolutionsFor,
  zoomPresets,
  type CapturePreset,
  type UiMode,
} from '../../services/camera/model';
import { CameraEvents, type CaptureStage } from '../../services/camera/types';
import type { FeatureKey } from '../../services/entitlements/features';
import { friendlyMessage } from '../../services/errors';
import type { VibeLook } from '../../services/looks/LookService';
import { createLogger } from '../../services/logging/logger';
import { getPermissionService, type PermissionStatus } from '../../services/permissions/PermissionService';
import { useAppStore } from '../../state/store';
import { ThemeProvider, useTheme } from '../../theme';
import { Chip, LastShot, ModeSwitcher, ShutterButton, uprightStyle, ZoomPills } from './CameraControls';
import { Countdown, FocusRing, GridOverlay, ShutterFlash, StatusPill } from './Overlays';
import { ProPanel } from './ProPanel';
import { StopDial } from './StopDial';
import { SpotlightMarkers, SpotlightPanel } from './SpotlightControls';
import { useCameraSession } from './useCameraSession';
import { VibeSheet } from './VibeSheet';

const log = createLogger('camera');
const FLASH_CYCLE: FlashMode[] = ['off', 'auto', 'on'];
const TIMER_CYCLE = [0, 3, 10] as const;
const ASPECTS: AspectRatio[] = ['4:3', '16:9', '1:1'];

type SessionState = 'starting' | 'ready' | 'error';
type Tuning = Pick<CaptureOptions, 'denoise' | 'sharpen' | 'detail' | 'vibrance' | 'autoTone'>;

interface Processing {
  captureId: string;
  stage: CaptureStage;
  frames: number;
}

/** The camera is always dark: controls sit over a live image, not the app theme. */
export function CameraScreen(props: TabScreenProps<'Camera'>) {
  return (
    <ThemeProvider preference="dark">
      <CameraStudio {...props} />
    </ThemeProvider>
  );
}

function CameraStudio({ navigation }: TabScreenProps<'Camera'>) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const window = useWindowDimensions();
  const focused = useIsFocused();
  const foreground = useAppStore(s => s.isForeground);
  const repos = useAppStore(s => s.repos);
  const settings = useAppStore(s => s.settings);
  const updateSetting = useAppStore(s => s.updateSetting);
  const loadProjects = useAppStore(s => s.loadProjects);
  const camera = getCameraService();
  const permissions = getPermissionService();
  const session = useCameraSession();
  const caps: CameraCapabilities | null = session.capabilities;

  const [permission, setPermission] = useState<PermissionStatus | 'checking'>('checking');
  const [lens, setLens] = useState<LensFacing>('back');
  const [flash, setFlash] = useState<FlashMode>('off');
  const [timer, setTimer] = useState<(typeof TIMER_CYCLE)[number]>(0);
  const [mode, setMode] = useState<UiMode>(DEFAULT_MODE);
  const [spotPoints, setSpotPoints] = useState<{ x: number; y: number }[]>([]);
  const [blurLevel, setBlurLevel] = useState<BlurLevel>('medium');
  const [glowLevel, setGlowLevel] = useState<GlowLevel>('smooth');
  const [vibeOpen, setVibeOpen] = useState(false);
  const [premiumFor, setPremiumFor] = useState<FeatureKey | null>(null);
  const can = useFeatureCheck();
  const [raw, setRaw] = useState(false);
  const [peaking, setPeaking] = useState(false);
  const [manual, setManual] = useState<ManualSettings>({});
  const [tuning, setTuning] = useState<Tuning>({});
  const [state, setState] = useState<SessionState>('starting');
  const [sessionError, setSessionError] = useState<string | null>(null);
  const [menu, setMenu] = useState<null | 'aspect' | 'resolution' | 'ev'>(null);
  const [evIndex, setEvIndex] = useState(0);
  const [capturing, setCapturing] = useState(false);
  const [processing, setProcessing] = useState<Processing | null>(null);
  const [countdown, setCountdown] = useState<number | null>(null);
  const [lastShot, setLastShot] = useState<{ path: string; imageId: string | null } | null>(null);
  const [toast, setToast] = useState<{ text: string; tone: 'success' | 'danger' } | null>(null);
  const [flashKey, setFlashKey] = useState(0);
  const [previewKey, setPreviewKey] = useState(0);
  const countdownTimer = useRef<ReturnType<typeof setInterval> | null>(null);

  const aspect = settings.cameraAspectRatio;
  const resolution = settings.cameraResolution;
  const rotation = session.orientation;

  // ---- permission: re-checked on focus and on return from background
  useEffect(() => {
    if (!focused || !foreground) {
      return;
    }
    let cancelled = false;
    permissions
      .ensure('camera')
      .then(status => !cancelled && setPermission(status))
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
    const t = setTimeout(() => setToast(null), 2400);
    return () => clearTimeout(t);
  }, [toast]);

  // The camera only runs while visible and foregrounded.
  const active = focused && foreground && permission === 'granted';
  const [wasActive, setWasActive] = useState(active);
  if (active !== wasActive) {
    setWasActive(active);
    if (!active) {
      setState('starting');
      setCountdown(null); // leaving the screen cancels a pending self-timer
    }
  }

  // ---- session configuration (stored natively, applied on every bind)
  const rawActive = mode === 'pro' && raw && !!caps?.raw;
  useEffect(() => {
    camera.configure({ aspectRatio: aspect, resolution, raw: rawActive });
  }, [camera, aspect, resolution, rawActive]);

  useEffect(() => {
    camera.setFlash(flash);
  }, [camera, flash]);

  const peakingActive = mode === 'pro' && peaking && state === 'ready';
  useEffect(() => {
    camera.setFocusPeaking(peakingActive);
  }, [camera, peakingActive]);

  // Vibe Match (premium): the camera leans toward the reference's light and
  // colour; every shot is then finished in its style.
  const vibe: VibeLook | null = settings.vibeLook && can('vibeMatch').allowed ? settings.vibeLook : null;
  const vibeCamera = useMemo(() => (vibe ? lookCameraSettings(vibe.look, caps) : null), [vibe, caps]);

  // Manual controls: the user's in Pro mode; otherwise Vibe Match's white
  // balance; otherwise full auto.
  const sessionReady = state === 'ready';
  const manualToApply: ManualSettings | null =
    mode === 'pro'
      ? manual
      : vibeCamera && vibeCamera.whiteBalance !== 'auto'
        ? { whiteBalance: vibeCamera.whiteBalance, kelvin: vibeCamera.kelvin }
        : null;
  const manualKey = JSON.stringify(manualToApply);
  useEffect(() => {
    if (!sessionReady) {
      return;
    }
    const m = JSON.parse(manualKey) as ManualSettings | null;
    if (!m) {
      camera.resetManual();
      return;
    }
    camera.setManual(m).catch(e => log.warn('manual controls rejected', e));
  }, [camera, sessionReady, manualKey]);

  // Vibe Match brightness, as exposure compensation (re-applied after rebinds).
  const vibeEv = vibeCamera?.evIndex ?? null;
  useEffect(() => {
    if (!sessionReady || vibeEv === null || mode === 'pro') {
      return;
    }
    camera
      .setExposureCompensation(vibeEv)
      .then(setEvIndex)
      .catch(() => undefined);
  }, [camera, sessionReady, vibeEv, mode]);

  const { refreshCapabilities, markFocus } = session;
  const onReady = useCallback(async () => {
    setState('ready');
    setSessionError(null);
    // A rebind (lens, ratio, recovery) resets exposure compensation natively.
    setEvIndex(0);
    await refreshCapabilities();
  }, [refreshCapabilities]);

  // Fall back gracefully when the active camera lacks the selected mode.
  const modes = useMemo(() => availableModes(caps), [caps]);
  const effectiveMode: UiMode = modes.some(m => m.id === mode) ? mode : 'photo';

  // ---- capture
  useEffect(() => {
    const shutter = camera.on(CameraEvents.shutter, () => setFlashKey(k => k + 1));
    const progress = camera.on(CameraEvents.progress, p =>
      setProcessing(cur => (cur && cur.captureId === p.captureId ? { ...cur, stage: p.stage } : cur)),
    );
    return () => {
      shutter.remove();
      progress.remove();
    };
  }, [camera]);

  const saveRecord = useCallback(
    async (shot: CaptureResult) => {
      if (!repos) {
        return null;
      }
      const inbox = await repos.projects.ensureInbox();
      const record = await repos.images.add({
        projectId: inbox.id,
        path: shot.path,
        source: 'camera',
        width: shot.width,
        height: shot.height,
        sizeBytes: shot.sizeBytes,
        mimeType: 'image/jpeg',
        capturedAt: shot.capturedAt,
        metadata: {
          captureMode: shot.mode,
          lens,
          look: !!shot.look,
          portrait: shot.portrait ?? null,
          originalPath: shot.originalPath ?? null,
          originalOwned: !!shot.originalPath,
          rawPath: shot.rawPath ?? null,
          report: shot.report ?? null,
          timings: shot.timings ?? null,
        },
      });
      void loadProjects();
      return record.id;
    },
    [repos, lens, loadProjects],
  );

  const takePhoto = useCallback(async () => {
    const captureMode = captureModeFor(effectiveMode, rawActive);
    const look = vibe && captureMode !== 'raw' ? vibe : null;
    const processed =
      captureMode === 'enhance' ||
      captureMode === 'night' ||
      captureMode === 'superres' ||
      captureMode === 'spotlight' ||
      !!look;
    if (processed && processing) {
      setToast({ text: friendlyMessage('BUSY'), tone: 'danger' });
      return;
    }
    const captureId = camera.newCaptureId();
    if (processed) {
      setProcessing({ captureId, stage: 'capturing', frames: caps?.capturePlans[captureMode]?.frames ?? 1 });
    } else {
      setCapturing(true);
    }
    try {
      const shot = await camera.capture(
        {
          mode: captureMode,
          flash,
          mirrorFront: settings.mirrorFrontCamera,
          ...(processed ? tuning : {}),
          ...(captureMode === 'spotlight'
            ? { points: flattenPoints(spotPoints), ...spotlightSettings(blurLevel, glowLevel) }
            : {}),
          ...(look ? { look: look.look, lookStrength: look.strength } : {}),
        },
        captureId,
      );
      const imageId = await saveRecord(shot);
      setLastShot({ path: shot.path, imageId });
      setToast({
        text: shot.rawPath ? 'Saved with RAW' : `${look ? 'Vibe Match · ' : ''}${modeLabel(shot.mode)} photo saved`,
        tone: 'success',
      });
    } catch (e) {
      if (errorCode(e) !== 'CANCELLED') {
        log.error('capture failed', e);
        setToast({
          text: friendlyMessage(errorCode(e), 'That photo didn’t come through. Please try again.'),
          tone: 'danger',
        });
      }
    } finally {
      if (processed) {
        setProcessing(cur => (cur?.captureId === captureId ? null : cur));
      } else {
        setCapturing(false);
      }
    }
  }, [
    effectiveMode,
    rawActive,
    vibe,
    processing,
    camera,
    caps,
    flash,
    settings.mirrorFrontCamera,
    tuning,
    spotPoints,
    blurLevel,
    glowLevel,
    saveRecord,
  ]);

  // Premium modes ask before switching (enforcement is off until billing ships).
  const chooseMode = useCallback(
    (m: UiMode, feature: FeatureKey) => {
      if (!can(feature).allowed) {
        setPremiumFor(feature);
        return;
      }
      setMode(m);
      setTuning({});
    },
    [can],
  );

  const stopCountdown = useCallback(() => {
    if (countdownTimer.current) {
      clearInterval(countdownTimer.current);
      countdownTimer.current = null;
    }
    setCountdown(null);
  }, []);

  const onShutter = useCallback(() => {
    if (countdown !== null) {
      stopCountdown();
      return;
    }
    if (timer === 0) {
      void takePhoto();
      return;
    }
    let remaining: number = timer;
    setCountdown(remaining);
    countdownTimer.current = setInterval(() => {
      remaining -= 1;
      if (remaining <= 0) {
        stopCountdown();
        void takePhoto();
      } else {
        setCountdown(remaining);
      }
    }, 1000);
  }, [countdown, timer, takePhoto, stopCountdown]);

  // ...and its interval (state is reset during render above).
  useEffect(() => {
    if (!active && countdownTimer.current) {
      clearInterval(countdownTimer.current);
      countdownTimer.current = null;
    }
  }, [active]);
  useEffect(
    () => () => {
      if (countdownTimer.current) {
        clearInterval(countdownTimer.current);
      }
    },
    [],
  );

  const applyPreset = useCallback(
    (preset: CapturePreset) => {
      setMode(preset.mode);
      setTuning(preset.tuning ?? {});
      if (preset.aspectRatio && preset.aspectRatio !== aspect) {
        setState('starting');
        updateSetting('cameraAspectRatio', preset.aspectRatio).catch(() => undefined);
      }
      if (preset.shutterNs !== undefined) {
        setManual(m => ({ ...m, exposureTimeNs: preset.shutterNs, iso: null }));
      }
      setToast({ text: `${preset.title} preset`, tone: 'success' });
    },
    [aspect, updateSetting],
  );

  const switchLens = () => {
    setState('starting');
    setManual({});
    setLens(l => (l === 'back' ? 'front' : 'back'));
  };

  // ---------------------------------------------------------------- render

  if (permission !== 'granted') {
    return (
      <View style={{ flex: 1, justifyContent: 'center', padding: 24, paddingTop: insets.top + 24 }}>
        <AuroraBackground variant="calm" />
        {permission === 'checking' ? (
          <LoadingState label="Preparing camera…" />
        ) : (
          <GlassSurface style={{ padding: 28, alignItems: 'center', gap: 14 }}>
            <View style={[styles.permissionIcon, { backgroundImage: theme.gradients.spectrum }]}>
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

  // Preview frame sized to the chosen ratio so what you see is what is saved.
  const frameWidth = window.width;
  const frameHeight = Math.min(window.height, frameWidth / portraitAspect(aspect));
  const topBarHeight = insets.top + 56;
  const frameTop = aspect === '16:9' ? Math.max(0, (window.height - frameHeight) / 2 - 40) : topBarHeight;
  const presets = caps ? zoomPresets(caps.zoom.min, caps.zoom.max) : [1];
  const evValues = exposureIndices(caps?.exposureCompensation ?? null);
  const resolutions = resolutionsFor(caps, aspect);
  const currentResolution = resolutions.find(r => r.id === resolution) ?? resolutions[0];
  const ready = state === 'ready';
  const md = session.metadata;

  const recovering = session.recoveringAttempt !== null && state !== 'error';

  return (
    <View style={styles.root}>
      <View style={{ position: 'absolute', top: frameTop, width: frameWidth, height: frameHeight, overflow: 'hidden' }}>
        <CameraPreview
          key={previewKey}
          lensFacing={lens}
          active={active}
          gesturesEnabled={countdown === null}
          onReady={() => void onReady()}
          onError={(code, message) => {
            log.warn(`camera error ${code}`, message);
            setState('error');
            setSessionError(message);
          }}
          onFocusTap={(x, y) => {
            markFocus(x, y);
            setMenu(m => (m === 'ev' ? m : null));
            // In Spotlight, taps also choose what stands out.
            if (effectiveMode === 'spotlight') {
              setSpotPoints(p => toggleSpotlightPoint(p, { x, y }));
            }
          }}
        />
        {settings.cameraGrid ? <GridOverlay /> : null}
        {effectiveMode === 'spotlight' ? (
          <SpotlightMarkers points={spotPoints} frame={{ width: frameWidth, height: frameHeight }} />
        ) : null}
        {session.focus ? (
          <FocusRing
            key={session.focus.key}
            x={session.focus.x}
            y={session.focus.y}
            status={session.focus.status}
            frame={{ width: frameWidth, height: frameHeight }}
          />
        ) : null}
        <ShutterFlash trigger={flashKey} />
        {countdown !== null ? <Countdown value={countdown} /> : null}
      </View>

      {state === 'error' ? (
        <View style={[StyleSheet.absoluteFill, { justifyContent: 'center', padding: 24 }]}>
          <ErrorState
            title="Camera unavailable"
            message={sessionError ?? 'Another app may be using the camera.'}
            onRetry={() => {
              setState('starting');
              setPreviewKey(k => k + 1); // remount: a fresh bind
            }}
          />
        </View>
      ) : null}

      {/* Top bar */}
      <View style={[styles.topBar, { paddingTop: insets.top + 8 }]}>
        <Chip
          icon="close"
          accessibilityLabel="Close camera"
          onPress={() => navigation.navigate('Studio')}
          rotation={rotation}
        />
        <View style={styles.topGroup}>
          {caps?.flash !== false ? (
            <Chip
              icon={flash === 'off' ? 'flashOff' : 'flash'}
              label={flash === 'auto' ? 'A' : undefined}
              active={flash !== 'off'}
              tint={flash === 'on' ? theme.colors.premium : undefined}
              accessibilityLabel={`Flash ${flash}. Double tap to change`}
              onPress={() => setFlash(FLASH_CYCLE[(FLASH_CYCLE.indexOf(flash) + 1) % FLASH_CYCLE.length])}
              rotation={rotation}
            />
          ) : null}
          <Chip
            icon="timer"
            label={timer ? `${timer}s` : undefined}
            active={timer > 0}
            accessibilityLabel={timer ? `Timer ${timer} seconds` : 'Timer off'}
            onPress={() => setTimer(TIMER_CYCLE[(TIMER_CYCLE.indexOf(timer) + 1) % TIMER_CYCLE.length])}
            rotation={rotation}
          />
          <Chip
            icon="wand"
            active={!!vibe}
            tint={vibe ? theme.colors.premium : undefined}
            accessibilityLabel={vibe ? 'Vibe Match on. Change style' : 'Vibe Match. Shoot in the style of any photo'}
            onPress={() => (can('vibeMatch').allowed ? setVibeOpen(true) : setPremiumFor('vibeMatch'))}
            rotation={rotation}
            testID="vibe-chip"
          />
          <Chip
            icon="grid"
            active={settings.cameraGrid}
            accessibilityLabel={settings.cameraGrid ? 'Hide grid' : 'Show grid'}
            onPress={() => updateSetting('cameraGrid', !settings.cameraGrid).catch(() => undefined)}
            rotation={rotation}
          />
          {evValues.length ? (
            <Chip
              icon="exposure"
              label={
                evIndex !== 0 && caps?.exposureCompensation
                  ? formatEv(evIndex, caps.exposureCompensation.step)
                  : undefined
              }
              active={menu === 'ev' || evIndex !== 0}
              accessibilityLabel="Exposure compensation"
              onPress={() => setMenu(m => (m === 'ev' ? null : 'ev'))}
              rotation={rotation}
            />
          ) : null}
          <Chip
            label={aspect}
            active={menu === 'aspect'}
            accessibilityLabel={`Aspect ratio ${aspect}`}
            onPress={() => setMenu(m => (m === 'aspect' ? null : 'aspect'))}
            rotation={rotation}
          />
          {resolutions.length > 1 && currentResolution ? (
            <Chip
              label={`${Math.round(currentResolution.megapixels)}MP`}
              active={menu === 'resolution'}
              accessibilityLabel={`Resolution ${resolutionLabel(currentResolution)}`}
              onPress={() => setMenu(m => (m === 'resolution' ? null : 'resolution'))}
              rotation={rotation}
            />
          ) : null}
        </View>
      </View>

      {/* Pop-down menus */}
      {menu === 'aspect' || menu === 'resolution' ? (
        <View style={[styles.menu, { top: insets.top + 54 }]}>
          {menu === 'aspect'
            ? ASPECTS.filter(a => !caps || caps.aspectRatios.includes(a)).map(a => (
                <Chip
                  key={a}
                  label={a}
                  active={a === aspect}
                  accessibilityLabel={`Aspect ratio ${a}`}
                  onPress={() => {
                    setMenu(null);
                    // Only a real change rebinds natively (and fires onReady again).
                    if (a !== aspect) {
                      setState('starting');
                      updateSetting('cameraAspectRatio', a).catch(() => undefined);
                    }
                  }}
                />
              ))
            : resolutions.map(r => (
                <Chip
                  key={r.id}
                  label={resolutionLabel(r)}
                  active={r.id === currentResolution?.id}
                  accessibilityLabel={`Resolution ${resolutionLabel(r)}, ${r.width} by ${r.height}`}
                  onPress={() => {
                    setMenu(null);
                    if (r.id !== resolution) {
                      setState('starting');
                      updateSetting('cameraResolution', r.id).catch(() => undefined);
                    }
                  }}
                />
              ))}
        </View>
      ) : null}

      {/* Status */}
      <View style={[styles.status, { top: topBarHeight + 8 }]} pointerEvents="box-none">
        {recovering ? (
          <StatusPill label={`Reconnecting camera… (attempt ${session.recoveringAttempt})`} tone="warning" busy />
        ) : null}
        {processing ? (
          <StatusPill
            label={progressLabel(processing.stage, processing.frames)}
            tone="premium"
            busy
            onCancel={() => camera.cancelCapture(processing.captureId)}
          />
        ) : null}
        {toast ? (
          <Badge label={toast.text} tone={toast.tone} icon={toast.tone === 'danger' ? 'alert' : 'check'} />
        ) : null}
        {effectiveMode === 'pro' && md ? (
          <View style={styles.readout} accessibilityLabel="Live exposure">
            <AppText variant="mono" style={styles.readoutText}>
              {[
                md.iso ? `ISO ${md.iso}` : null,
                md.exposureTimeNs ? formatShutter(md.exposureTimeNs) : null,
                md.aperture ? `ƒ/${md.aperture.toFixed(1)}` : null,
                md.focusDiopters != null && caps?.manual.focus ? formatFocusDistance(md.focusDiopters) : null,
                isManualActive(manual) ? 'M' : 'A',
              ]
                .filter(Boolean)
                .join('  ·  ')}
            </AppText>
            {md.focusLocked ? <View style={[styles.afDot, { backgroundColor: theme.colors.success }]} /> : null}
          </View>
        ) : null}
      </View>

      {/* Bottom controls */}
      <View style={[styles.bottom, { paddingBottom: insets.bottom + 12 }]} pointerEvents="box-none">
        {menu === 'ev' && caps?.exposureCompensation ? (
          <View style={styles.evPanel}>
            <StopDial
              label="Exposure compensation"
              values={evValues}
              value={evIndex}
              format={i => formatEv(i, caps.exposureCompensation?.step ?? 1)}
              onChange={i => {
                setEvIndex(i);
                camera
                  .setExposureCompensation(i)
                  .then(setEvIndex)
                  .catch(() => undefined);
              }}
            />
          </View>
        ) : null}

        {effectiveMode === 'spotlight' ? (
          <SpotlightPanel
            blur={blurLevel}
            glow={glowLevel}
            points={spotPoints.length}
            onBlur={setBlurLevel}
            onGlow={setGlowLevel}
            onClear={() => setSpotPoints([])}
          />
        ) : null}

        {effectiveMode === 'pro' && caps ? (
          <ProPanel
            caps={caps}
            manual={manual}
            metadata={md}
            sharpness={session.sharpness}
            raw={raw}
            peaking={peaking}
            onManual={setManual}
            onRaw={setRaw}
            onPeaking={setPeaking}
            onPreset={applyPreset}
          />
        ) : null}

        {presets.length > 1 ? (
          <View style={{ alignItems: 'center' }}>
            <ZoomPills
              presets={presets}
              zoom={session.zoom}
              onSelect={z => camera.setZoom(z).catch(() => undefined)}
              rotation={rotation}
            />
          </View>
        ) : null}

        <ModeSwitcher
          modes={modes}
          value={effectiveMode}
          onChange={m => chooseMode(m, modes.find(d => d.id === m)?.feature ?? 'cameraCapture')}
        />

        <View style={styles.dock}>
          <LastShot
            path={lastShot?.path ?? null}
            progress={processing ? 0 : null}
            rotation={rotation}
            onPress={() => {
              if (lastShot?.imageId) {
                navigation.navigate('PhotoPreview', { imageId: lastShot.imageId });
              }
            }}
          />
          <ShutterButton
            mode={effectiveMode}
            disabled={!ready || (capturing && countdown === null)}
            busy={capturing}
            countdown={countdown}
            onPress={onShutter}
          />
          <View style={uprightStyle(rotation)}>
            <Chip
              icon="flip"
              accessibilityLabel={lens === 'back' ? 'Switch to front camera' : 'Switch to back camera'}
              onPress={switchLens}
            />
          </View>
        </View>
        <AppText variant="caption" align="center" style={styles.caption} numberOfLines={1}>
          {state === 'starting'
            ? 'Starting camera…'
            : `${vibe ? 'Vibe Match on · ' : ''}${modes.find(m => m.id === effectiveMode)?.description ?? ''}`}
        </AppText>
      </View>

      <VibeSheet
        visible={vibeOpen}
        look={settings.vibeLook}
        onChange={look => {
          updateSetting('vibeLook', look).catch(() => undefined);
          if (!look) {
            setVibeOpen(false);
          }
        }}
        onClose={() => setVibeOpen(false)}
      />
      <PremiumSheet feature={premiumFor} onClose={() => setPremiumFor(null)} />
    </View>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#000' },
  permissionIcon: { width: 72, height: 72, borderRadius: 36, alignItems: 'center', justifyContent: 'center' },
  topBar: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    paddingHorizontal: 12,
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  topGroup: { flexDirection: 'row', gap: 6, alignItems: 'center' },
  menu: {
    position: 'absolute',
    right: 12,
    flexDirection: 'row',
    gap: 6,
    padding: 6,
    borderRadius: 22,
    backgroundColor: 'rgba(10,9,17,0.72)',
  },
  status: { position: 'absolute', left: 0, right: 0, alignItems: 'center', gap: 8 },
  readout: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 12,
    height: 28,
    borderRadius: 14,
    backgroundColor: 'rgba(10,9,17,0.6)',
  },
  readoutText: { color: '#FFFFFF' },
  afDot: { width: 6, height: 6, borderRadius: 3 },
  bottom: { position: 'absolute', left: 0, right: 0, bottom: 0, gap: 10 },
  evPanel: {
    marginHorizontal: 24,
    paddingHorizontal: 8,
    borderRadius: 20,
    backgroundColor: 'rgba(10,9,17,0.6)',
  },
  dock: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 32,
  },
  caption: { color: 'rgba(255,255,255,0.7)', paddingHorizontal: 24 },
});
