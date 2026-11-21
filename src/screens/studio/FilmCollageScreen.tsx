import React, { useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, Image, Pressable, ScrollView, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, IconButton, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { ValueSlider } from '../../components/ValueSlider';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import {
  COLLAGE_ASPECTS,
  COLLAGE_BACKGROUNDS,
  COLLAGE_LAYOUTS,
  FILM_STYLES,
  FRAME_LOOKS,
  MAX_FRAMES,
  MAX_PHOTOS,
  collageLayout,
  filmStyle,
  framesAfterRemoving,
  mixedFrames,
  plainFrames,
  type CollageChoice,
  type FilmChoice,
  type FilmFrame,
} from '../../services/studio/film';
import { getStorageService } from '../../services/storage/StorageService';
import { getStudioService, StudioError, type SceneResult } from '../../services/studio/StudioService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';

type Mode = 'film' | 'collage';
const pct = (v: number) => `${Math.round(v * 100)}`;

function Chip({
  label,
  selected,
  onPress,
  testID,
  locked,
  swatch,
}: {
  label: string;
  selected: boolean;
  onPress: () => void;
  testID?: string;
  locked?: boolean;
  swatch?: string;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="radio"
      accessibilityLabel={locked ? `${label}, Premium` : label}
      accessibilityState={{ selected }}
      testID={testID}
      style={[styles.chip, { borderColor: selected ? theme.colors.accent : 'rgba(255,255,255,0.14)' }]}
    >
      {locked ? <Icon name="crown" size={11} color={theme.colors.accent} /> : null}
      {swatch ? <View style={[styles.swatch, { backgroundColor: swatch }]} /> : null}
      <AppText variant="caption" style={{ fontWeight: selected ? '700' : '500' }}>
        {label}
      </AppText>
    </Pressable>
  );
}

/**
 * Film & Collage: the person's photos as frames on a strip of film (classic
 * 35mm, photo booth, amber negative, cinema) or in a collage, each frame with
 * its own look, zoom and mirror. "Mix it up" varies them like a contact sheet,
 * so even one photo fills a whole strip. Crops centre on faces.
 */
export function FilmCollageScreen({ navigation, route }: RootScreenProps<'FilmCollage'>) {
  const theme = useTheme();
  const studio = useMemo(() => getStudioService(), []);
  const can = useFeatureCheck();
  const premiumFilm = can('filmPremium').allowed;
  const repos = useAppStore(st => st.repos);
  const loadProjects = useAppStore(st => st.loadProjects);
  const [mode, setMode] = useState<Mode>('film');
  const [photos, setPhotos] = useState<string[]>([]);
  const [film, setFilm] = useState<FilmChoice>({
    style: 'classic',
    vertical: true,
    portrait: false,
    markings: true,
    frames: [],
  });
  const [collage, setCollage] = useState<CollageChoice>({
    layout: 'hero3',
    aspect: 'portrait',
    gap: 0.015,
    radius: 0.04,
    background: '#F6F1EA',
    frames: [],
  });
  const [selected, setSelected] = useState(0);
  const [preview, setPreview] = useState<SceneResult | null>(null);
  const [busy, setBusy] = useState<null | 'preview' | 'save'>(null);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState(false);

  const frames = mode === 'film' ? film.frames : collage.frames;
  const setFrames = (next: FilmFrame[]) =>
    mode === 'film' ? setFilm(f => ({ ...f, frames: next })) : setCollage(c => ({ ...c, frames: next }));
  const cellCount = collageLayout(collage.layout).cells.length;

  const addPhoto = (path: string) => {
    setPhotos(prev => {
      if (prev.length >= MAX_PHOTOS) return prev;
      const next = [...prev, path];
      // A strip grows with the photos (four frames at least); a collage keeps its layout.
      setFilm(f => ({
        ...f,
        frames:
          f.frames.length === 0
            ? mixedFrames(next.length, 4)
            : plainFrames(next.length, Math.max(f.frames.length, next.length)),
      }));
      setCollage(c => ({ ...c, frames: plainFrames(next.length, collageLayout(c.layout).cells.length) }));
      return next;
    });
  };
  const pick = async () => {
    const path = await getStorageService().pickImage('imports');
    if (path) addPhoto(path);
  };
  const shoot = () => navigation.navigate('Main', { screen: 'Camera', params: { returnTo: 'FilmCollage' } });
  const removePhoto = (i: number) => {
    setPhotos(prev => prev.filter((_, k) => k !== i));
    setFilm(f => ({ ...f, frames: framesAfterRemoving(f.frames, i) }));
    setCollage(c => ({ ...c, frames: framesAfterRemoving(c.frames, i) }));
  };

  // A photo just taken in the camera joins the tray (once).
  const shot = route.params?.photo;
  const addRef = useRef(addPhoto);
  useEffect(() => {
    addRef.current = addPhoto;
  });
  useEffect(() => {
    if (!shot) return;
    navigation.setParams({ photo: undefined });
    addRef.current(shot);
  }, [shot, navigation]);

  // The preview follows every change (the latest wins).
  const request = useRef(0);
  const ready = photos.length > 0 && frames.length > 0;
  const draw = async (m: Mode, ph: string[], f: FilmChoice, c: CollageChoice) => {
    const id = ++request.current;
    setBusy('preview');
    try {
      const r = m === 'film' ? await studio.renderFilm(ph, f, 'preview') : await studio.renderCollage(ph, c, 'preview');
      if (id !== request.current) return;
      setPreview(r);
      setError(null);
    } catch {
      if (id === request.current) setError('This couldn’t be drawn. Please try again.');
    } finally {
      if (id === request.current) setBusy(b => (b === 'preview' ? null : b));
    }
  };
  const drawRef = useRef(draw);
  useEffect(() => {
    drawRef.current = draw;
  });
  useEffect(() => {
    if (ready) void drawRef.current(mode, photos, film, collage);
  }, [ready, mode, photos, film, collage]);
  const shown = ready ? preview : null;

  const save = async () => {
    if (photos.length === 0) return;
    setBusy('save');
    setError(null);
    try {
      const r =
        mode === 'film'
          ? await studio.renderFilm(photos, film, 'full')
          : await studio.renderCollage(photos, collage, 'full');
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
          metadata: {
            studio: { tool: mode, ...(mode === 'film' ? { film: film.style } : { layout: collage.layout }) },
          },
        });
        void loadProjects();
        navigation.navigate('PhotoPreview', { imageId: record.id });
      }
    } catch (e) {
      if (e instanceof StudioError && e.code === 'PREMIUM_REQUIRED') setPremiumOpen(true);
      else setError('Your photo couldn’t be saved. Please try again.');
    } finally {
      setBusy(null);
    }
  };

  const current = frames[Math.min(selected, Math.max(0, frames.length - 1))];
  const editFrame = (patch: Partial<FilmFrame>) =>
    setFrames(frames.map((f, i) => (i === Math.min(selected, frames.length - 1) ? { ...f, ...patch } : f)));
  const count = mode === 'film' ? frames.length : cellCount;

  return (
    <Screen title="Film & Collage" onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <View style={styles.segment} accessibilityRole="tablist">
          {(['film', 'collage'] as const).map(m => (
            <Pressable
              key={m}
              onPress={() => {
                setMode(m);
                setSelected(0);
              }}
              accessibilityRole="tab"
              accessibilityState={{ selected: mode === m }}
              testID={`mode-${m}`}
              style={[styles.segmentItem, mode === m && { backgroundColor: 'rgba(255,255,255,0.12)' }]}
            >
              <Icon name={m === 'film' ? 'film' : 'grid'} size={16} color={theme.colors.textPrimary} />
              <AppText variant="bodyStrong">{m === 'film' ? 'Film strip' : 'Collage'}</AppText>
            </Pressable>
          ))}
        </View>

        <GlassSurface radius={20} style={styles.previewBox}>
          {shown ? (
            <Image
              source={{ uri: `file://${shown.outputPath}` }}
              style={{ width: '100%', aspectRatio: shown.width / shown.height, maxHeight: 460 }}
              resizeMode="contain"
              accessibilityLabel={mode === 'film' ? 'Your film strip' : 'Your collage'}
              testID="film-preview"
            />
          ) : (
            <AppText variant="body" tone="secondary" align="center">
              {mode === 'film'
                ? 'Add photos and they become frames on a strip of film. One photo is enough: Mix it up makes every frame different.'
                : 'Add photos and choose a layout.'}
            </AppText>
          )}
          {busy === 'preview' ? <ActivityIndicator style={styles.spinner} color={theme.colors.accent} /> : null}
        </GlassSurface>

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}

        {/* Photos */}
        <View style={{ gap: 8 }}>
          <AppText variant="bodyStrong">Photos {photos.length > 0 ? `· ${photos.length}` : ''}</AppText>
          <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
            {photos.map((p, i) => (
              <View key={`${p}-${i}`}>
                <Image source={{ uri: `file://${p}` }} style={styles.thumb} accessibilityLabel={`Photo ${i + 1}`} />
                <Pressable
                  onPress={() => removePhoto(i)}
                  accessibilityRole="button"
                  accessibilityLabel={`Remove photo ${i + 1}`}
                  style={styles.remove}
                  testID={`remove-photo-${i}`}
                >
                  <Icon name="close" size={12} color="#FFFFFF" />
                </Pressable>
              </View>
            ))}
            {photos.length < MAX_PHOTOS ? (
              <>
                <Pressable
                  onPress={() => void pick()}
                  accessibilityRole="button"
                  accessibilityLabel="Add a photo"
                  style={styles.add}
                  testID="film-add"
                >
                  <Icon name="image" size={20} color={theme.colors.textSecondary} />
                  <AppText variant="caption" tone="secondary">
                    Add
                  </AppText>
                </Pressable>
                <Pressable
                  onPress={shoot}
                  accessibilityRole="button"
                  accessibilityLabel="Take a photo"
                  style={styles.add}
                  testID="film-shoot"
                >
                  <Icon name="camera" size={20} color={theme.colors.textSecondary} />
                  <AppText variant="caption" tone="secondary">
                    Camera
                  </AppText>
                </Pressable>
              </>
            ) : null}
          </ScrollView>
        </View>

        {photos.length > 0 && mode === 'film' ? (
          <GlassSurface radius={18} style={styles.section}>
            <AppText variant="bodyStrong">Film</AppText>
            <View style={styles.wrap} accessibilityRole="radiogroup">
              {FILM_STYLES.map(s => (
                <Chip
                  key={s.id}
                  label={s.name}
                  selected={film.style === s.id}
                  locked={s.premium && !premiumFilm}
                  testID={`film-style-${s.id}`}
                  onPress={() =>
                    s.premium && !premiumFilm
                      ? setPremiumOpen(true)
                      : setFilm(f => ({ ...f, style: s.id, portrait: s.portrait }))
                  }
                />
              ))}
            </View>
            <AppText variant="caption" tone="secondary">
              {filmStyle(film.style).about}
            </AppText>
            <View style={styles.wrap}>
              <Chip
                label="Vertical"
                selected={film.vertical}
                onPress={() => setFilm(f => ({ ...f, vertical: true }))}
                testID="film-vertical"
              />
              <Chip
                label="Horizontal"
                selected={!film.vertical}
                onPress={() => setFilm(f => ({ ...f, vertical: false }))}
                testID="film-horizontal"
              />
              <Chip
                label="Portrait frames"
                selected={film.portrait}
                onPress={() => setFilm(f => ({ ...f, portrait: !f.portrait }))}
                testID="film-portrait"
              />
              <Chip
                label="Edge numbers"
                selected={film.markings}
                onPress={() => setFilm(f => ({ ...f, markings: !f.markings }))}
                testID="film-markings"
              />
            </View>
            <View style={styles.row}>
              <AppText variant="body" style={{ flex: 1 }}>
                Frames · {film.frames.length}
              </AppText>
              <IconButton
                icon="minus"
                label="Fewer frames"
                size={36}
                disabled={film.frames.length <= 1}
                onPress={() => setFilm(f => ({ ...f, frames: f.frames.slice(0, -1) }))}
              />
              <IconButton
                icon="plus"
                label="More frames"
                size={36}
                disabled={film.frames.length >= MAX_FRAMES}
                onPress={() =>
                  setFilm(f => ({
                    ...f,
                    frames: [...f.frames, ...mixedFrames(photos.length, f.frames.length + 1).slice(-1)],
                  }))
                }
              />
            </View>
          </GlassSurface>
        ) : null}

        {photos.length > 0 && mode === 'collage' ? (
          <GlassSurface radius={18} style={styles.section}>
            <AppText variant="bodyStrong">Layout</AppText>
            <View style={styles.wrap} accessibilityRole="radiogroup">
              {COLLAGE_LAYOUTS.map(l => (
                <Pressable
                  key={l.id}
                  onPress={() =>
                    setCollage(c => ({ ...c, layout: l.id, frames: plainFrames(photos.length, l.cells.length) }))
                  }
                  accessibilityRole="radio"
                  accessibilityLabel={l.name}
                  accessibilityState={{ selected: collage.layout === l.id }}
                  testID={`layout-${l.id}`}
                  style={[
                    styles.layout,
                    { borderColor: collage.layout === l.id ? theme.colors.accent : 'rgba(255,255,255,0.14)' },
                  ]}
                >
                  {l.cells.map(([x, y, w, h], i) => (
                    <View
                      key={i}
                      style={{
                        position: 'absolute',
                        left: `${x * 100}%`,
                        top: `${y * 100}%`,
                        width: `${w * 100}%`,
                        height: `${h * 100}%`,
                        padding: 1.5,
                      }}
                    >
                      <View style={{ flex: 1, borderRadius: 3, backgroundColor: 'rgba(255,255,255,0.35)' }} />
                    </View>
                  ))}
                </Pressable>
              ))}
            </View>
            <View style={styles.wrap} accessibilityRole="radiogroup">
              {COLLAGE_ASPECTS.map(a => (
                <Chip
                  key={a.id}
                  label={a.name}
                  selected={collage.aspect === a.id}
                  onPress={() => setCollage(c => ({ ...c, aspect: a.id }))}
                  testID={`aspect-${a.id}`}
                />
              ))}
            </View>
            <ValueSlider
              label="Spacing"
              value={collage.gap}
              min={0}
              max={0.06}
              step={0.005}
              onChange={v => setCollage(c => ({ ...c, gap: v }))}
              format={v => pct(v / 0.06)}
              testID="collage-gap"
            />
            <ValueSlider
              label="Round corners"
              value={collage.radius}
              min={0}
              max={0.3}
              step={0.01}
              onChange={v => setCollage(c => ({ ...c, radius: v }))}
              format={v => pct(v / 0.3)}
              testID="collage-radius"
            />
            <View style={styles.wrap} accessibilityRole="radiogroup">
              {COLLAGE_BACKGROUNDS.map(b => (
                <Pressable
                  key={b}
                  onPress={() => setCollage(c => ({ ...c, background: b }))}
                  accessibilityRole="radio"
                  accessibilityLabel={`Background ${b}`}
                  accessibilityState={{ selected: collage.background === b }}
                  style={[
                    styles.colour,
                    {
                      backgroundColor: b,
                      borderColor: collage.background === b ? theme.colors.accent : 'rgba(255,255,255,0.3)',
                    },
                  ]}
                />
              ))}
            </View>
          </GlassSurface>
        ) : null}

        {photos.length > 0 && current ? (
          <GlassSurface radius={18} style={styles.section}>
            <View style={styles.row}>
              <AppText variant="bodyStrong" style={{ flex: 1 }}>
                Transform each frame
              </AppText>
              <Button
                label="Mix it up"
                icon="sparkles"
                variant="glass"
                onPress={() => setFrames(mixedFrames(photos.length, count))}
                testID="film-mix"
              />
            </View>
            <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 6 }}>
              {frames.map((f, i) => (
                <Chip
                  key={i}
                  label={`${mode === 'film' ? 'Frame' : 'Cell'} ${i + 1}`}
                  selected={i === Math.min(selected, frames.length - 1)}
                  onPress={() => setSelected(i)}
                  testID={`frame-${i}`}
                />
              ))}
            </ScrollView>
            {photos.length > 1 ? (
              <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 6 }}>
                {photos.map((p, i) => (
                  <Pressable
                    key={`${p}-${i}`}
                    onPress={() => editFrame({ photo: i })}
                    accessibilityRole="radio"
                    accessibilityLabel={`Use photo ${i + 1}`}
                    accessibilityState={{ selected: current.photo === i }}
                  >
                    <Image
                      source={{ uri: `file://${p}` }}
                      style={[styles.small, { borderColor: current.photo === i ? theme.colors.accent : 'transparent' }]}
                    />
                  </Pressable>
                ))}
              </ScrollView>
            ) : null}
            <View style={styles.wrap} accessibilityRole="radiogroup">
              {FRAME_LOOKS.map(l => (
                <Chip
                  key={l.id}
                  label={l.name}
                  selected={current.look === l.id}
                  onPress={() => editFrame({ look: l.id })}
                  testID={`look-${l.id}`}
                />
              ))}
            </View>
            <ValueSlider
              label="Zoom"
              value={current.zoom}
              min={1}
              max={3}
              step={0.1}
              onChange={v => editFrame({ zoom: v })}
              format={v => `${v.toFixed(1)}×`}
              testID="frame-zoom"
            />
            <ValueSlider
              label="Left or right"
              value={current.panX}
              min={-1}
              max={1}
              step={0.05}
              onChange={v => editFrame({ panX: v })}
              format={v => pct(v)}
              testID="frame-pan-x"
            />
            <ValueSlider
              label="Up or down"
              value={current.panY}
              min={-1}
              max={1}
              step={0.05}
              onChange={v => editFrame({ panY: v })}
              format={v => pct(v)}
              testID="frame-pan-y"
            />
            <View style={styles.wrap}>
              <Chip
                label="Mirror"
                selected={current.flip}
                onPress={() => editFrame({ flip: !current.flip })}
                testID="frame-flip"
              />
            </View>
          </GlassSurface>
        ) : null}

        {photos.length > 0 ? (
          <Button
            label={mode === 'film' ? 'Save film strip' : 'Save collage'}
            icon="download"
            size="lg"
            loading={busy === 'save'}
            disabled={busy === 'save'}
            onPress={() => void save()}
            testID="film-save"
          />
        ) : null}
      </View>
      <PremiumSheet feature={premiumOpen ? 'filmPremium' : null} onClose={() => setPremiumOpen(false)} />
    </Screen>
  );
}

