import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Alert, Image, Pressable, View, type GestureResponderEvent, type LayoutChangeEvent } from 'react-native';

import {
  AppText,
  Badge,
  Button,
  ErrorState,
  GlassSurface,
  Icon,
  IconButton,
  LoadingState,
  Screen,
  SegmentedControl,
  StatTile,
  type IconName,
} from '../components';
import { PremiumSheet } from '../components/PremiumSheet';
import { useFeatureCheck } from '../hooks/useFeature';
import type { RootScreenProps } from '../navigation/types';
import { errorCode } from '../native';
import {
  flattenPoints,
  modeLabel,
  spotlightSettings,
  toggleSpotlightPoint,
  type BlurLevel,
  type GlowLevel,
} from '../services/camera/model';
import type { ImageRecord } from '../services/database/repositories/ImagesRepository';
import type { FeatureKey } from '../services/entitlements/features';
import { friendlyMessage } from '../services/errors';
import { lookDescription, VIBE_STRENGTHS } from '../services/looks/LookService';
import { createLogger } from '../services/logging/logger';
import { getProcessingService, ProcessingError } from '../services/processing/ImageProcessingService';
import {
  editedPathFor,
  enhancedCopyMetadata,
  enhanceSuggestion,
  filesToDelete,
  formatMegapixels,
  noiseReduction,
  type EditKind,
} from '../services/processing/quality';
import type { QualityReport } from '../services/processing/types';
import { formatBytes, getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { VibeSheet } from './camera/VibeSheet';
import { SpotlightMarkers, SpotlightPanel } from './camera/SpotlightControls';
import { CompareView } from './CompareView';

const log = createLogger('photo');

type ViewMode = 'photo' | 'compare';

interface Tool {
  id: EditKind;
  label: string;
  description: string;
  icon: IconName;
  feature: FeatureKey;
}

/** Edits available on any photo, under their app names. */
export const TOOLS: readonly Tool[] = [
  { id: 'enhance', label: 'Clarity', description: 'Sharper, cleaner, brighter', icon: 'sparkles', feature: 'clarity' },
  { id: 'spotlight', label: 'Spotlight', description: 'Make your subject shine', icon: 'target', feature: 'spotlight' },
  {
    id: 'look',
    label: 'Vibe Match',
    description: 'Copy the style of a photo you love',
    icon: 'wand',
    feature: 'vibeMatch',
  },
  { id: 'superres', label: 'Ultra Detail', description: 'Twice the detail', icon: 'layers', feature: 'multiShot' },
];

interface EditResult {
  kind: EditKind;
  outputPath: string;
  width: number;
  height: number;
  sizeBytes: number;
  report: unknown;
}

const str = (v: unknown): string | null => (typeof v === 'string' && v.length > 0 ? v : null);
const num = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? v : null);

/** Plain-language highlights of what an edit or capture improved. */
export function improvementBadges(
  mode: string,
  report: unknown,
  portrait: unknown,
  look: boolean,
): { label: string; icon: IconName; tone: 'success' | 'accent' | 'premium' }[] {
  const out: { label: string; icon: IconName; tone: 'success' | 'accent' | 'premium' }[] = [];
  const r = report && typeof report === 'object' ? (report as Record<string, unknown>) : null;
  const before = num(r?.noiseBefore);
  const after = num(r?.noiseAfter);
  if (before !== null && after !== null && noiseReduction(before, after) >= 5) {
    out.push({ label: `Grain −${noiseReduction(before, after)}%`, icon: 'sparkles', tone: 'success' });
  }
  const frames = num(r?.framesInput);
  if (frames !== null && frames > 1) {
    out.push({ label: `${num(r?.framesUsed) ?? frames} shots blended`, icon: 'layers', tone: 'accent' });
  }
  if (num(r?.upscale) === 2 || mode === 'superres') {
    out.push({ label: '2× detail', icon: 'crown', tone: 'premium' });
  }
  const p = portrait && typeof portrait === 'object' ? (portrait as Record<string, unknown>) : null;
  if (p || mode === 'spotlight') {
    out.push({ label: 'Background softened', icon: 'target', tone: 'accent' });
    const spots = num(p?.spots) ?? 0;
    if ((num(p?.skin) ?? 0) > 0.002) {
      out.push({
        label: spots > 0 ? `Glow · ${spots} blemishes cleared` : 'Glow skin',
        icon: 'sparkles',
        tone: 'success',
      });
    }
  }
  if (look || mode === 'look') {
    out.push({ label: 'Vibe Match style', icon: 'wand', tone: 'premium' });
  }
  return out;
}

