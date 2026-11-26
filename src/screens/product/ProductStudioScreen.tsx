import React, { useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, ScrollView, StyleSheet, TextInput, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import type { Preset } from '../../services/database/repositories/PresetsRepository';
import type { FeatureKey } from '../../services/entitlements/features';
import {
  DEFAULT_MATTE,
  getProductService,
  ProductError,
  type MatteSettings,
  type ProductCutout,
  type ProductSession,
  type ProductShot,
} from '../../services/product/ProductService';
import {
  BACKDROP_COLOURS,
  BACKDROP_KINDS,
  CATALOG_PRESETS,
  DEFAULT_SETUP,
  EXPORT_PROFILES,
  TEMPLATES,
  exportProfile,
  isProductSetup,
  pack,
  setupPremium,
  type BackdropKind,
  type ProductSetup,
} from '../../services/product/setup';
import { PLATES, thumbAsset } from '../../services/studio/scene';
import {
  defaultExportSize,
  exportPx,
  exportSize,
  EXPORT_SIZES,
  type ExportSizeId,
} from '../../services/studio/exportSize';
import { getStudioService, type Stroke } from '../../services/studio/StudioService';
import { getStorageService } from '../../services/storage/StorageService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { BrushCanvas } from '../studio/BrushCanvas';
import { DownloadSize } from '../studio/DownloadSize';

import { Choice, pct, signed, Tabs, Toggle } from './controls';
import { recordActivity } from '../../services/rewards/RewardsService';
import { t as tx } from '../../i18n';

type Panel = 'setups' | 'product' | 'backdrop' | 'shadow' | 'colour' | 'layout' | 'export';
const PANELS: readonly { value: Panel; label: string }[] = [
  { value: 'setups', label: tx('product_studio_setups') },
  { value: 'product', label: tx('product_studio_cut_out') },
  { value: 'backdrop', label: tx('product_studio_backdrop') },
  { value: 'shadow', label: tx('product_studio_shadow') },
  { value: 'colour', label: tx('product_studio_colour') },
  { value: 'layout', label: tx('product_studio_layout') },
  { value: 'export', label: tx('product_studio_export') },
];
type View_ = 'after' | 'before';
type Brush = 'keep' | 'remove';

/**
 * Product Studio (Phase 12): a product photo made a catalogue shot. The
 * product is cut out (its own shadow and the old paper left behind), set on
 * a clean backdrop with a shadow that grounds it, its colour corrected, and
 * placed by a template; then saved at an export profile's shape and size.
 * Every control is the person's to change; setups can be saved and reused
 * in every project. The original photo is never changed.
 */
export function ProductStudioScreen({ navigation, route }: RootScreenProps<'ProductStudio'>) {
  const theme = useTheme();
  const product = useMemo(() => getProductService(), []);
  const can = useFeatureCheck();
  const pro = can('productPro').allowed;
  const premiumSizes = can('exportHd').allowed;
  const canSave = can('savedSetups').allowed;
  const canBrand = can('customWatermark').allowed;
  const repos = useAppStore(st => st.repos);
  const loadProjects = useAppStore(st => st.loadProjects);
  const brandings = useAppStore(st => st.settings.brandingPresets);
  // Kept for the visit: a photo coming back from the camera brings new params.
  const [projectId] = useState(route.params?.projectId ?? null);

  const [source, setSource] = useState<{ path: string; imageId: string | null } | null>(null);
  const [session, setSession] = useState<ProductSession | null>(null);
  const [cutout, setCutout] = useState<ProductCutout | null>(null);
  const [setup, setSetup] = useState<ProductSetup>(DEFAULT_SETUP);
  const [setupId, setSetupId] = useState<string | null>('soft-paper');
  const [profileId, setProfileId] = useState('post');
  const [size, setSize] = useState<ExportSizeId>(defaultExportSize(premiumSizes));
  const [brandingId, setBrandingId] = useState<string | null>(null);
  const [matte, setMatte] = useState<MatteSettings>(DEFAULT_MATTE);
  const [strokes, setStrokes] = useState<Stroke[]>([]);
  const [brush, setBrush] = useState<Brush>('remove');
  const [panel, setPanel] = useState<Panel>('setups');
  const [view, setView] = useState<View_>('after');
  const [preview, setPreview] = useState<ProductShot | null>(null);
  const [busy, setBusy] = useState<null | 'open' | 'preview' | 'save'>(null);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState<FeatureKey | null>(null);
  const [saved, setSaved] = useState<Preset<ProductSetup>[]>([]);
  const [naming, setNaming] = useState(false);
  const [name, setName] = useState('');
  const [packItems, setPackItems] = useState<string[] | null>(null);

  const profile = exportProfile(profileId);
  const lock = (feature: FeatureKey) => setPremiumOpen(feature);

  // Saved setups, and the project's defaults (its pack, setup, profile, size and brand mark).
  useEffect(() => {
    if (!repos) return;
    let live = true;
    (async () => {
      const list = await repos.presets.list('product', isProductSetup);
      if (!live) return;
      setSaved(list);
      if (!projectId) return;
      const p = await repos.projects.get(projectId);
      if (!live || !p) return;
      const d = p.defaults;
      const pk = pack(d.packId);
      if (pk?.kind === 'product') setPackItems(pk.items);
      const preset = CATALOG_PRESETS.find(c => c.id === d.setupId);
      const own = list.find(s => s.id === d.setupId);
      if (preset) {
        setSetup(preset.setup);
        setSetupId(preset.id);
        setProfileId(preset.profile);
      } else if (own) {
        setSetup(own.data);
        setSetupId(own.id);
      }
      if (d.profileId && EXPORT_PROFILES.some(x => x.id === d.profileId)) setProfileId(d.profileId);
      if (d.sizeId && EXPORT_SIZES.some(x => x.id === d.sizeId)) setSize(d.sizeId as ExportSizeId);
      if (d.brandingId) setBrandingId(d.brandingId);
    })().catch(() => undefined);
    return () => {
      live = false;
    };
  }, [repos, projectId]);

  // ---- The photo.
  const open = async (path: string, imageId: string | null) => {
    if (session) product.close(session.sessionId);
    setSource({ path, imageId });
    setSession(null);
    setCutout(null);
    setPreview(null);
    setStrokes([]);
    setMatte(DEFAULT_MATTE);
    setBusy('open');
    setError(null);
    try {
      const s = await product.open(path);
      setSession(s);
      setCutout(s);
      if (!s.report.found) setError(tx('product_studio_no_product_was_found_paint'));
    } catch {
      setError(tx('product_studio_this_photo_couldn_t_be'));
    } finally {
      setBusy(null);
    }
  };
  const openRef = useRef(open);
  useEffect(() => {
    openRef.current = open;
  });
  const shot = route.params?.photo;
  const fromLibrary = route.params?.imageId;
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined });
    void openRef.current(shot, null);
  }, [shot, navigation]);
  useEffect(() => {
    if (!fromLibrary || !repos) return;
    navigation.setParams({ imageId: undefined });
    void repos.images.get(fromLibrary).then(r => (r ? openRef.current(r.path, r.id) : undefined));
  }, [fromLibrary, repos, navigation]);
  // The session ends with the screen.
  const sessionRef = useRef<string | null>(null);
  useEffect(() => {
    sessionRef.current = session?.sessionId ?? null;
  }, [session]);
  useEffect(
    () => () => {
      if (sessionRef.current) product.close(sessionRef.current);
    },
    [product],
  );

  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (path) void open(path, null);
  };
  const takePhoto = () => navigation.navigate('Main', { screen: 'Camera', params: { returnTo: 'ProductStudio' } });

  // ---- Corrections to the cut-out (the latest wins).
  const refineSerial = useRef(0);
  const refine = async (nextStrokes: Stroke[], nextMatte: MatteSettings) => {
    if (!session) return;
    const id = ++refineSerial.current;
    try {
      const r = await product.refine(session.sessionId, nextStrokes, nextMatte);
      if (id === refineSerial.current) setCutout(r);
    } catch {
      if (id === refineSerial.current) setError(tx('product_studio_the_cut_out_couldn_t'));
    }
  };
  const addStroke = (s: Stroke) => {
    const next = [...strokes, s];
    setStrokes(next);
    void refine(next, matte);
  };
  const undoStroke = () => {
    const next = strokes.slice(0, -1);
    setStrokes(next);
    void refine(next, matte);
  };
  const setMatteAndRefine = (patch: Partial<MatteSettings>) => {
    const next = { ...matte, ...patch };
    setMatte(next);
    void refine(strokes, next);
  };

  // ---- The preview follows every change (the latest wins).
  const drawSerial = useRef(0);
  const draw = async (sid: string, s: ProductSetup, pid: string) => {
    const id = ++drawSerial.current;
    setBusy(b => b ?? 'preview');
    try {
      const r = await product.render(sid, s, exportProfile(pid), 480, 'preview');
      if (id !== drawSerial.current) return;
      setPreview(r);
      setError(null);
    } catch {
      if (id === drawSerial.current) setError(tx('product_studio_the_preview_couldn_t_be'));
    } finally {
      if (id === drawSerial.current) setBusy(b => (b === 'preview' ? null : b));
    }
  };
  const drawRef = useRef(draw);
  useEffect(() => {
    drawRef.current = draw;
  });
  const found = !!cutout?.report.found;
  useEffect(() => {
    if (session && found) void drawRef.current(session.sessionId, setup, profileId);
  }, [session, found, cutout, setup, profileId]);

  // ---- Editing the setup.
  const edit = <K extends keyof ProductSetup>(part: K, patch: Partial<ProductSetup[K]>) => {
    setSetup(s => ({ ...s, [part]: { ...s[part], ...patch } }));
    setSetupId(null);
  };
  const applyPreset = (id: string) => {
    const p = CATALOG_PRESETS.find(c => c.id === id);
    if (!p) return;
    setSetup(p.setup);
    setSetupId(p.id);
    setProfileId(p.profile);
  };
  const applySaved = (p: Preset<ProductSetup>) => {
    setSetup(p.data);
    setSetupId(p.id);
  };
  const backdropKind = (k: BackdropKind) => {
    const def = BACKDROP_KINDS.find(x => x.id === k);
    if (def?.premium && !pro) return lock('productPro');
    edit('backdrop', { kind: k, ...(k === 'scene' && !setup.backdrop.plate ? { plate: PLATES[0]?.id } : {}) });
    if (k === 'transparent') setProfileId('png');
  };

  const saveSetup = async () => {
    if (!repos || !name.trim()) return;
    if (!canSave) return lock('savedSetups');
    try {
      const p = await repos.presets.save('product', name, setup);
      setSaved(list => [p, ...list]);
      setSetupId(p.id);
      setNaming(false);
      setName('');
    } catch {
      setError(tx('product_studio_the_setup_couldn_t_be'));
    }
  };
  const forProject = async () => {
    if (!repos || !projectId) return;
    await repos.projects.setDefaults(projectId, {
      setupId: setupId ?? undefined,
      profileId,
      sizeId: size,
      brandingId: brandingId ?? null,
    });
    void loadProjects();
  };

  // ---- Saving the photo.
  const branding = brandings.find(b => b.id === brandingId) ?? null;
  const save = async () => {
    if (!session || !repos) return;
    if ((setupPremium(setup) || profile.premium) && !pro) return lock('productPro');
    if (exportSize(size).premium && !premiumSizes) return lock('exportHd');
    setBusy('save');
    setError(null);
    try {
      const r = await product.render(
        session.sessionId,
        setup,
        profile,
        exportPx(size),
        'full',
        canBrand ? branding?.spec : null,
      );
      try {
        await getStudioService().saveToGallery(r.outputPath);
      } catch {
        // Still in the library.
      }
      const target = projectId ?? (await repos.projects.ensureInbox()).id;
      recordActivity('product', 'studio', 'edit');
      const record = await repos.images.add({
        projectId: target,
        path: r.outputPath,
        source: 'export',
        width: r.width,
        height: r.height,
        sizeBytes: r.sizeBytes,
        mimeType: r.format === 'png' ? 'image/png' : 'image/jpeg',
        capturedAt: Date.now(),
        metadata: {
          captureMode: 'product',
          ...(source?.imageId ? { sourceImageId: source.imageId } : {}),
          originalPath: source?.path ?? null,
          originalOwned: false,
          cleanPath: r.cleanPath,
          product: { setup, profile: profile.id, resolution: exportPx(size) },
        },
      });
      void loadProjects();
      navigation.navigate('PhotoPreview', { imageId: record.id });
    } catch (e) {
      if (e instanceof ProductError && e.code === 'PREMIUM_REQUIRED')
        lock(exportSize(size).premium ? 'exportHd' : 'productPro');
      else setError(tx('product_studio_your_photo_couldn_t_be'));
    } finally {
      setBusy(null);
    }
  };

  // ---- The view.
  const shownUri =
    view === 'before' || !preview ? (session ? `file://${session.photoPath}` : null) : `file://${preview.outputPath}`;
  const aspect =
    view === 'after' && preview ? preview.width / preview.height : session ? session.width / session.height : 1;
  const presetList = packItems ? CATALOG_PRESETS.filter(c => packItems.includes(c.id)) : CATALOG_PRESETS;

  return (
    <Screen title="Product Studio" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        {!session ? (
          <GlassSurface radius={20} style={styles.start}>
            <Icon name="bag" size={28} color={theme.colors.accent} />
            <AppText variant="title2" align="center">
              {tx('product_studio_catalogue_shots_from_your_phone')}
            </AppText>
            <AppText variant="caption" tone="secondary" align="center">
              {tx('product_studio_shoot_your_product_on_paper')}
            </AppText>
            {busy === 'open' ? <ActivityIndicator color={theme.colors.accent} /> : null}
            <View style={styles.row}>
              <Button label={tx('product_studio_choose_a_photo')} icon="image" onPress={pick} testID="product-pick" />
              <Button
                label={tx('product_studio_take_a_photo')}
                icon="camera"
                variant="glass"
                onPress={takePhoto}
                testID="product-shoot"
              />
            </View>
          </GlassSurface>
        ) : (
          <>
            <View style={styles.segment} accessibilityRole="tablist">
              {(['before', 'after'] as const).map(v => (
                <Pressable
                  key={v}
                  onPress={() => setView(v)}
                  accessibilityRole="tab"
                  accessibilityState={{ selected: view === v }}
                  testID={`product-view-${v}`}
                  style={[styles.segmentItem, view === v ? { backgroundColor: 'rgba(255,255,255,0.12)' } : null]}
                >
                  <AppText variant="bodyStrong">
                    {v === 'before' ? tx('product_studio_before') : tx('product_studio_after')}
                  </AppText>
                </Pressable>
              ))}
            </View>
            {panel === 'product' && cutout ? (
              <View style={[styles.stage, { aspectRatio: session.width / session.height }]}>
                <BrushCanvas
                  photoUri={`file://${session.photoPath}`}
                  cutoutUri={`file://${cutout.cutoutPath}`}
                  photoWidth={session.width}
                  photoHeight={session.height}
                  brush={brush}
                  radius={0.03}
                  backing="checks"
                  tapMode={false}
                  onStroke={s => addStroke({ brush: s.brush, radius: s.radius, points: s.points })}
                  onTap={() => undefined}
                  label={tx('product_studio_the_cut_out_product_paint')}
                />
              </View>
            ) : (
              <GlassSurface radius={20} style={[styles.stage, { aspectRatio: Math.max(0.5, Math.min(2, aspect)) }]}>
                {shownUri ? (
                  <Image
                    source={{ uri: shownUri }}
                    style={StyleSheet.absoluteFill}
                    resizeMode="contain"
                    accessibilityLabel={
                      view === 'before'
                        ? tx('product_studio_your_original_photo')
                        : tx('product_studio_the_catalogue_shot')
                    }
                    testID="product-preview"
                  />
                ) : null}
                {busy === 'preview' ? <ActivityIndicator style={styles.spinner} color={theme.colors.accent} /> : null}
              </GlassSurface>
            )}
          </>
        )}

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}
        {cutout && !cutout.report.plain && cutout.report.found ? (
          <AppText variant="caption" tone="tertiary" testID="product-busy-backdrop">
            {tx('product_studio_a_busy_background_the_outline')}
          </AppText>
        ) : null}

        {session ? (
          <>
            <Tabs tabs={PANELS} value={panel} onChange={setPanel} testPrefix="product-tab" />
            <GlassSurface radius={18} style={styles.section}>
              {panel === 'setups' ? (
                <View style={{ gap: 10 }} testID="product-setups">
                  <AppText variant="caption" tone="secondary">
                    {tx('product_studio_ready_made_catalogue_looks')}
                    {packItems ? tx('product_studio_from_this_project_s_pack') : ''}
                    {tx('product_studio_pick_one_then_change_anything')}
                  </AppText>
                  <View style={styles.wrap}>
                    {presetList.map(p => (
                      <Choice
                        key={p.id}
                        label={p.name}
                        selected={setupId === p.id}
                        locked={p.premium && !pro}
                        onPress={() => (p.premium && !pro ? lock('productPro') : applyPreset(p.id))}
                        testID={`product-preset-${p.id}`}
                      />
                    ))}
                  </View>
                  {saved.length > 0 ? (
                    <>
                      <AppText variant="caption" tone="secondary">
                        {tx('product_studio_your_setups')}
                      </AppText>
                      <View style={styles.wrap}>
                        {saved.map(p => (
                          <Choice
                            key={p.id}
                            label={p.name}
                            selected={setupId === p.id}
                            onPress={() => applySaved(p)}
                            testID={`product-saved-${p.id}`}
                          />
                        ))}
                      </View>
                    </>
                  ) : null}
                  {naming ? (
                    <View style={styles.row}>
                      <TextInput
                        value={name}
                        onChangeText={setName}
                        placeholder={tx('product_studio_name_this_setup_e_g')}
                        placeholderTextColor={theme.colors.textTertiary}
                        maxLength={60}
                        autoFocus
                        onSubmitEditing={saveSetup}
                        accessibilityLabel={tx('product_studio_setup_name')}
                        testID="product-setup-name"
                        style={[
                          styles.input,
                          { color: theme.colors.textPrimary, borderColor: theme.colors.glassStroke },
                        ]}
                      />
                      <Button
                        label={tx('product_studio_save')}
                        icon="check"
                        onPress={saveSetup}
                        disabled={!name.trim()}
                        testID="product-setup-save"
                      />
                    </View>
                  ) : (
                    <View style={styles.row}>
                      <Choice
                        label={tx('product_studio_save_this_setup')}
                        icon="plus"
                        selected={false}
                        locked={!canSave}
                        onPress={() => (canSave ? setNaming(true) : lock('savedSetups'))}
                        testID="product-setup-new"
                      />
                      {projectId ? (
                        <Choice
                          label={tx('product_studio_use_for_this_project')}
                          icon="folder"
                          selected={false}
                          onPress={forProject}
                          testID="product-for-project"
                        />
                      ) : null}
                    </View>
                  )}
                </View>
              ) : null}

              {panel === 'product' ? (
                <View style={{ gap: 10 }} testID="product-cutout">
                  <AppText variant="caption" tone="secondary">
                    {tx('product_studio_paint_over_the_photo_to')}
                    {cutout
                      ? ` ${cutout.report.objects} ${cutout.report.objects === 1 ? 'object' : 'objects'} found.`
                      : ''}
                  </AppText>
                  <View style={styles.wrap}>
                    <Choice
                      label={tx('product_studio_keep')}
                      selected={brush === 'keep'}
                      onPress={() => setBrush('keep')}
                      testID="product-brush-keep"
                    />
                    <Choice
                      label={tx('product_studio_remove')}
                      selected={brush === 'remove'}
                      onPress={() => setBrush('remove')}
                      testID="product-brush-remove"
                    />
                    <Choice
                      label={tx('product_studio_undo')}
                      icon="undo"
                      selected={false}
                      onPress={undoStroke}
                      testID="product-brush-undo"
                    />
                  </View>
                  <ValueSlider
                    label={tx('product_studio_close_colours')}
                    value={matte.tolerance}
                    onChange={v => setMatte(m => ({ ...m, tolerance: v }))}
                    onEnd={() => setMatteAndRefine({})}
                    format={pct}
                    testID="product-tolerance"
                  />
                  <ValueSlider
                    label={tx('product_studio_edge_softness')}
                    value={matte.softness}
                    onChange={v => setMatte(m => ({ ...m, softness: v }))}
                    onEnd={() => setMatteAndRefine({})}
                    format={pct}
                    testID="product-softness"
                  />
                  <ValueSlider
                    label={tx('product_studio_edge_in_or_out')}
                    value={matte.shift}
                    min={-1}
                    max={1}
                    onChange={v => setMatte(m => ({ ...m, shift: v }))}
                    onEnd={() => setMatteAndRefine({})}
                    format={signed}
                    testID="product-shift"
                  />
                </View>
              ) : null}

              {panel === 'backdrop' ? (
                <View style={{ gap: 10 }} testID="product-backdrop">
                  <View style={styles.wrap}>
                    {BACKDROP_KINDS.map(k => (
                      <Choice
                        key={k.id}
                        label={k.name}
                        selected={setup.backdrop.kind === k.id}
                        locked={k.premium && !pro}
                        onPress={() => backdropKind(k.id)}
                        testID={`product-kind-${k.id}`}
                      />
                    ))}
                  </View>
                  {setup.backdrop.kind === 'scene' ? (
                    <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
                      {PLATES.slice(0, 12).map(p => (
                        <Pressable
                          key={p.id}
                          onPress={() => edit('backdrop', { plate: p.id })}
                          accessibilityRole="radio"
                          accessibilityLabel={p.name}
                          accessibilityState={{ selected: setup.backdrop.plate === p.id }}
                          style={[
                            styles.plate,
                            { borderColor: setup.backdrop.plate === p.id ? theme.colors.accent : 'transparent' },
                          ]}
                        >
                          <Image source={{ uri: `asset:/${thumbAsset(p.id)}` }} style={styles.plateImg} />
                        </Pressable>
                      ))}
                    </ScrollView>
                  ) : null}
                  {setup.backdrop.kind !== 'transparent' && setup.backdrop.kind !== 'scene' ? (
                    <>
                      <AppText variant="caption" tone="secondary">
                        {setup.backdrop.kind === 'solid'
                          ? tx('product_studio_colour')
                          : setup.backdrop.kind === 'radial'
                            ? tx('product_studio_centre')
                            : tx('product_studio_wall')}
                      </AppText>
                      <Swatches
                        value={setup.backdrop.wall}
                        onChange={hex => edit('backdrop', { wall: hex })}
                        testPrefix="product-wall"
                      />
                      {setup.backdrop.kind !== 'solid' ? (
                        <>
                          <AppText variant="caption" tone="secondary">
                            {setup.backdrop.kind === 'radial' ? tx('product_studio_edges') : tx('product_studio_floor')}
                          </AppText>
                          <Swatches
                            value={setup.backdrop.floor}
                            onChange={hex => edit('backdrop', { floor: hex })}
                            testPrefix="product-floor"
                          />
                        </>
                      ) : null}
                      {setup.backdrop.kind === 'sweep' || setup.backdrop.kind === 'surface' ? (
                        <ValueSlider
                          label={tx('product_studio_where_the_floor_starts')}
                          value={setup.backdrop.horizon}
                          min={0.3}
                          max={0.9}
                          onChange={v => edit('backdrop', { horizon: v })}
                          format={pct}
                          testID="product-horizon"
                        />
                      ) : null}
                      <ValueSlider
                        label={tx('product_studio_blend')}
                        value={setup.backdrop.softness}
                        onChange={v => edit('backdrop', { softness: v })}
                        format={pct}
                        testID="product-blend"
                      />
                      <ValueSlider
                        label={tx('product_studio_studio_light')}
                        value={setup.backdrop.light}
                        onChange={v => edit('backdrop', { light: v })}
                        format={pct}
                        testID="product-light"
                      />
                    </>
                  ) : null}
                </View>
              ) : null}

              {panel === 'shadow' ? (
                <View style={{ gap: 10 }} testID="product-shadow">
                  <ValueSlider
                    label={tx('product_studio_contact_shadow')}
                    value={setup.shadow.contact}
                    onChange={v => edit('shadow', { contact: v })}
                    format={pct}
                    testID="product-contact"
                  />
                  <ValueSlider
                    label={tx('product_studio_soft_shadow')}
                    value={setup.shadow.soft}
                    onChange={v => edit('shadow', { soft: v })}
                    format={pct}
                    testID="product-soft"
                  />
                  {pro ? (
                    <>
                      <ValueSlider
                        label={tx('product_studio_cast_shadow')}
                        value={setup.shadow.cast}
                        onChange={v => edit('shadow', { cast: v })}
                        format={pct}
                        testID="product-cast"
                      />
                      {setup.shadow.cast > 0 ? (
                        <>
                          <ValueSlider
                            label={tx('product_studio_light_from')}
                            value={setup.shadow.castAngle}
                            min={-70}
                            max={70}
                            step={1}
                            onChange={v => edit('shadow', { castAngle: v })}
                            format={v => `${Math.round(v)}°`}
                            testID="product-cast-angle"
                          />
                          <ValueSlider
                            label={tx('product_studio_shadow_length_behind_in_front')}
                            value={setup.shadow.castLength}
                            min={-1}
                            max={1}
                            onChange={v => edit('shadow', { castLength: v })}
                            format={signed}
                            testID="product-cast-length"
                          />
                          <ValueSlider
                            label={tx('product_studio_shadow_softness')}
                            value={setup.shadow.castSoftness}
                            onChange={v => edit('shadow', { castSoftness: v })}
                            format={pct}
                            testID="product-cast-soft"
                          />
                        </>
                      ) : null}
                      <ValueSlider
                        label={tx('product_studio_reflection')}
                        value={setup.shadow.reflection}
                        onChange={v => edit('shadow', { reflection: v })}
                        format={pct}
                        testID="product-reflection"
                      />
                    </>
                  ) : (
                    <Choice
                      label={tx('product_studio_cast_shadows_and_reflections')}
                      selected={false}
                      locked
                      onPress={() => lock('productPro')}
                      testID="product-shadow-locked"
                    />
                  )}
                </View>
              ) : null}

              {panel === 'colour' ? (
                <View style={{ gap: 8 }} testID="product-colour">
                  <Toggle
                    label={tx('product_studio_auto_white_balance')}
                    hint={
                      cutout?.report.plain
                        ? tx('product_studio_takes_out_the_colour_of', { backdrop: cutout.report.backdrop })
                        : tx('product_studio_needs_a_plain_background')
                    }
                    value={setup.correction.autoWhite}
                    onChange={v => edit('correction', { autoWhite: v })}
                    testID="product-auto-white"
                  />
                  <Toggle
                    label={tx('product_studio_auto_brightness')}
                    hint={tx('product_studio_brings_a_grey_looking_white')}
                    value={setup.correction.autoExposure}
                    onChange={v => edit('correction', { autoExposure: v })}
                    testID="product-auto-exposure"
                  />
                  <ValueSlider
                    label={tx('product_studio_exposure')}
                    value={setup.correction.exposure}
                    min={-1}
                    max={1}
                    onChange={v => edit('correction', { exposure: v })}
                    format={signed}
                    testID="product-exposure"
                  />
                  <ValueSlider
                    label={tx('product_studio_contrast')}
                    value={setup.correction.contrast}
                    min={-1}
                    max={1}
                    onChange={v => edit('correction', { contrast: v })}
                    format={signed}
                    testID="product-contrast"
                  />
                  <ValueSlider
                    label={tx('product_studio_colour')}
                    value={setup.correction.saturation}
                    min={-1}
                    max={1}
                    onChange={v => edit('correction', { saturation: v })}
                    format={signed}
                    testID="product-saturation"
                  />
                  <ValueSlider
                    label={tx('product_studio_warmth')}
                    value={setup.correction.warmth}
                    min={-1}
                    max={1}
                    onChange={v => edit('correction', { warmth: v })}
                    format={signed}
                    testID="product-warmth"
                  />
                  <ValueSlider
                    label={tx('product_studio_tint')}
                    value={setup.correction.tint}
                    min={-1}
                    max={1}
                    onChange={v => edit('correction', { tint: v })}
                    format={signed}
                    testID="product-tint"
                  />
                </View>
              ) : null}

              {panel === 'layout' ? (
                <View style={{ gap: 10 }} testID="product-layout">
                  <View style={styles.wrap}>
                    {TEMPLATES.map(t => (
                      <Choice
                        key={t.id}
                        label={t.name}
                        selected={
                          JSON.stringify(setup.layout) === JSON.stringify({ ...t.layout, flip: setup.layout.flip })
                        }
                        locked={t.premium && !pro}
                        onPress={() =>
                          t.premium && !pro
                            ? lock('productPro')
                            : edit('layout', { ...t.layout, flip: setup.layout.flip })
                        }
                        testID={`product-template-${t.id}`}
                      />
                    ))}
                  </View>
                  <ValueSlider
                    label={tx('product_studio_size')}
                    value={setup.layout.fill}
                    min={0.2}
                    max={1}
                    onChange={v => edit('layout', { fill: v })}
                    format={pct}
                    testID="product-fill"
                  />
                  <ValueSlider
                    label={tx('product_studio_left_or_right')}
                    value={setup.layout.x}
                    onChange={v => edit('layout', { x: v })}
                    format={pct}
                    testID="product-x"
                  />
                  <ValueSlider
                    label={tx('product_studio_up_or_down')}
                    value={setup.layout.y}
                    onChange={v => edit('layout', { y: v })}
                    format={pct}
                    testID="product-y"
                  />
                  <Toggle
                    label={tx('product_studio_standing_on_the_floor')}
                    hint={tx('product_studio_up_or_down_places_its')}
                    value={setup.layout.ground}
                    onChange={v => edit('layout', { ground: v })}
                    testID="product-ground"
                  />
                  <Toggle
                    label={tx('product_studio_mirror')}
                    value={setup.layout.flip}
                    onChange={v => edit('layout', { flip: v })}
                    testID="product-flip"
                  />
                </View>
              ) : null}

              {panel === 'export' ? (
                <View style={{ gap: 10 }} testID="product-export">
                  <AppText variant="caption" tone="secondary">
                    {tx('product_studio_shape_and_format')}
                  </AppText>
                  <View style={styles.wrap}>
                    {EXPORT_PROFILES.map(p => (
                      <Choice
                        key={p.id}
                        label={p.name}
                        selected={profileId === p.id}
                        locked={p.premium && !pro}
                        onPress={() => (p.premium && !pro ? lock('productPro') : setProfileId(p.id))}
                        testID={`product-profile-${p.id}`}
                      />
                    ))}
                  </View>
                  <AppText variant="caption" tone="tertiary">
                    {profile.about}
                  </AppText>
                  <DownloadSize
                    value={size}
                    onChange={setSize}
                    premium={premiumSizes}
                    onLocked={() => lock('exportHd')}
                  />
                  <AppText variant="caption" tone="secondary">
                    {tx('product_studio_your_brand_mark')}
                  </AppText>
                  <View style={styles.wrap}>
                    <Choice
                      label={tx('product_studio_none')}
                      selected={brandingId === null}
                      onPress={() => setBrandingId(null)}
                      testID="product-brand-none"
                    />
                    {brandings.map(b => (
                      <Choice
                        key={b.id}
                        label={b.name}
                        selected={brandingId === b.id}
                        locked={!canBrand}
                        onPress={() => (canBrand ? setBrandingId(b.id) : lock('customWatermark'))}
                        testID={`product-brand-${b.id}`}
                      />
                    ))}
                  </View>
                  {brandings.length === 0 ? (
                    <AppText variant="caption" tone="tertiary">
                      {tx('product_studio_save_a_branding_in_the')}
                    </AppText>
                  ) : null}
                </View>
              ) : null}
            </GlassSurface>

            <View style={styles.row}>
              <Button
                label={tx('product_studio_another_photo')}
                variant="glass"
                onPress={pick}
                testID="product-another"
              />
              <Button
                label={tx('product_studio_save')}
                icon="download"
                onPress={save}
                loading={busy === 'save'}
                disabled={!found || busy === 'open'}
                testID="product-save"
                style={{ flex: 1 }}
              />
            </View>
          </>
        ) : null}
      </View>
      <PremiumSheet feature={premiumOpen} onClose={() => setPremiumOpen(null)} />
    </Screen>
  );
}

