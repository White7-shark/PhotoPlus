import React, { useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import type { Preset } from '../../services/database/repositories/PresetsRepository';
import type { ImageRecord } from '../../services/database/repositories/ImagesRepository';
import type { Project } from '../../services/database/repositories/ProjectsRepository';
import type { FeatureKey } from '../../services/entitlements/features';
import { importPhoto } from '../../services/library/importPhoto';
import { getProcessingService } from '../../services/processing/ImageProcessingService';
import { FREE_BATCH, MAX_BATCH, runBatch, type BatchItem, type BatchJob } from '../../services/product/batch';
import { getProductService, ProductError } from '../../services/product/ProductService';
import {
  CATALOG_PRESETS,
  EXPORT_PROFILES,
  LOOK_PRESETS,
  exportProfile,
  isLookValues,
  isProductSetup,
  pack,
  setupPremium,
  type LookPreset,
  type ProductSetup,
} from '../../services/product/setup';
import { defaultExportSize, exportPx, exportSize, type ExportSizeId } from '../../services/studio/exportSize';
import { getStorageService } from '../../services/storage/StorageService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { DownloadSize } from '../studio/DownloadSize';

import { Choice, Tabs } from './controls';
import { t as tx } from '../../i18n';

type Mode = 'product' | 'look';
const SHAPES = [
  { id: 'as-shot', name: tx('batch_as_shot'), shape: null },
  { id: '1-1', name: '1:1', shape: { aspectW: 1, aspectH: 1 } },
  { id: '4-5', name: '4:5', shape: { aspectW: 4, aspectH: 5 } },
  { id: '9-16', name: '9:16', shape: { aspectW: 9, aspectH: 16 } },
  { id: '3-2', name: '3:2', shape: { aspectW: 3, aspectH: 2 } },
  { id: '16-9', name: '16:9', shape: { aspectW: 16, aspectH: 9 } },
] as const;

const isOriginal = (r: ImageRecord) => typeof r.metadata.sourceImageId !== 'string';

/**
 * Batch editing (Phase 12): one setup reapplied to many photos. Product
 * shots (a catalogue preset or a saved setup, at one export profile and
 * size, with the brand mark) or a photo look (cropped to one shape). Each
 * result is a new photo beside its original; originals stay as they are.
 */
export function BatchScreen({ navigation, route }: RootScreenProps<'Batch'>) {
  const theme = useTheme();
  const can = useFeatureCheck();
  const unlimited = can('batchEditing').allowed;
  const pro = can('productPro').allowed;
  const premiumSizes = can('exportHd').allowed;
  const canBrand = can('customWatermark').allowed;
  const repos = useAppStore(st => st.repos);
  const loadProjects = useAppStore(st => st.loadProjects);
  const settings = useAppStore(st => st.settings);
  const projectId = route.params?.projectId ?? null;

  const [project, setProject] = useState<Project | null>(null);
  const [library, setLibrary] = useState<ImageRecord[]>([]);
  const [chosen, setChosen] = useState<string[]>(route.params?.imageIds ?? []);
  const [mode, setMode] = useState<Mode>('product');
  const [setupId, setSetupId] = useState('marketplace-white');
  const [lookId, setLookId] = useState('bright-clean');
  const [shapeId, setShapeId] = useState<(typeof SHAPES)[number]['id']>('as-shot');
  const [profileId, setProfileId] = useState('square');
  const [size, setSize] = useState<ExportSizeId>(defaultExportSize(premiumSizes));
  const [brandingId, setBrandingId] = useState<string | null>(null);
  const [savedSetups, setSavedSetups] = useState<Preset<ProductSetup>[]>([]);
  const [savedLooks, setSavedLooks] = useState<Preset<LookPreset['look']>[]>([]);
  const [items, setItems] = useState<BatchItem[] | null>(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState<FeatureKey | null>(null);
  const signal = useRef({ cancelled: false });

  // The project's photos and its defaults; saved setups and looks.
  const givenIds = route.params?.imageIds;
  useEffect(() => {
    if (!repos) return;
    let live = true;
    (async () => {
      const [setups, looks] = await Promise.all([
        repos.presets.list('product', isProductSetup),
        repos.presets.list('look', isLookValues),
      ]);
      const fromIds = await Promise.all((givenIds ?? []).map(id => repos.images.get(id)));
      const p = projectId ? await repos.projects.get(projectId) : null;
      const inProject = p ? await repos.images.listByProject(p.id, 500) : [];
      if (!live) return;
      setSavedSetups(setups);
      setSavedLooks(looks);
      setProject(p);
      const byId = new Map([...inProject, ...fromIds.filter((r): r is ImageRecord => !!r)].map(r => [r.id, r]));
      setLibrary([...byId.values()]);
      if (p) {
        const d = p.defaults;
        const pk = pack(d.packId);
        if (d.setupId) setSetupId(d.setupId);
        else if (pk?.kind === 'product') setSetupId(pk.items[0]);
        if (d.lookId) setLookId(d.lookId);
        else if (pk?.kind === 'look') setLookId(pk.items[0]);
        if (pk) setMode(pk.kind);
        if (d.profileId) setProfileId(d.profileId);
        if (d.sizeId) setSize(d.sizeId as ExportSizeId);
        if (d.brandingId) setBrandingId(d.brandingId);
      }
    })().catch(() => {
      if (live) setError(tx('batch_your_photos_couldn_t_be'));
    });
    return () => {
      live = false;
    };
  }, [repos, projectId, givenIds]);

  const originals = library.filter(isOriginal);
  const toggle = (id: string) => setChosen(c => (c.includes(id) ? c.filter(x => x !== id) : [...c, id]));
  const add = async () => {
    if (!repos) return;
    const id = await importPhoto(repos, projectId);
    if (!id) return;
    const r = await repos.images.get(id);
    if (r) {
      setLibrary(l => [r, ...l]);
      setChosen(c => [...c, id]);
    }
  };

  const setupOf = (id: string): { name: string; setup: ProductSetup; premium: boolean } | null => {
    const preset = CATALOG_PRESETS.find(c => c.id === id);
    if (preset) return { name: preset.name, setup: preset.setup, premium: preset.premium };
    const own = savedSetups.find(s => s.id === id);
    return own ? { name: own.name, setup: own.data, premium: !!setupPremium(own.data) } : null;
  };
  const lookOf = (id: string): { name: string; look: LookPreset['look']; premium: boolean } | null => {
    const preset = LOOK_PRESETS.find(l => l.id === id);
    if (preset) return { name: preset.name, look: preset.look, premium: preset.premium };
    const own = savedLooks.find(l => l.id === id);
    return own ? { name: own.name, look: own.data, premium: false } : null;
  };

  const limit = unlimited ? MAX_BATCH : FREE_BATCH;
  const photos = chosen.map(id => library.find(r => r.id === id)).filter((r): r is ImageRecord => !!r);
  const branding = settings.brandingPresets.find(b => b.id === brandingId) ?? null;

  const run = async () => {
    if (!repos || photos.length === 0) return;
    if (photos.length > limit) return setPremiumOpen('batchEditing');
    let job: BatchJob;
    if (mode === 'product') {
      const s = setupOf(setupId);
      if (!s) return setError(tx('batch_choose_a_setup_first'));
      const profile = exportProfile(profileId);
      if ((s.premium || profile.premium) && !pro) return setPremiumOpen('productPro');
      if (exportSize(size).premium && !premiumSizes) return setPremiumOpen('exportHd');
      job = {
        kind: 'product',
        name: s.name,
        setup: s.setup,
        profile,
        resolution: exportPx(size),
        watermark: canBrand ? branding?.spec : null,
      };
    } else {
      const l = lookOf(lookId);
      if (!l) return setError(tx('batch_choose_a_look_first'));
      if (l.premium && !pro) return setPremiumOpen('productPro');
      job = {
        kind: 'look',
        name: l.name,
        look: l.look,
        shape: SHAPES.find(x => x.id === shapeId)?.shape ?? null,
        watermark: canBrand && branding ? branding.spec : settings.watermark,
      };
    }
    signal.current = { cancelled: false };
    setRunning(true);
    setError(null);
    setItems(photos.map(image => ({ image, status: 'waiting' })));
    try {
      const done = await runBatch(
        photos,
        job,
        {
          repos,
          product: getProductService(),
          renderEdit: r => getProcessingService().renderEdit(r),
          exists: p => getStorageService().exists(p),
        },
        {
          projectId,
          signal: signal.current,
          onItem: (i, item) => setItems(list => (list ? list.map((x, k) => (k === i ? item : x)) : list)),
        },
      );
      setItems(done);
      void loadProjects();
    } catch (e) {
      if (e instanceof ProductError && e.code === 'PREMIUM_REQUIRED') setPremiumOpen('productPro');
      else setError(tx('batch_the_batch_stopped_the_photos'));
    } finally {
      setRunning(false);
    }
  };

  const doneCount = items?.filter(i => i.status === 'done').length ?? 0;
  const failed = items?.filter(i => i.status === 'failed').length ?? 0;

  return (
    <Screen title={tx('batch_batch_edit')} onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <AppText variant="body" tone="secondary">
          {tx('batch_one_setup_or_look_on')}
        </AppText>

        <GlassSurface radius={18} style={styles.section}>
          <View style={styles.head}>
            <AppText variant="bodyStrong" style={{ flex: 1 }}>
              {tx('batch_photos')} {photos.length}
              {!unlimited ? ` of ${FREE_BATCH}` : ''}
            </AppText>
            <Choice label={tx('batch_add_photos')} icon="plus" selected={false} onPress={add} testID="batch-add" />
          </View>
          {originals.length === 0 && photos.length === 0 ? (
            <AppText variant="caption" tone="tertiary">
              {tx('batch_add_the_photos_to_edit')}
              {project ? tx('batch_or_take_them_into', { name: project.name }) : ''}.
            </AppText>
          ) : null}
          <View style={styles.grid} testID="batch-photos">
            {[...originals, ...photos.filter(p => !isOriginal(p))].map(r => {
              const on = chosen.includes(r.id);
              return (
                <Pressable
                  key={r.id}
                  onPress={() => toggle(r.id)}
                  accessibilityRole="checkbox"
                  accessibilityState={{ checked: on }}
                  accessibilityLabel={tx('batch_photo')}
                  testID={`batch-photo-${r.id}`}
                  style={[styles.thumb, { borderColor: on ? theme.colors.accent : 'transparent' }]}
                >
                  <Image source={{ uri: `file://${r.path}` }} style={StyleSheet.absoluteFill} />
                  {on ? (
                    <View style={[styles.tick, { backgroundColor: theme.colors.accent }]}>
                      <Icon name="check" size={12} color="#FFFFFF" />
                    </View>
                  ) : null}
                </Pressable>
              );
            })}
          </View>
          {!unlimited ? (
            <Pressable onPress={() => setPremiumOpen('batchEditing')} accessibilityRole="button" testID="batch-limit">
              <AppText variant="caption" tone="tertiary">
                {tx('batch_up_to')} {FREE_BATCH} {tx('batch_photos_at_a_time_on')} {MAX_BATCH}.
              </AppText>
            </Pressable>
          ) : null}
        </GlassSurface>

        <Tabs
          tabs={[
            { value: 'product', label: tx('batch_product_shots') },
            { value: 'look', label: tx('batch_photo_look') },
          ]}
          value={mode}
          onChange={setMode}
          testPrefix="batch-mode"
        />
        <GlassSurface radius={18} style={styles.section}>
          {mode === 'product' ? (
            <>
              <AppText variant="caption" tone="secondary">
                {tx('batch_setup')}
              </AppText>
              <View style={styles.wrap}>
                {CATALOG_PRESETS.map(p => (
                  <Choice
                    key={p.id}
                    label={p.name}
                    selected={setupId === p.id}
                    locked={p.premium && !pro}
                    onPress={() => setSetupId(p.id)}
                    testID={`batch-setup-${p.id}`}
                  />
                ))}
                {savedSetups.map(p => (
                  <Choice
                    key={p.id}
                    label={p.name}
                    icon="sliders"
                    selected={setupId === p.id}
                    onPress={() => setSetupId(p.id)}
                    testID={`batch-setup-${p.id}`}
                  />
                ))}
              </View>
              <AppText variant="caption" tone="secondary">
                {tx('batch_shape_and_format')}
              </AppText>
              <View style={styles.wrap}>
                {EXPORT_PROFILES.map(p => (
                  <Choice
                    key={p.id}
                    label={p.name}
                    selected={profileId === p.id}
                    locked={p.premium && !pro}
                    onPress={() => setProfileId(p.id)}
                    testID={`batch-profile-${p.id}`}
                  />
                ))}
              </View>
              <DownloadSize
                value={size}
                onChange={setSize}
                premium={premiumSizes}
                onLocked={() => setPremiumOpen('exportHd')}
              />
            </>
          ) : (
            <>
              <AppText variant="caption" tone="secondary">
                {tx('batch_look')}
              </AppText>
              <View style={styles.wrap}>
                {LOOK_PRESETS.map(l => (
                  <Choice
                    key={l.id}
                    label={l.name}
                    selected={lookId === l.id}
                    locked={l.premium && !pro}
                    onPress={() => setLookId(l.id)}
                    testID={`batch-look-${l.id}`}
                  />
                ))}
                {savedLooks.map(l => (
                  <Choice
                    key={l.id}
                    label={l.name}
                    icon="sliders"
                    selected={lookId === l.id}
                    onPress={() => setLookId(l.id)}
                    testID={`batch-look-${l.id}`}
                  />
                ))}
              </View>
              <AppText variant="caption" tone="secondary">
                {tx('batch_shape')}
              </AppText>
              <View style={styles.wrap}>
                {SHAPES.map(s => (
                  <Choice
                    key={s.id}
                    label={s.name}
                    selected={shapeId === s.id}
                    onPress={() => setShapeId(s.id)}
                    testID={`batch-shape-${s.id}`}
                  />
                ))}
              </View>
            </>
          )}
          <AppText variant="caption" tone="secondary">
            {tx('batch_brand_mark')}
          </AppText>
          <View style={styles.wrap}>
            <Choice
              label={mode === 'look' ? tx('batch_your_watermark') : tx('batch_none')}
              selected={brandingId === null}
              onPress={() => setBrandingId(null)}
              testID="batch-brand-none"
            />
            {settings.brandingPresets.map(b => (
              <Choice
                key={b.id}
                label={b.name}
                selected={brandingId === b.id}
                locked={!canBrand}
                onPress={() => (canBrand ? setBrandingId(b.id) : setPremiumOpen('customWatermark'))}
                testID={`batch-brand-${b.id}`}
              />
            ))}
          </View>
        </GlassSurface>

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}

        {items ? (
          <GlassSurface radius={18} style={styles.section} testID="batch-results">
            <AppText variant="bodyStrong" accessibilityLiveRegion="polite">
              {running
                ? tx('batch_editing_of', {
                    value: Math.min(doneCount + failed + 1, items.length),
                    length: items.length,
                  })
                : tx('batch_done_of', { doneCount, length: items.length })}
              {failed ? tx('batch_couldn_t_be_edited', { failed }) : ''}
            </AppText>
            {items.map((it, i) => (
              <Pressable
                key={it.image.id}
                onPress={() => (it.result ? navigation.navigate('PhotoPreview', { imageId: it.result.id }) : undefined)}
                disabled={!it.result}
                accessibilityRole="button"
                accessibilityLabel={tx('batch_photo_2', { value: i + 1, status: it.status })}
                testID={`batch-item-${i}`}
                style={styles.result}
              >
                <Image
                  source={{ uri: `file://${it.image.path}` }}
                  style={styles.small}
                  accessibilityLabel={tx('batch_before')}
                />
                <Icon name="chevronRight" size={14} color={theme.colors.textTertiary} />
                {it.result ? (
                  <Image
                    source={{ uri: `file://${it.result.path}` }}
                    style={styles.small}
                    accessibilityLabel={tx('batch_after')}
                  />
                ) : (
                  <View style={[styles.small, styles.waiting]}>
                    {it.status === 'working' ? <ActivityIndicator color={theme.colors.accent} /> : null}
                  </View>
                )}
                <AppText
                  variant="caption"
                  tone={it.status === 'failed' ? 'secondary' : 'tertiary'}
                  style={{ flex: 1 }}
                  numberOfLines={2}
                >
                  {it.status === 'done'
                    ? tx('batch_saved_tap_to_compare')
                    : it.status === 'failed'
                      ? (it.error ?? tx('batch_couldn_t_be_edited_2'))
                      : it.status === 'working'
                        ? tx('batch_working')
                        : tx('batch_waiting')}
                </AppText>
              </Pressable>
            ))}
          </GlassSurface>
        ) : null}

        <View style={styles.row}>
          {running ? (
            <Button
              label={tx('batch_stop')}
              variant="glass"
              onPress={() => (signal.current.cancelled = true)}
              testID="batch-stop"
            />
          ) : null}
          <Button
            label={
              running
                ? tx('batch_working')
                : tx('batch_edit', { length: photos.length, value: photos.length === 1 ? 'photo' : 'photos' })
            }
            icon="wand"
            onPress={run}
            loading={running}
            disabled={running || photos.length === 0}
            testID="batch-run"
            style={{ flex: 1 }}
          />
        </View>
      </View>
      <PremiumSheet feature={premiumOpen} onClose={() => setPremiumOpen(null)} />
    </Screen>
  );
}

const styles = StyleSheet.create({
  section: { padding: 14, gap: 10 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 6 },
  thumb: { width: 72, height: 72, borderRadius: 12, overflow: 'hidden', borderWidth: 2 },
  tick: {
    position: 'absolute',
    top: 4,
    right: 4,
    width: 20,
    height: 20,
    borderRadius: 10,
    alignItems: 'center',
    justifyContent: 'center',
  },
  row: { flexDirection: 'row', gap: 10, alignItems: 'center' },
  result: { flexDirection: 'row', alignItems: 'center', gap: 8, minHeight: 56 },
  small: { width: 52, height: 52, borderRadius: 10 },
  waiting: { backgroundColor: 'rgba(255,255,255,0.06)', alignItems: 'center', justifyContent: 'center' },
});
