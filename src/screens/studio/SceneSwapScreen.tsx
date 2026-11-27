import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Alert, Image, View } from 'react-native';

import { AppText, Button, GlassSurface, IconButton, Screen, SegmentedControl, ToggleRow } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import { createId } from '../../services/database/ids';
import { getStorageService } from '../../services/storage/StorageService';
import { addPhotos, saveWork } from '../../services/projects/studios';
import {
  MAX_MY_BACKGROUNDS,
  myPlate,
  SHAPE_LABELS,
  SIZE_MAX,
  SIZE_MIN,
  type FrameShape,
  type PlateDef,
} from '../../services/studio/scene';
import { getStudioService, StudioError, type Brush, type SceneResult } from '../../services/studio/StudioService';
import { useAppStore } from '../../state/store';
import { defaultExportSize, exportPx, type ExportSizeId } from '../../services/studio/exportSize';
import { useTheme } from '../../theme';
import { isOutfit } from '../../services/studio/colors';
import type { HairBrush } from '../../services/studio/hair';
import { BackgroundPicker } from './BackgroundPicker';
import { HAIR_BRUSH_COLOURS } from './HairEditor';
import { DownloadSize } from './DownloadSize';
import { LipstickPanel, PinkLipsPanel } from './LipsPanel';
import { HairPicker } from './HairPicker';
import { FinishPanel, LightPanel, TemplateRow } from './StudioLightPanel';
import { BrushCanvas, type Backing } from './BrushCanvas';
import { PlacementView } from './PlacementView';
import { useSceneSwap, type Step } from './useSceneSwap';
import { recordActivity } from '../../services/rewards/RewardsService';
import { t as tx } from '../../i18n';

const STEPS: { step: Step; label: string }[] = [
  { step: 'pick', label: tx('scene_swap_photo') },
  { step: 'cutout', label: tx('scene_swap_cut_out') },
  { step: 'scene', label: tx('scene_swap_scene') },
];

const BRUSHES: { value: Brush; label: string }[] = [
  { value: 'keep', label: tx('scene_swap_keep') },
  { value: 'remove', label: tx('scene_swap_remove') },
  { value: 'hair', label: tx('scene_swap_fix_hair') },
];

const BACKINGS: { value: Backing; label: string }[] = [
  { value: 'dim', label: tx('scene_swap_photo') },
  { value: 'checks', label: tx('scene_swap_checks') },
  { value: 'white', label: tx('scene_swap_white') },
  { value: 'black', label: tx('scene_swap_black') },
];

type Panel = 'backgrounds' | 'light' | 'finish' | 'hair' | 'lips' | 'lipstick' | 'adjust' | 'frame';
const PANELS: { value: Panel; label: string }[] = [
  { value: 'backgrounds', label: tx('scene_swap_backgrounds') },
  { value: 'light', label: 'Light' },
  { value: 'finish', label: tx('scene_swap_finish') },
  { value: 'hair', label: tx('scene_swap_hair') },
  { value: 'lips', label: tx('scene_swap_pink_lips') },
  { value: 'lipstick', label: tx('scene_swap_lipstick') },
  { value: 'adjust', label: tx('scene_swap_adjust') },
  { value: 'frame', label: tx('scene_swap_frame') },
];

const pct = (v: number) => `${Math.round(v * 100)}`;
const signed = (v: number) => (Math.abs(v) < 0.005 ? '0' : `${v > 0 ? '+' : '−'}${Math.round(Math.abs(v) * 100)}`);

/**
 * Scene Swap (Premium): you, in another place. Choose a photo; the person is
 * cut out (correct it with Keep, Remove and Fix hair); choose a scene; move
 * and size yourself in it; the light, colour and shadows are matched; save
 * the photo, or the cut-out on its own with a transparent background.
 */
