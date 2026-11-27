import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Modal, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen, ToggleRow, type IconName } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { VideoView } from '../../components/VideoView';
import { useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import { createId } from '../../services/database/ids';
import {
  CAMERAS,
  DEFAULT_SETTINGS,
  DURATIONS,
  EFFECTS,
  MAX_ARROWS,
  MAX_PINS,
  SKY_MIN,
  cameraAbout,
  cameraName,
  effectName,
  type MotionArrow,
  type MotionPin,
  type MotionSettings,
} from '../../services/motion/catalog';
import {
  MotionError,
  UNKNOWN_ALLOWANCE,
  getMotionService,
  type MotionAllowance,
  type MotionScene,
  type MotionVideo,
} from '../../services/motion/MotionService';
import { recordActivity } from '../../services/rewards/RewardsService';
import { getStorageService } from '../../services/storage/StorageService';
import { useTheme } from '../../theme';
import { MotionCanvas, type MotionTool } from './MotionCanvas';

type Tab = 'camera' | 'animate' | 'effects' | 'video';
const TABS: readonly { id: Tab; icon: IconName }[] = [
  { id: 'camera', icon: 'camera' },
  { id: 'animate', icon: 'flow' },
  { id: 'effects', icon: 'sparkles' },
  { id: 'video', icon: 'film' },
];
const TOOLS: readonly { id: MotionTool; icon: IconName }[] = [
  { id: 'arrow', icon: 'flow' },
  { id: 'anchor', icon: 'pin' },
  { id: 'erase', icon: 'eraser' },
];
/** A preview follows a change after this pause (a burst of taps makes one preview). */
const PREVIEW_DELAY_MS = 250;

/**
 * Photo Motion: one photo becomes a seamless video loop. A 3D camera move
 * through the photo's depth, motion the person draws (water, hair, clouds)
 * with pins for what stays still, a drifting sky, and effects. Previews are
 * free and follow every change; finished videos are Premium, or two a day on
 * the free plan, each after a short rewarded ad (and with a small mark).
 */
export function MotionScreen({ navigation, route }: RootScreenProps<'Motion'>) {
  const theme = useTheme();
  const t = useT();
  const motion = useMemo(() => getMotionService(), []);
  const [photo, setPhoto] = useState<string | null>(null);
  const [scene, setScene] = useState<MotionScene | null>(null);
  const [settings, setSettings] = useState<MotionSettings>(DEFAULT_SETTINGS);
  const [arrows, setArrows] = useState<MotionArrow[]>([]);
  const [pins, setPins] = useState<MotionPin[]>([]);
  const [tab, setTab] = useState<Tab>('camera');
  const [tool, setTool] = useState<MotionTool>('arrow');
  const [preview, setPreview] = useState<MotionVideo | null>(null);
  const [finished, setFinished] = useState<MotionVideo | null>(null);
  const [gallery, setGallery] = useState<{ video: MotionVideo; saved: boolean }[]>([]);
  const [allowance, setAllowance] = useState<MotionAllowance | null>(null);
  const [busy, setBusy] = useState<null | 'reading' | 'preview' | 'full' | 'ad' | 'save'>(null);
  const [error, setError] = useState<string | null>(null);
  const [adNotice, setAdNotice] = useState<string | null>(null);
  const [adOpen, setAdOpen] = useState(false);
  const [premiumOpen, setPremiumOpen] = useState(false);

  const accent = theme.colors.accent;
  const unlimited = allowance?.remaining === -1;
  const limit = allowance?.limit ?? UNKNOWN_ALLOWANCE.limit;
  const left = allowance && !unlimited ? Math.max(0, allowance.remaining) : null;
  const unlocked = !!allowance && !unlimited && allowance.credits > 0 && (left ?? 0) > 0;

  const refreshAllowance = useCallback(() => {
    void motion.allowance().then(setAllowance);
  }, [motion]);
  useEffect(refreshAllowance, [refreshAllowance]);

  // The latest choices, for previews made after a pause and for the finished video.
  const latest = useRef({ scene, settings, arrows, pins });
  useEffect(() => {
    latest.current = { scene, settings, arrows, pins };
  });

  // Previews: a newer one cancels the older; a short pause lets several changes make one.
  const job = useRef<string | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const runPreview = useCallback(() => {
    const { scene: sc, settings: s, arrows: a, pins: p } = latest.current;
    if (!sc) return;
    const id = createId('motion');
    if (job.current) motion.cancel(job.current);
    job.current = id;
    setBusy(b => (b === 'full' || b === 'ad' ? b : 'preview'));
    motion
      .render(sc.sessionId, s, a, p, 'preview', id)
      .then(v => {
        if (job.current !== id) return;
        setPreview(v);
        setError(null);
      })
      .catch((e: unknown) => {
        if (job.current === id && !(e instanceof MotionError && e.code === 'CANCELLED')) setError(t('motion_failed'));
      })
      .finally(() => {
        if (job.current === id) {
          job.current = null;
          setBusy(b => (b === 'preview' ? null : b));
        }
      });
  }, [motion, t]);
  const schedulePreview = useCallback(() => {
    if (timer.current) clearTimeout(timer.current);
    setFinished(null);
    timer.current = setTimeout(() => {
      timer.current = null;
      runPreview();
    }, PREVIEW_DELAY_MS);
  }, [runPreview]);

  // Changes: sliders update as they move and preview when the finger lifts; taps preview at once.
  const change = (next: Partial<MotionSettings>, now = true) => {
    const s = { ...latest.current.settings, ...next };
    latest.current = { ...latest.current, settings: s };
    setSettings(s);
    if (now) schedulePreview();
  };
  const addArrows = (added: MotionArrow[]) => {
    const a = [...latest.current.arrows, ...added].slice(0, MAX_ARROWS);
    latest.current = { ...latest.current, arrows: a };
    setArrows(a);
    schedulePreview();
  };
  const addPin = (pin: MotionPin) => {
    if (latest.current.pins.length >= MAX_PINS) return;
    const p = [...latest.current.pins, pin];
    latest.current = { ...latest.current, pins: p };
    setPins(p);
    schedulePreview();
  };
  const erase = (hit: { kind: 'arrow' | 'pin'; index: number }) => {
    if (hit.kind === 'arrow') {
      const a = latest.current.arrows.filter((_, i) => i !== hit.index);
      latest.current = { ...latest.current, arrows: a };
      setArrows(a);
    } else {
      const p = latest.current.pins.filter((_, i) => i !== hit.index);
      latest.current = { ...latest.current, pins: p };
      setPins(p);
    }
    schedulePreview();
  };
  const clearDrawing = () => {
    latest.current = { ...latest.current, arrows: [], pins: [] };
    setArrows([]);
    setPins([]);
    schedulePreview();
  };

  // A photo: read once, then a first preview (a gentle float, faces still; the sky drifts when found).
  const sessionRef = useRef<string | null>(null);
  const openPhoto = async (path: string) => {
    if (sessionRef.current) motion.end(sessionRef.current);
    sessionRef.current = null;
    if (job.current) motion.cancel(job.current);
    job.current = null;
    setPhoto(path);
    setScene(null);
    setPreview(null);
    setFinished(null);
    setArrows([]);
    setPins([]);
    setError(null);
    setBusy('reading');
    try {
      const sc = await motion.start(path);
      sessionRef.current = sc.sessionId;
      const s = { ...latest.current.settings, sky: sc.sky >= SKY_MIN };
      latest.current = { scene: sc, settings: s, arrows: [], pins: [] };
      setScene(sc);
      setSettings(s);
      setBusy(null);
      runPreview();
    } catch {
      setBusy(null);
      setError(t('motion_failed'));
    }
  };
  const openRef = useRef(openPhoto);
  useEffect(() => {
    openRef.current = openPhoto;
  });
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (path) await openPhoto(path);
  };
  const shot = route.params?.photo;
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined });
    void openRef.current(shot);
  }, [shot, navigation]);
  useEffect(
    () => () => {
      if (timer.current) clearTimeout(timer.current);
      if (job.current) motion.cancel(job.current);
      if (sessionRef.current) motion.end(sessionRef.current);
    },
    [motion],
  );

  // The finished video: Premium straight away; the free plan after an ad (unless one is unlocked).
  const make = async () => {
    const { scene: sc, settings: s, arrows: a, pins: p } = latest.current;
    if (!sc) return;
    if (timer.current) clearTimeout(timer.current);
    timer.current = null;
    if (job.current) motion.cancel(job.current);
    job.current = null;
    setBusy('full');
    setError(null);
    try {
      const v = await motion.render(sc.sessionId, s, a, p, 'full');
      setFinished(v);
      setGallery(g => [{ video: v, saved: false }, ...g]);
      recordActivity('studio', 'edit');
      refreshAllowance();
    } catch (e) {
      const code = e instanceof MotionError ? e.code : '';
      refreshAllowance();
      if (code === 'AD_REQUIRED') setAdOpen(true);
      else if (code === 'PREMIUM_REQUIRED') setPremiumOpen(true);
      else setError(t('motion_failed'));
    } finally {
      setBusy(null);
    }
  };
  const create = () => {
    if (!scene || busy === 'full') return;
    if (unlimited || unlocked) {
      void make();
    } else if (left === 0) {
      setPremiumOpen(true);
    } else {
      setAdNotice(null);
      setAdOpen(true);
    }
  };
  const watchAd = async () => {
    setBusy('ad');
    setAdNotice(null);
    try {
      const r = await motion.unlock();
      setAllowance(r.allowance);
      if (r.rewarded) {
        setAdOpen(false);
        setBusy(null);
        await make();
        return;
      }
      setAdNotice(t('motion_ad_not_finished'));
    } catch (e) {
      const code = e instanceof MotionError ? e.code : '';
      if (code === 'DAILY_LIMIT') {
        setAdOpen(false);
        refreshAllowance();
        setPremiumOpen(true);
      } else if (code === 'SUBSCRIBED') {
        setAdOpen(false);
        setBusy(null);
        await make();
        return;
      } else setAdNotice(t('motion_ad_unavailable'));
    }
    setBusy(b => (b === 'ad' ? null : b));
  };

  const save = async (item: MotionVideo | null = finished) => {
    if (!item) return;
    setBusy('save');
    try {
      await getStorageService().saveVideoToGallery(item.outputPath);
      setGallery(g => g.map(x => (x.video === item ? { ...x, saved: true } : x)));
    } catch {
      setError(t('motion_failed'));
    } finally {
      setBusy(null);
    }
  };
  const share = (item: MotionVideo | null = finished) => {
    if (item) void getStorageService().share([item.outputPath], 'Photo Motion');
  };
  const savedNow = !!finished && gallery.some(g => g.video === finished && g.saved);

  const aspect = scene ? scene.width / scene.height : 4 / 5;
  const shown = finished ?? preview;
  const camera = CAMERAS[settings.camera] ?? CAMERAS[0];
  const hasSky = !!scene && scene.sky >= SKY_MIN;
  const drawing = tab === 'animate' && !finished;

  const chip = (on: boolean) => [
    styles.chip,
    { borderColor: on ? accent : 'rgba(255,255,255,0.14)' },
    on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
  ];

  return (
    <Screen title="Photo Motion" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        {!photo ? (
          <GlassSurface radius={24} style={styles.intro} glow="rgba(245,198,107,0.22)">
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name="motion" size={28} color="#1A1206" strokeWidth={2} />
            </View>
            <AppText variant="overline" tone="secondary" align="center">
              {t('motion_overline').toUpperCase()}
            </AppText>
            <AppText variant="title1" align="center" accessibilityRole="header">
              {t('motion_intro_title')}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {t('motion_intro_body')}
            </AppText>
            <View style={styles.points}>
              {(
                [
                  ['aperture', 'motion_point_camera'],
                  ['flow', 'motion_point_flow'],
                  ['sparkles', 'motion_point_effects'],
                ] as const
              ).map(([icon, key]) => (
                <View key={key} style={styles.point}>
                  <View style={styles.pointIcon}>
                    <Icon name={icon} size={16} color={accent} />
                  </View>
                  <AppText variant="callout" style={{ flex: 1 }}>
                    {t(key)}
                  </AppText>
                </View>
              ))}
            </View>
            <Button
              label={t('motion_choose_photo')}
              icon="image"
              size="lg"
              onPress={() => void pick()}
              testID="motion-pick"
            />
            {!unlimited ? (
              <AppText variant="caption" tone="secondary" align="center">
                {t('motion_premium_message', { limit })}
              </AppText>
            ) : null}
          </GlassSurface>
        ) : (
          <GlassSurface radius={22} style={styles.stage} testID="motion-stage">
            {drawing ? (
              <MotionCanvas
                photo={photo}
                aspect={aspect}
                arrows={arrows}
                pins={pins}
                tool={tool}
                onArrows={addArrows}
                onPin={addPin}
                onErase={erase}
                label={t(`motion_hint_${tool}`)}
              />
            ) : shown ? (
              <VideoView
                key={shown.outputPath}
                source={shown.outputPath}
                loop
                muted
                style={{ width: '100%', aspectRatio: shown.width / shown.height, borderRadius: 16 }}
                accessibilityLabel={t('motion_video_of')}
                testID="motion-video"
              />
            ) : (
              <View style={[styles.wait, { aspectRatio: aspect }]}>
                <Image source={{ uri: `file://${photo}` }} style={[StyleSheet.absoluteFill, { opacity: 0.45 }]} />
                <ActivityIndicator color={accent} />
              </View>
            )}
            <View style={styles.tags} pointerEvents="none">
              {!finished && preview && !drawing ? (
                <View style={styles.tag}>
                  <AppText variant="caption" style={styles.tagText}>
                    {t('motion_preview_badge').toUpperCase()}
                  </AppText>
                </View>
              ) : null}
              {scene?.depth ? (
                <View style={styles.tag}>
                  <Icon name="layers" size={12} color="#FFFFFF" />
                  <AppText variant="caption" style={styles.tagText}>
                    {t('motion_depth_ready')}
                  </AppText>
                </View>
              ) : null}
              {hasSky ? (
                <View style={styles.tag}>
                  <Icon name="sun" size={12} color="#FFFFFF" />
                  <AppText variant="caption" style={styles.tagText}>
                    {t('motion_sky_found')}
                  </AppText>
                </View>
              ) : null}
            </View>
            {busy === 'reading' || busy === 'preview' || busy === 'full' ? (
              <View style={styles.busy} accessibilityLiveRegion="polite">
                <ActivityIndicator size="small" color={accent} />
                <AppText variant="caption" style={{ fontWeight: '700', color: '#FFFFFF' }}>
                  {busy === 'reading'
                    ? t('motion_reading')
                    : busy === 'full'
                      ? t('motion_making')
                      : t('motion_previewing')}
                </AppText>
              </View>
            ) : null}
          </GlassSurface>
        )}

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}

        {photo && scene ? (
          <>
            {finished ? (
              <GlassSurface radius={18} style={styles.section} testID="motion-result">
                <View style={styles.row}>
                  <Icon name="check" size={18} color={accent} />
                  <AppText variant="bodyStrong" style={{ flex: 1 }}>
                    {t('motion_ready')}
                  </AppText>
                  <AppText variant="caption" tone="tertiary">
                    {t('motion_seconds', { n: Math.round(finished.durationMs / 1000) })}
                  </AppText>
                </View>
                <View style={styles.row}>
                  <View style={{ flex: 1 }}>
                    <Button
                      label={savedNow ? t('motion_saved') : t('motion_save')}
                      icon={savedNow ? 'check' : 'download'}
                      size="lg"
                      loading={busy === 'save'}
                      disabled={busy === 'save' || savedNow}
                      onPress={() => void save()}
                      testID="motion-save"
                    />
                  </View>
                  <Button
                    label={t('motion_share')}
                    icon="share"
                    variant="glass"
                    size="lg"
                    onPress={() => share()}
                    testID="motion-share"
                  />
                </View>
                <Button
                  label={t('motion_edit_again')}
                  icon="sliders"
                  variant="ghost"
                  onPress={() => setFinished(null)}
                  testID="motion-edit"
                />
              </GlassSurface>
            ) : (
              <>
                <View style={styles.tabs} accessibilityRole="tablist">
                  {TABS.map(x => {
                    const on = tab === x.id;
                    return (
                      <Pressable
                        key={x.id}
                        onPress={() => setTab(x.id)}
                        accessibilityRole="tab"
                        accessibilityState={{ selected: on }}
                        testID={`motion-tab-${x.id}`}
                        style={[styles.tab, on ? { backgroundImage: theme.gradients.action } : null]}
                      >
                        <Icon name={x.icon} size={18} color={on ? '#FFFFFF' : theme.colors.textSecondary} />
                        <AppText
                          variant="caption"
                          numberOfLines={1}
                          style={{ fontWeight: '700', color: on ? '#FFFFFF' : theme.colors.textSecondary }}
                        >
                          {t(`motion_tab_${x.id}`)}
                        </AppText>
                      </Pressable>
                    );
                  })}
                </View>

                <GlassSurface radius={18} style={styles.section} testID={`motion-panel-${tab}`}>
                  {tab === 'camera' ? (
                    <>
                      <ScrollView
                        horizontal
                        showsHorizontalScrollIndicator={false}
                        contentContainerStyle={{ gap: 10 }}
                        accessibilityRole="radiogroup"
                      >
                        {CAMERAS.map(c => {
                          const on = settings.camera === c.id;
                          return (
                            <Pressable
                              key={c.key}
                              onPress={() => change({ camera: c.id })}
                              accessibilityRole="radio"
                              accessibilityState={{ selected: on }}
                              accessibilityLabel={`${cameraName(c)}, ${cameraAbout(c)}`}
                              testID={`motion-camera-${c.key}`}
                              style={[styles.card, { borderColor: on ? accent : 'rgba(255,255,255,0.12)' }]}
                            >
                              <View style={[styles.cardIcon, on ? { backgroundImage: theme.gradients.premium } : null]}>
                                <Icon name={c.icon} size={20} color={on ? '#1A1206' : theme.colors.textSecondary} />
                              </View>
                              <AppText
                                variant="caption"
                                align="center"
                                numberOfLines={1}
                                style={{ fontWeight: on ? '800' : '600' }}
                              >
                                {cameraName(c)}
                              </AppText>
                            </Pressable>
                          );
                        })}
                      </ScrollView>
                      <AppText variant="callout" tone="secondary">
                        {cameraAbout(camera)}
                      </AppText>
                      {settings.camera !== 0 ? (
                        <ValueSlider
                          label={t('motion_strength')}
                          value={settings.cameraAmount}
                          onChange={v => change({ cameraAmount: v }, false)}
                          onEnd={schedulePreview}
                          format={v => `${Math.round(v * 100)}%`}
                          testID="motion-strength"
                        />
                      ) : null}
                    </>
                  ) : null}

                  {tab === 'animate' ? (
                    <>
                      <View style={styles.row} accessibilityRole="radiogroup">
                        {TOOLS.map(x => {
                          const on = tool === x.id;
                          return (
                            <Pressable
                              key={x.id}
                              onPress={() => setTool(x.id)}
                              accessibilityRole="radio"
                              accessibilityState={{ selected: on }}
                              testID={`motion-tool-${x.id}`}
                              style={[chip(on), styles.toolChip]}
                            >
                              <Icon name={x.icon} size={16} color={on ? accent : theme.colors.textSecondary} />
                              <AppText variant="caption" numberOfLines={1} style={{ fontWeight: on ? '700' : '500' }}>
                                {t(`motion_tool_${x.id}`)}
                              </AppText>
                            </Pressable>
                          );
                        })}
                        <Pressable
                          onPress={clearDrawing}
                          disabled={!arrows.length && !pins.length}
                          accessibilityRole="button"
                          accessibilityLabel={t('motion_clear')}
                          testID="motion-clear"
                          style={[styles.clear, { opacity: arrows.length || pins.length ? 1 : 0.4 }]}
                        >
                          <Icon name="trash" size={16} color={theme.colors.textSecondary} />
                        </Pressable>
                      </View>
                      <AppText variant="callout" tone="secondary">
                        {t(`motion_hint_${tool}`)}
                      </AppText>
                      <AppText variant="caption" tone="tertiary" testID="motion-counts">
                        {t('motion_counts', { arrows: arrows.length, pins: pins.length })}
                      </AppText>
                      <ValueSlider
                        label={t('motion_speed')}
                        value={settings.flowSpeed}
                        min={0.2}
                        max={2}
                        onChange={v => change({ flowSpeed: v }, false)}
                        onEnd={schedulePreview}
                        format={v => `${v.toFixed(1)}×`}
                        testID="motion-speed"
                      />
                      <ToggleRow
                        icon="figure"
                        title={t('motion_keep_faces')}
                        subtitle={
                          scene.faces > 0
                            ? `${t('motion_keep_faces_about')} ${t('motion_faces_found', { n: scene.faces })}`
                            : t('motion_keep_faces_about')
                        }
                        value={settings.keepFaces}
                        onChange={v => change({ keepFaces: v })}
                      />
                      {hasSky ? (
                        <>
                          <ToggleRow
                            icon="sun"
                            title={t('motion_sky')}
                            subtitle={t('motion_sky_about')}
                            value={settings.sky}
                            onChange={v => change({ sky: v })}
                            last={!settings.sky}
                          />
                          {settings.sky ? (
                            <ValueSlider
                              label={t('motion_sky_speed')}
                              value={settings.skySpeed}
                              min={-1}
                              max={1}
                              step={0.1}
                              onChange={v => change({ skySpeed: v }, false)}
                              onEnd={schedulePreview}
                              format={v =>
                                Math.abs(v) < 0.05
                                  ? '0'
                                  : `${v < 0 ? t('motion_left') : t('motion_right')} ${Math.round(Math.abs(v) * 100)}%`
                              }
                              testID="motion-sky-speed"
                            />
                          ) : null}
                        </>
                      ) : (
                        <AppText variant="caption" tone="tertiary">
                          {t('motion_sky_none')}
                        </AppText>
                      )}
                    </>
                  ) : null}

                  {tab === 'effects' ? (
                    <>
                      <View style={styles.wrap} accessibilityRole="radiogroup">
                        {EFFECTS.map(e => {
                          const on = settings.effect === e.id;
                          return (
                            <Pressable
                              key={e.key}
                              onPress={() => change({ effect: e.id })}
                              accessibilityRole="radio"
                              accessibilityState={{ selected: on }}
                              testID={`motion-effect-${e.key}`}
                              style={[chip(on), styles.toolChip]}
                            >
                              <Icon name={e.icon} size={14} color={on ? accent : theme.colors.textSecondary} />
                              <AppText variant="caption" style={{ fontWeight: on ? '700' : '500' }}>
                                {effectName(e)}
                              </AppText>
                            </Pressable>
                          );
                        })}
                      </View>
                      {settings.effect !== 0 ? (
                        <ValueSlider
                          label={t('motion_amount')}
                          value={settings.effectAmount}
                          onChange={v => change({ effectAmount: v }, false)}
                          onEnd={schedulePreview}
                          format={v => `${Math.round(v * 100)}%`}
                          testID="motion-amount"
                        />
                      ) : null}
                      <AppText variant="caption" tone="tertiary">
                        {t('motion_effects_about')}
                      </AppText>
                    </>
                  ) : null}

                  {tab === 'video' ? (
                    <>
                      <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
                        {t('motion_length')}
                      </AppText>
                      <View style={styles.row} accessibilityRole="radiogroup">
                        {DURATIONS.map(d => {
                          const on = settings.duration === d;
                          return (
                            <Pressable
                              key={d}
                              onPress={() => change({ duration: d })}
                              accessibilityRole="radio"
                              accessibilityState={{ selected: on }}
                              testID={`motion-length-${d}`}
                              style={[chip(on), { flex: 1, alignItems: 'center' }]}
                            >
                              <AppText variant="callout" style={{ fontWeight: on ? '800' : '500' }}>
                                {t('motion_seconds', { n: d })}
                              </AppText>
                            </Pressable>
                          );
                        })}
                      </View>
                      <ToggleRow
                        icon="refresh"
                        title={t('motion_loop')}
                        subtitle={settings.loop ? t('motion_loop_about') : t('motion_once_about')}
                        value={settings.loop}
                        onChange={v => change({ loop: v })}
                        last
                      />
                      <View style={styles.row}>
                        <Icon name="film" size={14} color={theme.colors.textSecondary} />
                        <AppText variant="caption" tone="secondary" style={{ flex: 1 }}>
                          {t('motion_quality')}
                        </AppText>
                      </View>
                      <AppText variant="caption" tone="tertiary">
                        {t('motion_preview_note')}
                      </AppText>
                    </>
                  ) : null}
                </GlassSurface>

                <View style={styles.plan} testID="motion-allowance">
                  <Icon name="crown" size={14} color={accent} />
                  <View style={{ flex: 1, gap: 2 }}>
                    <AppText variant="caption" style={{ fontWeight: '700' }}>
                      {unlimited
                        ? t('motion_unlimited')
                        : left === 0
                          ? t('motion_free_used', { limit })
                          : unlocked
                            ? t('motion_free_unlocked')
                            : t('motion_free_left', { n: left ?? limit, limit })}
                    </AppText>
                    {!unlimited && left !== 0 ? (
                      <AppText variant="caption" tone="tertiary">
                        {t('motion_free_ad')}
                      </AppText>
                    ) : null}
                  </View>
                </View>

                <Button
                  label={
                    unlimited || unlocked
                      ? t('motion_create')
                      : left === 0
                        ? t('motion_go_premium')
                        : t('motion_watch_create')
                  }
                  icon={unlimited || unlocked ? 'motion' : left === 0 ? 'crown' : 'play'}
                  variant="premium"
                  size="lg"
                  loading={busy === 'full'}
                  disabled={busy === 'full' || busy === 'ad' || busy === 'reading'}
                  onPress={create}
                  testID="motion-create"
                />
                {busy === 'full' ? (
                  <AppText variant="caption" tone="tertiary" align="center">
                    {t('motion_making_about')}
                  </AppText>
                ) : null}
              </>
            )}

            <Button
              label={t('motion_change_photo')}
              icon="image"
              variant="glass"
              disabled={busy === 'full'}
              onPress={() => void pick()}
              testID="motion-change"
            />

            {gallery.length > 0 ? (
              <View style={{ gap: 10 }} testID="motion-gallery">
                <AppText variant="overline" tone="secondary">
                  {t('motion_gallery').toUpperCase()}
                </AppText>
                <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 12 }}>
                  {gallery.map((g, i) => (
                    <GlassSurface key={`${g.video.outputPath}-${i}`} radius={16} style={styles.galleryCard}>
                      <Pressable
                        onPress={() => setFinished(g.video)}
                        accessibilityRole="button"
                        accessibilityLabel={t('motion_video_of')}
                        testID={`motion-gallery-play-${i}`}
                      >
                        {g.video.posterPath ? (
                          <Image
                            source={{ uri: `file://${g.video.posterPath}` }}
                            style={{ width: '100%', aspectRatio: g.video.width / g.video.height, borderRadius: 10 }}
                            resizeMode="cover"
                          />
                        ) : (
                          <View style={[styles.wait, { aspectRatio: g.video.width / g.video.height }]} />
                        )}
                        <View style={styles.play}>
                          <Icon name="play" size={18} color="#FFFFFF" />
                        </View>
                      </Pressable>
                      <View style={styles.row}>
                        <View style={{ flex: 1 }}>
                          <Button
                            label={g.saved ? t('motion_saved') : t('motion_save')}
                            icon={g.saved ? 'check' : 'download'}
                            disabled={g.saved || busy === 'save'}
                            onPress={() => void save(g.video)}
                            testID={`motion-gallery-save-${i}`}
                          />
                        </View>
                        <Button
                          label={t('motion_share')}
                          icon="share"
                          variant="glass"
                          onPress={() => share(g.video)}
                          testID={`motion-gallery-share-${i}`}
                        />
                      </View>
                    </GlassSurface>
                  ))}
                </ScrollView>
              </View>
            ) : null}
          </>
        ) : null}
      </View>

      {adOpen ? (
        <Modal
          transparent
          animationType="fade"
          visible
          onRequestClose={() => setAdOpen(false)}
          statusBarTranslucent
          navigationBarTranslucent
        >
          <Pressable
            style={styles.scrim}
            onPress={() => busy !== 'ad' && setAdOpen(false)}
            accessibilityRole="button"
            accessibilityLabel={t('motion_ad_later')}
          >
            <Pressable onPress={() => undefined} style={styles.sheetWrap}>
              <GlassSurface intensity="strong" radius={28} style={styles.sheet} testID="motion-ad-sheet">
                <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
                  <Icon name="play" size={24} color="#1A1206" strokeWidth={2} />
                </View>
                <AppText variant="title2" align="center" accessibilityRole="header">
                  {t('motion_ad_title')}
                </AppText>
                <AppText variant="body" tone="secondary" align="center">
                  {t('motion_ad_body', { limit })}
                </AppText>
                {left !== null ? (
                  <AppText variant="caption" align="center" style={{ fontWeight: '700' }}>
                    {t('motion_free_left', { n: left, limit })}
                  </AppText>
                ) : null}
                {adNotice ? (
                  <AppText
                    variant="callout"
                    align="center"
                    style={{ color: theme.colors.warning }}
                    accessibilityLiveRegion="polite"
                  >
                    {adNotice}
                  </AppText>
                ) : null}
                <Button
                  label={t('motion_ad_watch')}
                  icon="play"
                  variant="primary"
                  size="lg"
                  loading={busy === 'ad'}
                  disabled={busy === 'ad'}
                  onPress={() => void watchAd()}
                  testID="motion-ad-watch"
                />
                <Button
                  label={t('motion_go_premium')}
                  icon="crown"
                  variant="premium"
                  disabled={busy === 'ad'}
                  onPress={() => {
                    setAdOpen(false);
                    setPremiumOpen(true);
                  }}
                  testID="motion-ad-premium"
                />
                <Button
                  label={t('motion_ad_later')}
                  variant="ghost"
                  disabled={busy === 'ad'}
                  onPress={() => setAdOpen(false)}
                />
              </GlassSurface>
            </Pressable>
          </Pressable>
        </Modal>
      ) : null}
      <PremiumSheet
        feature={premiumOpen ? 'motion' : null}
        message={left === 0 ? t('motion_free_used', { limit }) : t('motion_premium_message', { limit })}
        onClose={() => setPremiumOpen(false)}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  intro: { padding: 22, gap: 12, alignItems: 'stretch' },
  badge: {
    width: 56,
    height: 56,
    borderRadius: 28,
    alignItems: 'center',
    justifyContent: 'center',
    alignSelf: 'center',
  },
  points: { gap: 10, paddingVertical: 4 },
  point: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  pointIcon: {
    width: 30,
    height: 30,
    borderRadius: 15,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(245,198,107,0.12)',
  },
  stage: { padding: 10, alignItems: 'stretch', justifyContent: 'center' },
  wait: {
    width: '100%',
    borderRadius: 16,
    overflow: 'hidden',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.05)',
  },
  tags: { position: 'absolute', top: 18, left: 18, flexDirection: 'row', flexWrap: 'wrap', gap: 6, maxWidth: '70%' },
  tag: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
    paddingHorizontal: 8,
    paddingVertical: 4,
    borderRadius: 999,
    backgroundColor: 'rgba(0,0,0,0.55)',
  },
  tagText: { color: '#FFFFFF', fontWeight: '700', fontSize: 11 },
  busy: {
    position: 'absolute',
    bottom: 18,
    alignSelf: 'center',
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 12,
    paddingVertical: 7,
    borderRadius: 999,
    backgroundColor: 'rgba(0,0,0,0.6)',
  },
  tabs: {
    flexDirection: 'row',
    padding: 4,
    gap: 4,
    borderRadius: 18,
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.1)',
  },
  tab: { flex: 1, alignItems: 'center', gap: 3, paddingVertical: 8, borderRadius: 14 },
  section: { padding: 16, gap: 12 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  chip: { paddingHorizontal: 12, paddingVertical: 8, borderRadius: 999, borderWidth: 1.5 },
  toolChip: { flexDirection: 'row', alignItems: 'center', gap: 6, flexShrink: 1 },
  clear: {
    marginLeft: 'auto',
    width: 38,
    height: 38,
    borderRadius: 19,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.06)',
  },
  card: {
    width: 84,
    paddingVertical: 10,
    paddingHorizontal: 6,
    gap: 6,
    borderRadius: 16,
    borderWidth: 1.5,
    alignItems: 'center',
  },
  cardIcon: {
    width: 40,
    height: 40,
    borderRadius: 20,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.06)',
  },
  plan: { flexDirection: 'row', alignItems: 'flex-start', gap: 8 },
  galleryCard: { width: 200, padding: 10, gap: 8 },
  play: {
    position: 'absolute',
    top: 8,
    right: 8,
    width: 30,
    height: 30,
    borderRadius: 15,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(0,0,0,0.55)',
  },
  scrim: { flex: 1, backgroundColor: 'rgba(5,5,10,0.6)', justifyContent: 'center', padding: 24 },
  sheetWrap: { width: '100%', maxWidth: 440, alignSelf: 'center' },
  sheet: { padding: 22, gap: 12 },
});
