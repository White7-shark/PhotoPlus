import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import { AdUnlockSheet } from '../../components/AdUnlockSheet';
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
import { owedLine, unlockNotice, unlockedNow } from '../../services/ads/toolAllowance';
import {
  FIRST_SEED,
  MastersError,
  getMastersService,
  type AdAllowance,
  type Painting,
} from '../../services/masters/MastersService';
import { createId } from '../../services/database/ids';
import { getStorageService } from '../../services/storage/StorageService';
import {
  ARTIST_WAY,
  INTENSITIES,
  clampControls,
  frameInset,
  intensityOf,
  type ArtControls,
  type IntensityId,
} from '../../services/masters/controls';
import {
  PENCIL_DEFAULTS,
  clampPencil,
  signatureRequest,
  type ArtMode,
  type PencilOptions,
  type Signature,
} from '../../services/masters/pencil';
import { addPhotos } from '../../services/projects/studios';
import type { StringKey } from '../../i18n';
import { CompareSlider } from './CompareSlider';
import { PhotoArtsStudio } from './PhotoArtsStudio';
import { useAppStore } from '../../state/store';
import { PhotoArtsTouch } from './PhotoArtsTouch';
import { PencilPanel } from './PencilPanel';
import { SignaturePanel } from './SignaturePanel';
import { ValueSlider } from '../../components/ValueSlider';
import { speedLabel } from './ArtLiveScreen';
import { useTheme } from '../../theme';
import type { ArtJob } from './ArtLiveScreen';

const LEVELS: readonly MastersLevel[] = [0, 1, 2];
const MODES: readonly ArtMode[] = ['paint', 'pencil'];

/**
 * Photo Arts: the person's photo painted — or drawn in pencil — the way one
 * of twenty great artists worked. Movements group the artists; each card
 * shows the photo already in that artist's hand. Previews are free; the
 * finished work is made live (the pencil or brush moving over the sheet)
 * and kept in the project's gallery. Paintings: Premium, or one a day on the
 * free plan; drawings: Premium, or two a day on the free plan, each after an
 * ad. Premium can sign every work.
 */