export function SceneSwapScreen({ navigation, route }: RootScreenProps<'SceneSwap'>) {
  const theme = useTheme();
  const studio = useMemo(() => getStudioService(), []);
  const s = useSceneSwap(studio);
  const can = useFeatureCheck();
  const allowed = can('backgroundReplace').allowed;
  const repos = useAppStore(st => st.repos);
  const settings = useAppStore(st => st.settings);
  const updateSetting = useAppStore(st => st.updateSetting);
  const loadProjects = useAppStore(st => st.loadProjects);
  const [brush, setBrush] = useState<Brush>('keep');
  const [radius, setRadius] = useState(0.03);
  const [backing, setBacking] = useState<Backing>('dim');
  const [tapMode, setTapMode] = useState(false);
  const [panel, setPanel] = useState<Panel>('backgrounds');
  const [premiumOpen, setPremiumOpen] = useState<
    null | 'backgroundReplace' | 'studioHair' | 'studioLight' | 'exportHd'
  >(null);
  const sizesAllowed = can('exportHd').allowed;
  const [size, setSize] = useState<ExportSizeId>(defaultExportSize(sizesAllowed));
  const hairAllowed = can('studioHair').allowed;
  const lightAllowed = can('studioLight').allowed;
  // Hair brushes (painted on the photo while editing the hair).
  const [hairBrush, setHairBrush] = useState<HairBrush>('erase');
  const [hairRadius, setHairRadius] = useState(0.03);

  const back = () => {
    if (s.step === 'scene') s.setStep('cutout');
    else if (s.step === 'cutout') s.setStep('pick');
    else navigation.goBack();
  };

  const projectId = route.params?.projectId;
  // The photo the work is made from (kept with it, to edit it again).
  const sourceRef = useRef<string | null>(null);
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (!path) return;
    sourceRef.current = path;
    if (repos && projectId) void addPhotos(repos, projectId, [path]).catch(() => undefined);
    await s.start(path);
  };

  /** Take the photo with the app's camera; it comes straight back here. */
  const shoot = () => navigation.navigate('Main', { screen: 'Camera', params: { returnTo: 'SceneSwap' } });

  // A photo just taken in the camera opens straight away (once).
  const shot = route.params?.photo;
  // Every creation starts from a project: without one (e.g. a photo just taken), choose or create it first.
  useEffect(() => {
    if (!projectId) navigation.replace('StudioProjects', { studio: 'photoshoot', ...(shot ? { photo: shot } : {}) });
    // Once, on arrival.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const startRef = useRef(s.start);
  useEffect(() => {
    startRef.current = s.start;
  });
  useEffect(() => {
    if (!shot || !projectId) return;
    navigation.setParams({ photo: undefined, edit: undefined });
    sourceRef.current = shot;
    void startRef.current(shot);
  }, [shot, navigation, projectId]);

  const addBackground = async () => {
    const path = await getStorageService().pickImage('backgrounds');
    if (!path) return;
    Image.getSize(
      `file://${path}`,
      (w, h) => {
        const mine = settings.myBackgrounds;
        const plate = myPlate(createId('bg'), `My background ${mine.length + 1}`, path, w, h);
        void updateSetting('myBackgrounds', [plate, ...mine].slice(0, MAX_MY_BACKGROUNDS)).catch(() => undefined);
        s.chooseBackground({ kind: 'plate', plate });
      },
      () => s.setError('That photo couldn’t be opened.'),
    );
  };

  const removeMine = (p: PlateDef) =>
    Alert.alert(tx('scene_swap_remove_this_background'), p.name, [
      { text: tx('scene_swap_cancel'), style: 'cancel' },
      {
        text: tx('scene_swap_remove'),
        style: 'destructive',
        onPress: () =>
          void updateSetting(
            'myBackgrounds',
            settings.myBackgrounds.filter(m => m.id !== p.id),
          ).catch(() => undefined),
      },
    ]);

  /** Files a saved studio photo in the library and the gallery, then opens it. */
  const keep = async (r: SceneResult) => {
    try {
      await studio.saveToGallery(r.outputPath);
    } catch {
      // The photo is still in the library.
    }
    if (!repos) return;
    const inbox = await repos.projects.ensureInbox();
    recordActivity('studio', 'edit');
    const record = await repos.images.add({
      projectId: inbox.id,
      path: r.outputPath,
      source: 'export',
      width: r.width,
      height: r.height,
      sizeBytes: r.sizeBytes,
      mimeType: r.format === 'png' ? 'image/png' : 'image/jpeg',
      capturedAt: Date.now(),
      metadata: { studio: { background: s.background.kind === 'plate' ? s.background.plate.id : s.background.kind } },
    });
    void loadProjects();
    // In a project: kept in its gallery, and its preview opens.
    if (projectId) {
      try {
        const item = await saveWork(repos, projectId, {
          path: r.outputPath,
          sourcePath: sourceRef.current,
          title: tx('sp_scene_title'),
          width: r.width,
          height: r.height,
          settings: { background: s.background.kind === 'plate' ? s.background.plate.id : s.background.kind },
        });
        navigation.navigate('StudioItem', { itemId: item.id, fresh: true });
        return;
      } catch {
        // Fall back to the library's preview.
      }
    }
    navigation.navigate('PhotoPreview', { imageId: record.id });
  };

  const save = async (cutoutOnly: boolean) => {
    if (!allowed || (!hairAllowed && s.hairChosen) || (!lightAllowed && s.studioLightPremium)) {
      setPremiumOpen(!allowed ? 'backgroundReplace' : !hairAllowed && s.hairChosen ? 'studioHair' : 'studioLight');
      return;
    }
    try {
      const r = cutoutOnly ? await s.saveCutout(exportPx(size)) : await s.save(exportPx(size));
      if (r) await keep(r);
    } catch (e) {
      if (e instanceof StudioError && e.code === 'PREMIUM_REQUIRED') setPremiumOpen('backgroundReplace');
    }
  };

  const stepIndex = STEPS.findIndex(x => x.step === s.step);
  return (
    <Screen title="Scene Swap" onBack={back} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <View
          style={{ flexDirection: 'row', gap: 8 }}
          accessibilityRole="progressbar"
          accessibilityLabel={tx('scene_swap_step_of_3', { value: stepIndex + 1 })}
        >
          {STEPS.map((x, i) => (
            <View
              key={x.step}
              style={{
                flex: 1,
                paddingVertical: 6,
                borderRadius: 999,
                alignItems: 'center',
                backgroundColor: i <= stepIndex ? theme.colors.accent : 'rgba(255,255,255,0.08)',
              }}
            >
              <AppText
                variant="caption"
                style={{ color: i <= stepIndex ? '#FFFFFF' : theme.colors.textSecondary, fontWeight: '700' }}
              >
                {x.label}
              </AppText>
            </View>
          ))}
        </View>

        {s.error ? (
          <GlassSurface radius={14} style={{ padding: 12 }} accessibilityLiveRegion="polite">
            <AppText variant="callout" style={{ color: theme.colors.warning }}>
              {s.error}
            </AppText>
          </GlassSurface>
        ) : null}

        {s.step === 'pick' ? (
          <GlassSurface radius={22} style={{ padding: 20, gap: 12 }}>
            <AppText variant="title2">{tx('scene_swap_put_yourself_in_a_new')}</AppText>
            <AppText variant="body" tone="secondary">
              {tx('scene_swap_choose_a_photo_of_a')}
            </AppText>
            <AppText variant="caption" tone="secondary">
              {tx('scene_swap_works_best_with_one_person')}
            </AppText>
            <Button
              label={tx('scene_swap_choose_a_photo')}
              icon="image"
              size="lg"
              loading={s.busy === 'start'}
              onPress={() => void pick()}
              testID="scene-pick"
            />
            <Button
              label={tx('scene_swap_take_a_photo')}
              icon="camera"
              size="lg"
              variant="glass"
              disabled={s.busy === 'start'}
              onPress={shoot}
              testID="scene-shoot"
            />
          </GlassSurface>
        ) : null}

        {s.step === 'cutout' && s.session && s.cut ? (
          <>
            <BrushCanvas
              photoUri={`file://${s.session.photoPath}`}
              cutoutUri={`file://${s.cut.cutoutPath}`}
              photoWidth={s.session.width}
              photoHeight={s.session.height}
              brush={brush}
              radius={radius}
              backing={backing}
              tapMode={tapMode}
              onStroke={s.addStroke}
              onTap={p => {
                setTapMode(false);
                s.chooseWho(p);
              }}
            />
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <View style={{ flex: 1 }}>
                <SegmentedControl label={tx('scene_swap_brush')} options={BRUSHES} value={brush} onChange={setBrush} />
              </View>
              <IconButton
                icon="undo"
                label={tx('scene_swap_undo')}
                onPress={s.undo}
                disabled={s.strokes.length === 0}
                size={40}
              />
              <IconButton icon="redo" label={tx('scene_swap_redo')} onPress={s.redo} disabled={!s.canRedo} size={40} />
            </View>
            <AppText variant="caption" tone="secondary">
              {brush === 'keep'
                ? tx('scene_swap_paint_over_anything_of_the')
                : brush === 'remove'
                  ? tx('scene_swap_paint_over_anything_that_isn')
                  : tx('scene_swap_paint_over_fine_hair_fur')}
              {s.busy === 'cut' ? tx('scene_swap_updating') : ''}
            </AppText>
            <ValueSlider
              label={tx('scene_swap_brush_size')}
              value={radius}
              min={0.01}
              max={0.08}
              step={0.005}
              onChange={setRadius}
              format={v => `${Math.round(v * 1000) / 10}`}
            />
            <SegmentedControl
              label={tx('scene_swap_show_on')}
              options={BACKINGS}
              value={backing}
              onChange={setBacking}
            />
            <GlassSurface radius={18} style={{ padding: 14, gap: 10 }}>
              <AppText variant="bodyStrong">{tx('scene_swap_edges')}</AppText>
              <ValueSlider
                label={tx('scene_swap_hair_detail')}
                value={s.edge.hairDetail}
                onChange={v => s.changeEdge({ hairDetail: v })}
                format={pct}
              />
              <ValueSlider
                label={tx('scene_swap_softer_edge')}
                value={s.edge.softness}
                onChange={v => s.changeEdge({ softness: v })}
                format={pct}
              />
              <ValueSlider
                label={tx('scene_swap_shrink_or_grow')}
                value={s.edge.shift}
                min={-1}
                max={1}
                onChange={v => s.changeEdge({ shift: v })}
                format={v => signed(-v)}
              />
            </GlassSurface>
            <View style={{ flexDirection: 'row', gap: 10, flexWrap: 'wrap' }}>
              <Button
                label={tapMode ? tx('scene_swap_tap_the_person') : tx('scene_swap_wrong_person')}
                variant="glass"
                icon="figure"
                onPress={() => setTapMode(t => !t)}
              />
              <Button label={tx('scene_swap_start_over')} variant="ghost" icon="refresh" onPress={s.resetCutout} />
            </View>
            <Button
              label={tx('scene_swap_choose_a_scene')}
              icon="layers"
              size="lg"
              disabled={!s.report?.found}
              onPress={s.goToScene}
              testID="scene-next"
            />
          </>
        ) : null}

        {s.step === 'scene' && s.session && s.cut && s.placement && s.report ? (
          <>
            {s.hairBrushing && s.hairCanvas ? (
              <BrushCanvas<HairBrush>
                photoUri={`file://${s.session.photoPath}`}
                cutoutUri={`file://${s.hairCanvas}`}
                photoWidth={s.session.width}
                photoHeight={s.session.height}
                brush={hairBrush}
                radius={hairRadius}
                backing="dim"
                tapMode={false}
                colours={HAIR_BRUSH_COLOURS}
                label={tx('scene_swap_the_person_with_their_hair')}
                onStroke={st => s.setHair({ ...s.hair, strokes: [...(s.hair.strokes ?? []), st] })}
                onTap={() => undefined}
              />
            ) : (
              <PlacementView
                sceneUri={s.scene ? `file://${s.scene.outputPath}` : null}
                emptyUri={s.empty ? `file://${s.empty.outputPath}` : null}
                cutoutUri={`file://${s.cut.cutoutPath}`}
                frameAspect={s.background.kind === 'transparent' ? s.photoAspect : s.frameAspect}
                photoAspect={s.photoAspect}
                box={s.report.box}
                placement={s.placement}
                horizon={s.horizon}
                follow={s.follow}
                rendering={s.busy === 'render'}
                onChange={s.setPlacement}
              />
            )}
            <SegmentedControl
              label={tx('scene_swap_scene_options')}
              options={PANELS}
              value={panel}
              onChange={setPanel}
            />
            {panel === 'backgrounds' ? (
              <BackgroundPicker
                value={s.background}
                mine={settings.myBackgrounds}
                onChoose={s.chooseBackground}
                onAdd={() => void addBackground()}
                onRemoveMine={removeMine}
                outfit={isOutfit(s.report.outfit) ? s.report.outfit : []}
              />
            ) : null}
            {panel === 'light' ? (
              <LightPanel
                value={s.light}
                onChoose={s.chooseLight}
                onChange={s.setLight}
                premium={lightAllowed}
                onLocked={() => setPremiumOpen('studioLight')}
              />
            ) : null}
            {panel === 'finish' ? (
              <FinishPanel
                value={s.finish}
                onChange={s.setFinish}
                premium={lightAllowed}
                onLocked={() => setPremiumOpen('studioLight')}
              />
            ) : null}
            {panel === 'hair' ? (
              <HairPicker
                value={s.hair}
                onChange={s.setHair}
                locked={!hairAllowed}
                onLocked={() => setPremiumOpen('studioHair')}
                brushes={{
                  brushing: s.hairBrushing,
                  onBrushing: s.setHairBrushing,
                  brush: hairBrush,
                  onBrush: setHairBrush,
                  radius: hairRadius,
                  onRadius: setHairRadius,
                }}
              />
            ) : null}
            {panel === 'lips' ? <PinkLipsPanel value={s.lips} onChange={s.setLips} /> : null}
            {panel === 'lipstick' ? <LipstickPanel value={s.lips} onChange={s.setLips} /> : null}
            {panel === 'adjust' ? (
              <GlassSurface radius={18} style={{ padding: 14, gap: 10 }}>
                <ValueSlider
                  label={tx('scene_swap_size')}
                  value={s.size}
                  min={SIZE_MIN}
                  max={SIZE_MAX}
                  step={0.02}
                  onChange={s.setSize}
                  format={v => `${Math.round(v * 100)}%`}
                  testID="scene-size"
                />
                <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
                  <AppText
                    variant="caption"
                    tone={s.trueToScene ? 'accent' : 'secondary'}
                    style={{ flex: 1 }}
                    testID="scene-size-note"
                  >
                    {s.sizeIsTrue
                      ? s.trueToScene
                        ? tx('scene_swap_true_to_the_scene_for')
                        : s.size > 1
                          ? tx('scene_swap_larger_than_the_scene_gives')
                          : tx('scene_swap_smaller_than_the_scene_gives')
                      : s.trueToScene
                        ? tx('scene_swap_the_size_that_suits_this')
                        : tx('scene_swap_your_own_size')}
                  </AppText>
                  <Button
                    label={tx('scene_swap_auto_size')}
                    variant="glass"
                    icon="wand"
                    disabled={s.trueToScene}
                    onPress={s.autoSize}
                    testID="scene-auto-size"
                  />
                </View>
                <ValueSlider
                  label="Spotlight"
                  value={s.look.spotlight}
                  onChange={v => s.setLook({ spotlight: v })}
                  format={pct}
                  testID="scene-spotlight"
                />
                <ValueSlider
                  label={tx('scene_swap_match_the_light')}
                  value={s.look.colorMatch}
                  onChange={v => s.setLook({ colorMatch: v })}
                  format={pct}
                />
                <ValueSlider
                  label={tx('scene_swap_blend_the_edges')}
                  value={s.look.lightWrap}
                  onChange={v => s.setLook({ lightWrap: v })}
                  format={pct}
                />
                <ValueSlider
                  label={tx('scene_swap_shadow')}
                  value={s.look.shadow}
                  onChange={v => s.setLook({ shadow: v })}
                  format={pct}
                />
                {s.background.kind === 'plate' ? (
                  <ValueSlider
                    label={tx('scene_swap_background_blur')}
                    value={s.look.bgBlur}
                    onChange={v => s.setLook({ bgBlur: v })}
                    format={pct}
                  />
                ) : null}
                <ValueSlider
                  label={tx('scene_swap_tilt')}
                  value={s.placement.tilt}
                  min={-1}
                  max={1}
                  onChange={v => s.setPlacement({ ...s.placement!, tilt: v })}
                  format={signed}
                />
                <ToggleRow
                  title={tx('scene_swap_mirror')}
                  icon="flip"
                  value={s.placement.flip}
                  onChange={v => s.setPlacement({ ...s.placement!, flip: v })}
                />
                <ToggleRow
                  title={tx('scene_swap_follow_the_scene_s_depth')}
                  subtitle={tx('scene_swap_drag_back_and_you_shrink')}
                  icon="aspect"
                  value={s.follow}
                  onChange={s.setFollow}
                  last
                />
                <Button
                  label={tx('scene_swap_put_me_back')}
                  variant="ghost"
                  icon="refresh"
                  onPress={s.resetPlacement}
                />
              </GlassSurface>
            ) : null}
            {panel === 'frame' ? (
              <>
                <SegmentedControl
                  label={tx('scene_swap_frame')}
                  options={(Object.keys(SHAPE_LABELS) as FrameShape[]).map(v => ({ value: v, label: SHAPE_LABELS[v] }))}
                  value={s.shape}
                  onChange={s.chooseShape}
                />
                <TemplateRow
                  onApply={s.applyTemplate}
                  premium={lightAllowed}
                  onLocked={() => setPremiumOpen('studioLight')}
                />
              </>
            ) : null}
            {s.background.kind === 'plate' && s.background.plate.credit ? (
              <AppText variant="caption" tone="tertiary">
                {tx('scene_swap_scene_2')} {s.background.plate.credit.source} (
                {s.background.plate.credit.authors.join(', ')}
                {tx('scene_swap_free_to_use')}
              </AppText>
            ) : null}
            <DownloadSize
              value={size}
              onChange={setSize}
              premium={sizesAllowed}
              onLocked={() => setPremiumOpen('exportHd')}
            />
            <View style={{ flexDirection: 'row', gap: 10 }}>
              <View style={{ flex: 1 }}>
                <Button
                  label={tx('scene_swap_save_photo')}
                  icon={allowed ? 'check' : 'crown'}
                  size="lg"
                  loading={s.busy === 'save'}
                  onPress={() => void save(false)}
                  testID="scene-save"
                />
              </View>
              <Button
                label={tx('scene_swap_cut_out_only')}
                variant="glass"
                icon="layers"
                onPress={() => void save(true)}
                testID="scene-save-cutout"
              />
            </View>
          </>
        ) : null}
      </View>
      <PremiumSheet feature={premiumOpen} onClose={() => setPremiumOpen(null)} />
    </Screen>
  );
}
