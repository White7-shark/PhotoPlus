import React, { useCallback, useEffect, useState } from 'react';
import { View } from 'react-native';

import { AppText, Button, Icon } from '../components';
import { getAdsService, type RewardProgress } from '../services/ads/AdsService';
import { useTheme } from '../theme';

/**
 * Free plan: watch four short ads to share, save or export this photo
 * without the watermark. Progress is kept per photo (people can stop and
 * come back) and counted natively; the export checks it natively too.
 */
export function WatermarkUnlock({
  photoPath,
  onUnlocked,
  testID = 'watermark-unlock',
}: {
  /** The photo's untouched copy. */
  photoPath: string;
  onUnlocked?: () => void;
  testID?: string;
}) {
  const theme = useTheme();
  const ads = getAdsService();
  const [progress, setProgress] = useState<RewardProgress | null>(null);
  const [watching, setWatching] = useState(false);
  const [note, setNote] = useState<string | null>(null);

  useEffect(() => {
    let live = true;
    void ads.progress(photoPath).then(p => {
      if (live) setProgress(p);
    });
    return () => {
      live = false;
    };
  }, [ads, photoPath]);

  const watch = useCallback(async () => {
    setWatching(true);
    setNote(null);
    try {
      const r = await ads.watch(photoPath);
      setProgress(r);
      if (!r.rewarded) setNote('Watch the ad to the end for it to count.');
      if (r.unlocked) onUnlocked?.();
    } catch {
      setNote('No ad is available right now. Please try again in a moment.');
    } finally {
      setWatching(false);
    }
  }, [ads, onUnlocked, photoPath]);

  if (!ads.available || progress === null) return null;
  const { watched, required, unlocked } = progress;

  if (unlocked) {
    return (
      <View
        style={{ flexDirection: 'row', alignItems: 'center', gap: 8, justifyContent: 'center' }}
        testID={`${testID}-done`}
      >
        <Icon name="check" size={16} color={theme.colors.success} />
        <AppText variant="caption">This photo goes out without the watermark.</AppText>
      </View>
    );
  }

  return (
    <View style={{ gap: 8 }} testID={testID}>
      <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}>
        <AppText variant="bodyStrong">Remove the watermark</AppText>
        <View style={{ flexDirection: 'row', gap: 5 }} accessibilityLabel={`${watched} of ${required} ads watched`}>
          {Array.from({ length: required }, (_, i) => (
            <View
              key={i}
              style={{
                width: 9,
                height: 9,
                borderRadius: 5,
                backgroundColor: i < watched ? theme.colors.premium : theme.colors.glassStroke,
              }}
            />
          ))}
        </View>
      </View>
      <AppText variant="caption" tone="secondary">
        {watched === 0
          ? `Watch ${required} short ads to share or save this photo without the Photo Plus mark.`
          : `${required - watched} more to go. Your progress is kept for this photo.`}
      </AppText>
      <Button
        label={watched === 0 ? 'Watch an ad' : `Watch ad ${watched + 1} of ${required}`}
        icon="play"
        variant="glass"
        loading={watching}
        disabled={watching}
        onPress={() => void watch()}
        testID={`${testID}-watch`}
      />
      {note ? (
        <AppText variant="caption" tone="secondary" align="center">
          {note}
        </AppText>
      ) : null}
    </View>
  );
}
