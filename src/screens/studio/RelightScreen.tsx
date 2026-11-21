import React, { useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import { friendlyMessage } from '../../services/errors';
import { getStorageService } from '../../services/storage/StorageService';
import { defaultExportSize, exportPx, exportSize, type ExportSizeId } from '../../services/studio/exportSize';
import { lightPreset, NO_FINISH, type FinishChoice, type LightChoice } from '../../services/studio/light';
import type { LipsChoice } from '../../services/studio/lips';
import {
  getStudioService,
  PLAIN_BACKGROUND,
  StudioError,
  type RelightBackground,
  type SceneResult,
  type Session,
} from '../../services/studio/StudioService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';

import { DownloadSize } from './DownloadSize';
import { LipstickPanel, PinkLipsPanel } from './LipsPanel';
import { FinishPanel, LightPanel } from './StudioLightPanel';

type Panel = 'light' | 'background' | 'lips' | 'finish';
const PANELS: { value: Panel; label: string }[] = [
  { value: 'light', label: 'Light' },
  { value: 'background', label: 'Background' },
  { value: 'lips', label: 'Lips' },
  { value: 'finish', label: 'Finish' },
];
const signed = (v: number) => (Math.abs(v) < 0.005 ? '0' : `${v > 0 ? '+' : '−'}${Math.round(Math.abs(v) * 100)}`);
const pct = (v: number) => `${Math.round(v * 100)}`;

/**
 * Relight: studio light on the person in their own photo. The person is
 * found and lit (soft, Rembrandt, split, rim, neon…; turned, softened and
 * eased back by hand); the background can be darkened like a studio
 * backdrop, softened like a fast lens, and made more or less colourful; lips
 * and a finish complete it. Nothing is moved: the person stays where they
 * stood.
 */
export function RelightScreen({ navigation, route }: RootScreenProps<'Relight'>) {
  const theme = useTheme();
  const studio = useMemo(() => getStudioService(), []);
  const can = useFeatureCheck();
  const lightAllowed = can('studioLight').allowed;
  const premiumSizes = can('exportHd').allowed;
  const repos = useAppStore(st => st.repos);
  const loadProjects = useAppStore(st => st.loadProjects);
  const [session, setSession] = useState<Session | null>(null);
  const [light, setLight] = useState<LightChoice | null>({ preset: 'soft-studio', strength: 1 });
  const [background, setBackground] = useState<RelightBackground>(PLAIN_BACKGROUND);
  const [lips, setLips] = useState<LipsChoice | null>(null);
  const [finish, setFinish] = useState<FinishChoice>(NO_FINISH);
  const [panel, setPanel] = useState<Panel>('light');
  const [size, setSize] = useState<ExportSizeId>(defaultExportSize(premiumSizes));
  const [preview, setPreview] = useState<SceneResult | null>(null);
  const [busy, setBusy] = useState<null | 'start' | 'preview' | 'save'>(null);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState<null | 'relight' | 'studioLight' | 'exportHd'>(null);

  const start = async (path: string) => {
    setBusy('start');
    setError(null);
    try {
      if (session) studio.end(session.sessionId);
      setSession(await studio.start(path));
    } catch (e) {
      setError(
        e instanceof StudioError && e.code === 'NO_PERSON'
          ? 'Nobody was found in this photo. Choose one with a person in it.'
          : 'This photo couldn’t be opened.',
      );
    } finally {
      setBusy(null);
    }
  };
  const startRef = useRef(start);
  useEffect(() => {
    startRef.current = start;
  });
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (path) await start(path);
  };
  const shoot = () => navigation.navigate('Main', { screen: 'Camera', params: { returnTo: 'Relight' } });
  const shot = route.params?.photo;
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined });
    void startRef.current(shot);
  }, [shot, navigation]);
  useEffect(
    () => () => {
      if (session) studio.end(session.sessionId);
    },
    [session, studio],
  );

  // The preview follows every change (the latest wins).
  const request = useRef(0);
  const draw = async (
    id: string,
    l: LightChoice | null,
    b: RelightBackground,
    lp: LipsChoice | null,
    f: FinishChoice,
  ) => {
    const n = ++request.current;
    setBusy('preview');
    try {
      const r = await studio.renderRelight(id, { light: l, background: b, lips: lp, finish: f }, 'preview');
      if (n !== request.current) return;
      setPreview(r);
      setError(null);
    } catch (e) {
      if (n === request.current)
        setError(
          e instanceof StudioError && e.code === 'NO_FACE'
            ? friendlyMessage('NO_FACE')
            : 'This couldn’t be drawn. Please try again.',
        );
    } finally {
      if (n === request.current) setBusy(x => (x === 'preview' ? null : x));
    }
  };
  const drawRef = useRef(draw);
  useEffect(() => {
    drawRef.current = draw;
  });
  const sessionId = session?.sessionId;
  useEffect(() => {
    if (sessionId) void drawRef.current(sessionId, light, background, lips, finish);
  }, [sessionId, light, background, lips, finish]);
  const shown = session ? preview : null;

  const chooseLight = (id: string) => {
    const p = lightPreset(id);
    if (!p?.light) setLight(null);
    else setLight(l => ({ preset: id, strength: l?.strength ?? 1 }));
  };

  const save = async () => {
    if (!session) return;
    setBusy('save');
    setError(null);
    try {
      const r = await studio.renderRelight(
        session.sessionId,
        { light, background, lips, finish },
        'full',
        exportPx(size),
      );
      try {
        await studio.saveToGallery(r.outputPath);
      } catch {
        // The photo is still in the library.
      }
      if (repos) {
        const inbox = await repos.projects.ensureInbox();
        const record = await repos.images.add({
          projectId: inbox.id,
          path: r.outputPath,
          source: 'export',
          width: r.width,
          height: r.height,
          sizeBytes: r.sizeBytes,
          mimeType: 'image/jpeg',
          capturedAt: Date.now(),
          metadata: { studio: { tool: 'relight', light: light?.preset ?? 'as-shot' } },
        });
        void loadProjects();
        navigation.navigate('PhotoPreview', { imageId: record.id });
      }
    } catch (e) {
      if (e instanceof StudioError && e.code === 'PREMIUM_REQUIRED')
        setPremiumOpen(exportSize(size).premium && !premiumSizes ? 'exportHd' : 'relight');
      else setError('Your photo couldn’t be saved. Please try again.');
    } finally {
      setBusy(null);
    }
  };

  return (
    <Screen title="Relight" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <GlassSurface radius={20} style={styles.previewBox}>
          {shown ? (
            <Image
              source={{ uri: `file://${shown.outputPath}` }}
              style={{ width: '100%', aspectRatio: shown.width / shown.height, maxHeight: 440 }}
              resizeMode="contain"
              accessibilityLabel="Your photo, relit"
              testID="relight-preview"
            />
          ) : (
            <View style={{ gap: 12, alignItems: 'stretch', width: '100%' }}>
              <AppText variant="title2">Studio light on your own photo</AppText>
              <AppText variant="body" tone="secondary">
                Choose or take a photo of a person. They are lit like a studio shoot, right where they stand, and the
                background follows.
              </AppText>
              <Button
                label="Choose a photo"
                icon="image"
                size="lg"
                loading={busy === 'start'}
                onPress={() => void pick()}
                testID="relight-pick"
              />
              <Button
                label="Take a photo"
                icon="camera"
                size="lg"
                variant="glass"
                disabled={busy === 'start'}
                onPress={shoot}
                testID="relight-shoot"
              />
            </View>
          )}
          {busy === 'preview' ? <ActivityIndicator style={styles.spinner} color={theme.colors.accent} /> : null}
        </GlassSurface>

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}

        {session ? (
          <>
            <View style={styles.tabs} accessibilityRole="tablist">
              {PANELS.map(p => (
                <Pressable
                  key={p.value}
                  onPress={() => setPanel(p.value)}
                  accessibilityRole="tab"
                  accessibilityState={{ selected: panel === p.value }}
                  testID={`relight-tab-${p.value}`}
                  style={[
                    styles.tab,
                    { borderColor: panel === p.value ? theme.colors.accent : 'rgba(255,255,255,0.14)' },
                  ]}
                >
                  <AppText variant="caption" style={{ fontWeight: panel === p.value ? '700' : '500' }}>
                    {p.label}
                  </AppText>
                </Pressable>
              ))}
            </View>
            <GlassSurface radius={18} style={styles.section}>
              {panel === 'light' ? (
                <LightPanel
                  value={light}
                  onChoose={chooseLight}
                  onChange={setLight}
                  premium={lightAllowed}
                  onLocked={() => setPremiumOpen('studioLight')}
                />
              ) : null}
              {panel === 'background' ? (
                <View style={{ gap: 10 }} testID="relight-background">
                  <AppText variant="caption" tone="secondary">
                    The room behind the person: darker like a studio backdrop, softer like a fast lens.
                  </AppText>
                  <ValueSlider
                    label="Brightness"
                    value={background.brightness}
                    min={-1}
                    max={1}
                    step={0.05}
                    onChange={v => setBackground(b => ({ ...b, brightness: v }))}
                    format={signed}
                    testID="relight-bg-brightness"
                  />
                  <ValueSlider
                    label="Soften"
                    value={background.blur}
                    min={0}
                    max={1}
                    step={0.05}
                    onChange={v => setBackground(b => ({ ...b, blur: v }))}
                    format={pct}
                    testID="relight-bg-blur"
                  />
                  <ValueSlider
                    label="Colour"
                    value={background.colour}
                    min={-1}
                    max={1}
                    step={0.05}
                    onChange={v => setBackground(b => ({ ...b, colour: v }))}
                    format={signed}
                    testID="relight-bg-colour"
                  />
                  <Button
                    label="Reset background"
                    variant="ghost"
                    icon="refresh"
                    onPress={() => setBackground(PLAIN_BACKGROUND)}
                    testID="relight-bg-reset"
                  />
                </View>
              ) : null}
              {panel === 'lips' ? (
                <View style={{ gap: 16 }}>
                  <PinkLipsPanel value={lips} onChange={setLips} />
                  <LipstickPanel value={lips} onChange={setLips} />
                </View>
              ) : null}
              {panel === 'finish' ? (
                <FinishPanel
                  value={finish}
                  onChange={setFinish}
                  premium={lightAllowed}
                  onLocked={() => setPremiumOpen('studioLight')}
                />
              ) : null}
            </GlassSurface>
            <DownloadSize
              value={size}
              onChange={setSize}
              premium={premiumSizes}
              onLocked={() => setPremiumOpen('exportHd')}
            />
            <View style={{ flexDirection: 'row', gap: 10 }}>
              <Button
                label="Another photo"
                variant="glass"
                icon="image"
                onPress={() => void pick()}
                testID="relight-change"
              />
              <View style={{ flex: 1 }}>
                <Button
                  label="Save"
                  icon="download"
                  size="lg"
                  loading={busy === 'save'}
                  disabled={busy === 'save'}
                  onPress={() => void save()}
                  testID="relight-save"
                />
              </View>
            </View>
          </>
        ) : null}
      </View>
      <PremiumSheet feature={premiumOpen} onClose={() => setPremiumOpen(null)} />
    </Screen>
  );
}

const styles = StyleSheet.create({
  previewBox: { padding: 12, minHeight: 220, alignItems: 'center', justifyContent: 'center' },
  spinner: { position: 'absolute', top: 12, right: 12 },
  section: { padding: 14, gap: 10 },
  tabs: { flexDirection: 'row', gap: 8 },
  tab: { flex: 1, alignItems: 'center', paddingVertical: 9, borderRadius: 999, borderWidth: 1.5 },
});
