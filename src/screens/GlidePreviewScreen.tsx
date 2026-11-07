import React, { useState } from 'react';
import { View } from 'react-native';

import { AppText, Badge, Button, GlassSurface, IconButton, Screen, SegmentedControl } from '../components';
import { VideoView } from '../components/VideoView';
import type { RootScreenProps } from '../navigation/types';
import { errorCode } from '../native';
import { steadierPercent } from '../services/camera/glide';
import { friendlyMessage } from '../services/errors';
import { createLogger } from '../services/logging/logger';
import { getStorageService } from '../services/storage/StorageService';

const log = createLogger('glide-preview');

type ClipView = 'glide' | 'before';

/** A finished Glide clip: play it, compare with the clip as recorded, share or save. */
export function GlidePreviewScreen({ navigation, route }: RootScreenProps<'GlidePreview'>) {
  const { clip } = route.params;
  const [view, setView] = useState<ClipView>('glide');
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
      setMessage({ text: 'Saved to your gallery', tone: 'success' });
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

  return (
    <Screen
      title="Glide"
      overline={`${(clip.durationMs / 1000).toFixed(1)} s · ${clip.width}×${clip.height}`}
      onBack={() => navigation.goBack()}
      headerRight={
        <IconButton icon="share" label="Share video" onPress={() => void share()} size={44} testID="share-video" />
      }
      bottomInset={32}
    >
      <View style={{ gap: 16 }}>
        <SegmentedControl
          label="View"
          value={view}
          onChange={v => {
            setFailed(false);
            setView(v);
          }}
          options={[
            { value: 'glide', label: 'Glide' },
            { value: 'before', label: 'As recorded' },
          ]}
        />

        <View style={{ width: '100%', aspectRatio: aspect, borderRadius: 18, overflow: 'hidden' }}>
          {failed ? (
            <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', backgroundColor: '#000' }}>
              <AppText variant="body" tone="secondary" align="center">
                This clip can’t be played.
              </AppText>
            </View>
          ) : (
            <VideoView
              key={view}
              source={view === 'glide' ? clip.outputPath : clip.rawPath}
              muted={muted}
              accessibilityLabel={view === 'glide' ? 'Your Glide clip' : 'The clip as recorded'}
              onError={() => setFailed(true)}
              style={{ flex: 1 }}
            />
          )}
        </View>

        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
          <AppText variant="caption" tone="secondary" style={{ flex: 1 }}>
            {view === 'glide' ? 'Steadied, cleaned and sharpened' : 'Before Glide'}
          </AppText>
          <Button
            label={muted ? 'Sound off' : 'Sound on'}
            variant="ghost"
            accessibilityHint={muted ? 'Turns the sound on' : 'Turns the sound off'}
            onPress={() => setMuted(m => !m)}
            testID="mute"
          />
        </View>

        <GlassSurface style={{ padding: 16, gap: 10 }}>
          <AppText variant="title3">What Glide did</AppText>
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
            {steadier ? <Badge label={`${steadier}% steadier`} tone="success" icon="check" /> : null}
            <Badge label="Cleaner in low light" tone="success" icon="check" />
            <Badge label="Sharper detail" tone="success" icon="check" />
            {clip.watermarked ? <Badge label="Photo Plus mark" tone="neutral" icon="info" /> : null}
          </View>
        </GlassSurface>

        {message ? (
          <Badge label={message.text} tone={message.tone} icon={message.tone === 'danger' ? 'alert' : 'check'} />
        ) : null}

        <Button label="Share" icon="share" onPress={() => void share()} fullWidth testID="glide-share" />
        <Button
          label={saved ? 'Saved to gallery' : 'Save to gallery'}
          icon={saved ? 'check' : 'image'}
          variant="glass"
          loading={saving}
          disabled={saved}
          onPress={() => void save()}
          fullWidth
          testID="glide-save"
        />
      </View>
    </Screen>
  );
}
