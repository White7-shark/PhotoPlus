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
import {
  allowanceLabel,
  checkAllowance,
  isDailyUsage,
  recordUse,
  USAGE_KEY,
  type DailyUsage,
} from '../services/entitlements/allowance';
import { FREE_DAILY_ALLOWANCE, type FeatureKey } from '../services/entitlements/features';
import { friendlyMessage } from '../services/errors';
import { lookDescription, saveVibeLook, VIBE_STRENGTHS } from '../services/looks/LookService';
import { createLogger } from '../services/logging/logger';
import { getProcessingService, ProcessingError } from '../services/processing/ImageProcessingService';
import {
  isLensProfiles,
  lensNotice,
  LENS_PROFILES_KEY,
  rememberReading,
  type LensProfiles,
  type LensReport,
} from '../services/processing/lens';
import {
  cleanOf,
  editedPathFor,
  enhancedCopyMetadata,
  enhanceSuggestion,
  filesToDelete,
  formatMegapixels,
  noiseReduction,
  type EditKind,
} from '../services/processing/quality';
import { editRecipe, effectiveRecipe, transformPlan, type RecipeStep } from '../services/processing/recipe';
import type { QualityReport } from '../services/processing/types';
import { formatBytes, getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { VibeSheet } from './camera/VibeSheet';
import { WatermarkUnlock } from './WatermarkUnlock';
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
  {
    id: 'transform',
    label: 'Transform',
    description: 'The look of a high-end camera',
    icon: 'aperture',
    feature: 'transform',
  },
];

/** Photos up to this long edge can get twice the detail from Transform. */
const TRANSFORM_2X_MAX_EDGE = 2048;

