import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Alert, Image, View } from 'react-native';

import {
  AppText,
  Badge,
  Button,
  ErrorState,
  GlassSurface,
  LoadingState,
  Screen,
  SegmentedControl,
  StatTile,
} from '../components';
import type { RootScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import type { ImageRecord } from '../services/database/repositories/ImagesRepository';
import { createLogger } from '../services/logging/logger';
import { getProcessingService, ProcessingError } from '../services/processing/ImageProcessingService';
import {
  enhancedCopyMetadata,
  enhancedPathFor,
  enhanceSuggestion,
  filesToDelete,
  formatMegapixels,
  noiseReduction,
} from '../services/processing/quality';
import type { EnhanceResult, QualityReport } from '../services/processing/types';
import { formatBytes, getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { CompareView } from './CompareView';

const log = createLogger('photo');

const MODE_LABELS: Record<string, string> = {
  standard: 'Photo',
  raw: 'RAW + JPEG',
  enhance: 'Enhanced',
  night: 'Night',
  superres: 'Super-res',
};

type ViewMode = 'photo' | 'compare';

const str = (v: unknown): string | null => (typeof v === 'string' && v.length > 0 ? v : null);
const num = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? v : null);

interface ReportSummary {
  noiseBefore: number;
  noiseAfter: number;
  framesUsed: number | null;
  framesInput: number | null;
  upscale: number;
}

/** Engine reports are stored as JSON metadata; read them defensively. */
function asReport(v: unknown): ReportSummary | null {
  if (!v || typeof v !== 'object') {
    return null;
  }
  const r = v as Record<string, unknown>;
  const before = num(r.noiseBefore);
  const after = num(r.noiseAfter);
  if (before === null || after === null) {
    return null;
  }
  return {
    noiseBefore: before,
    noiseAfter: after,
    framesUsed: num(r.framesUsed),
    framesInput: num(r.framesInput),
    upscale: num(r.upscale) ?? 1,
  };
}

function shutterLabel(seconds: number): string {
  return seconds >= 1 ? `${Math.round(seconds * 10) / 10}″` : `1/${Math.round(1 / seconds)}`;
}

/**
 * Photo preview after capture: the photo, its capture details, delete, and
 * Enhance with a before/after comparison. Processed captures (Enhance,
 * Night, Super-res) already carry their original, so the comparison is
 * available immediately.
 */
export function PhotoPreviewScreen({ navigation, route }: RootScreenProps<'PhotoPreview'>) {
  const theme = useTheme();
  const repos = useAppStore(s => s.repos);
  const loadProjects = useAppStore(s => s.loadProjects);
  const [image, setImage] = useState<ImageRecord | null>(null);
  const [status, setStatus] = useState<'loading' | 'ready' | 'missing'>('loading');
  const [quality, setQuality] = useState<QualityReport | null>(null);
  const [exif, setExif] = useState<Record<string, unknown> | null>(null);
  const [view, setView] = useState<ViewMode>('photo');
  const [upscale, setUpscale] = useState<1 | 2>(1);
  const [enhancing, setEnhancing] = useState(false);
  const [result, setResult] = useState<EnhanceResult | null>(null);
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
        // The original of a processed capture shows why it was worth it.
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

  const runEnhance = useCallback(async () => {
    if (!image) {
      return;
    }
    abort.current = new AbortController();
    setEnhancing(true);
    try {
      const storage = getStorageService();
      let outputPath = enhancedPathFor(image.path, upscale);
      for (let attempt = 1; (await storage.exists(outputPath)) && attempt < 20; attempt++) {
        outputPath = enhancedPathFor(image.path, upscale, attempt);
      }
      const r = await getProcessingService().enhance(
        { inputPath: image.path, outputPath, upscale, autoTone: true },
        { signal: abort.current.signal },
      );
      setResult(r);
      setView('compare');
    } catch (e) {
      if (!(e instanceof ProcessingError && e.cancelled)) {
        Alert.alert('Enhance failed', errorMessage(e));
      }
    } finally {
      setEnhancing(false);
    }
  }, [image, upscale]);

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
        metadata: enhancedCopyMetadata(image, upscale, result.report),
      });
      void loadProjects();
      navigation.replace('PhotoPreview', { imageId: record.id });
    } catch (e) {
      Alert.alert('Could not save', errorMessage(e));
    }
  }, [image, result, repos, upscale, loadProjects, navigation]);

  const discardResult = useCallback(async () => {
    if (result) {
      await getStorageService()
        .deleteFile(result.outputPath)
        .catch(() => undefined);
    }
    setResult(null);
    setView('photo');
  }, [result]);

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
            Alert.alert('Could not delete', errorMessage(e));
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
  const report = asReport(result?.report ?? image.metadata.report);
  const before = result ? image.path : original;
  const after = result ? result.outputPath : image.path;
  const suggestion = enhanceSuggestion(original ? null : quality);
  const compareAvailable = !!before;
  const exposureTime = num(exif?.exposureTime);

  return (
    <Screen
      title={MODE_LABELS[mode] ?? 'Photo'}
      overline={image.capturedAt ? new Date(image.capturedAt).toLocaleString() : undefined}
      onBack={() => navigation.goBack()}
      bottomInset={32}
    >
      <View style={{ gap: 16 }}>
        {compareAvailable ? (
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
          <Image
            source={{ uri: `file://${result?.outputPath ?? image.path}` }}
            style={{ width: '100%', aspectRatio: aspect, borderRadius: 18, backgroundColor: '#000' }}
            resizeMode="contain"
            accessibilityLabel="Captured photo"
          />
        )}

        {report ? (
          <GlassSurface style={{ padding: 16, gap: 10 }}>
            <AppText variant="title3">What the engine did</AppText>
            <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
              <Badge
                label={`Noise −${noiseReduction(report.noiseBefore, report.noiseAfter)}%`}
                tone="success"
                icon="sparkles"
              />
              {report.framesInput && report.framesInput > 1 ? (
                <Badge
                  label={`${report.framesUsed ?? report.framesInput}/${report.framesInput} frames fused`}
                  tone="accent"
                  icon="layers"
                />
              ) : null}
              {report.upscale === 2 ? <Badge label="2× resolution" tone="premium" icon="crown" /> : null}
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
        ) : !original ? (
          <GlassSurface style={{ padding: 16, gap: 12 }}>
            <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}>
              <AppText variant="title3">Enhance</AppText>
              {suggestion.suggested ? <Badge label="Suggested" tone="accent" icon="sparkles" /> : null}
            </View>
            <AppText variant="body" tone="secondary">
              {suggestion.suggested
                ? `This photo has ${suggestion.reasons.join(' and ')}. Enhance restores detail on-device.`
                : 'Denoise, deblur and balance this photo on-device.'}
            </AppText>
            <SegmentedControl
              label="Output size"
              value={upscale === 2 ? 'x2' : 'x1'}
              onChange={v => setUpscale(v === 'x2' ? 2 : 1)}
              options={[
                { value: 'x1', label: 'Same size' },
                { value: 'x2', label: 'Super-res 2×' },
              ]}
            />
            {enhancing ? (
              <Button label="Cancel" variant="glass" icon="stop" onPress={() => abort.current?.abort()} />
            ) : (
              <Button label="Enhance photo" icon="wand" onPress={() => void runEnhance()} />
            )}
            {enhancing ? <LoadingState label="Restoring detail…" /> : null}
          </GlassSurface>
        ) : null}

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
              value="DNG saved"
              icon="layers"
              tint={theme.colors.premium}
              style={{ flexBasis: '47%', flexGrow: 1 }}
            />
          ) : null}
        </View>

        <Button label="Delete photo" variant="danger" icon="trash" onPress={confirmDelete} />
      </View>
    </Screen>
  );
}
