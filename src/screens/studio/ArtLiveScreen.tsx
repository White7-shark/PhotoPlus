import React, { useEffect, useMemo, useRef, useState } from 'react';
import { ActivityIndicator, StyleSheet, View } from 'react-native';

import { AppText, Button, Screen } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { ArtRevealView } from '../../components/ArtRevealView';
import { t as tr, useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import { artistById, artistName } from '../../services/masters/catalog';
import type { ArtControls } from '../../services/masters/controls';
import { MastersError, getMastersService, type Painting } from '../../services/masters/MastersService';
import { LIVE_MS, type ArtMode, type PencilOptions } from '../../services/masters/pencil';
import type { MastersLevel } from '../../services/masters/catalog';
import { saveWork } from '../../services/projects/studios';
import { recordActivity } from '../../services/rewards/RewardsService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';

/** "Normal", "Slow · 0.5×", "Fast · 2×". */
export function speedLabel(v: number): string {
  const x = `${(Math.round(v * 10) / 10).toString()}×`;
  return Math.abs(v - 1) < 0.05 ? tr('live_speed_normal') : v < 1 ? `${tr('live_speed_slow')} · ${x}` : `${tr('live_speed_fast')} · ${x}`;
}

/** A Photo Arts job: everything the work is made with (kept with the work, to edit it again). */
export interface ArtJob {
  artist: number;
  level: MastersLevel;
  seed: number;
  mode: ArtMode;
  controls?: ArtControls;
  pencil?: PencilOptions;
  frame: number;
  signature?: Record<string, unknown>;
}

/**
 * The live studio: the blank sheet, and the master's pencil (or brush)
 * drawing the work stroke by stroke — fast enough not to keep anyone
 * waiting, slow enough to watch. The finished full-size work is made at the
 * same time; when both are done it is kept in the project and its preview opens.
 */
export function ArtLiveScreen({ navigation, route }: RootScreenProps<'ArtLive'>) {
  const theme = useTheme();
  const t = useT();
  const repos = useAppStore(s => s.repos);
  const savedSpeed = useAppStore(s => s.settings.artLiveSpeed);
  const updateSetting = useAppStore(s => s.updateSetting);
  // The person's own pace: slower to savour every stroke, faster not to wait.
  const [speed, setSpeed] = useState(savedSpeed);
  const speedRef = useRef(savedSpeed);
  const masters = useMemo(() => getMastersService(), []);
  const { photo, projectId, title } = route.params;
  const job = route.params.job as unknown as ArtJob;
  const name = artistName(artistById(job.artist));
  const [live, setLive] = useState<Painting | null>(null);
  const [full, setFull] = useState<Painting | null>(null);
  const [revealed, setRevealed] = useState(false);
  const [skip, setSkip] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const saving = useRef(false);

  // The quick preview to draw live (with its blank sheet), and the finished work, both at once.
  useEffect(() => {
    let alive = true;
    const extras = { mode: job.mode, pencil: job.pencil, signature: job.signature };
    masters
      .paint(photo, job.artist, job.level, 'preview', job.seed, undefined, job.controls, job.frame, {
        ...extras,
        live: true,
      })
      .then(p => {
        if (alive) {
          if (p.blankPath) setLive(p);
          else setRevealed(true);
        }
      })
      .catch(() => alive && setRevealed(true));
    masters
      .paint(photo, job.artist, job.level, 'full', job.seed, undefined, job.controls, job.frame, extras)
      .then(p => alive && setFull(p))
      .catch((e: unknown) => {
        if (!alive) return;
        if (e instanceof MastersError && (e.code === 'AD_REQUIRED' || e.code === 'PREMIUM_REQUIRED'))
          navigation.goBack();
        else setError(t('masters_failed'));
      });
    return () => {
      alive = false;
    };
    // One job per visit.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Drawn and finished: kept in the project, then its preview.
  useEffect(() => {
    if (!revealed || !full || saving.current || !repos) return;
    saving.current = true;
    void saveWork(repos, projectId, {
      path: full.outputPath,
      sourcePath: photo,
      title,
      width: full.width,
      height: full.height,
      settings: { ...job, ...(full.inner ? { inner: full.inner } : {}) },
    })
      .then(item => {
        recordActivity('studio', 'edit');
        navigation.replace('StudioItem', { itemId: item.id, fresh: true });
      })
      .catch(() => {
        saving.current = false;
        setError(t('masters_failed'));
      });
  }, [revealed, full, repos, projectId, photo, title, job, navigation, t]);

  const drawing = job.mode === 'pencil';
  const caption = error
    ? error
    : !live && !revealed
      ? drawing
        ? t('live_preparing_paper')
        : t('live_preparing_canvas')
      : revealed
        ? t('live_finishing')
        : drawing
          ? t('live_drawing', { artist: name })
          : t('live_painting', { artist: name });

  return (
    <Screen title={drawing ? t('pencil_title') : 'Photo Arts'} onBack={() => navigation.goBack()} bottomInset={24}>
      <View style={styles.wrap}>
        <View style={styles.stage} testID="art-live-stage">
          {live?.blankPath ? (
            <ArtRevealView
              work={live.outputPath}
              blank={live.blankPath}
              tool={drawing ? 'pencil' : 'brush'}
              artist={job.artist}
              inner={live.inner}
              durationMs={Math.round(LIVE_MS[job.mode] / speed)}
              finish={skip}
              onDone={() => setRevealed(true)}
              onError={() => setRevealed(true)}
              accessibilityLabel={t('live_label')}
              style={StyleSheet.absoluteFill}
            />
          ) : (
            <View style={styles.wait}>
              <ActivityIndicator color={theme.colors.accent} />
            </View>
          )}
        </View>
        <View style={styles.caption} accessibilityLiveRegion="polite">
          {revealed && !error ? <ActivityIndicator size="small" color={theme.colors.accent} /> : null}
          <AppText
            variant="bodyStrong"
            align="center"
            style={error ? { color: theme.colors.warning } : undefined}
            testID="art-live-caption"
          >
            {caption}
          </AppText>
        </View>
        {live && !revealed && !error ? (
          <ValueSlider
            label={drawing ? t('live_speed_drawing') : t('live_speed_painting')}
            value={speed}
            min={0.5}
            max={2.5}
            step={0.1}
            onChange={v => {
              speedRef.current = v;
              setSpeed(v);
            }}
            onEnd={() =>
              void updateSetting('artLiveSpeed', Math.round(speedRef.current * 10) / 10).catch(() => undefined)
            }
            format={v => speedLabel(v)}
            testID="art-live-speed"
          />
        ) : null}
        {error ? (
          <Button label={t('common_back')} variant="glass" onPress={() => navigation.goBack()} testID="art-live-back" />
        ) : live && !revealed ? (
          <Button
            label={t('live_skip')}
            icon="chevronRight"
            variant="ghost"
            onPress={() => setSkip(true)}
            testID="art-live-skip"
          />
        ) : null}
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  wrap: { flex: 1, gap: 14, minHeight: 520 },
  stage: { flex: 1, minHeight: 420, borderRadius: 18, overflow: 'hidden', backgroundColor: '#0E0E12' },
  wait: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  caption: { flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8 },
});
