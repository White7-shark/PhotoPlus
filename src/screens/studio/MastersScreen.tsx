import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import {
  ARTISTS,
  MOVEMENTS,
  artistAbout,
  artistById,
  artistName,
  artistTechnique,
  artistsOf,
  levelAbout,
  levelName,
  movementName,
  type MastersLevel,
  type Movement,
} from '../../services/masters/catalog';
import {
  FIRST_SEED,
  MastersError,
  getMastersService,
  type Allowance,
  type Painting,
} from '../../services/masters/MastersService';
import { createId } from '../../services/database/ids';
import { recordActivity } from '../../services/rewards/RewardsService';
import { getStorageService } from '../../services/storage/StorageService';
import { getStudioService } from '../../services/studio/StudioService';
import { ARTIST_WAY, clampControls, type ArtControls } from '../../services/masters/controls';
import { useAppStore } from '../../state/store';
import { MasterstrokeTouch } from './MasterstrokeTouch';
import { useTheme } from '../../theme';

const LEVELS: readonly MastersLevel[] = [0, 1, 2];

/**
 * Masterstroke: the person's photo painted the way one of twenty great artists
 * worked. Movements group the artists; each card shows the photo already in
 * that artist's hand. Previews are free; the finished painting (Study,
 * Painting or Masterpiece) is Premium, or one a day on the free plan.
 */
