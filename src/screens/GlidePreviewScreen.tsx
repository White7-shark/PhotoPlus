import React, { useRef, useState } from 'react';
import { View } from 'react-native';

import { AppText, Badge, Button, GlassSurface, IconButton, Screen, SegmentedControl } from '../components';
import { PremiumSheet } from '../components/PremiumSheet';
import { ValueSlider } from '../components/ValueSlider';
import { VideoView } from '../components/VideoView';
import type { RootScreenProps } from '../navigation/types';
import { errorCode } from '../native';
import { glideFileName, glideRequest, steadierPercent, type GlideClip, type GlideSubject } from '../services/camera/glide';
import { frameGlideScene } from '../services/camera/glideScene';
import { friendlyMessage } from '../services/errors';
import { createLogger } from '../services/logging/logger';
import { getProcessingService } from '../services/processing/ImageProcessingService';
import { getStorageService, joinPath } from '../services/storage/StorageService';
import { PLATES, SIZE_MAX, SIZE_MIN, type Background } from '../services/studio/scene';
import { useAppStore } from '../state/store';
import { BackgroundPicker } from './studio/BackgroundPicker';
import { t as tx } from '../i18n';

const log = createLogger('glide-preview');

type ClipView = 'glide' | 'before';

/** A finished Glide clip: play it, compare with the clip as recorded, share or save. */
export function GlidePreviewScreen({ navigation, route }: RootScreenProps<'GlidePreview'>) {
  const [clip, setClip] = useState<GlideClip>(route.params.clip);
  const [view, setView] = useState<ClipView>('glide');
  // Glide in a new scene (Premium): the person moved into a background.
  const plan = useAppStore(st => st.entitlements.plan);
  const settings = useAppStore(st => st.settings);
  const [scenePanel, setScenePanel] = useState(false);
  const [background, setBackground] = useState<Background>({ kind: 'plate', plate: PLATES[0] });
  const [size, setSize] = useState(1);
  const [crafting, setCrafting] = useState<number | null>(null);
  const [premiumOpen, setPremiumOpen] = useState(false);
  const subject = useRef<GlideSubject | null>(null);
  const abort = useRef<AbortController | null>(null);
  const [muted, setMuted] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(clip.galleryUri !== null);
  const [message, setMessage] = useState<{ text: string; tone: 'success' | 'danger' } | null>(null);
  const [failed, setFailed] = useState(false);
  const steadier = steadierPercent(clip.shakeBefore, clip.shakeAfter);
  const aspect = clip.width > 0 && clip.height > 0 ? clip.width / clip.height : 9 / 16;

  const share = async () => {
    try {
      await getStorageService().share([clip.outputPath], 'Share video');
    } catch (e) {
      log.warn('share failed', e);
      setMessage({ text: friendlyMessage(errorCode(e) ?? 'SHARE_FAILED'), tone: 'danger' });
    }
  };

  const save = async () => {
    setSaving(true);
    try {
      await getStorageService().saveVideoToGallery(clip.outputPath);
      setSaved(true);
      setMessage({ text: tx('glide_preview_saved_to_your_gallery'), tone: 'success' });
    } catch (e) {
      log.warn('gallery save failed', e);
      setMessage({
        text: friendlyMessage(errorCode(e), 'The clip couldn’t be saved. Check your free space and try again.'),
        tone: 'danger',
      });
    } finally {
      setSaving(false);
    }
  };

  const openScene = () => {
    if (plan !== 'premium') {
      setPremiumOpen(true);
      return;
    }
    setScenePanel(p => !p);
  };

  const makeScene = async () => {
    setMessage(null);
    setCrafting(0);
    const controller = new AbortController();
    abort.current = controller;
    try {
      const processing = getProcessingService();
      subject.current ??= await processing.glideSubject(clip.rawPath);
      const framed = frameGlideScene(subject.current, background, size);
      if (!framed) {
        setMessage({ text: tx('glide_preview_nobody_was_found_in_this'), tone: 'danger' });
        return;
      }
      const paths = await getStorageService().getPaths();
      const outputPath = joinPath(paths.exports, glideFileName(new Date()));
      const result = await processing.glide(
        glideRequest({ inputPath: clip.rawPath, outputPath, spotlight: 'off', recipe: null, watermark: settings.watermark, scene: framed.scene, speed: clip.speed, lively: settings.livelyColour }),
        { signal: controller.signal, onProgress: setCrafting },
      );
      const galleryUri = await getStorageService()
        .saveVideoToGallery(result.outputPath)
        .catch(() => null);
      setClip({ ...result, rawPath: clip.rawPath, galleryUri });
      setSaved(galleryUri !== null);
      setView('glide');
      setFailed(false);
      setScenePanel(false);
      setMessage({ text: tx('glide_preview_you_re_in_the_new'), tone: 'success' });
    } catch (e) {
      const code = errorCode(e);
      if (code === 'PREMIUM_REQUIRED') setPremiumOpen(true);
      else if (code !== 'CANCELLED') {
        log.warn('scene glide failed', e);
        setMessage({ text: friendlyMessage(code, 'The clip couldn’t be made in that scene. Please try again.'), tone: 'danger' });
      }
    } finally {
      abort.current = null;
      setCrafting(null);
    }
  };

  return (
    <Screen
      title="Glide"
      overline={tx('glide_preview_s', { value: (clip.durationMs / 1000).toFixed(1), width: clip.width, height: clip.height })}
      onBack={() => navigation.goBack()}
      headerRight={
        <IconButton icon="share" label={tx('glide_preview_share_video')} onPress={() => void share()} size={44} testID="share-video" />
      }
      bottomInset={32}
    >
      <View style={{ gap: 16 }}>
        <SegmentedControl
          label={tx('glide_preview_view')}
          value={view}
          onChange={v => {
            setFailed(false);
            setView(v);
          }}
          options={[
            { value: 'glide', label: 'Glide' },
            { value: 'before', label: tx('glide_preview_as_recorded') },
          ]}
        />

        <View style={{ width: '100%', aspectRatio: aspect, borderRadius: 18, overflow: 'hidden' }}>
          {failed ? (
            <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', backgroundColor: '#000' }}>
              <AppText variant="body" tone="secondary" align="center">
                {tx('glide_preview_this_clip_can_t_be')}
              </AppText>
            </View>
          ) : (
            <VideoView
              key={view}
              source={view === 'glide' ? clip.outputPath : clip.rawPath}
              muted={muted}
              accessibilityLabel={view === 'glide' ? tx('glide_preview_your_glide_clip') : tx('glide_preview_the_clip_as_recorded')}
              onError={() => setFailed(true)}
              style={{ flex: 1 }}
            />
          )}
        </View>

        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
          <AppText variant="caption" tone="secondary" style={{ flex: 1 }}>
            {view === 'glide' ? (clip.scene ? tx('glide_preview_in_a_new_scene_steadied') : tx('glide_preview_steadied_cleaned_and_sharpened')) : tx('glide_preview_before_glide')}
          </AppText>
          <Button
            label={muted ? tx('glide_preview_sound_off') : tx('glide_preview_sound_on')}
            variant="ghost"
            accessibilityHint={muted ? tx('glide_preview_turns_the_sound_on') : tx('glide_preview_turns_the_sound_off')}
            onPress={() => setMuted(m => !m)}
            testID="mute"
          />
        </View>

        <GlassSurface style={{ padding: 16, gap: 10 }}>
          <AppText variant="title3">{tx('glide_preview_what_glide_did')}</AppText>
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
            {steadier ? <Badge label={tx('glide_preview_steadier', { steadier })} tone="success" icon="check" /> : null}
            <Badge label={tx('glide_preview_cleaner_in_low_light')} tone="success" icon="check" />
            <Badge label={tx('glide_preview_sharper_detail')} tone="success" icon="check" />
            {clip.watermarked ? <Badge label={tx('glide_preview_photo_plus_mark')} tone="neutral" icon="info" /> : null}
          </View>
        </GlassSurface>

        <GlassSurface style={{ padding: 16, gap: 12 }}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <View style={{ flex: 1, gap: 2 }}>
              <AppText variant="title3">{tx('glide_preview_put_me_in_a_new')}</AppText>
              <AppText variant="caption" tone="secondary">
                {tx('glide_preview_the_background_is_replaced_in')}
              </AppText>
            </View>
            <Button
              label={scenePanel ? tx('glide_preview_close') : tx('glide_preview_choose')}
              icon={plan === 'premium' ? 'layers' : 'crown'}
              variant="glass"
              onPress={openScene}
              testID="glide-scene"
            />
          </View>
          {scenePanel ? (
            <>
              <BackgroundPicker value={background} mine={settings.myBackgrounds} onChoose={setBackground} transparent={false} />
              <ValueSlider
                label={tx('glide_preview_size')}
                value={size}
                min={SIZE_MIN}
                max={SIZE_MAX}
                step={0.02}
                onChange={setSize}
                format={v => `${Math.round(v * 100)}%`}
                testID="glide-scene-size"
              />
              <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
                <AppText variant="caption" tone={Math.abs(size - 1) < 0.05 ? 'accent' : 'secondary'} style={{ flex: 1 }}>
                  {Math.abs(size - 1) < 0.05 ? tx('glide_preview_auto_size_true_to_the') : size > 1 ? tx('glide_preview_larger_than_the_scene_gives') : tx('glide_preview_smaller_than_the_scene_gives')}
                </AppText>
                <Button label={tx('glide_preview_auto_size')} variant="ghost" icon="wand" disabled={Math.abs(size - 1) < 0.05} onPress={() => setSize(1)} testID="glide-scene-auto" />
              </View>
              {crafting !== null ? (
                <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
                  <AppText variant="body" style={{ flex: 1 }}>{tx('glide_preview_moving_you_into_the_scene', { value: Math.round(crafting * 100) })}</AppText>
                  <Button label={tx('glide_preview_stop')} variant="ghost" onPress={() => abort.current?.abort()} testID="glide-scene-stop" />
                </View>
              ) : (
                <Button label={tx('glide_preview_make_my_glide_here')} icon="sparkles" onPress={() => void makeScene()} fullWidth testID="glide-scene-make" />
              )}
            </>
          ) : null}
        </GlassSurface>

        {message ? (
          <Badge label={message.text} tone={message.tone} icon={message.tone === 'danger' ? 'alert' : 'check'} />
        ) : null}

        <Button label={tx('glide_preview_share')} icon="share" onPress={() => void share()} fullWidth testID="glide-share" />
        <Button
          label={saved ? tx('glide_preview_saved_to_gallery') : tx('glide_preview_save_to_gallery')}
          icon={saved ? 'check' : 'image'}
          variant="glass"
          loading={saving}
          disabled={saved}
          onPress={() => void save()}
          fullWidth
          testID="glide-save"
        />
      </View>
      <PremiumSheet feature={premiumOpen ? 'glideScene' : null} onClose={() => setPremiumOpen(false)} />
    </Screen>
  );
}