const styles = StyleSheet.create({
  segment: { flexDirection: 'row', gap: 6, padding: 4, borderRadius: 16, backgroundColor: 'rgba(255,255,255,0.05)' },
  segmentItem: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
    paddingVertical: 10,
    borderRadius: 12,
  },
  previewBox: { padding: 12, minHeight: 200, alignItems: 'center', justifyContent: 'center' },
  spinner: { position: 'absolute', top: 12, right: 12 },
  section: { padding: 14, gap: 10 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 999,
    borderWidth: 1.5,
  },
  swatch: { width: 12, height: 12, borderRadius: 6 },
  thumb: { width: 72, height: 90, borderRadius: 12 },
  small: { width: 44, height: 56, borderRadius: 8, borderWidth: 2 },
  remove: {
    position: 'absolute',
    top: 4,
    right: 4,
    width: 22,
    height: 22,
    borderRadius: 11,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(0,0,0,0.6)',
  },
  add: {
    width: 72,
    height: 90,
    borderRadius: 12,
    borderWidth: 1.5,
    borderStyle: 'dashed',
    borderColor: 'rgba(255,255,255,0.25)',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 4,
  },
  layout: { width: 46, height: 58, borderRadius: 8, borderWidth: 1.5, overflow: 'hidden' },
  colour: { width: 30, height: 30, borderRadius: 15, borderWidth: 2 },
});