interface EditResult {
  kind: EditKind;
  outputPath: string;
  width: number;
  height: number;
  sizeBytes: number;
  report: unknown;
  /** The result without the Photo Plus mark (free plan). */
  cleanPath: string | null;
  /** Spotlight result of a Transform that re-applied it. */
  portrait?: unknown;
  /** What this edit did, for the kept photo's recipe. */
  step: RecipeStep | null;
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
  if (mode === 'transform') {
    out.unshift({ label: 'High-end lens look', icon: 'aperture', tone: 'premium' });
    if ((num(r?.fringesFixedPx) ?? 0) >= 0.5) {
      out.push({ label: 'Colour fringes removed', icon: 'check', tone: 'success' });
    }
    if ((num(r?.deblur) ?? 0) >= 0.6) {
      out.push({ label: 'Sharper detail', icon: 'sparkles', tone: 'success' });
    }
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
  const watermark = useAppStore(s => s.settings.watermark);
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
  const [lens, setLens] = useState<LensReport | null>(null);
  const [lensProfiles, setLensProfiles] = useState<LensProfiles>({});
  const [usage, setUsage] = useState<DailyUsage | null>(null);
  const [premiumMessage, setPremiumMessage] = useState<string | null>(null);
  const [transformDetail, setTransformDetail] = useState<'standard' | 'double'>('standard');
  const entitlements = useAppStore(s => s.entitlements);
  // The day the allowance is counted on: when the screen opened.
  const [openedAt] = useState(() => Date.now());
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
        if (record.width > 0 && Math.max(record.width, record.height) <= TRANSFORM_2X_MAX_EDGE) {
          setTransformDetail('double');
        }
        // What the camera's lens left in the photo: read on the untouched
        // photo (effects such as a blurred background would mislead it).
        const settingsRepo = repos?.settings;
        const recipe = effectiveRecipe(record);
        const lensId = str(record.metadata.lens);
        const [profiles, used] = await Promise.all([
          settingsRepo?.getInternal<unknown>(LENS_PROFILES_KEY).catch(() => null),
          settingsRepo?.getInternal<unknown>(USAGE_KEY).catch(() => null),
        ]);
        if (cancelled) {
          return;
        }
        const known = isLensProfiles(profiles) ? profiles : {};
        setLensProfiles(known);
        setUsage(isDailyUsage(used) ? used : null);
        if (str(record.metadata.captureMode) !== 'transform') {
          processing
            .analyzeLens(recipe.basePath)
            .then(async r => {
              if (cancelled) {
                return;
              }
              setLens(r);
              // Remember readings of this camera's own untouched photos.
              if (record.source === 'camera' && lensId && !recipe.baseStyled && settingsRepo) {
                const next = rememberReading(known, lensId, record.id, r);
                setLensProfiles(next);
                await settingsRepo.setInternal(LENS_PROFILES_KEY, next);
              }
            })
            .catch(() => undefined);
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

  const transformAccess = checkAllowance('transform', entitlements, usage, openedAt);
  const chooseTool = (t: Tool) => {
    const access = t.id === 'transform' ? transformAccess : can(t.feature);
    if (!access.allowed) {
      setPremiumMessage(
        t.id === 'transform' && FREE_DAILY_ALLOWANCE.transform
          ? `You’ve used today’s ${FREE_DAILY_ALLOWANCE.transform} free Transforms. Premium makes Transform unlimited.`
          : null,
      );
      setPremiumFor(t.feature);
      return;
    }
    setTool(cur => (cur === t.id ? null : t.id));
    if (t.id === 'spotlight') {
      setView('photo'); // choosing happens on the photo itself
    }
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
      // Edits start from the photo without the mark (it is drawn once, on the result).
      const source = cleanOf(image);
      let r: EditResult;
      if (tool === 'transform') {
        if (!transformAccess.allowed) {
          setPremiumFor('transform');
          return;
        }
        const plan = transformPlan(image);
        // The untouched photo may have been removed: then Transform works on
        // this photo as it is, keeping its effects.
        const baseKept = plan.inputPath === source || (await storage.exists(plan.inputPath));
        const out = await processing.transform(
          baseKept
            ? {
                inputPath: plan.inputPath,
                outputPath,
                upscale: transformDetail === 'double' ? 2 : 1,
                keepStyle: plan.keepStyle,
                spotlight: plan.spotlight,
                look: plan.look?.look ?? null,
                lookStrength: plan.look?.strength,
                watermark,
              }
            : {
                inputPath: source,
                watermark,
                outputPath,
                upscale: transformDetail === 'double' ? 2 : 1,
                keepStyle: plan.keepStyle || plan.replayed > 0,
              },
          { signal },
        );
        // A Transform counts once it has made a photo.
        const nextUsage = recordUse(usage, 'transform', openedAt);
        setUsage(nextUsage);
        repos?.settings.setInternal(USAGE_KEY, nextUsage).catch(() => undefined);
        r = {
          kind: tool,
          ...out,
          cleanPath: out.cleanPath ?? null,
          report: out.transform,
          portrait: out.portrait,
          step: null,
        };
      } else if (tool === 'spotlight') {
        const settings = spotlightSettings(blur, glow);
        const out = await processing.spotlight(
          { inputPath: source, outputPath, points: flattenPoints(points), ...settings, watermark },
          { signal },
        );
        r = {
          kind: tool,
          ...out,
          cleanPath: out.cleanPath ?? null,
          report: out.portrait,
          step: { kind: 'spotlight', points: flattenPoints(points), ...settings },
        };
      } else if (tool === 'look') {
        if (!vibeLook) {
          setVibeOpen(true);
          return;
        }
        const out = await processing.applyLook(
          { inputPath: source, outputPath, look: vibeLook.look, strength: vibeLook.strength, watermark },
          { signal },
        );
        r = {
          kind: tool,
          ...out,
          cleanPath: out.cleanPath ?? null,
          report: null,
          step: { kind: 'look', look: vibeLook.look, strength: vibeLook.strength },
        };
      } else {
        const out = await processing.enhance(
          { inputPath: source, outputPath, upscale: tool === 'superres' ? 2 : 1, autoTone: true, watermark },
          { signal },
        );
        r = { kind: tool, ...out, cleanPath: out.cleanPath ?? null, report: out.report, step: { kind: 'clarity' } };
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
  }, [
    image,
    tool,
    points,
    blur,
    glow,
    vibeLook,
    watermark,
    transformAccess.allowed,
    transformDetail,
    usage,
    repos,
    openedAt,
  ]);

  /** Saves the result as its own photo; then shows it, or opens it in the editor. */
  const keepResult = useCallback(
    async (then: 'show' | 'edit' = 'show') => {
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
            ...enhancedCopyMetadata(
              image,
              result.kind,
              result.kind === 'spotlight' ? null : result.report,
              result.cleanPath,
            ),
            ...(result.kind === 'spotlight' ? { portrait: result.report } : {}),
            ...(result.kind === 'transform' && result.portrait ? { portrait: result.portrait } : {}),
            ...(result.kind === 'look' || (result.kind === 'transform' && transformPlan(image).look)
              ? { look: true }
              : {}),
            // What was done, so Transform can always start over from the untouched photo.
            recipe: result.step ? editRecipe(image, result.step) : effectiveRecipe(image),
          },
        });
        void loadProjects();
        if (then === 'edit') {
          navigation.replace('Editor', { imageId: record.id });
        } else {
          navigation.replace('PhotoPreview', { imageId: record.id });
        }
      } catch (e) {
        Alert.alert('Couldn’t save', friendlyMessage(errorCode(e)));
      }
    },
    [image, result, repos, loadProjects, navigation],
  );

  const discardResult = useCallback(async () => {
    if (result) {
      const storage = getStorageService();
      await Promise.allSettled(
        [result.outputPath, result.cleanPath].filter((p): p is string => !!p).map(p => storage.deleteFile(p)),
      );
    }
    setResult(null);
    setView(image && str(image.metadata.originalPath) ? 'compare' : 'photo');
  }, [result, image]);

  // Sharing goes through export: the photo leaves the app with the watermark
  // its plan requires (drawn natively, once, on the clean photo).
  const share = useCallback(async () => {
    const path = result ? (result.cleanPath ?? result.outputPath) : image ? cleanOf(image) : null;
    if (!path) {
      return;
    }
    try {
      const out = await getProcessingService().exportImage({ inputPath: path, watermark, destination: 'share' });
      await getStorageService().share([out.outputPath], 'Share your photo');
    } catch (e) {
      Alert.alert('Couldn’t share', friendlyMessage(errorCode(e), friendlyMessage('SHARE_FAILED')));
    }
  }, [result, image, watermark]);

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
        result.kind === 'spotlight' ? result.report : (result.portrait ?? null),
        result.kind === 'look',
      )
    : improvementBadges(mode, image.metadata.report, image.metadata.portrait, image.metadata.look === true);
  const before = result ? image.path : original;
  const after = result ? result.outputPath : image.path;
  const suggestion = enhanceSuggestion(original ? null : quality);
  const notice = result ? null : lensNotice(lens, lensProfiles[str(image.metadata.lens) ?? ''] ?? undefined);
  const plan = transformPlan(image);
  const kept = [plan.spotlight ? 'Spotlight' : null, plan.look ? 'Vibe Match' : null].filter(Boolean);
  const transformTool = TOOLS.find(t => t.id === 'transform');
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

        {notice && tool !== 'transform' ? (
          <GlassSurface style={{ padding: 16, gap: 10 }} testID="lens-notice">
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <Icon name="aperture" size={20} color={theme.colors.premium} />
              <AppText variant="title3" style={{ flex: 1 }} accessibilityRole="header">
                {notice.title}
              </AppText>
            </View>
            <AppText variant="body" tone="secondary">
              {notice.body}
            </AppText>
            {notice.signs.length ? (
              <AppText variant="caption" tone="secondary">
                Seen in this photo: {notice.signs.join(', ')}.
              </AppText>
            ) : null}
            <Button
              label="Try Transform"
              variant="premium"
              icon="aperture"
              onPress={() => transformTool && chooseTool(transformTool)}
              testID="try-transform"
            />
            {allowanceLabel(transformAccess, 'Transform') ? (
              <AppText variant="caption" tone="secondary" align="center">
                {allowanceLabel(transformAccess, 'Transform')}
              </AppText>
            ) : null}
          </GlassSurface>
        ) : null}

        {result ? (
          <View style={{ gap: 10 }}>
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
            <Button
              label="Keep and edit photo"
              icon="sliders"
              variant="glass"
              onPress={() => void keepResult('edit')}
              testID="keep-and-edit"
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
                const premium =
                  !can(t.feature).allowed ||
                  t.feature === 'vibeMatch' ||
                  t.feature === 'multiShot' ||
                  t.feature === 'transform';
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

            {tool === 'transform' ? (
              <View style={{ gap: 10 }}>
                <AppText variant="body" tone="secondary">
                  Restores fine detail, sharpens soft edges, removes colour fringes and grain, as if a high-end camera
                  took it.
                  {kept.length
                    ? ` Your ${kept.join(' and ')} ${kept.length > 1 ? 'effects are' : 'effect is'} re-applied on top.`
                    : ''}
                </AppText>
                {Math.max(image.width, image.height) <= TRANSFORM_2X_MAX_EDGE ? (
                  <SegmentedControl
                    label="Detail"
                    value={transformDetail}
                    onChange={setTransformDetail}
                    options={[
                      { value: 'standard', label: 'Standard' },
                      { value: 'double', label: '2× detail' },
                    ]}
                  />
                ) : null}
                {allowanceLabel(transformAccess, 'Transform') ? (
                  <Badge label={allowanceLabel(transformAccess, 'Transform') ?? ''} tone="premium" icon="crown" />
                ) : null}
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

        <Button
          label="Edit photo"
          icon="sliders"
          onPress={() => navigation.navigate('Editor', { imageId: image.id })}
          testID="edit-photo"
        />
        <Button label="Share" variant="glass" icon="share" onPress={() => void share()} />
        {entitlements.plan !== 'premium' && !result ? <WatermarkUnlock photoPath={cleanOf(image)} /> : null}
        <Button label="Delete photo" variant="danger" icon="trash" onPress={confirmDelete} />
      </View>
      <VibeSheet
        visible={vibeOpen}
        look={vibeLook}
        onChange={look => {
          saveVibeLook(look, vibeLook).catch(() => undefined);
          setVibeOpen(false);
        }}
        onClose={() => setVibeOpen(false)}
      />
      <PremiumSheet
        feature={premiumFor}
        message={premiumMessage}
        onClose={() => {
          setPremiumFor(null);
          setPremiumMessage(null);
        }}
      />
    </Screen>
  );
}