function shutterLabel(seconds: number): string {
  return seconds >= 1 ? `${Math.round(seconds * 10) / 10}″` : `1/${Math.round(1 / seconds)}`;
}

/**
 * Photo preview: the photo, before/after for edited shots, share, delete,
 * and the editing tools (Clarity, Spotlight with tap-to-choose, Vibe Match,
 * Ultra Detail).
 */
export function PhotoPreviewScreen({ navigation, route }: RootScreenProps<'PhotoPreview'>) {
  const theme = useTheme();
  const repos = useAppStore(s => s.repos);
  const loadProjects = useAppStore(s => s.loadProjects);
  const vibeLook = useAppStore(s => s.settings.vibeLook);
  const updateSetting = useAppStore(s => s.updateSetting);
  const can = useFeatureCheck();
  const [image, setImage] = useState<ImageRecord | null>(null);
  const [status, setStatus] = useState<'loading' | 'ready' | 'missing'>('loading');
  const [quality, setQuality] = useState<QualityReport | null>(null);
  const [exif, setExif] = useState<Record<string, unknown> | null>(null);
  const [view, setView] = useState<ViewMode>('photo');
  const [tool, setTool] = useState<EditKind | null>(null);
  const [working, setWorking] = useState(false);
  const [result, setResult] = useState<EditResult | null>(null);
  const [points, setPoints] = useState<{ x: number; y: number }[]>([]);
  const [blur, setBlur] = useState<BlurLevel>('medium');
  const [glow, setGlow] = useState<GlowLevel>('smooth');
  const [vibeOpen, setVibeOpen] = useState(false);
  const [premiumFor, setPremiumFor] = useState<FeatureKey | null>(null);
  const [photoSize, setPhotoSize] = useState({ width: 0, height: 0 });
  const abort = useRef<AbortController | null>(null);

  useEffect(() => {
    let cancelled = false;
    (async () => {
      const record = repos ? await repos.images.get(route.params.imageId) : null;
      if (cancelled) {
        return;
      }
      setImage(record);
      setStatus(record ? 'ready' : 'missing');
      if (record) {
        const processing = getProcessingService();
        processing
          .metadata(record.path)
          .then(m => !cancelled && setExif(m))
          .catch(() => undefined);
        // The original of an edited shot shows why it was worth it.
        const analysed = str(record.metadata.originalPath) ?? record.path;
        processing
          .analyzeQuality(analysed)
          .then(q => !cancelled && setQuality(q))
          .catch(() => undefined);
        if (str(record.metadata.originalPath)) {
          setView('compare');
        }
      }
    })().catch(e => {
      log.error('load failed', e);
      if (!cancelled) {
        setStatus('missing');
      }
    });
    return () => {
      cancelled = true;
      abort.current?.abort();
    };
  }, [repos, route.params.imageId]);

  const chooseTool = (t: Tool) => {
    if (!can(t.feature).allowed) {
      setPremiumFor(t.feature);
      return;
    }
    setTool(cur => (cur === t.id ? null : t.id));
    if (t.id === 'look' && !vibeLook) {
      setVibeOpen(true);
    }
  };

  const apply = useCallback(async () => {
    if (!image || !tool) {
      return;
    }
    abort.current = new AbortController();
    setWorking(true);
    try {
      const storage = getStorageService();
      let outputPath = editedPathFor(image.path, tool);
      for (let attempt = 1; (await storage.exists(outputPath)) && attempt < 20; attempt++) {
        outputPath = editedPathFor(image.path, tool, attempt);
      }
      const processing = getProcessingService();
      const signal = abort.current.signal;
      let r: EditResult;
      if (tool === 'spotlight') {
        const out = await processing.spotlight(
          { inputPath: image.path, outputPath, points: flattenPoints(points), ...spotlightSettings(blur, glow) },
          { signal },
        );
        r = { kind: tool, ...out, report: out.portrait };
      } else if (tool === 'look') {
        if (!vibeLook) {
          setVibeOpen(true);
          return;
        }
        const out = await processing.applyLook(
          { inputPath: image.path, outputPath, look: vibeLook.look, strength: vibeLook.strength },
          { signal },
        );
        r = { kind: tool, ...out, report: null };
      } else {
        const out = await processing.enhance(
          { inputPath: image.path, outputPath, upscale: tool === 'superres' ? 2 : 1, autoTone: true },
          { signal },
        );
        r = { kind: tool, ...out, report: out.report };
      }
      setResult(r);
      setView('compare');
    } catch (e) {
      if (!(e instanceof ProcessingError && e.cancelled)) {
        Alert.alert('That didn’t work', friendlyMessage(errorCode(e)));
      }
    } finally {
      setWorking(false);
    }
  }, [image, tool, points, blur, glow, vibeLook]);

  const keepResult = useCallback(async () => {
    if (!image || !result || !repos) {
      return;
    }
    try {
      const record = await repos.images.add({
        projectId: image.projectId,
        path: result.outputPath,
        source: 'export',
        width: result.width,
        height: result.height,
        sizeBytes: result.sizeBytes,
        mimeType: 'image/jpeg',
        capturedAt: image.capturedAt,
        metadata: {
          ...enhancedCopyMetadata(image, result.kind, result.kind === 'spotlight' ? null : result.report),
          ...(result.kind === 'spotlight' ? { portrait: result.report } : {}),
          ...(result.kind === 'look' ? { look: true } : {}),
        },
      });
      void loadProjects();
      navigation.replace('PhotoPreview', { imageId: record.id });
    } catch (e) {
      Alert.alert('Couldn’t save', friendlyMessage(errorCode(e)));
    }
  }, [image, result, repos, loadProjects, navigation]);

  const discardResult = useCallback(async () => {
    if (result) {
      await getStorageService()
        .deleteFile(result.outputPath)
        .catch(() => undefined);
    }
    setResult(null);
    setView(image && str(image.metadata.originalPath) ? 'compare' : 'photo');
  }, [result, image]);

  const share = useCallback(async () => {
    const path = result?.outputPath ?? image?.path;
    if (!path) {
      return;
    }
    try {
      await getStorageService().share([path], 'Share your photo');
    } catch (e) {
      Alert.alert('Couldn’t share', friendlyMessage(errorCode(e), friendlyMessage('SHARE_FAILED')));
    }
  }, [result, image]);

  const confirmDelete = () => {
    if (!image || !repos) {
      return;
    }
    Alert.alert('Delete photo?', 'It will be removed from this device.', [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Delete',
        style: 'destructive',
        onPress: async () => {
          try {
            await repos.images.remove(image.id);
            const storage = getStorageService();
            await Promise.allSettled(filesToDelete(image).map(p => storage.deleteFile(p)));
            void loadProjects();
            navigation.goBack();
          } catch (e) {
            Alert.alert('Couldn’t delete', friendlyMessage(errorCode(e)));
          }
        },
      },
    ]);
  };

  if (status === 'loading') {
    return (
      <Screen title="Photo" onBack={() => navigation.goBack()} bottomInset={24}>
        <LoadingState label="Opening photo…" />
      </Screen>
    );
  }
  if (!image) {
    return (
      <Screen title="Photo" onBack={() => navigation.goBack()} bottomInset={24}>
        <ErrorState title="Photo not found" message="It may have been deleted." onRetry={() => navigation.goBack()} />
      </Screen>
    );
  }

  const original = str(image.metadata.originalPath);
  const aspect = image.width > 0 && image.height > 0 ? image.width / image.height : 3 / 4;
  const mode = str(image.metadata.captureMode) ?? 'standard';
  const badges = result
    ? improvementBadges(
        result.kind,
        result.kind === 'spotlight' ? null : result.report,
        result.kind === 'spotlight' ? result.report : null,
        result.kind === 'look',
      )
    : improvementBadges(mode, image.metadata.report, image.metadata.portrait, image.metadata.look === true);
  const before = result ? image.path : original;
  const after = result ? result.outputPath : image.path;
  const suggestion = enhanceSuggestion(original ? null : quality);
  const exposureTime = num(exif?.exposureTime);
  const choosing = tool === 'spotlight' && !result;

  const onPhotoTap = (e: GestureResponderEvent) => {
    if (!choosing || photoSize.width === 0) {
      return;
    }
    const x = Math.max(0, Math.min(1, e.nativeEvent.locationX / photoSize.width));
    const y = Math.max(0, Math.min(1, e.nativeEvent.locationY / photoSize.height));
    setPoints(p => toggleSpotlightPoint(p, { x, y }));
  };

  return (
    <Screen
      title={modeLabel(mode)}
      overline={image.capturedAt ? new Date(image.capturedAt).toLocaleString() : undefined}
      onBack={() => navigation.goBack()}
      headerRight={
        <IconButton icon="share" label="Share photo" onPress={() => void share()} size={44} testID="share" />
      }
      bottomInset={32}
    >
      <View style={{ gap: 16 }}>
        {before ? (
          <SegmentedControl
            label="View"
            value={view}
            onChange={setView}
            options={[
              { value: 'photo', label: 'Photo' },
              { value: 'compare', label: 'Before / after' },
            ]}
          />
        ) : null}

        {view === 'compare' && before ? (
          <CompareView beforeUri={`file://${before}`} afterUri={`file://${after}`} aspect={aspect} />
        ) : (
          <Pressable
            onPress={onPhotoTap}
            disabled={!choosing}
            accessibilityLabel={choosing ? 'Photo. Tap what should shine' : 'Photo'}
            onLayout={(e: LayoutChangeEvent) =>
              setPhotoSize({ width: e.nativeEvent.layout.width, height: e.nativeEvent.layout.height })
            }
            style={{
              width: '100%',
              aspectRatio: aspect,
              borderRadius: 18,
              overflow: 'hidden',
              backgroundColor: '#000',
            }}
          >
            <Image
              source={{ uri: `file://${result?.outputPath ?? image.path}` }}
              style={{ width: '100%', height: '100%' }}
              resizeMode="contain"
            />
            {choosing ? <SpotlightMarkers points={points} frame={photoSize} /> : null}
          </Pressable>
        )}

        {badges.length ? (
          <GlassSurface style={{ padding: 16, gap: 10 }}>
            <AppText variant="title3">What we improved</AppText>
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
              {badges.map(b => (
                <Badge key={b.label} label={b.label} tone={b.tone} icon={b.icon} />
              ))}
            </View>
          </GlassSurface>
        ) : null}

        {result ? (
          <View style={{ flexDirection: 'row', gap: 10 }}>
            <Button label="Keep" icon="check" onPress={() => void keepResult()} style={{ flex: 1 }} />
            <Button
              label="Discard"
              variant="glass"
              icon="close"
              onPress={() => void discardResult()}
              style={{ flex: 1 }}
            />
          </View>
        ) : (
          <GlassSurface style={{ padding: 16, gap: 14 }}>
            <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}>
              <AppText variant="title3">Make it shine</AppText>
              {suggestion.suggested ? <Badge label="Clarity suggested" tone="accent" icon="sparkles" /> : null}
            </View>
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 10 }} accessibilityRole="tablist">
              {TOOLS.map(t => {
                const selected = tool === t.id;
                const premium = !can(t.feature).allowed || t.feature === 'vibeMatch' || t.feature === 'multiShot';
                return (
                  <Pressable
                    key={t.id}
                    testID={`tool-${t.id}`}
                    onPress={() => chooseTool(t)}
                    accessibilityRole="tab"
                    accessibilityLabel={`${t.label}${premium ? ', premium' : ''}`}
                    accessibilityHint={t.description}
                    accessibilityState={{ selected }}
                    style={{
                      flexBasis: '47%',
                      flexGrow: 1,
                      padding: 12,
                      borderRadius: 16,
                      gap: 6,
                      borderWidth: 1,
                      borderColor: selected ? theme.colors.accent : theme.colors.glassStroke,
                      backgroundColor: selected ? 'rgba(255,122,69,0.12)' : theme.colors.glassFill,
                    }}
                  >
                    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
                      <Icon name={t.icon} size={18} color={selected ? theme.colors.accent : theme.colors.textPrimary} />
                      <AppText variant="bodyStrong" style={{ flex: 1 }}>
                        {t.label}
                      </AppText>
                      {premium ? <Icon name="crown" size={14} color={theme.colors.premium} strokeWidth={2.2} /> : null}
                    </View>
                    <AppText variant="caption" tone="secondary" numberOfLines={2}>
                      {t.description}
                    </AppText>
                  </Pressable>
                );
              })}
            </View>

            {tool === 'enhance' && suggestion.suggested ? (
              <AppText variant="body" tone="secondary">
                This photo has {suggestion.reasons.join(' and ')}. Clarity cleans it up right on your phone.
              </AppText>
            ) : null}
            {tool === 'spotlight' ? (
              <SpotlightPanel
                blur={blur}
                glow={glow}
                points={points.length}
                onBlur={setBlur}
                onGlow={setGlow}
                onClear={() => setPoints([])}
              />
            ) : null}
            {tool === 'look' && vibeLook ? (
              <View style={{ gap: 10 }}>
                <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
                  <Image
                    source={{ uri: `file://${vibeLook.referencePath}` }}
                    style={{ width: 52, height: 52, borderRadius: 12 }}
                  />
                  <AppText variant="bodyStrong" style={{ flex: 1 }}>
                    {lookDescription(vibeLook).text}
                  </AppText>
                  <Button label="Change" variant="ghost" onPress={() => setVibeOpen(true)} />
                </View>
                <SegmentedControl
                  label="Strength"
                  value={VIBE_STRENGTHS.find(s => Math.abs(s.value - vibeLook.strength) < 0.01)?.id ?? 'balanced'}
                  onChange={id => {
                    const s = VIBE_STRENGTHS.find(v => v.id === id);
                    if (s) {
                      updateSetting('vibeLook', { ...vibeLook, strength: s.value }).catch(() => undefined);
                    }
                  }}
                  options={VIBE_STRENGTHS.map(s => ({ value: s.id, label: s.label }))}
                />
              </View>
            ) : null}

            {tool ? (
              working ? (
                <View style={{ gap: 10 }}>
                  <LoadingState label="Perfecting your photo…" />
                  <Button label="Cancel" variant="glass" icon="stop" onPress={() => abort.current?.abort()} />
                </View>
              ) : (
                <Button
                  label={`Apply ${TOOLS.find(t => t.id === tool)?.label ?? ''}`}
                  icon="wand"
                  onPress={() => void apply()}
                  disabled={tool === 'look' && !vibeLook}
                  testID="apply-tool"
                />
              )
            ) : null}
          </GlassSurface>
        )}

        <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 10 }}>
          <StatTile
            label="Size"
            value={`${image.width}×${image.height}`}
            icon="image"
            style={{ flexBasis: '47%', flexGrow: 1 }}
          />
          <StatTile
            label="Resolution"
            value={formatMegapixels(image.width, image.height)}
            icon="aperture"
            style={{ flexBasis: '47%', flexGrow: 1 }}
          />
          <StatTile
            label="File"
            value={formatBytes(image.sizeBytes)}
            icon="storage"
            style={{ flexBasis: '47%', flexGrow: 1 }}
          />
          {num(exif?.iso) !== null ? (
            <StatTile label="ISO" value={`${num(exif?.iso)}`} icon="sun" style={{ flexBasis: '47%', flexGrow: 1 }} />
          ) : null}
          {exposureTime !== null ? (
            <StatTile
              label="Shutter"
              value={shutterLabel(exposureTime)}
              icon="timer"
              style={{ flexBasis: '47%', flexGrow: 1 }}
            />
          ) : null}
          {str(image.metadata.rawPath) ? (
            <StatTile
              label="RAW"
              value="Saved"
              icon="layers"
              tint={theme.colors.premium}
              style={{ flexBasis: '47%', flexGrow: 1 }}
            />
          ) : null}
        </View>

        <Button label="Share" variant="glass" icon="share" onPress={() => void share()} />
        <Button label="Delete photo" variant="danger" icon="trash" onPress={confirmDelete} />
      </View>
      <VibeSheet
        visible={vibeOpen}
        look={vibeLook}
        onChange={look => {
          updateSetting('vibeLook', look).catch(() => undefined);
          setVibeOpen(false);
        }}
        onClose={() => setVibeOpen(false)}
      />
      <PremiumSheet feature={premiumFor} onClose={() => setPremiumFor(null)} />
    </Screen>
  );
}
