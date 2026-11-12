import React, { useCallback, useEffect, useMemo, useReducer, useRef, useState } from 'react';
import {
  Alert,
  Image,
  Pressable,
  ScrollView,
  View,
  type GestureResponderEvent,
  type LayoutChangeEvent,
} from 'react-native';

import { AppText, Button, ErrorState, GlassSurface, Icon, IconButton, LoadingState, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import { errorCode } from '../../native';
import { toggleSpotlightPoint } from '../../services/camera/model';
import type { ImageRecord } from '../../services/database/repositories/ImagesRepository';
import { createId } from '../../services/database/ids';
import { allProps, findProp, propSource, type PropCategory, type PropDef } from '../../services/edit/catalog';
import {
  EMPTY_DOCUMENT,
  geometryOf,
  hasEdits,
  isEditDocument,
  withDefaults,
  type EditDocument,
} from '../../services/edit/document';
import { canRedo, canUndo, historyReducer, startHistory } from '../../services/edit/history';
import {
  defaultPlacement,
  fitToFace,
  mainFace,
  type FaceGeometry,
  type PropPlacement,
} from '../../services/edit/placement';
import type { BrandingPreset, WatermarkSpec } from '../../services/edit/watermark';
import {
  allowanceLabel,
  checkAllowance,
  isDailyUsage,
  recordUse,
  USAGE_KEY,
  type DailyUsage,
} from '../../services/entitlements/allowance';
import type { FeatureKey } from '../../services/entitlements/features';
import { friendlyMessage } from '../../services/errors';
import { createLogger } from '../../services/logging/logger';
import { getProcessingService, ProcessingError } from '../../services/processing/ImageProcessingService';
import { cleanOf, editedPathFor } from '../../services/processing/quality';
import { getStorageService } from '../../services/storage/StorageService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { SpotlightMarkers } from '../camera/SpotlightControls';
import { aspectCrop, CROP_ASPECTS, isFullCrop, photoInView, viewToPhoto } from './geometry';
import {
  AdjustPanel,
  CropPanel,
  EDITOR_TOOLS,
  FocusPanel,
  FramesPanel,
  LooksPanel,
  PropsPanel,
  SpotlightEditPanel,
  TransformEditPanel,
  type ToolId,
} from './panels';
import { PortraitPanel, SpotlightAdvanced } from './PortraitPanel';
import { BackdropEditPanel } from './BackdropPanel';
import { SkinTonePanel } from './SkinTonePanel';
import { BeautyEditPanel } from './BeautyPanel';
import { WatermarkUnlock } from '../WatermarkUnlock';
import { WatermarkPanel } from './WatermarkPanel';

const log = createLogger('editor');

interface Preview {
  path: string;
  width: number;
  height: number;
  photoRect: [number, number, number, number];
}

const str = (v: unknown): string | null => (typeof v === 'string' && v.length > 0 ? v : null);

/** Tools that work on the photo itself (frame hidden while they are open). */
const PHOTO_TOOLS: readonly ToolId[] = ['crop', 'props', 'spotlight', 'focus'];

/**
 * The photo editor. The original photo is never changed: edits are a
 * document replayed by the engine for the live preview, for saved copies and
 * for exports, so every step can be undone (and a saved edit re-opened).
 */
export function EditorScreen({ navigation, route }: RootScreenProps<'Editor'>) {
  const theme = useTheme();
  const repos = useAppStore(s => s.repos);
  const loadProjects = useAppStore(s => s.loadProjects);
  const settings = useAppStore(s => s.settings);
  const updateSetting = useAppStore(s => s.updateSetting);
  const entitlements = useAppStore(s => s.entitlements);
  const can = useFeatureCheck();
  const [image, setImage] = useState<ImageRecord | null>(null);
  const [status, setStatus] = useState<'loading' | 'ready' | 'missing'>('loading');
  const [base, setBase] = useState<string | null>(null);
  const [history, dispatch] = useReducer(historyReducer, startHistory(EMPTY_DOCUMENT));
  const doc = history.present;
  const [tool, setTool] = useState<ToolId>('adjust');
  const [preview, setPreview] = useState<Preview | null>(null);
  const [rendering, setRendering] = useState(false);
  const [skinToneNoFace, setSkinToneNoFace] = useState(false);
  const [viewSize, setViewSize] = useState({ width: 0, height: 0 });
  const [comparing, setComparing] = useState(false);
  const [propCategory, setPropCategory] = useState<PropCategory>('hats');
  const [selectedProp, setSelectedProp] = useState<number | null>(null);
  const [live, setLive] = useState<{
    prop?: PropPlacement;
    crop?: EditDocument['crop'];
    frame?: EditDocument['frame'];
  } | null>(null);
  const [cropAspect, setCropAspect] = useState('original');
  const [cropSize, setCropSize] = useState(1);
  const [findingLines, setFindingLines] = useState(false);
  const [premiumFor, setPremiumFor] = useState<FeatureKey | null>(null);
  const [premiumMessage, setPremiumMessage] = useState<string | null>(null);
  const [usage, setUsage] = useState<DailyUsage | null>(null);
  const [openedAt] = useState(() => Date.now());
  const [busy, setBusy] = useState<'save' | 'share' | 'gallery' | null>(null);
  const faces = useRef(new Map<string, FaceGeometry[]>());
  const lastPreview = useRef<string | null>(null);
  const renderAbort = useRef<AbortController | null>(null);
  const counter = useRef(0);
  const spec = settings.watermark;
  const presets = settings.brandingPresets;
  const customProps = settings.customProps;
  const propList = useMemo(() => allProps(customProps), [customProps]);
  const [frameThumbs, setFrameThumbs] = useState<Record<string, string>>({});
  const premiumPlan = entitlements.plan === 'premium';

  // ---- frame previews: every frame around this photo, made once per photo -----
  useEffect(() => {
    if (tool !== 'frames' || !base) return;
    let live = true;
    void getProcessingService()
      .frameThumbnails(base)
      .then(t => {
        if (live) setFrameThumbs(t);
      });
    return () => {
      live = false;
    };
  }, [tool, base]);

  // ---- load -------------------------------------------------------------------
  useEffect(() => {
    let cancelled = false;
    (async () => {
      const record = repos ? await repos.images.get(route.params.imageId) : null;
      if (cancelled) return;
      setImage(record);
      if (!record) {
        setStatus('missing');
        return;
      }
      // A saved edit re-opens on its untouched photo, with its edits.
      const savedBase = str(record.metadata.editBase);
      const saved = isEditDocument(record.metadata.edit) ? withDefaults(record.metadata.edit) : null;
      setBase(savedBase && saved ? savedBase : cleanOf(record));
      dispatch({ type: 'reset', doc: savedBase && saved ? saved : EMPTY_DOCUMENT });
      const used = await repos?.settings.getInternal<unknown>(USAGE_KEY).catch(() => null);
      if (!cancelled) setUsage(isDailyUsage(used) ? used : null);
      setStatus('ready');
    })().catch(e => {
      log.error('load failed', e);
      if (!cancelled) setStatus('missing');
    });
    return () => {
      cancelled = true;
      renderAbort.current?.abort();
    };
  }, [repos, route.params.imageId]);

  const update = useCallback(
    (patch: Partial<EditDocument>, group?: string) =>
      dispatch({ type: 'set', doc: { ...history.present, ...patch }, group }),
    [history.present],
  );
  const commit = useCallback(() => dispatch({ type: 'commit' }), []);
  // For results that arrive later (the photo may have been edited meanwhile).
  const latestUpdate = useRef(update);
  useEffect(() => {
    latestUpdate.current = update;
  }, [update]);

  // ---- live preview -------------------------------------------------------------
  const photoTool = PHOTO_TOOLS.includes(tool);
  const renderDoc = useMemo<EditDocument>(() => (tool === 'crop' ? { ...doc, crop: null } : doc), [doc, tool]);
  const skipProps = useMemo(
    () => (tool === 'props' && selectedProp !== null ? [selectedProp] : []),
    [tool, selectedProp],
  );
  const showMark = !photoTool;

  useEffect(() => {
    if (!base || status !== 'ready') return;
    const run = async () => {
      renderAbort.current?.abort();
      const abort = new AbortController();
      renderAbort.current = abort;
      setRendering(true);
      try {
        const dir = (await getStorageService().getPaths()).processingCache;
        const outputPath = `${dir}/editor_${createId('p')}_${counter.current++}.jpg`;
        const r = await getProcessingService().renderEdit(
          {
            inputPath: base,
            outputPath,
            doc: renderDoc,
            watermark: showMark ? spec : null,
            preview: true,
            maxDimension: 1280,
            skipProps,
            skipFrame: photoTool,
          },
          { signal: abort.signal },
        );
        if (abort.signal.aborted) return;
        const old = lastPreview.current;
        lastPreview.current = r.outputPath;
        setPreview({ path: r.outputPath, width: r.width, height: r.height, photoRect: r.photoRect });
        setSkinToneNoFace(r.portrait?.skinToneApplied === false);
        if (old)
          void getStorageService()
            .deleteFile(old)
            .catch(() => undefined);
      } catch (e) {
        if (!(e instanceof ProcessingError && e.cancelled)) log.warn('preview failed', e);
      } finally {
        if (!abort.signal.aborted) setRendering(false);
      }
    };
    const timer = setTimeout(() => void run(), 120);
    return () => clearTimeout(timer);
  }, [base, status, renderDoc, spec, showMark, skipProps, photoTool]);

  useEffect(
    () => () => {
      if (lastPreview.current)
        void getStorageService()
          .deleteFile(lastPreview.current)
          .catch(() => undefined);
    },
    [],
  );

  // ---- premium ------------------------------------------------------------------
  const transformAccess = checkAllowance('transform', entitlements, usage, openedAt);
  const askPremium = (feature: FeatureKey, message?: string) => {
    setPremiumMessage(message ?? null);
    setPremiumFor(feature);
  };

  // ---- props --------------------------------------------------------------------
  const facesFor = useCallback(async (): Promise<FaceGeometry[]> => {
    if (!base) return [];
    const key = JSON.stringify(geometryOf(doc));
    const known = faces.current.get(key);
    if (known) return known;
    const found = await getProcessingService().detectFaces(base, doc);
    faces.current.set(key, found);
    return found;
  }, [base, doc]);
  const autoPerspective = useCallback(async () => {
    if (!base || findingLines) return;
    setFindingLines(true);
    try {
      const vertical = await getProcessingService().detectPerspective(base, doc);
      if (Math.abs(vertical) < 0.02) {
        Alert.alert('Lines look straight', 'No leaning walls or buildings to fix in this photo.');
        return;
      }
      // Props were placed on the old geometry: keep them, the user can move them.
      latestUpdate.current({ perspectiveV: Math.round(vertical * 100) / 100, perspectiveH: 0 });
      commit();
    } finally {
      setFindingLines(false);
    }
  }, [base, doc, findingLines, commit]);
  const photoAspect = preview && preview.height > 0 ? preview.width / preview.height : 0.75;
  const placementFor = useCallback(
    async (def: PropDef, current?: PropPlacement): Promise<PropPlacement> => {
      const face = mainFace(await facesFor());
      const p = face ? fitToFace(def, face, photoAspect) : defaultPlacement(def);
      return current ? { ...p, flip: current.flip } : p;
    },
    [facesFor, photoAspect],
  );
  const addProp = async (def: PropDef) => {
    if (doc.props.length >= 12) return;
    const p = await placementFor(def);
    update({ props: [...doc.props, p] });
    setSelectedProp(doc.props.length);
  };
  const selected = selectedProp !== null ? doc.props[selectedProp] : undefined;
  const selectedDef = selected ? findProp(selected.id, customProps) : undefined;
  const setSelected = (patch: Partial<PropPlacement>, group?: string) => {
    if (selectedProp === null || !selected) return;
    const next = doc.props.map((p, i) => (i === selectedProp ? { ...p, ...patch } : p));
    update({ props: next }, group);
  };

  // ---- gestures on the photo -----------------------------------------------------------
  const photoArea = preview ? photoInView(viewSize, preview, photoTool ? [0, 0, 1, 1] : preview.photoRect) : null;
  // Where the current touch started (written only in touch handlers).
  const gesture = useRef({ x: 0, y: 0, pageX: 0, pageY: 0, moved: false });

  const handleTap = (x: number, y: number) => {
    if (!preview) return;
    const p = viewToPhoto({ x, y }, viewSize, preview, photoTool ? [0, 0, 1, 1] : preview.photoRect);
    if (!p) return;
    if (tool === 'spotlight') {
      const pts: { x: number; y: number }[] = [];
      const raw = doc.spotlight?.points ?? [];
      for (let i = 0; i + 1 < raw.length; i += 2) pts.push({ x: raw[i], y: raw[i + 1] });
      const next = toggleSpotlightPoint(pts, p).flatMap(q => [q.x, q.y]);
      update({ spotlight: { points: next, blur: doc.spotlight?.blur ?? 0.6 } });
    } else if (tool === 'focus') {
      const f = doc.focus ?? { kind: 'radial' as const, x: 0.5, y: 0.5, size: 0.25, angle: 0, strength: 0.6 };
      update({ focus: { ...f, x: p.x, y: p.y } });
    } else if (tool === 'props') {
      // Select the prop under the finger (the last drawn wins).
      const aspect = preview.width / preview.height;
      const hit = [...doc.props.keys()].reverse().find(i => {
        const q = doc.props[i];
        const def = findProp(q.id, customProps);
        const halfH = (q.width * (def?.aspect ?? 0.5) * aspect) / 2;
        return Math.abs(p.x - q.cx) <= q.width / 2 && Math.abs(p.y - q.cy) <= halfH;
      });
      setSelectedProp(hit ?? null);
    }
  };

  const handleMove = (dx: number, dy: number) => {
    const area = photoArea;
    if (!area || area.w === 0) return;
    if (tool === 'props' && selectedProp !== null && selected) {
      setLive({ prop: { ...selected, cx: selected.cx + dx / area.w, cy: selected.cy + dy / area.h } });
    } else if (tool === 'crop') {
      const c = doc.crop ?? { x: 0, y: 0, w: 1, h: 1 };
      setLive({
        crop: {
          ...c,
          x: Math.min(1 - c.w, Math.max(0, c.x + dx / area.w)),
          y: Math.min(1 - c.h, Math.max(0, c.y + dy / area.h)),
        },
      });
    } else if (tool === 'frames' && doc.frame) {
      const f = doc.frame;
      setLive({
        frame: {
          ...f,
          x: Math.max(-1, Math.min(1, f.x - (2 * dx) / area.w)),
          y: Math.max(-1, Math.min(1, f.y - (2 * dy) / area.h)),
        },
      });
    }
  };

  const handleEnd = () => {
    if (live?.prop && selectedProp !== null) {
      const moved = live.prop;
      update({ props: doc.props.map((p, i) => (i === selectedProp ? moved : p)) });
    } else if (live?.crop) {
      update({ crop: isFullCrop(live.crop) ? null : live.crop });
    } else if (live?.frame) {
      update({ frame: live.frame });
    }
    setLive(null);
  };

  const touch = {
    onStartShouldSetResponder: () => true,
    onMoveShouldSetResponder: () => true,
    onResponderTerminationRequest: () => false,
    onResponderGrant: (e: GestureResponderEvent) => {
      gesture.current = {
        x: e.nativeEvent.locationX,
        y: e.nativeEvent.locationY,
        pageX: e.nativeEvent.pageX,
        pageY: e.nativeEvent.pageY,
        moved: false,
      };
    },
    onResponderMove: (e: GestureResponderEvent) => {
      const dx = e.nativeEvent.pageX - gesture.current.pageX;
      const dy = e.nativeEvent.pageY - gesture.current.pageY;
      if (Math.abs(dx) + Math.abs(dy) > 6) gesture.current.moved = true;
      if (gesture.current.moved) handleMove(dx, dy);
    },
    onResponderRelease: () => {
      if (gesture.current.moved) handleEnd();
      else handleTap(gesture.current.x, gesture.current.y);
    },
    onResponderTerminate: handleEnd,
  };

  // ---- crop presets ----------------------------------------------------------------
  const applyCrop = (aspectId: string, size: number) => {
    if (!preview) return;
    const a = CROP_ASPECTS.find(x => x.id === aspectId) ?? CROP_ASPECTS[0];
    const c = doc.crop;
    const next = aspectCrop(a.ratio, preview, size, c ? c.x + c.w / 2 : 0.5, c ? c.y + c.h / 2 : 0.5);
    update({ crop: isFullCrop(next) ? null : next }, 'crop-size');
  };

  // ---- save & export -------------------------------------------------------------------
  const transformCheck = (): boolean => {
    if (!doc.transform) return true;
    if (!transformAccess.allowed) {
      askPremium('transform', 'You’ve used today’s free Transforms. Premium makes Transform unlimited.');
      return false;
    }
    return true;
  };
  const countTransform = () => {
    if (!doc.transform) return;
    const next = recordUse(usage, 'transform', openedAt);
    setUsage(next);
    repos?.settings.setInternal(USAGE_KEY, next).catch(() => undefined);
  };

  const save = async () => {
    if (!image || !base || !repos || !transformCheck()) return;
    setBusy('save');
    try {
      const storage = getStorageService();
      let outputPath = editedPathFor(base, 'edit');
      for (let n = 1; (await storage.exists(outputPath)) && n < 50; n++) outputPath = editedPathFor(base, 'edit', n);
      const cleanOutputPath = outputPath.replace(/\.jpg$/, '_clean.jpg');
      const r = await getProcessingService().renderEdit({
        inputPath: base,
        outputPath,
        doc,
        watermark: spec,
        preview: false,
        cleanOutputPath,
      });
      countTransform();
      const record = await repos.images.add({
        projectId: image.projectId,
        path: r.outputPath,
        source: 'export',
        width: r.width,
        height: r.height,
        sizeBytes: r.sizeBytes,
        mimeType: 'image/jpeg',
        capturedAt: image.capturedAt,
        metadata: {
          captureMode: 'edit',
          lens: image.metadata.lens ?? null,
          sourceImageId: image.id,
          originalPath: base,
          originalOwned: false,
          cleanPath: r.cleanPath,
          // The untouched photo and the edits: the edit can be re-opened and changed.
          editBase: base,
          edit: doc,
          portrait: r.portrait,
          recipe: { basePath: r.cleanPath ?? r.outputPath, baseStyled: true, steps: [] },
        },
      });
      void loadProjects();
      navigation.replace('PhotoPreview', { imageId: record.id });
    } catch (e) {
      Alert.alert('Couldn’t save', friendlyMessage(errorCode(e)));
    } finally {
      setBusy(null);
    }
  };

  const exportTo = async (destination: 'share' | 'gallery') => {
    if (!base || !transformCheck()) return;
    setBusy(destination);
    try {
      const out = await getProcessingService().exportImage({
        inputPath: base,
        doc,
        watermark: spec,
        format: settings.defaultExportFormat,
        quality: settings.defaultExportQuality,
        destination,
      });
      countTransform();
      if (destination === 'share') {
        await getStorageService().share([out.outputPath], 'Share your photo');
      } else {
        Alert.alert('Saved to your gallery', 'Find it in Pictures › Photo Plus.');
      }
    } catch (e) {
      Alert.alert(destination === 'share' ? 'Couldn’t share' : 'Couldn’t save', friendlyMessage(errorCode(e)));
    } finally {
      setBusy(null);
    }
  };

  const leave = () => {
    if (!hasEdits(doc) || doc === EMPTY_DOCUMENT) {
      navigation.goBack();
      return;
    }
    Alert.alert('Discard your edits?', 'Your original photo stays as it is.', [
      { text: 'Keep editing', style: 'cancel' },
      { text: 'Discard', style: 'destructive', onPress: () => navigation.goBack() },
    ]);
  };

  // ---- watermark ----------------------------------------------------------------------
  const setSpec = (s: WatermarkSpec) => updateSetting('watermark', s).catch(() => undefined);
  const savePreset = (name: string) => {
    const preset: BrandingPreset = { id: createId('brand'), name, spec };
    updateSetting('brandingPresets', [...presets, preset].slice(-12)).catch(() => undefined);
  };
  const pickLogo = async () => {
    const path = await getStorageService()
      .pickImage('imports')
      .catch(() => null);
    if (path)
      void setSpec({
        ...spec,
        custom: { ...(spec.custom ?? { text: '', color: '#FFFFFF', align: 'center' }), logoPath: path },
      });
  };

  // ---- render -------------------------------------------------------------------------
  if (status === 'loading') {
    return (
      <Screen title="Edit" onBack={() => navigation.goBack()} bottomInset={24}>
        <LoadingState label="Opening photo…" />
      </Screen>
    );
  }
  if (!image || !base) {
    return (
      <Screen title="Edit" onBack={() => navigation.goBack()} bottomInset={24}>
        <ErrorState title="Photo not found" message="It may have been deleted." onRetry={() => navigation.goBack()} />
      </Screen>
    );
  }

  const shownCrop = live?.crop !== undefined ? live.crop : doc.crop;
  const liveProp = live?.prop ?? (tool === 'props' ? selected : undefined);
  const liveDef = liveProp ? findProp(liveProp.id, customProps) : undefined;
  const spotPts: { x: number; y: number }[] = [];
  const raw = doc.spotlight?.points ?? [];
  for (let i = 0; i + 1 < raw.length; i += 2) spotPts.push({ x: raw[i], y: raw[i + 1] });

  return (
    <Screen
      title="Edit"
      onBack={leave}
      headerRight={
        <View style={{ flexDirection: 'row', gap: 6 }}>
          <IconButton
            icon="undo"
            label="Undo"
            onPress={() => dispatch({ type: 'undo' })}
            disabled={!canUndo(history)}
            size={40}
            testID="undo"
          />
          <IconButton
            icon="redo"
            label="Redo"
            onPress={() => dispatch({ type: 'redo' })}
            disabled={!canRedo(history)}
            size={40}
            testID="redo"
          />
        </View>
      }
      bottomInset={32}
    >
      <View style={{ gap: 14 }}>
        <View
          {...touch}
          onLayout={(e: LayoutChangeEvent) =>
            setViewSize({ width: e.nativeEvent.layout.width, height: e.nativeEvent.layout.height })
          }
          accessibilityLabel={
            tool === 'spotlight'
              ? 'Photo. Tap what should shine'
              : tool === 'focus'
                ? 'Photo. Tap what stays sharp'
                : 'Photo being edited'
          }
          testID="editor-photo"
          style={{ width: '100%', aspectRatio: 0.9, borderRadius: 18, overflow: 'hidden', backgroundColor: '#000' }}
        >
          {preview ? (
            <Image
              source={{ uri: `file://${comparing ? base : preview.path}` }}
              style={{ width: '100%', height: '100%' }}
              resizeMode="contain"
            />
          ) : (
            <LoadingState label="Preparing your photo…" />
          )}
          {photoArea && !comparing ? (
            <>
              {tool === 'crop' ? (
                <View
                  pointerEvents="none"
                  style={{
                    position: 'absolute',
                    left: photoArea.x,
                    top: photoArea.y,
                    width: photoArea.w,
                    height: photoArea.h,
                  }}
                >
                  {(() => {
                    const c = shownCrop ?? { x: 0, y: 0, w: 1, h: 1 };
                    const dim = 'rgba(0,0,0,0.55)';
                    return (
                      <>
                        <View
                          style={{
                            position: 'absolute',
                            left: 0,
                            right: 0,
                            top: 0,
                            height: `${c.y * 100}%`,
                            backgroundColor: dim,
                          }}
                        />
                        <View
                          style={{
                            position: 'absolute',
                            left: 0,
                            right: 0,
                            top: `${(c.y + c.h) * 100}%`,
                            bottom: 0,
                            backgroundColor: dim,
                          }}
                        />
                        <View
                          style={{
                            position: 'absolute',
                            left: 0,
                            width: `${c.x * 100}%`,
                            top: `${c.y * 100}%`,
                            height: `${c.h * 100}%`,
                            backgroundColor: dim,
                          }}
                        />
                        <View
                          style={{
                            position: 'absolute',
                            left: `${(c.x + c.w) * 100}%`,
                            right: 0,
                            top: `${c.y * 100}%`,
                            height: `${c.h * 100}%`,
                            backgroundColor: dim,
                          }}
                        />
                        <View
                          testID="crop-rect"
                          style={{
                            position: 'absolute',
                            left: `${c.x * 100}%`,
                            top: `${c.y * 100}%`,
                            width: `${c.w * 100}%`,
                            height: `${c.h * 100}%`,
                            borderWidth: 2,
                            borderColor: '#fff',
                          }}
                        />
                      </>
                    );
                  })()}
                </View>
              ) : null}
              {tool === 'skintone' ? (
                <SkinTonePanel
                  doc={doc}
                  update={update}
                  commit={commit}
                  premiumAllowed={can('skinTonesPremium').allowed}
                  onPremium={() => askPremium('skinTonesPremium')}
                  noFace={skinToneNoFace}
                />
              ) : null}
              {tool === 'spotlight' ? (
                <View
                  pointerEvents="none"
                  style={{
                    position: 'absolute',
                    left: photoArea.x,
                    top: photoArea.y,
                    width: photoArea.w,
                    height: photoArea.h,
                  }}
                >
                  <SpotlightMarkers points={spotPts} frame={{ width: photoArea.w, height: photoArea.h }} />
                </View>
              ) : null}
              {tool === 'focus' && doc.focus ? (
                <View
                  pointerEvents="none"
                  style={{
                    position: 'absolute',
                    left: photoArea.x + doc.focus.x * photoArea.w - 18,
                    top: photoArea.y + doc.focus.y * photoArea.h - 18,
                    width: 36,
                    height: 36,
                    borderRadius: 18,
                    borderWidth: 2,
                    borderColor: theme.colors.premium,
                  }}
                />
              ) : null}
              {liveProp && liveDef ? (
                <Image
                  pointerEvents="none"
                  source={{ uri: propSource(liveDef) }}
                  resizeMode="stretch"
                  style={{
                    position: 'absolute',
                    width: liveProp.width * photoArea.w,
                    height: liveProp.width * photoArea.w * liveDef.aspect,
                    left: photoArea.x + liveProp.cx * photoArea.w - (liveProp.width * photoArea.w) / 2,
                    top: photoArea.y + liveProp.cy * photoArea.h - (liveProp.width * photoArea.w * liveDef.aspect) / 2,
                    transform: [{ rotate: `${liveProp.rotation}deg` }, { scaleX: liveProp.flip ? -1 : 1 }],
                  }}
                />
              ) : null}
            </>
          ) : null}
          {rendering ? (
            <View pointerEvents="none" style={{ position: 'absolute', right: 10, top: 10 }}>
              <Icon name="refresh" size={16} color="#fff" />
            </View>
          ) : null}
        </View>

        <View style={{ flexDirection: 'row', gap: 10 }}>
          <Pressable
            onPressIn={() => setComparing(true)}
            onPressOut={() => setComparing(false)}
            accessibilityRole="button"
            accessibilityLabel="Hold to see the original"
            testID="compare"
            style={{
              flexDirection: 'row',
              alignItems: 'center',
              gap: 6,
              paddingHorizontal: 14,
              borderRadius: 22,
              borderWidth: 1,
              borderColor: theme.colors.glassStroke,
            }}
          >
            <Icon name="compare" size={16} color={theme.colors.textPrimary} />
            <AppText variant="caption">Original</AppText>
          </Pressable>
          <Button
            label="Save"
            icon="check"
            onPress={() => void save()}
            loading={busy === 'save'}
            disabled={busy !== null}
            style={{ flex: 1 }}
            testID="editor-save"
          />
        </View>

        <ScrollView
          horizontal
          showsHorizontalScrollIndicator={false}
          contentContainerStyle={{ gap: 8 }}
          accessibilityRole="tablist"
        >
          {EDITOR_TOOLS.map(t => {
            const active = tool === t.id;
            return (
              <Pressable
                key={t.id}
                onPress={() => {
                  setTool(t.id);
                  setSelectedProp(null);
                }}
                accessibilityRole="tab"
                accessibilityLabel={`${t.label}${t.premium ? ', premium' : ''}`}
                accessibilityState={{ selected: active }}
                testID={`editor-tool-${t.id}`}
                style={{
                  alignItems: 'center',
                  gap: 4,
                  paddingHorizontal: 12,
                  paddingVertical: 8,
                  borderRadius: 14,
                  backgroundColor: active ? 'rgba(255,122,69,0.16)' : 'transparent',
                }}
              >
                <Icon name={t.icon} size={20} color={active ? theme.colors.accent : theme.colors.textPrimary} />
                <View style={{ flexDirection: 'row', alignItems: 'center', gap: 3 }}>
                  <AppText variant="caption">{t.label}</AppText>
                  {t.premium ? <Icon name="crown" size={11} color={theme.colors.premium} strokeWidth={2.2} /> : null}
                </View>
              </Pressable>
            );
          })}
        </ScrollView>

        <GlassSurface style={{ padding: 16 }}>
          {tool === 'adjust' ? <AdjustPanel doc={doc} update={update} commit={commit} /> : null}
          {tool === 'crop' ? (
            <CropPanel
              doc={doc}
              update={update}
              commit={commit}
              aspect={cropAspect}
              onAspect={id => {
                setCropAspect(id);
                applyCrop(id, cropSize);
                commit();
              }}
              onAutoPerspective={autoPerspective}
              autoPerspectiveBusy={findingLines}
              perspectiveLocked={!can('advancedEditing').allowed}
              onPremium={() => askPremium('advancedEditing')}
              cropSize={cropSize}
              onCropSize={v => {
                setCropSize(v);
                applyCrop(cropAspect, v);
              }}
            />
          ) : null}
          {tool === 'looks' ? (
            <LooksPanel
              doc={doc}
              update={update}
              commit={commit}
              locked={!can('advancedRetouchGrade').allowed}
              onPremium={() => askPremium('advancedRetouchGrade')}
            />
          ) : null}
          {tool === 'beauty' ? (
            <BeautyEditPanel
              doc={doc}
              update={update}
              commit={commit}
              premiumAllowed={can('beautyPremium').allowed}
              onPremium={() => askPremium('beautyPremium')}
            />
          ) : null}
          {tool === 'backdrop' ? (
            <BackdropEditPanel
              doc={doc}
              update={update}
              commit={commit}
              premiumAllowed={can('backdropsPremium').allowed}
              onPremium={() => askPremium('backdropsPremium')}
            />
          ) : null}
          {tool === 'spotlight' ? (
            <SpotlightEditPanel
              doc={doc}
              update={update}
              advanced={
                <SpotlightAdvanced
                  doc={doc}
                  update={update}
                  commit={commit}
                  locked={!can('advancedPortrait').allowed}
                  onPremium={() => askPremium('advancedPortrait')}
                />
              }
            />
          ) : null}
          {tool === 'retouch' ? (
            <PortraitPanel
              doc={doc}
              update={update}
              commit={commit}
              locked={!can('advancedPortrait').allowed}
              onPremium={() => askPremium('advancedPortrait')}
            />
          ) : null}
          {tool === 'focus' ? <FocusPanel doc={doc} update={update} commit={commit} /> : null}
          {tool === 'transform' ? (
            <TransformEditPanel
              doc={doc}
              update={update}
              allowance={transformAccess.allowed ? allowanceLabel(transformAccess, 'Transform') : 'none'}
              onPremium={() =>
                askPremium('transform', 'You’ve used today’s free Transforms. Premium makes Transform unlimited.')
              }
            />
          ) : null}
          {tool === 'props' ? (
            <PropsPanel
              props={propList}
              onCreate={() => navigation.navigate('CreateProp')}
              category={propCategory}
              onCategory={setPropCategory}
              selected={
                selected && selectedDef
                  ? { def: selectedDef, width: selected.width, rotation: selected.rotation }
                  : null
              }
              onAdd={def => void addProp(def)}
              locked={!can('premiumProps').allowed}
              onPremium={() => askPremium('premiumProps')}
              placed={doc.props.length}
              onSize={v => setSelected({ width: v }, 'prop-size')}
              onRotate={v => setSelected({ rotation: v }, 'prop-turn')}
              onFlip={() => setSelected({ flip: !selected?.flip })}
              onFit={() => {
                if (selectedDef && selected) void placementFor(selectedDef, selected).then(p => setSelected(p));
              }}
              onRemove={() => {
                if (selectedProp === null) return;
                update({ props: doc.props.filter((_, i) => i !== selectedProp) });
                setSelectedProp(null);
              }}
              commit={commit}
            />
          ) : null}
          {tool === 'frames' ? (
            <FramesPanel
              thumbnails={frameThumbs}
              doc={doc}
              update={update}
              commit={commit}
              locked={!can('premiumFrames').allowed}
              onPremium={() => askPremium('premiumFrames')}
            />
          ) : null}
          {tool === 'watermark' ? (
            <WatermarkPanel
              spec={spec}
              onChange={setSpec}
              premium={premiumPlan}
              onPremium={() => askPremium('customWatermark')}
              presets={presets}
              onSavePreset={savePreset}
              onDeletePreset={id =>
                updateSetting(
                  'brandingPresets',
                  presets.filter(p => p.id !== id),
                ).catch(() => undefined)
              }
              onPickLogo={() => void pickLogo()}
            />
          ) : null}
        </GlassSurface>

        <View style={{ flexDirection: 'row', gap: 10 }}>
          <Button
            label="Share"
            icon="share"
            variant="glass"
            onPress={() => void exportTo('share')}
            loading={busy === 'share'}
            disabled={busy !== null}
            style={{ flex: 1 }}
            testID="editor-share"
          />
          <Button
            label="Save to gallery"
            icon="image"
            variant="glass"
            onPress={() => void exportTo('gallery')}
            loading={busy === 'gallery'}
            disabled={busy !== null}
            style={{ flex: 1 }}
            testID="editor-gallery"
          />
        </View>
        {!premiumPlan && base ? <WatermarkUnlock photoPath={base} /> : null}
        <AppText variant="caption" tone="secondary" align="center">
          {premiumPlan
            ? spec.enabled
              ? 'Your branding goes on photos you share.'
              : 'Photos you share have no watermark.'
            : 'Photos you share carry the Photo Plus mark. Your original stays untouched.'}
        </AppText>
      </View>
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
