import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Alert, Image, View } from 'react-native';

import { AppText, Button, GlassSurface, IconButton, Screen, SegmentedControl, ToggleRow } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import { createId } from '../../services/database/ids';
import { getStorageService } from '../../services/storage/StorageService';
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

const STEPS: { step: Step; label: string }[] = [
  { step: 'pick', label: 'Photo' },
  { step: 'cutout', label: 'Cut-out' },
  { step: 'scene', label: 'Scene' },
];

const BRUSHES: { value: Brush; label: string }[] = [
  { value: 'keep', label: 'Keep' },
  { value: 'remove', label: 'Remove' },
  { value: 'hair', label: 'Fix hair' },
];

const BACKINGS: { value: Backing; label: string }[] = [
  { value: 'dim', label: 'Photo' },
  { value: 'checks', label: 'Checks' },
  { value: 'white', label: 'White' },
  { value: 'black', label: 'Black' },
];

type Panel = 'backgrounds' | 'light' | 'finish' | 'hair' | 'lips' | 'lipstick' | 'adjust' | 'frame';
const PANELS: { value: Panel; label: string }[] = [
  { value: 'backgrounds', label: 'Backgrounds' },
  { value: 'light', label: 'Light' },
  { value: 'finish', label: 'Finish' },
  { value: 'hair', label: 'Hair' },
  { value: 'lips', label: 'Pink lips' },
  { value: 'lipstick', label: 'Lipstick' },
  { value: 'adjust', label: 'Adjust' },
  { value: 'frame', label: 'Frame' },
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

  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (path) await s.start(path);
  };

  /** Take the photo with the app's camera; it comes straight back here. */
  const shoot = () => navigation.navigate('Main', { screen: 'Camera', params: { returnTo: 'SceneSwap' } });

  // A photo just taken in the camera opens straight away (once).
  const shot = route.params?.photo;
  const startRef = useRef(s.start);
  useEffect(() => {
    startRef.current = s.start;
  });
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined });
    void startRef.current(shot);
  }, [shot, navigation]);

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
    Alert.alert('Remove this background?', p.name, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Remove',
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
          accessibilityLabel={`Step ${stepIndex + 1} of 3`}
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
            <AppText variant="title2">Put yourself in a new place</AppText>
            <AppText variant="body" tone="secondary">
              Choose a photo of a person, or take one now. They are cut out on this phone — hair and all — and you
              choose where they go: a studio, a park at golden hour, a café, a plain colour, or a photo of your own.
            </AppText>
            <AppText variant="caption" tone="secondary">
              Works best with one person, head and shoulders or head to toe, in a sharp photo.
            </AppText>
            <Button
              label="Choose a photo"
              icon="image"
              size="lg"
              loading={s.busy === 'start'}
              onPress={() => void pick()}
              testID="scene-pick"
            />
            <Button
              label="Take a photo"
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
                <SegmentedControl label="Brush" options={BRUSHES} value={brush} onChange={setBrush} />
              </View>
              <IconButton icon="undo" label="Undo" onPress={s.undo} disabled={s.strokes.length === 0} size={40} />
              <IconButton icon="redo" label="Redo" onPress={s.redo} disabled={!s.canRedo} size={40} />
            </View>
            <AppText variant="caption" tone="secondary">
              {brush === 'keep'
                ? 'Paint over anything of the person that was missed.'
                : brush === 'remove'
                  ? 'Paint over anything that isn’t the person.'
                  : 'Paint over fine hair, fur or lace: the colours decide what stays.'}
              {s.busy === 'cut' ? '  Updating…' : ''}
            </AppText>
            <ValueSlider
              label="Brush size"
              value={radius}
              min={0.01}
              max={0.08}
              step={0.005}
              onChange={setRadius}
              format={v => `${Math.round(v * 1000) / 10}`}
            />
            <SegmentedControl label="Show on" options={BACKINGS} value={backing} onChange={setBacking} />
            <GlassSurface radius={18} style={{ padding: 14, gap: 10 }}>
              <AppText variant="bodyStrong">Edges</AppText>
              <ValueSlider
                label="Hair detail"
                value={s.edge.hairDetail}
                onChange={v => s.changeEdge({ hairDetail: v })}
                format={pct}
              />
              <ValueSlider
                label="Softer edge"
                value={s.edge.softness}
                onChange={v => s.changeEdge({ softness: v })}
                format={pct}
              />
              <ValueSlider
                label="Shrink or grow"
                value={s.edge.shift}
                min={-1}
                max={1}
                onChange={v => s.changeEdge({ shift: v })}
                format={v => signed(-v)}
              />
            </GlassSurface>
            <View style={{ flexDirection: 'row', gap: 10, flexWrap: 'wrap' }}>
              <Button
                label={tapMode ? 'Tap the person…' : 'Wrong person?'}
                variant="glass"
                icon="figure"
                onPress={() => setTapMode(t => !t)}
              />
              <Button label="Start over" variant="ghost" icon="refresh" onPress={s.resetCutout} />
            </View>
            <Button
              label="Choose a scene"
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
                label="The person with their hair. Paint with a finger to erase, restore or colour"
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
            <SegmentedControl label="Scene options" options={PANELS} value={panel} onChange={setPanel} />
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
                  label="Size"
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
                        ? 'True to the scene for where you stand.'
                        : s.size > 1
                          ? 'Larger than the scene gives you here.'
                          : 'Smaller than the scene gives you here.'
                      : s.trueToScene
                        ? 'The size that suits this frame.'
                        : 'Your own size.'}
                  </AppText>
                  <Button
                    label="Auto size"
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
                  label="Match the light"
                  value={s.look.colorMatch}
                  onChange={v => s.setLook({ colorMatch: v })}
                  format={pct}
                />
                <ValueSlider
                  label="Blend the edges"
                  value={s.look.lightWrap}
                  onChange={v => s.setLook({ lightWrap: v })}
                  format={pct}
                />
                <ValueSlider
                  label="Shadow"
                  value={s.look.shadow}
                  onChange={v => s.setLook({ shadow: v })}
                  format={pct}
                />
                {s.background.kind === 'plate' ? (
                  <ValueSlider
                    label="Background blur"
                    value={s.look.bgBlur}
                    onChange={v => s.setLook({ bgBlur: v })}
                    format={pct}
                  />
                ) : null}
                <ValueSlider
                  label="Tilt"
                  value={s.placement.tilt}
                  min={-1}
                  max={1}
                  onChange={v => s.setPlacement({ ...s.placement!, tilt: v })}
                  format={signed}
                />
                <ToggleRow
                  title="Mirror"
                  icon="flip"
                  value={s.placement.flip}
                  onChange={v => s.setPlacement({ ...s.placement!, flip: v })}
                />
                <ToggleRow
                  title="Follow the scene’s depth"
                  subtitle="Drag back and you shrink, drag nearer and you grow, as in the scene"
                  icon="aspect"
                  value={s.follow}
                  onChange={s.setFollow}
                  last
                />
                <Button label="Put me back" variant="ghost" icon="refresh" onPress={s.resetPlacement} />
              </GlassSurface>
            ) : null}
            {panel === 'frame' ? (
              <>
                <SegmentedControl
                  label="Frame"
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
                Scene: {s.background.plate.credit.source} ({s.background.plate.credit.authors.join(', ')}), free to use.
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
                  label="Save photo"
                  icon={allowed ? 'check' : 'crown'}
                  size="lg"
                  loading={s.busy === 'save'}
                  onPress={() => void save(false)}
                  testID="scene-save"
                />
              </View>
              <Button
                label="Cut-out only"
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