export function MastersScreen({ navigation, route }: RootScreenProps<'Masters'>) {
  const theme = useTheme();
  const t = useT();
  const masters = useMemo(() => getMastersService(), []);
  const prefs = useAppStore(s => s.settings.photoArts);
  const savedSignature = useAppStore(s => s.settings.artSignature);
  const savedSpeed = useAppStore(s => s.settings.artLiveSpeed);
  const [liveSpeed, setLiveSpeed] = useState(savedSpeed);
  const liveSpeedRef = useRef(savedSpeed);
  const updateSetting = useAppStore(s => s.updateSetting);
  const repos = useAppStore(s => s.repos);
  const projectId = route.params?.projectId;
  const [photo, setPhoto] = useState<string | null>(null);
  const [mode, setMode] = useState<ArtMode>('paint');
  const [movement, setMovement] = useState<Movement>('renaissance');
  const [artist, setArtist] = useState(0);
  const [level, setLevel] = useState<MastersLevel>(prefs.level);
  // Your touch: remembered from last time when the person keeps it.
  const [controls, setControls] = useState<ArtControls>(() =>
    prefs.remember ? clampControls(prefs.controls) : { ...ARTIST_WAY },
  );
  const [pencil, setPencil] = useState<PencilOptions>(PENCIL_DEFAULTS);
  const [signature, setSignature] = useState<Signature>(savedSignature);
  const [touchOpen, setTouchOpen] = useState(false);
  const [studioOpen, setStudioOpen] = useState(false);
  const [sigOpen, setSigOpen] = useState(false);
  const [frame, setFrame] = useState(prefs.frame);
  // The latest choices, for previews made when a finger lifts (and for screen readers' steps).
  const touch = useRef(controls);
  const pen = useRef(pencil);
  const sig = useRef(signature);
  const modeRef = useRef(mode);
  const changeTouch = (k: ArtControls) => {
    touch.current = k;
    setControls(k);
  };
  const [seed, setSeed] = useState(FIRST_SEED);
  const [preview, setPreview] = useState<Painting | null>(null);
  const [thumbs, setThumbs] = useState<Record<string, string>>({});
  const [pencilDay, setPencilDay] = useState<AdAllowance | null>(null);
  const [busy, setBusy] = useState<null | 'preview' | 'ad'>(null);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState<null | 'masters' | 'pencil' | 'artSignature'>(null);
  const [adOpen, setAdOpen] = useState(false);
  const [adNotice, setAdNotice] = useState<string | null>(null);

  const chosen = artistById(artist);
  // Photo Arts' day (paintings and drawings together): five works, each after an ad, on the free plan.
  const premium = pencilDay?.remaining === -1;
  const unlimited = premium;
  const pencilLeft = pencilDay && pencilDay.remaining !== -1 ? Math.max(0, pencilDay.remaining) : null;
  const pencilLimit = pencilDay?.limit ?? 5;
  const pencilUnlocked = !!pencilDay && pencilDay.remaining !== -1 && pencilDay.credits > 0 && (pencilLeft ?? 0) > 0;
  // Ads owed from offline unlocks: paid off first; at the most allowed the tool waits for them.
  const owed = pencilDay && !premium ? pencilDay.owed : 0;
  const used = pencilLeft === 0 && owed === 0;
  const [adNote, setAdNote] = useState<string | null>(null);

  const refreshAllowance = useCallback(() => {
    void masters.allowance().then(setPencilDay);
  }, [masters]);
  useEffect(refreshAllowance, [refreshAllowance]);
  useEffect(() => navigation.addListener?.('focus', refreshAllowance), [navigation, refreshAllowance]);

  /** What the preview (and the finished work) is made with: the signature only on Premium. */
  const extras = (m: ArtMode = modeRef.current, p: PencilOptions = pen.current, s: Signature = sig.current) => ({
    mode: m,
    ...(m === 'pencil' ? { pencil: p } : {}),
    ...(premium ? { signature: signatureRequest(s) } : {}),
  });

  // The preview follows each choice (a newer one cancels the older).
  const job = useRef<string | null>(null);
  const draw = (
    path: string,
    a: number,
    l: MastersLevel,
    sd: number,
    k: ArtControls = touch.current,
    fr = frame,
    ex = extras(),
  ) => {
    const id = createId('masters');
    if (job.current) masters.cancel(job.current);
    job.current = id;
    setBusy('preview');
    masters
      .paint(path, a, l, 'preview', sd, id, k, fr, ex)
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
  const redraw = (ex = extras()) => {
    if (photo) draw(photo, artist, level, seed, touch.current, frame, ex);
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

  // Painting or pencil: the preview and the cards follow.
  const chooseMode = (m: ArtMode) => {
    if (m === modeRef.current) return;
    modeRef.current = m;
    setMode(m);
    setThumbs({});
    redraw(extras(m));
  };
  const changePencil = (p: PencilOptions, now: boolean) => {
    const c = clampPencil(p);
    pen.current = c;
    setPencil(c);
    if (now) redraw(extras('pencil', c));
  };
  const changeSignature = (s: Signature, done: boolean) => {
    sig.current = s;
    setSignature(s);
    if (done) {
      void updateSetting('artSignature', s).catch(() => undefined);
      redraw(extras(modeRef.current, pen.current, s));
    }
  };

  // Your touch: the preview repaints when the finger lifts; kept for next time when remembered.
  const keepTouch = (k: ArtControls) => {
    if (prefs.remember)
      void updateSetting('photoArts', { ...prefs, controls: clampControls(k) }).catch(() => undefined);
  };
  const touchDone = () => {
    if (photo) draw(photo, artist, level, seed, touch.current);
    keepTouch(touch.current);
  };
  // Transformation, surface and background repaint at once (one tap each), and are kept with the touch.
  const setChoice = (next: Partial<ArtControls>) => {
    const k = clampControls({ ...touch.current, ...next });
    changeTouch(k);
    if (modeRef.current === 'pencil' && next.intensity !== undefined) {
      changePencil({ ...pen.current, intensity: next.intensity }, true);
    } else if (photo) draw(photo, artist, level, seed, k);
    keepTouch(k);
  };
  const chooseFrame = (f: number) => {
    setFrame(f);
    if (photo) draw(photo, artist, level, seed, touch.current, f);
    void updateSetting('photoArts', { ...prefs, frame: f }).catch(() => undefined);
  };
  const touchReset = () => {
    // The artist's way for the sliders; the transformation and the studio choices stay as chosen.
    const k = {
      ...ARTIST_WAY,
      intensity: touch.current.intensity,
      surface: touch.current.surface,
      backdrop: touch.current.backdrop,
    };
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
  /** A new photo goes into the project's photos too. */
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (!path) return;
    if (repos && projectId) void addPhotos(repos, projectId, [path]).catch(() => undefined);
    openPhoto(path);
  };

  // A photo from the project (and, to edit a work again, what it was made with).
  const shot = route.params?.photo;
  const edit = route.params?.edit;
  // Every creation starts from a project: without one, choose (or create) it first.
  useEffect(() => {
    if (!projectId) navigation.replace('StudioProjects', { studio: 'arts', ...(shot ? { photo: shot } : {}) });
    // Once, on arrival.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  /** Opens a work again with what it was made with (artist, finish, mode, controls, pencil, frame). */
  const applyEdit = (path: string, e: Record<string, unknown>) => {
    const j = e as Partial<ArtJob>;
    const a = typeof j.artist === 'number' && ARTISTS.some(x => x.id === j.artist) ? j.artist : 0;
    setArtist(a);
    setMovement(artistById(a).movement);
    if (j.level === 0 || j.level === 1 || j.level === 2) setLevel(j.level);
    if (typeof j.seed === 'number') setSeed(j.seed);
    if (j.mode === 'pencil' || j.mode === 'paint') {
      modeRef.current = j.mode;
      setMode(j.mode);
    }
    if (j.controls) changeTouch(clampControls(j.controls));
    if (j.pencil) {
      pen.current = clampPencil(j.pencil);
      setPencil(pen.current);
    }
    if (typeof j.frame === 'number') setFrame(j.frame);
    setPhoto(path);
    setPreview(null);
    draw(
      path,
      a,
      j.level ?? level,
      j.seed ?? FIRST_SEED,
      j.controls ? clampControls(j.controls) : touch.current,
      j.frame ?? frame,
      extras(modeRef.current, pen.current),
    );
  };
  const editRef = useRef(applyEdit);
  useEffect(() => {
    editRef.current = applyEdit;
  });
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined, edit: undefined });
    if (edit) editRef.current(shot, edit);
    else openRef.current(shot);
    // Opened once per photo.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shot, navigation]);

  // Each card shows the photo in that artist's hand: the movement on show, one at a time.
  useEffect(() => {
    if (!photo || !prefs.cardPreviews) return;
    let live = true;
    const todo = artistsOf(movement).filter(a => !thumbs[`${mode}${a.id}`]);
    void (async () => {
      for (const a of todo) {
        try {
          const p = await masters.paint(photo, a.id, 0, 'thumb', FIRST_SEED, undefined, undefined, 0, { mode });
          if (!live) return;
          setThumbs(cur => ({ ...cur, [`${mode}${a.id}`]: p.outputPath }));
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
  }, [photo, movement, mode, masters, prefs.cardPreviews]);

  const chooseMovement = (m: Movement) => {
    setMovement(m);
    const first = artistsOf(m)[0];
    if (first && chosen.movement !== m) choose({ artist: first.id });
  };

  /** The finished work, made live in the studio and kept in the project. */
  const begin = () => {
    if (!photo || !projectId) return;
    if (job.current) masters.cancel(job.current);
    job.current = null;
    const j: ArtJob = {
      artist,
      level,
      seed,
      mode,
      frame,
      ...(mode === 'paint' ? { controls: touch.current } : { pencil: pen.current }),
      ...(premium && signatureRequest(sig.current) ? { signature: signatureRequest(sig.current) } : {}),
    };
    const title = `${artistName(chosen)} · ${mode === 'pencil' ? t('pencil_title') : t('masters_mode_paint')}`;
    navigation.navigate('ArtLive', { job: j as unknown as Record<string, unknown>, photo, projectId, title });
  };
  const create = () => {
    if (!photo) return;
    if (unlimited || pencilUnlocked) begin();
    else if (used) setPremiumOpen(mode === 'pencil' ? 'pencil' : 'masters');
    else {
      setAdNotice(null);
      setAdOpen(true);
    }
  };
  const watchAd = async () => {
    setBusy('ad');
    setAdNotice(null);
    try {
      const r = await masters.unlockArts();
      setPencilDay(r.allowance);
      setAdNote(unlockNotice(r));
      if (r.rewarded && unlockedNow(r.allowance)) {
        setAdOpen(false);
        begin();
      } else setAdNotice(r.rewarded ? unlockNotice(r) : t('pencil_ad_not_finished'));
    } catch (e) {
      const code = e instanceof MastersError ? e.code : '';
      if (code === 'DAILY_LIMIT') {
        setAdOpen(false);
        setPremiumOpen(mode === 'pencil' ? 'pencil' : 'masters');
      } else if (code === 'SUBSCRIBED') {
        setAdOpen(false);
        begin();
      } else if (code === 'OWED_ADS') {
        // Offline with every allowed ad owed: frozen until online.
        setAdNotice(e instanceof Error ? e.message : t('ads_owed_frozen', { n: owed }));
        refreshAllowance();
      } else setAdNotice(t('pencil_ad_unavailable'));
    } finally {
      setBusy(b => (b === 'ad' ? null : b));
    }
  };

  const intensity: IntensityId = intensityOf(mode === 'pencil' ? pencil.intensity : controls.intensity);
  const shown = preview;
  const name = artistName(chosen);
  const accent = theme.colors.accent;
  const technique = mode === 'pencil' ? t(`pencil_way_${chosen.key}` as StringKey) : artistTechnique(chosen);

  const planLine = unlimited
    ? t('arts_unlimited')
    : pencilLeft === 0
      ? t('arts_free_used', { limit: pencilLimit })
      : pencilUnlocked
        ? t('arts_free_unlocked')
        : t('arts_free_left', { n: pencilLeft ?? pencilLimit, limit: pencilLimit });
  const createLabel = used
    ? t('masters_go_premium')
    : owed > 0 && !pencilUnlocked
      ? t('ads_watch_owed', { n: owed })
      : unlimited || pencilUnlocked
        ? mode === 'pencil'
          ? t('pencil_draw')
          : t('masters_paint')
        : mode === 'pencil'
          ? t('pencil_watch_draw')
          : t('arts_watch_paint');

  return (
    <Screen
      title={mode === 'pencil' ? t('pencil_title') : 'Photo Arts'}
      onBack={() => navigation.goBack()}
      bottomInset={32}
    >
      <View style={{ gap: 16 }}>
        <View style={styles.modes} accessibilityRole="tablist">
          {MODES.map(m => {
            const on = mode === m;
            return (
              <Pressable
                key={m}
                onPress={() => chooseMode(m)}
                accessibilityRole="tab"
                accessibilityState={{ selected: on }}
                testID={`masters-mode-${m}`}
                style={[styles.mode, on ? { backgroundImage: theme.gradients.action } : null]}
              >
                <Icon
                  name={m === 'paint' ? 'brush' : 'sliders'}
                  size={16}
                  color={on ? '#FFFFFF' : theme.colors.textSecondary}
                />
                <AppText
                  variant="callout"
                  style={{ fontWeight: '700', color: on ? '#FFFFFF' : theme.colors.textSecondary }}
                >
                  {m === 'paint' ? t('masters_mode_paint') : t('pencil_title')}
                </AppText>
              </Pressable>
            );
          })}
        </View>

        {!photo ? (
          <GlassSurface radius={24} style={styles.intro} glow="rgba(245,198,107,0.22)">
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name="brush" size={26} color="#1A1206" strokeWidth={2} />
            </View>
            <AppText variant="overline" tone="secondary" align="center">
              {t('masters_overline').toUpperCase()}
            </AppText>
            <AppText variant="title1" align="center" accessibilityRole="header">
              {mode === 'pencil' ? t('pencil_intro_title') : t('masters_intro_title')}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {mode === 'pencil' ? t('pencil_intro_body') : t('masters_intro_body')}
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
                {t('arts_premium_message', { limit: pencilLimit })}
              </AppText>
            ) : null}
          </GlassSurface>
        ) : (
          <GlassSurface radius={22} style={styles.canvas}>
            {shown ? (
              <CompareSlider
                painting={{ uri: `file://${shown.outputPath}`, width: shown.width, height: shown.height }}
                photo={`file://${photo}`}
                inset={
                  shown.inner ? { x: shown.inner[0], y: shown.inner[1] } : frameInset(frame, shown.width, shown.height)
                }
                label={t('masters_painting_of', { artist: name })}
                hint={t('masters_compare')}
                testID="masters-canvas"
              />
            ) : (
              <View style={styles.wait}>
                <ActivityIndicator color={accent} />
              </View>
            )}
            {busy === 'preview' ? (
              <View style={styles.painting} accessibilityLiveRegion="polite">
                <ActivityIndicator size="small" color={accent} />
                <AppText variant="caption" style={{ fontWeight: '700' }}>
                  {mode === 'pencil' ? t('pencil_drawing') : t('masters_painting')}
                </AppText>
              </View>
            ) : null}
            <View style={styles.caption}>
              <AppText variant="caption" tone="tertiary">
                {t('masters_compare')}
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
                const thumb = thumbs[`${mode}${a.id}`];
                return (
                  <Pressable
                    key={a.id}
                    onPress={() => choose({ artist: a.id })}
                    accessibilityRole="button"
                    accessibilityState={{ selected: on }}
                    accessibilityLabel={`${artistName(a)}, ${mode === 'pencil' ? t(`pencil_way_${a.key}` as StringKey) : artistTechnique(a)}`}
                    testID={`masters-artist-${a.key}`}
                    style={[styles.card, { borderColor: on ? accent : 'rgba(255,255,255,0.12)' }]}
                  >
                    <View style={styles.thumb}>
                      {thumb ? (
                        <Image source={{ uri: `file://${thumb}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
                      ) : prefs.cardPreviews ? (
                        <ActivityIndicator color={accent} />
                      ) : (
                        // Cards without pictures (Settings): the artist's initial.
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
              <AppText variant="callout" style={{ color: accent, fontWeight: '700' }} testID="masters-technique">
                {technique}
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

            <View style={{ gap: 8 }}>
              <AppText variant="overline" tone="secondary">
                {t('masters_intensity').toUpperCase()}
              </AppText>
              <View style={styles.levels} accessibilityRole="radiogroup">
                {INTENSITIES.map(i => {
                  const on = intensity === i.id;
                  return (
                    <Pressable
                      key={i.id}
                      onPress={() => setChoice({ intensity: i.value })}
                      accessibilityRole="radio"
                      accessibilityState={{ selected: on }}
                      testID={`masters-intensity-${i.id}`}
                      style={[
                        styles.level,
                        { borderColor: on ? accent : 'rgba(255,255,255,0.14)' },
                        on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
                      ]}
                    >
                      <AppText variant="caption" style={{ fontWeight: on ? '800' : '500' }}>
                        {t(`masters_intensity_${i.id}` as StringKey)}
                      </AppText>
                    </Pressable>
                  );
                })}
              </View>
              <AppText variant="caption" tone="tertiary">
                {t(`masters_intensity_${intensity}_about` as StringKey)}
              </AppText>
            </View>

            {mode === 'pencil' ? (
              <PencilPanel
                value={pencil}
                onChange={p => changePencil(p, p.medium !== pen.current.medium || p.paper !== pen.current.paper)}
                onEnd={() => changePencil(pen.current, true)}
              />
            ) : (
              <PhotoArtsTouch
                value={controls}
                open={touchOpen}
                onToggle={() => setTouchOpen(o => !o)}
                onChange={changeTouch}
                onEnd={touchDone}
                onReset={touchReset}
                disabled={false}
              />
            )}

            <PhotoArtsStudio
              frameOnly={mode === 'pencil'}
              surface={controls.surface}
              backdrop={controls.backdrop}
              frame={frame}
              open={studioOpen}
              onToggle={() => setStudioOpen(o => !o)}
              onSurface={i => setChoice({ surface: i })}
              onBackdrop={i => setChoice({ backdrop: i })}
              onFrame={chooseFrame}
            />

            <SignaturePanel
              value={signature}
              premium={premium}
              open={sigOpen}
              onToggle={() => setSigOpen(o => !o)}
              onChange={s => changeSignature(s, false)}
              onEnd={s => changeSignature(s, true)}
              onLocked={() => setPremiumOpen('artSignature')}
            />

            <View style={styles.allowance} testID="masters-allowance">
              <Icon name="crown" size={14} color={accent} />
              <View style={{ flex: 1, gap: 2 }}>
                <AppText variant="caption" tone="secondary">
                  {planLine}
                </AppText>
                {!unlimited && pencilLeft !== 0 ? (
                  <AppText variant="caption" tone="tertiary">
                    {t('arts_free_ad')}
                  </AppText>
                ) : null}
                {pencilDay && owedLine(pencilDay) ? (
                  <AppText variant="caption" style={{ color: theme.colors.warning }} testID="masters-owed">
                    {owedLine(pencilDay)}
                  </AppText>
                ) : null}
                {adNote ? (
                  <AppText variant="caption" tone="secondary" testID="masters-ad-note">
                    {adNote}
                  </AppText>
                ) : null}
              </View>
            </View>

            <Button
              label={createLabel}
              icon={used ? 'crown' : !unlimited && !pencilUnlocked ? 'play' : 'brush'}
              variant="premium"
              size="lg"
              disabled={busy === 'ad' || !projectId}
              onPress={create}
              testID="masters-paint"
            />
            <AppText variant="caption" tone="tertiary" align="center">
              {mode === 'pencil' ? t('live_pencil_hint') : t('live_paint_hint')}
            </AppText>
            <ValueSlider
              label={mode === 'pencil' ? t('live_speed_drawing') : t('live_speed_painting')}
              value={liveSpeed}
              min={0.5}
              max={2.5}
              step={0.1}
              onChange={v => {
                liveSpeedRef.current = v;
                setLiveSpeed(v);
              }}
              onEnd={() =>
                void updateSetting('artLiveSpeed', Math.round(liveSpeedRef.current * 10) / 10).catch(() => undefined)
              }
              format={speedLabel}
              testID="masters-live-speed"
            />
            <Button
              label={t('masters_change_photo')}
              icon="image"
              variant="glass"
              onPress={() => void pick()}
              testID="masters-change"
            />
            {projectId ? (
              <Button
                label={t('sp_open_gallery')}
                icon="grid"
                variant="ghost"
                onPress={() => navigation.navigate('StudioProject', { projectId, tab: 'gallery' })}
                testID="masters-gallery"
              />
            ) : null}
          </>
        ) : null}
      </View>
      <AdUnlockSheet
        open={adOpen}
        title={t('arts_ad_title')}
        body={owed > 0 ? t('ads_owed_sheet', { n: owed }) : t('arts_ad_body', { limit: pencilLimit })}
        left={pencilLeft !== null ? t('arts_free_left', { n: pencilLeft, limit: pencilLimit }) : null}
        notice={adNotice}
        busy={busy === 'ad'}
        watchLabel={t('motion_ad_watch')}
        premiumLabel={t('motion_go_premium')}
        laterLabel={t('motion_ad_later')}
        onWatch={() => void watchAd()}
        onPremium={() => {
          setAdOpen(false);
          setPremiumOpen(mode === 'pencil' ? 'pencil' : 'masters');
        }}
        onClose={() => setAdOpen(false)}
        testID="arts-ad-sheet"
      />
      <PremiumSheet
        feature={premiumOpen}
        message={
          premiumOpen === 'masters' || premiumOpen === 'pencil'
            ? used
              ? t('arts_free_used', { limit: pencilLimit })
              : t('arts_premium_message', { limit: pencilLimit })
            : null
        }
        onClose={() => setPremiumOpen(null)}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  modes: {
    flexDirection: 'row',
    padding: 4,
    gap: 4,
    borderRadius: 16,
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.1)',
  },
  mode: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
    paddingVertical: 10,
    borderRadius: 12,
  },
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