function Swatches({
  value,
  onChange,
  testPrefix,
}: {
  value: string;
  onChange: (hex: string) => void;
  testPrefix: string;
}) {
  const theme = useTheme();
  return (
    <View style={styles.wrap} accessibilityRole="radiogroup">
      {BACKDROP_COLOURS.map(c => (
        <Pressable
          key={c.hex}
          onPress={() => onChange(c.hex)}
          accessibilityRole="radio"
          accessibilityLabel={c.name}
          accessibilityState={{ selected: value.toUpperCase() === c.hex }}
          testID={`${testPrefix}-${c.hex.slice(1)}`}
          style={[
            styles.colour,
            {
              backgroundColor: c.hex,
              borderColor: value.toUpperCase() === c.hex ? theme.colors.accent : 'rgba(255,255,255,0.3)',
            },
          ]}
        />
      ))}
    </View>
  );
}

const styles = StyleSheet.create({
  start: { padding: 20, gap: 12, alignItems: 'center' },
  row: { flexDirection: 'row', gap: 10, alignItems: 'center', flexWrap: 'wrap' },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  segment: { flexDirection: 'row', gap: 6, padding: 4, borderRadius: 16, backgroundColor: 'rgba(255,255,255,0.05)' },
  segmentItem: { flex: 1, alignItems: 'center', paddingVertical: 9, borderRadius: 12 },
  stage: { width: '100%', maxHeight: 420, borderRadius: 20, overflow: 'hidden', alignSelf: 'center' },
  spinner: { position: 'absolute', top: 12, right: 12 },
  section: { padding: 14, gap: 10 },
  input: { flex: 1, minHeight: 44, borderRadius: 12, borderWidth: 1, paddingHorizontal: 12, fontSize: 15 },
  colour: { width: 30, height: 30, borderRadius: 15, borderWidth: 2 },
  plate: { borderWidth: 2, borderRadius: 12, overflow: 'hidden' },
  plateImg: { width: 72, height: 90 },
});
