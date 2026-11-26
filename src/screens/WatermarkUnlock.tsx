import React, { useCallback, useEffect, useState } from 'react';
import { View } from 'react-native';

import { AppText, Button, Icon } from '../components';
import { getAdsService, type RewardProgress } from '../services/ads/AdsService';
import { useTheme } from '../theme';
import { t as tx } from '../i18n';

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
      if (!r.rewarded) setNote(tx('watermark_unlock_watch_the_ad_to_the'));
      if (r.unlocked) onUnlocked?.();
    } catch {
      setNote(tx('watermark_unlock_no_ad_is_available_right'));
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
        <AppText variant="caption">{tx('watermark_unlock_this_photo_goes_out_without')}</AppText>
      </View>
    );
  }

  return (
    <View style={{ gap: 8 }} testID={testID}>
      <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}>
        <AppText variant="bodyStrong">{tx('watermark_unlock_remove_the_watermark')}</AppText>
        <View
          style={{ flexDirection: 'row', gap: 5 }}
          accessibilityLabel={tx('watermark_unlock_of_ads_watched', { watched, required })}
        >
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
          ? tx('watermark_unlock_watch_short_ads_to_share', { required })
          : tx('watermark_unlock_more_to_go_your_progress', { value: required - watched })}
      </AppText>
      <Button
        label={
          watched === 0
            ? tx('watermark_unlock_watch_an_ad')
            : tx('watermark_unlock_watch_ad_of', { value: watched + 1, required })
        }
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
