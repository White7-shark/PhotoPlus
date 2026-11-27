import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import { AdUnlockSheet } from '../../components/AdUnlockSheet';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import { createId } from '../../services/database/ids';
import { addPhotos, saveWork } from '../../services/projects/studios';
import { recordActivity } from '../../services/rewards/RewardsService';
import { getStorageService } from '../../services/storage/StorageService';
import {
  BACKGROUNDS,
  DEFAULT_TOON,
  STYLES,
  backgroundName,
  clampToon,
  styleAbout,
  styleName,
  withStyle,
  type ToonSettings,
} from '../../services/toon/catalog';
import { ToonError, getToonService, type ToonAllowance, type ToonPicture } from '../../services/toon/ToonService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { CompareSlider } from './CompareSlider';

const SHADES = [2, 3, 4, 5, 6] as const;

/**
 * Toon Studio: the photo as anime, manga, a comic, a cartoon, a webtoon or
 * 90s retro anime — flat colour, cel shading, ink lines, bright eyes and a
 * painted background — with the person's own say over every part of it.
 * Previews are free and follow every change; the finished HD picture is
 * Premium, or two a day on the free plan, each after a short ad, and is kept
 * in the project's gallery.
 */
export function ToonScreen({ navigation, route }: RootScreenProps<'Toon'>) {
  const theme = useTheme();
  const t = useT();
  const toon = useMemo(() => getToonService(), []);
  const repos = useAppStore(s => s.repos);
  const projectId = route.params?.projectId;
  const [photo, setPhoto] = useState<string | null>(null);
  const [settings, setSettings] = useState<ToonSettings>(DEFAULT_TOON);
  const [preview, setPreview] = useState<ToonPicture | null>(null);
  const [thumbs, setThumbs] = useState<Record<number, string>>({});
  const [tuneOpen, setTuneOpen] = useState(true);
  const [allowance, setAllowance] = useState<ToonAllowance | null>(null);
  const [busy, setBusy] = useState<null | 'preview' | 'full' | 'ad'>(null);
  const [error, setError] = useState<string | null>(null);
  const [adOpen, setAdOpen] = useState(false);
  const [adNotice, setAdNotice] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState(false);
  const latest = useRef(settings);

  const accent = theme.colors.accent;
  const unlimited = allowance?.remaining === -1;
  const limit = allowance?.limit ?? 2;
  const left = allowance && !unlimited ? Math.max(0, allowance.remaining) : null;
  const unlocked = !!allowance && !unlimited && allowance.credits > 0 && (left ?? 0) > 0;

  const refreshAllowance = useCallback(() => {
    void toon.allowance().then(setAllowance);
  }, [toon]);
  useEffect(refreshAllowance, [refreshAllowance]);

  // Every creation starts from a project: without one, choose (or create) it first.
  const shot = route.params?.photo;
  const edit = route.params?.edit;
  useEffect(() => {
    if (!projectId) navigation.replace('StudioProjects', { studio: 'toon', ...(shot ? { photo: shot } : {}) });
    // Once, on arrival.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // The preview follows each change (a newer one cancels the older).
  const job = useRef<string | null>(null);
  const draw = (path: string, s: ToonSettings) => {
    const id = createId('toon');
    if (job.current) toon.cancel(job.current);
    job.current = id;
    setBusy('preview');
    toon
      .render(path, s, 'preview', 7, id)
      .then(p => {
        if (job.current !== id) return;
        setPreview(p);
        setError(null);
      })
      .catch((e: unknown) => {
        if (job.current === id && !(e instanceof ToonError && e.code === 'CANCELLED')) setError(t('toon_failed'));
      })
      .finally(() => {
        if (job.current === id) {
          job.current = null;
          setBusy(b => (b === 'preview' ? null : b));
        }
      });
  };
  useEffect(
    () => () => {
      if (job.current) toon.cancel(job.current);
    },
    [toon],
  );
  /** A change: `now` previews at once (a tap); sliders preview when the finger lifts. */
  const change = (next: Partial<ToonSettings>, now = true) => {
    const s = clampToon({ ...latest.current, ...next });
    latest.current = s;
    setSettings(s);
    if (now && photo) draw(photo, s);
  };
  const done = () => {
    if (photo) draw(photo, latest.current);
  };
  const chooseStyle = (style: number) => {
    const s = withStyle(latest.current, style);
    latest.current = s;
    setSettings(s);
    if (photo) draw(photo, s);
  };

  const open = (path: string, s: ToonSettings = latest.current) => {
    latest.current = s;
    setSettings(s);
    setPhoto(path);
    setPreview(null);
    setThumbs({});
    setError(null);
    draw(path, s);
  };
  const openRef = useRef(open);
  useEffect(() => {
    openRef.current = open;
  });
  useEffect(() => {
    if (!shot || !projectId) return;
    navigation.setParams({ photo: undefined, edit: undefined });
    openRef.current(shot, edit ? clampToon(edit) : latest.current);
    // edit comes with the photo.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shot, navigation, projectId]);
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (!path) return;
    if (repos && projectId) void addPhotos(repos, projectId, [path]).catch(() => undefined);
    open(path);
  };

  // Each style's card shows the photo in that style (with the chosen background).
  useEffect(() => {
    if (!photo) return;
    let live = true;
    void (async () => {
      for (const st of STYLES) {
        try {
          const p = await toon.render(
            photo,
            { ...withStyle(DEFAULT_TOON, st.id), background: latest.current.background },
            'thumb',
          );
          if (!live) return;
          setThumbs(cur => ({ ...cur, [st.id]: p.outputPath }));
        } catch {
          if (!live) return;
        }
      }
    })();
    return () => {
      live = false;
    };
  }, [photo, toon]);

  // The finished picture, in HD, kept in the project.
  const make = async () => {
    if (!photo || !projectId) return;
    if (job.current) toon.cancel(job.current);
    job.current = null;
    setBusy('full');
    setError(null);
    try {
      const s = latest.current;
      const p = await toon.render(photo, s, 'full');
      recordActivity('studio', 'edit');
      refreshAllowance();
      if (repos) {
        const st = STYLES[s.style] ?? STYLES[0];
        const item = await saveWork(repos, projectId, {
          path: p.outputPath,
          sourcePath: photo,
          title: `${styleName(st)} · ${backgroundName(BACKGROUNDS[s.background] ?? 'keep')}`,
          width: p.width,
          height: p.height,
          settings: { ...s },
        });
        navigation.navigate('StudioItem', { itemId: item.id, fresh: true });
      }
    } catch (e) {
      const code = e instanceof ToonError ? e.code : '';
      refreshAllowance();
      if (code === 'AD_REQUIRED') setAdOpen(true);
      else if (code === 'PREMIUM_REQUIRED') setPremiumOpen(true);
      else setError(t('toon_failed'));
    } finally {
      setBusy(b => (b === 'full' ? null : b));
    }
  };
  const create = () => {
    if (!photo || busy === 'full') return;
    if (unlimited || unlocked) void make();
    else if (left === 0) setPremiumOpen(true);
    else {
      setAdNotice(null);
      setAdOpen(true);
    }
  };
  const watchAd = async () => {
    setBusy('ad');
    setAdNotice(null);
    try {
      const r = await toon.unlock();
      setAllowance(r.allowance);
      if (r.rewarded) {
        setAdOpen(false);
        setBusy(null);
        await make();
        return;
      }
      setAdNotice(t('toon_ad_not_finished'));
    } catch (e) {
      const code = e instanceof ToonError ? e.code : '';
      if (code === 'DAILY_LIMIT') {
        setAdOpen(false);
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

  const style = STYLES[settings.style] ?? STYLES[0];
  const chip = (on: boolean) => [
    styles.chip,
    { borderColor: on ? accent : 'rgba(255,255,255,0.14)' },
    on ? { backgroundColor: 'rgba(245,198,107,0.12)' } : null,
  ];

  return (
    <Screen title="Toon Studio" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        {!photo ? (
          <GlassSurface radius={24} style={styles.intro} glow="rgba(245,198,107,0.22)">
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name="sparkles" size={26} color="#1A1206" strokeWidth={2} />
            </View>
            <AppText variant="overline" tone="secondary" align="center">
              {t('toon_overline').toUpperCase()}
            </AppText>
            <AppText variant="title1" align="center" accessibilityRole="header">
              {t('toon_intro_title')}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {t('toon_intro_body')}
            </AppText>
            <View style={styles.names}>
              {STYLES.map(s => (
                <AppText key={s.key} variant="caption" tone="tertiary">
                  {styleName(s)}
                </AppText>
              ))}
            </View>
            <Button
              label={t('toon_choose_photo')}
              icon="image"
              size="lg"
              onPress={() => void pick()}
              testID="toon-pick"
            />
            {!unlimited ? (
              <AppText variant="caption" tone="secondary" align="center">
                {t('toon_premium_message', { limit })}
              </AppText>
            ) : null}
          </GlassSurface>
        ) : (
          <GlassSurface radius={22} style={styles.canvas}>
            {preview ? (
              <CompareSlider
                painting={{ uri: `file://${preview.outputPath}`, width: preview.width, height: preview.height }}
                photo={`file://${photo}`}
                inset={{ x: 0, y: 0 }}
                label={t('toon_picture_of', { style: styleName(style) })}
                hint={t('masters_compare')}
                testID="toon-canvas"
              />
            ) : (
              <View style={styles.wait}>
                <ActivityIndicator color={accent} />
              </View>
            )}
            {busy === 'preview' || busy === 'full' ? (
              <View style={styles.busy} accessibilityLiveRegion="polite">
                <ActivityIndicator size="small" color={accent} />
                <AppText variant="caption" style={{ fontWeight: '700' }}>
                  {busy === 'full' ? t('toon_making') : t('toon_drawing')}
                </AppText>
              </View>
            ) : null}
            <View style={{ paddingTop: 8 }}>
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
            <AppText variant="overline" tone="secondary">
              {t('toon_style').toUpperCase()}
            </AppText>
            <ScrollView
              horizontal
              showsHorizontalScrollIndicator={false}
              contentContainerStyle={{ gap: 10 }}
              accessibilityRole="radiogroup"
            >
              {STYLES.map(s => {
                const on = settings.style === s.id;
                const thumb = thumbs[s.id];
                return (
                  <Pressable
                    key={s.key}
                    onPress={() => chooseStyle(s.id)}
                    accessibilityRole="radio"
                    accessibilityState={{ selected: on }}
                    accessibilityLabel={`${styleName(s)}, ${styleAbout(s)}`}
                    testID={`toon-style-${s.key}`}
                    style={[styles.card, { borderColor: on ? accent : 'rgba(255,255,255,0.12)' }]}
                  >
                    <View style={styles.thumb}>
                      {thumb ? (
                        <Image source={{ uri: `file://${thumb}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
                      ) : (
                        <Icon name={s.icon} size={22} color={accent} />
                      )}
                    </View>
                    <AppText variant="caption" style={{ fontWeight: '700' }} numberOfLines={1}>
                      {styleName(s)}
                    </AppText>
                  </Pressable>
                );
              })}
            </ScrollView>
            <AppText variant="callout" style={{ color: accent, fontWeight: '700' }} testID="toon-style-about">
              {styleAbout(style)}
            </AppText>

            <View style={{ gap: 8 }}>
              <AppText variant="overline" tone="secondary">
                {t('toon_background').toUpperCase()}
              </AppText>
              <View style={styles.wrap} accessibilityRole="radiogroup">
                {BACKGROUNDS.map((b, i) => (
                  <Pressable
                    key={b}
                    onPress={() => change({ background: i })}
                    accessibilityRole="radio"
                    accessibilityState={{ selected: settings.background === i }}
                    testID={`toon-bg-${b}`}
                    style={chip(settings.background === i)}
                  >
                    <AppText variant="caption" style={{ fontWeight: settings.background === i ? '700' : '500' }}>
                      {backgroundName(b)}
                    </AppText>
                  </Pressable>
                ))}
              </View>
            </View>

            <GlassSurface radius={18} style={styles.section} testID="toon-tune">
              <Pressable
                onPress={() => setTuneOpen(o => !o)}
                accessibilityRole="button"
                accessibilityState={{ expanded: tuneOpen }}
                testID="toon-tune-toggle"
                style={styles.head}
              >
                <Icon name="sliders" size={18} color={accent} />
                <AppText variant="bodyStrong" style={{ flex: 1 }}>
                  {t('toon_tune')}
                </AppText>
                <Icon name={tuneOpen ? 'minus' : 'plus'} size={18} color={theme.colors.textSecondary} />
              </Pressable>
              {tuneOpen ? (
                <View style={{ gap: 12 }}>
                  <ValueSlider
                    label={t('toon_lines')}
                    value={settings.lines}
                    onChange={v => change({ lines: v }, false)}
                    onEnd={done}
                    format={v => `${Math.round(v * 100)}%`}
                    testID="toon-lines"
                  />
                  <AppText variant="caption" tone="secondary" style={{ fontWeight: '700' }}>
                    {t('toon_shades')}
                  </AppText>
                  <View style={styles.row} accessibilityRole="radiogroup">
                    {SHADES.map(n => (
                      <Pressable
                        key={n}
                        onPress={() => change({ shades: n })}
                        accessibilityRole="radio"
                        accessibilityState={{ selected: settings.shades === n }}
                        accessibilityLabel={t('toon_shades_n', { n })}
                        testID={`toon-shades-${n}`}
                        style={[chip(settings.shades === n), { flex: 1, alignItems: 'center' }]}
                      >
                        <AppText variant="callout" style={{ fontWeight: settings.shades === n ? '800' : '500' }}>
                          {n}
                        </AppText>
                      </Pressable>
                    ))}
                  </View>
                  <ValueSlider
                    label={t('toon_vivid')}
                    value={settings.vivid}
                    min={-1}
                    max={1}
                    onChange={v => change({ vivid: v }, false)}
                    onEnd={done}
                    format={v => (v > 0 ? `+${Math.round(v * 100)}` : `${Math.round(v * 100)}`)}
                    testID="toon-vivid"
                  />
                  <ValueSlider
                    label={t('toon_detail')}
                    value={settings.detail}
                    onChange={v => change({ detail: v }, false)}
                    onEnd={done}
                    format={v => `${Math.round(v * 100)}%`}
                    testID="toon-detail"
                  />
                  <ValueSlider
                    label={t('toon_eyes')}
                    value={settings.eyes}
                    onChange={v => change({ eyes: v }, false)}
                    onEnd={done}
                    format={v => `${Math.round(v * 100)}%`}
                    testID="toon-eyes"
                  />
                  <ValueSlider
                    label={t('toon_skin')}
                    value={settings.skin}
                    onChange={v => change({ skin: v }, false)}
                    onEnd={done}
                    format={v => `${Math.round(v * 100)}%`}
                    testID="toon-skin"
                  />
                  <ValueSlider
                    label={t('toon_glow')}
                    value={settings.glow}
                    onChange={v => change({ glow: v }, false)}
                    onEnd={done}
                    format={v => `${Math.round(v * 100)}%`}
                    testID="toon-glow"
                  />
                  <Button
                    label={t('toon_reset')}
                    icon="refresh"
                    variant="ghost"
                    onPress={() => chooseStyle(settings.style)}
                    testID="toon-reset"
                  />
                </View>
              ) : null}
            </GlassSurface>

            <View style={styles.plan} testID="toon-allowance">
              <Icon name="crown" size={14} color={accent} />
              <View style={{ flex: 1, gap: 2 }}>
                <AppText variant="caption" style={{ fontWeight: '700' }}>
                  {unlimited
                    ? t('toon_unlimited')
                    : left === 0
                      ? t('toon_free_used', { limit })
                      : unlocked
                        ? t('toon_free_unlocked')
                        : t('toon_free_left', { n: left ?? limit, limit })}
                </AppText>
                {!unlimited && left !== 0 ? (
                  <AppText variant="caption" tone="tertiary">
                    {t('toon_free_ad')}
                  </AppText>
                ) : null}
              </View>
            </View>

            <Button
              label={
                unlimited || unlocked ? t('toon_create') : left === 0 ? t('motion_go_premium') : t('toon_watch_create')
              }
              icon={unlimited || unlocked ? 'sparkles' : left === 0 ? 'crown' : 'play'}
              variant="premium"
              size="lg"
              loading={busy === 'full'}
              disabled={busy === 'full' || busy === 'ad' || !projectId}
              onPress={create}
              testID="toon-create"
            />
            <Button
              label={t('toon_change_photo')}
              icon="image"
              variant="glass"
              disabled={busy === 'full'}
              onPress={() => void pick()}
              testID="toon-change"
            />
            {projectId ? (
              <Button
                label={t('sp_open_gallery')}
                icon="grid"
                variant="ghost"
                onPress={() => navigation.navigate('StudioProject', { projectId, tab: 'gallery' })}
                testID="toon-gallery"
              />
            ) : null}
          </>
        ) : null}
      </View>
      <AdUnlockSheet
        open={adOpen}
        title={t('toon_ad_title')}
        body={t('toon_ad_body', { limit })}
        left={left !== null ? t('toon_free_left', { n: left, limit }) : null}
        notice={adNotice}
        busy={busy === 'ad'}
        watchLabel={t('motion_ad_watch')}
        premiumLabel={t('motion_go_premium')}
        laterLabel={t('motion_ad_later')}
        onWatch={() => void watchAd()}
        onPremium={() => {
          setAdOpen(false);
          setPremiumOpen(true);
        }}
        onClose={() => setAdOpen(false)}
        testID="toon-ad-sheet"
      />
      <PremiumSheet
        feature={premiumOpen ? 'toon' : null}
        message={left === 0 ? t('toon_free_used', { limit }) : t('toon_premium_message', { limit })}
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
  busy: {
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
  card: { width: 96, padding: 6, gap: 4, borderRadius: 16, borderWidth: 1.5 },
  thumb: {
    width: '100%',
    aspectRatio: 0.8,
    borderRadius: 11,
    overflow: 'hidden',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.05)',
  },
  section: { padding: 14, gap: 12 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  row: { flexDirection: 'row', gap: 8 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  chip: { paddingHorizontal: 12, paddingVertical: 8, borderRadius: 999, borderWidth: 1.5 },
  plan: { flexDirection: 'row', alignItems: 'flex-start', gap: 8 },
});