export function MastersScreen({ navigation, route }: RootScreenProps<'Masters'>) {
  const theme = useTheme();
  const t = useT();
  const masters = useMemo(() => getMastersService(), []);
  const prefs = useAppStore(s => s.settings.masterstroke);
  const updateSetting = useAppStore(s => s.updateSetting);
  const [photo, setPhoto] = useState<string | null>(null);
  const [movement, setMovement] = useState<Movement>('renaissance');
  const [artist, setArtist] = useState(0);
  const [level, setLevel] = useState<MastersLevel>(prefs.level);
  // Your touch: remembered from last time when the person keeps it.
  const [controls, setControls] = useState<ArtControls>(() =>
    prefs.remember ? clampControls(prefs.controls) : { ...ARTIST_WAY },
  );
  const [touchOpen, setTouchOpen] = useState(false);
  // The latest touch, for when a finger lifts right after the last move (and for screen readers' steps).
  const touch = useRef(controls);
  const changeTouch = (k: ArtControls) => {
    touch.current = k;
    setControls(k);
  };
  const [seed, setSeed] = useState(FIRST_SEED);
  const [preview, setPreview] = useState<Painting | null>(null);
  const [finished, setFinished] = useState<Painting | null>(null);
  const [thumbs, setThumbs] = useState<Record<number, string>>({});
  const [allowance, setAllowance] = useState<Allowance | null>(null);
  const [busy, setBusy] = useState<null | 'preview' | 'full' | 'save'>(null);
  const [comparing, setComparing] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState(false);

  const chosen = artistById(artist);
  const unlimited = allowance?.remaining === -1;
  const freeLeft = allowance && !unlimited ? Math.max(0, allowance.remaining) : null;

  const refreshAllowance = useCallback(() => {
    void masters.allowance().then(setAllowance);
  }, [masters]);
  useEffect(refreshAllowance, [refreshAllowance]);

  // The preview follows each choice (a newer one cancels the older).
  const job = useRef<string | null>(null);
  const draw = (path: string, a: number, l: MastersLevel, sd: number, k: ArtControls = touch.current) => {
    const id = createId('masters');
    if (job.current) masters.cancel(job.current);
    job.current = id;
    setFinished(null);
    setSaved(false);
    setBusy('preview');
    masters
      .paint(path, a, l, 'preview', sd, id, k)
      .then(p => {
        if (job.current !== id) return;
        setPreview(p);
        setError(null);
      })
      .catch((e: unknown) => {
        if (job.current === id && !(e instanceof MastersError && e.code === 'CANCELLED')) setError(t('masters_failed'));
      })
      .finally(() => {
        if (job.current === id) {
          job.current = null;
          setBusy(b => (b === 'preview' ? null : b));
        }
      });
  };
  const choose = (next: { artist?: number; level?: MastersLevel; seed?: number }) => {
    const a = next.artist ?? artist;
    const l = next.level ?? level;
    const sd = next.seed ?? seed;
    setArtist(a);
    setLevel(l);
    setSeed(sd);
    if (photo) draw(photo, a, l, sd);
  };
  useEffect(
    () => () => {
      if (job.current) masters.cancel(job.current);
    },
    [masters],
  );

  // Your touch: the preview repaints when the finger lifts; kept for next time when remembered.
  const keepTouch = (k: ArtControls) => {
    if (prefs.remember)
      void updateSetting('masterstroke', { ...prefs, controls: clampControls(k) }).catch(() => undefined);
  };
  const touchDone = () => {
    if (photo) draw(photo, artist, level, seed, touch.current);
    keepTouch(touch.current);
  };
  const touchReset = () => {
    const k = { ...ARTIST_WAY };
    changeTouch(k);
    if (photo) draw(photo, artist, level, seed, k);
    keepTouch(k);
  };

  const openPhoto = (path: string) => {
    setPhoto(path);
    setThumbs({});
    setPreview(null);
    setSeed(FIRST_SEED);
    setError(null);
    draw(path, artist, level, FIRST_SEED);
  };
  const openRef = useRef(openPhoto);
  useEffect(() => {
    openRef.current = openPhoto;
  });
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (path) openPhoto(path);
  };
  const shot = route.params?.photo;
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined });
    openRef.current(shot);
  }, [shot, navigation]);

  // Each card shows the photo in that artist's hand: the movement on show, one at a time.
  useEffect(() => {
    if (!photo || !prefs.cardPreviews) return;
    let live = true;
    const todo = artistsOf(movement).filter(a => !thumbs[a.id]);
    void (async () => {
      for (const a of todo) {
        try {
          const p = await masters.paint(photo, a.id, 0, 'thumb');
          if (!live) return;
          setThumbs(cur => ({ ...cur, [a.id]: p.outputPath }));
        } catch {
          if (!live) return;
        }
      }
    })();
    return () => {
      live = false;
    };
    // thumbs is read for what is missing; a new thumb must not restart the queue.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [photo, movement, masters, prefs.cardPreviews]);

  const chooseMovement = (m: Movement) => {
    setMovement(m);
    const first = artistsOf(m)[0];
    if (first && chosen.movement !== m) choose({ artist: first.id });
  };

  const paint = async () => {
    if (!photo) return;
    if (freeLeft === 0) {
      setPremiumOpen(true);
      return;
    }
    if (job.current) masters.cancel(job.current);
    job.current = null;
    setBusy('full');
    setError(null);
    try {
      const p = await masters.paint(photo, artist, level, 'full', seed, undefined, touch.current);
      setFinished(p);
      recordActivity('studio', 'edit');
      if (p.remaining !== undefined && allowance) setAllowance({ ...allowance, remaining: p.remaining });
      else refreshAllowance();
    } catch (e) {
      if (e instanceof MastersError && e.code === 'PREMIUM_REQUIRED') {
        refreshAllowance();
        setPremiumOpen(true);
      } else setError(t('masters_failed'));
    } finally {
      setBusy(null);
    }
  };

  const save = async () => {
    if (!finished) return;
    setBusy('save');
    try {
      await getStudioService().saveToGallery(finished.outputPath);
      setSaved(true);
    } catch {
      setError(t('masters_failed'));
    } finally {
      setBusy(null);
    }
  };
  const share = () => void getStorageService().share([finished?.outputPath ?? ''], artistName(chosen));

  const shown = finished ?? preview;
  const name = artistName(chosen);
  const accent = theme.colors.accent;

  return (
    <Screen title="Masterstroke" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        {!photo ? (
          <GlassSurface radius={24} style={styles.intro} glow="rgba(245,198,107,0.22)">
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name="brush" size={26} color="#1A1206" strokeWidth={2} />
            </View>
            <AppText variant="overline" tone="secondary" align="center">
              {t('masters_overline').toUpperCase()}
            </AppText>
            <AppText variant="title1" align="center" accessibilityRole="header">
              {t('masters_intro_title')}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {t('masters_intro_body')}
            </AppText>
            <View style={styles.names}>
              {ARTISTS.slice(0, 8).map(a => (
                <AppText key={a.id} variant="caption" tone="tertiary">
                  {artistName(a)}
                </AppText>
              ))}
            </View>
            <Button
              label={t('masters_choose_photo')}
              icon="image"
              size="lg"
              onPress={() => void pick()}
              testID="masters-pick"
            />
            {!unlimited ? (
              <AppText variant="caption" tone="secondary" align="center">
                {t('masters_premium_message')}
              </AppText>
            ) : null}
          </GlassSurface>
        ) : (
          <GlassSurface radius={22} style={styles.canvas}>
            {shown ? (
              <Pressable
                onPressIn={() => setComparing(true)}
                onPressOut={() => setComparing(false)}
                accessibilityRole="image"
                accessibilityLabel={comparing ? t('masters_your_photo') : t('masters_painting_of', { artist: name })}
                accessibilityHint={t('masters_hold_compare')}
                testID="masters-canvas"
                style={{ width: '100%' }}
              >
                <Image
                  source={{ uri: `file://${comparing ? photo : shown.outputPath}` }}
                  style={{ width: '100%', aspectRatio: shown.width / shown.height, maxHeight: 460, borderRadius: 14 }}
                  resizeMode="contain"
                  testID="masters-painting"
                />
              </Pressable>
            ) : (
              <View style={styles.wait}>
                <ActivityIndicator color={accent} />
              </View>
            )}
            {busy === 'preview' || busy === 'full' ? (
              <View style={styles.painting} accessibilityLiveRegion="polite">
                <ActivityIndicator size="small" color={accent} />
                <AppText variant="caption" style={{ fontWeight: '700' }}>
                  {t('masters_painting')}
                </AppText>
              </View>
            ) : null}
            <View style={styles.caption}>
              <AppText variant="caption" tone="tertiary">
                {comparing ? t('masters_your_photo') : t('masters_hold_compare')}
              </AppText>
            </View>
          </GlassSurface>
        )}

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}

        {photo ? (
          <>
            <ScrollView
              horizontal
              showsHorizontalScrollIndicator={false}
              contentContainerStyle={{ gap: 8 }}
              accessibilityRole="tablist"
            >
              {MOVEMENTS.map(m => {
                const on = movement === m;
                return (
                  <Pressable
                    key={m}
                    onPress={() => chooseMovement(m)}
                    accessibilityRole="tab"
                    accessibilityState={{ selected: on }}
                    testID={`masters-movement-${m}`}
                    style={[styles.chip, { borderColor: on ? accent : 'rgba(255,255,255,0.14)' }]}
                  >
                    <AppText variant="caption" style={{ fontWeight: on ? '700' : '500' }}>
                      {movementName(m)}
                    </AppText>
                  </Pressable>
                );
              })}
            </ScrollView>

            <AppText variant="overline" tone="secondary">
              {t('masters_choose_artist').toUpperCase()}
            </AppText>
            <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
              {artistsOf(movement).map(a => {
                const on = artist === a.id;
                const thumb = thumbs[a.id];
                return (
                  <Pressable
                    key={a.id}
                    onPress={() => choose({ artist: a.id })}
                    accessibilityRole="button"
                    accessibilityState={{ selected: on }}
                    accessibilityLabel={`${artistName(a)}, ${artistTechnique(a)}`}
                    testID={`masters-artist-${a.key}`}
                    style={[styles.card, { borderColor: on ? accent : 'rgba(255,255,255,0.12)' }]}
                  >
                    <View style={styles.thumb}>
                      {thumb ? (
                        <Image source={{ uri: `file://${thumb}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
                      ) : prefs.cardPreviews ? (
                        <ActivityIndicator color={accent} />
                      ) : (
                        // Cards without paintings (Settings): the artist's initial.
                        <AppText variant="title1" style={{ color: accent }}>
                          {artistName(a).charAt(0)}
                        </AppText>
                      )}
                    </View>
                    <AppText variant="caption" style={{ fontWeight: '700' }} numberOfLines={1}>
                      {artistName(a)}
                    </AppText>
                    <AppText variant="caption" tone="tertiary" style={{ fontSize: 10 }} numberOfLines={1}>
                      {a.years}
                    </AppText>
                  </Pressable>
                );
              })}
            </ScrollView>

            <GlassSurface radius={18} style={styles.section}>
              <View style={{ flexDirection: 'row', alignItems: 'baseline', gap: 8, flexWrap: 'wrap' }}>
                <AppText variant="title3">{name}</AppText>
                <AppText variant="caption" tone="tertiary">
                  {chosen.years}
                </AppText>
              </View>
              <AppText variant="callout" style={{ color: accent, fontWeight: '700' }}>
                {artistTechnique(chosen)}
              </AppText>
              <AppText variant="body" tone="secondary">
                {artistAbout(chosen)}
              </AppText>
            </GlassSurface>

            <View style={{ gap: 8 }}>
              <AppText variant="overline" tone="secondary">
                {t('masters_level').toUpperCase()}
              </AppText>
              <View style={styles.levels} accessibilityRole="radiogroup">
                {LEVELS.map(l => {
                  const on = level === l;
                  return (
                    <Pressable
                      key={l}
                      onPress={() => choose({ level: l })}
                      accessibilityRole="radio"
                      accessibilityState={{ selected: on }}
                      testID={`masters-level-${l}`}
                      style={[
                        styles.level,
                        { borderColor: on ? accent : 'rgba(255,255,255,0.14)' },
                        on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
                      ]}
                    >
                      <AppText variant="caption" style={{ fontWeight: on ? '800' : '500' }}>
                        {levelName(l)}
                      </AppText>
                    </Pressable>
                  );
                })}
              </View>
              <AppText variant="caption" tone="tertiary">
                {levelAbout(level)}
              </AppText>
            </View>

            <MasterstrokeTouch
              value={controls}
              open={touchOpen}
              onToggle={() => setTouchOpen(o => !o)}
              onChange={changeTouch}
              onEnd={touchDone}
              onReset={touchReset}
              disabled={busy === 'full'}
            />

            <View style={styles.allowance} testID="masters-allowance">
              <Icon name="crown" size={14} color={accent} />
              <AppText variant="caption" tone="secondary" style={{ flex: 1 }}>
                {unlimited
                  ? t('masters_unlimited')
                  : freeLeft === 0
                    ? t('masters_free_used')
                    : freeLeft !== null && freeLeft > 1
                      ? t('masters_free_left', { n: freeLeft })
                      : t('masters_free_ready')}
              </AppText>
            </View>

            {finished ? (
              <View style={{ gap: 10 }}>
                <View style={{ flexDirection: 'row', gap: 10 }}>
                  <View style={{ flex: 1 }}>
                    <Button
                      label={saved ? t('masters_saved') : t('masters_save')}
                      icon={saved ? 'check' : 'download'}
                      size="lg"
                      loading={busy === 'save'}
                      disabled={busy === 'save' || saved}
                      onPress={() => void save()}
                      testID="masters-save"
                    />
                  </View>
                  <Button
                    label={t('masters_share')}
                    icon="share"
                    variant="glass"
                    size="lg"
                    onPress={share}
                    testID="masters-share"
                  />
                </View>
                <Button
                  label={t('masters_paint_again')}
                  icon="refresh"
                  variant="ghost"
                  onPress={() => choose({ seed: seed + 1 })}
                  testID="masters-again"
                />
              </View>
            ) : (
              <Button
                label={freeLeft === 0 ? t('masters_go_premium') : t('masters_paint')}
                icon={freeLeft === 0 ? 'crown' : 'brush'}
                variant="premium"
                size="lg"
                loading={busy === 'full'}
                disabled={busy === 'full'}
                onPress={() => void paint()}
                testID="masters-paint"
              />
            )}
            <Button
              label={t('masters_change_photo')}
              icon="image"
              variant="glass"
              disabled={busy === 'full'}
              onPress={() => void pick()}
              testID="masters-change"
            />
          </>
        ) : null}
      </View>
      <PremiumSheet
        feature={premiumOpen ? 'masters' : null}
        message={freeLeft === 0 ? t('masters_free_used') : null}
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
  names: { flexDirection: 'row', flexWrap: 'wrap', justifyContent: 'center', columnGap: 10, rowGap: 2 },
  canvas: { padding: 10, minHeight: 260, alignItems: 'center', justifyContent: 'center' },
  wait: { height: 240, alignItems: 'center', justifyContent: 'center' },
  painting: {
    position: 'absolute',
    top: 18,
    right: 18,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 999,
    backgroundColor: 'rgba(0,0,0,0.55)',
  },
  caption: { paddingTop: 8 },
  chip: { paddingHorizontal: 14, paddingVertical: 8, borderRadius: 999, borderWidth: 1.5 },
  card: { width: 104, padding: 6, gap: 4, borderRadius: 16, borderWidth: 1.5 },
  thumb: {
    width: '100%',
    aspectRatio: 0.8,
    borderRadius: 11,
    overflow: 'hidden',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.05)',
  },
  section: { padding: 16, gap: 6 },
  levels: { flexDirection: 'row', gap: 8 },
  level: { flex: 1, alignItems: 'center', paddingVertical: 10, borderRadius: 999, borderWidth: 1.5 },
  allowance: { flexDirection: 'row', alignItems: 'center', gap: 8 },
});
