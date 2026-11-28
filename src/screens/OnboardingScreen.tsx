/**
 * The first run: what Photo Plus is about, a page for each part of the app,
 * each with its own small animation, and at the end the 7-day free trial.
 *
 * The trial: in testing builds it is simulated on the phone (Premium for seven
 * days, once, on the trusted clock); in the store build Google Play Billing
 * starts it from the Premium page (the subscription's free-trial offer).
 */
import React, { useEffect, useMemo, useRef, useState } from 'react';
import {
  Animated,
  ScrollView,
  StyleSheet,
  View,
  useWindowDimensions,
  type NativeScrollEvent,
  type NativeSyntheticEvent,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { AppText, AuroraBackground, Button, GlassSurface, Icon, LogoFull, type IconName } from '../components';
import { useReducedMotion } from '../hooks/useReducedMotion';
import { t, type StringKey } from '../i18n';
import type { RootScreenProps } from '../navigation/types';
import { TRIAL_DAYS } from '../services/billing/plans';
import { getPermissionService } from '../services/permissions/PermissionService';
import { getProcessingService } from '../services/processing/ImageProcessingService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';

type Art = 'welcome' | 'camera' | 'assist' | 'studio' | 'arts' | 'motion' | 'toon' | 'private' | 'trial';

interface Page {
  art: Art;
  title: StringKey;
  body: StringKey;
}

export const PAGES: readonly Page[] = [
  { art: 'welcome', title: 'onb_welcome_title', body: 'onb_welcome_body' },
  { art: 'camera', title: 'onb_camera_title', body: 'onb_camera_body' },
  { art: 'assist', title: 'onb_assist_title', body: 'onb_assist_body' },
  { art: 'studio', title: 'onb_studio_title', body: 'onb_studio_body' },
  { art: 'arts', title: 'onb_arts_title', body: 'onb_arts_body' },
  { art: 'motion', title: 'onb_motion_title', body: 'onb_motion_body' },
  { art: 'toon', title: 'onb_toon_title', body: 'onb_toon_body' },
  { art: 'private', title: 'onb_private_title', body: 'onb_private_body' },
  { art: 'trial', title: 'onb_trial_title', body: 'onb_trial_body' },
];

/** A value going 0 → 1 → 0 forever (or still, when motion is reduced). */
function useLoop(duration: number, reduced: boolean, still = 0.5) {
  const [v] = useState(() => new Animated.Value(reduced ? still : 0));
  useEffect(() => {
    if (reduced) {
      v.setValue(still);
      return undefined;
    }
    const loop = Animated.loop(
      Animated.sequence([
        Animated.timing(v, {
          toValue: 1,
          duration,
          easing: (x: number) => (x < 0.5 ? 2 * x * x : 1 - (-2 * x + 2) ** 2 / 2),
          useNativeDriver: true,
        }),
        Animated.timing(v, {
          toValue: 0,
          duration,
          easing: (x: number) => (x < 0.5 ? 2 * x * x : 1 - (-2 * x + 2) ** 2 / 2),
          useNativeDriver: true,
        }),
      ]),
    );
    loop.start();
    return () => loop.stop();
  }, [v, duration, reduced, still]);
  return v;
}

/** A value going 0 → 1 and starting again (a turn, a strip running by). */
function useCycle(duration: number, reduced: boolean) {
  const [v] = useState(() => new Animated.Value(0));
  useEffect(() => {
    if (reduced) return undefined;
    const loop = Animated.loop(
      Animated.timing(v, { toValue: 1, duration, easing: (x: number) => x, useNativeDriver: true }),
    );
    loop.start();
    return () => loop.stop();
  }, [v, duration, reduced]);
  return v;
}

const ART_SIZE = 220;

function Bubble({ icon, size = 56, colour }: { icon: IconName; size?: number; colour?: string }) {
  const theme = useTheme();
  return (
    <View
      style={{
        width: size,
        height: size,
        borderRadius: size / 2,
        alignItems: 'center',
        justifyContent: 'center',
        backgroundImage: colour ?? theme.gradients.spectrum,
      }}
    >
      <Icon name={icon} size={size * 0.46} color="#FFFFFF" />
    </View>
  );
}

/** Each page's moving picture. */
function PageArt({ art, reduced }: { art: Art; reduced: boolean }) {
  const theme = useTheme();
  const pulse = useLoop(1400, reduced);
  const slow = useLoop(2600, reduced);
  const turn = useCycle(9000, reduced);
  const run = useCycle(3200, reduced);
  const spin = turn.interpolate({ inputRange: [0, 1], outputRange: ['0deg', '360deg'] });
  const unspin = turn.interpolate({ inputRange: [0, 1], outputRange: ['0deg', '-360deg'] });
  const scale = pulse.interpolate({ inputRange: [0, 1], outputRange: [0.92, 1.06] });
  const glow = pulse.interpolate({ inputRange: [0, 1], outputRange: [0.35, 0.9] });

  switch (art) {
    case 'welcome':
      // The app's three worlds orbiting its name.
      return (
        <View style={styles.art}>
          <Animated.View style={[StyleSheet.absoluteFill, { transform: [{ rotate: spin }] }]}>
            {(['camera', 'sparkles', 'brush'] as IconName[]).map((icon, i) => {
              const a = (i / 3) * 2 * Math.PI;
              return (
                <Animated.View
                  key={icon}
                  style={{
                    position: 'absolute',
                    left: ART_SIZE / 2 - 24 + Math.cos(a) * 84,
                    top: ART_SIZE / 2 - 24 + Math.sin(a) * 84,
                    transform: [{ rotate: unspin }],
                  }}
                >
                  <Bubble icon={icon} size={48} />
                </Animated.View>
              );
            })}
          </Animated.View>
          <Animated.View style={{ transform: [{ scale }] }}>
            <LogoFull size={120} />
          </Animated.View>
        </View>
      );
    case 'camera': {
      // A viewfinder finding focus, the shutter breathing.
      const corner = (rot: string, pos: object) => (
        <View style={[styles.corner, pos, { transform: [{ rotate: rot }], borderColor: theme.colors.accent }]} />
      );
      return (
        <View style={styles.art}>
          <Animated.View style={[styles.finder, { transform: [{ scale }] }]}>
            {corner('0deg', { left: 0, top: 0 })}
            {corner('90deg', { right: 0, top: 0 })}
            {corner('180deg', { right: 0, bottom: 0 })}
            {corner('270deg', { left: 0, bottom: 0 })}
          </Animated.View>
          <Animated.View style={[styles.focus, { opacity: glow, borderColor: '#FFD66B' }]} />
          <Animated.View style={{ position: 'absolute', bottom: 6, transform: [{ scale }] }}>
            <Bubble icon="camera" size={52} />
          </Animated.View>
        </View>
      );
    }
    case 'assist': {
      // A spotlight sweeping onto the person, the guide rings pulsing.
      const sweep = slow.interpolate({ inputRange: [0, 1], outputRange: ['-24deg', '24deg'] });
      return (
        <View style={styles.art}>
          <Animated.View style={[styles.beam, { transform: [{ rotate: sweep }] }]} />
          <Animated.View style={[styles.ring, { opacity: glow, transform: [{ scale }] }]} />
          <Bubble icon="figure" size={70} />
        </View>
      );
    }
    case 'studio': {
      // Scenes swapping behind the person.
      const scenes = [
        'linear-gradient(135deg, #5B8DEF, #A6E3FF)',
        'linear-gradient(135deg, #F7A35C, #F25F8B)',
        'linear-gradient(135deg, #3B3F7A, #8E7BE6)',
      ];
      return (
        <View style={styles.art}>
          {scenes.map((bg, i) => {
            const x = run.interpolate({
              inputRange: [0, 1],
              outputRange: [i * 26 - 26, i * 26 - 26 + 26 * (i === 2 ? -2 : 1)],
            });
            return (
              <Animated.View
                key={bg}
                style={[
                  styles.card,
                  {
                    backgroundImage: bg,
                    transform: [{ translateX: reduced ? i * 18 - 18 : x }, { rotate: `${(i - 1) * 8}deg` }],
                  },
                ]}
              />
            );
          })}
          <View style={{ position: 'absolute' }}>
            <Bubble icon="layers" size={58} />
          </View>
        </View>
      );
    }
    case 'arts': {
      // A brush crossing the canvas, the painting appearing behind it.
      const x = slow.interpolate({ inputRange: [0, 1], outputRange: [-80, 80] });
      const cover = slow.interpolate({ inputRange: [0, 1], outputRange: [0, 170] });
      return (
        <View style={styles.art}>
          <View style={[styles.canvas, { backgroundImage: 'linear-gradient(120deg, #2E5E8E, #C9A227 45%, #B33A3A)' }]}>
            <Animated.View style={[styles.paper, { transform: [{ translateX: cover }] }]} />
          </View>
          <Animated.View style={{ position: 'absolute', transform: [{ translateX: x }, { rotate: '-20deg' }] }}>
            <Bubble icon="brush" size={50} colour={theme.gradients.premium} />
          </Animated.View>
        </View>
      );
    }
    case 'motion': {
      // Layers moving at their own depth (parallax), as a 3D camera move does.
      const layer = (amp: number) => slow.interpolate({ inputRange: [0, 1], outputRange: [-amp, amp] });
      return (
        <View style={styles.art}>
          <Animated.View
            style={[
              styles.layer,
              { backgroundImage: 'linear-gradient(180deg, #23366E, #6C8BD8)', transform: [{ translateX: layer(6) }] },
            ]}
          />
          <Animated.View
            style={[styles.hill, { backgroundColor: '#3E7C5A', transform: [{ translateX: layer(16) }] }]}
          />
          <Animated.View style={{ position: 'absolute', bottom: 34, transform: [{ translateX: layer(30) }] }}>
            <Bubble icon="motion" size={54} />
          </Animated.View>
        </View>
      );
    }
    case 'toon': {
      // Photo on one side, cartoon on the other, the line between them gliding.
      const x = slow.interpolate({ inputRange: [0, 1], outputRange: [-60, 60] });
      return (
        <View style={styles.art}>
          <View style={[styles.canvas, { backgroundImage: 'linear-gradient(160deg, #8a6e5c, #c7a58c 50%, #5b4a3f)' }]}>
            <Animated.View
              style={[
                styles.toonSide,
                {
                  transform: [{ translateX: x }],
                  backgroundImage: 'linear-gradient(160deg, #F2B880 0 33%, #E0915A 33% 66%, #9C5B3B 66%)',
                },
              ]}
            />
            <Animated.View style={[styles.divider, { transform: [{ translateX: x }] }]} />
          </View>
          <View style={{ position: 'absolute', top: 18 }}>
            <Bubble icon="sparkles" size={44} colour={theme.gradients.premium} />
          </View>
        </View>
      );
    }
    case 'private':
      // The shield, and rewards popping up around it.
      return (
        <View style={styles.art}>
          <Animated.View style={[styles.ring, { opacity: glow, transform: [{ scale }] }]} />
          <Bubble icon="shield" size={78} />
          {[0, 1, 2].map(i => {
            const y = run.interpolate({ inputRange: [0, 1], outputRange: [10, -40] });
            const o = run.interpolate({ inputRange: [0, 0.2, 0.8, 1], outputRange: [0, 1, 1, 0] });
            return (
              <Animated.View
                key={i}
                style={{
                  position: 'absolute',
                  left: 46 + i * 58,
                  top: 40,
                  opacity: reduced ? 1 : o,
                  transform: [{ translateY: reduced ? 0 : y }],
                }}
              >
                <Icon name="crown" size={22} color="#F5C66B" />
              </Animated.View>
            );
          })}
        </View>
      );
    case 'trial':
      return (
        <View style={styles.art}>
          <Animated.View style={[styles.halo, { opacity: glow, transform: [{ scale }] }]} />
          <Bubble icon="crown" size={92} colour={theme.gradients.premium} />
        </View>
      );
  }
}

export function OnboardingScreen({ navigation }: RootScreenProps<'Onboarding'>) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const { width } = useWindowDimensions();
  const reduced = useReducedMotion();
  const updateSetting = useAppStore(s => s.updateSetting);
  const startFreeTrial = useAppStore(s => s.startFreeTrial);
  const [page, setPage] = useState(0);
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState<string | null>(null);
  const [simulated, setSimulated] = useState(false);
  const scroller = useRef<React.ComponentRef<typeof ScrollView>>(null);
  const last = PAGES.length - 1;

  useEffect(() => {
    let live = true;
    void getProcessingService()
      .trialState()
      .then(s => live && setSimulated(s.available))
      .catch(() => undefined);
    return () => {
      live = false;
    };
  }, []);

  const go = (i: number) => {
    const next = Math.max(0, Math.min(last, i));
    setPage(next);
    scroller.current?.scrollTo({ x: next * width, animated: !reduced });
  };
  const onScroll = (e: NativeSyntheticEvent<NativeScrollEvent>) => {
    const i = Math.round(e.nativeEvent.contentOffset.x / Math.max(1, width));
    if (i !== page) setPage(Math.max(0, Math.min(last, i)));
  };

  /** Done: remember it, ask for the camera, and open the app (or the Premium page to start the trial there). */
  const finish = async (then?: 'Premium') => {
    await getPermissionService()
      .ensure('camera')
      .catch(() => undefined);
    await updateSetting('onboardingCompleted', true).catch(() => undefined);
    navigation.reset({ index: 0, routes: [{ name: 'Main' }] });
    if (then) navigation.navigate(then);
  };
  const trial = async () => {
    setBusy(true);
    setNote(null);
    try {
      if (!simulated) {
        // The store build: Google Play Billing starts the trial from the Premium page.
        await finish('Premium');
        return;
      }
      const started = await startFreeTrial().catch(() => false);
      if (!started) {
        setNote(t('onb_trial_used'));
        return;
      }
      await finish();
    } finally {
      setBusy(false);
    }
  };
  const free = async () => {
    setBusy(true);
    try {
      await finish();
    } finally {
      setBusy(false);
    }
  };

  const points = useMemo(() => [t('onb_trial_point_tools'), t('onb_trial_point_ads'), t('onb_trial_point_cancel')], []);

  return (
    <View style={{ flex: 1, paddingTop: insets.top + 12, paddingBottom: insets.bottom + 16 }} testID="onboarding">
      <AuroraBackground />
      <View style={styles.top}>
        <AppText variant="caption" tone="tertiary" accessibilityLiveRegion="polite">
          {t('onb_page', { n: page + 1, total: PAGES.length })}
        </AppText>
        {page < last ? (
          <Button label={t('onb_skip')} variant="ghost" onPress={() => go(last)} testID="onboarding-skip" />
        ) : null}
      </View>
      <ScrollView
        ref={scroller}
        horizontal
        pagingEnabled
        showsHorizontalScrollIndicator={false}
        onMomentumScrollEnd={onScroll}
        scrollEventThrottle={16}
        style={{ flex: 1 }}
      >
        {PAGES.map((p, i) => (
          <View key={p.art} style={[styles.page, { width }]} testID={`onboarding-page-${i}`} accessible={i === page}>
            {/* Only the page on show (and its neighbours) animates. */}
            {Math.abs(i - page) <= 1 ? (
              <PageArt art={p.art} reduced={reduced || i !== page} />
            ) : (
              <View style={styles.art} />
            )}
            <AppText variant="title1" align="center" style={{ marginTop: 18 }} accessibilityRole="header">
              {p.art === 'trial' ? t('onb_trial_title', { days: TRIAL_DAYS }) : t(p.title)}
            </AppText>
            <AppText variant="body" tone="secondary" align="center" style={{ marginTop: 10, maxWidth: 340 }}>
              {p.art === 'trial' ? t('onb_trial_body', { days: TRIAL_DAYS }) : t(p.body)}
            </AppText>
            {p.art === 'trial' ? (
              <GlassSurface radius={18} style={styles.points}>
                {points.map(line => (
                  <View key={line} style={{ flexDirection: 'row', gap: 10, alignItems: 'center' }}>
                    <Icon name="check" size={16} color={theme.colors.accent} />
                    <AppText variant="callout" style={{ flex: 1 }}>
                      {line}
                    </AppText>
                  </View>
                ))}
              </GlassSurface>
            ) : null}
          </View>
        ))}
      </ScrollView>
      <View style={styles.dots} accessibilityElementsHidden>
        {PAGES.map((p, i) => (
          <View
            key={p.art}
            style={[
              styles.dot,
              {
                width: i === page ? 22 : 8,
                backgroundColor: i === page ? theme.colors.accent : 'rgba(255,255,255,0.25)',
              },
            ]}
          />
        ))}
      </View>
      <View style={styles.actions}>
        {page < last ? (
          <Button
            label={t('onb_next')}
            size="lg"
            icon="chevronRight"
            fullWidth
            onPress={() => go(page + 1)}
            testID="onboarding-next"
          />
        ) : (
          <>
            <Button
              label={t('onb_trial_cta', { days: TRIAL_DAYS })}
              variant="premium"
              size="lg"
              icon="crown"
              fullWidth
              loading={busy}
              onPress={() => void trial()}
              testID="onboarding-trial"
            />
            <Button
              label={t('onb_trial_free')}
              variant="ghost"
              fullWidth
              disabled={busy}
              onPress={() => void free()}
              testID="onboarding-free"
            />
            {note ? (
              <AppText variant="caption" tone="secondary" align="center" testID="onboarding-note">
                {note}
              </AppText>
            ) : null}
            {simulated ? (
              <AppText variant="caption" tone="tertiary" align="center" testID="onboarding-test-note">
                {t('onb_trial_test_note')}
              </AppText>
            ) : null}
          </>
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  top: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 20,
    minHeight: 40,
  },
  page: { alignItems: 'center', justifyContent: 'center', paddingHorizontal: 24 },
  art: { width: ART_SIZE, height: ART_SIZE, alignItems: 'center', justifyContent: 'center' },
  finder: { width: 150, height: 150 },
  corner: {
    position: 'absolute',
    width: 34,
    height: 34,
    borderTopWidth: 4,
    borderLeftWidth: 4,
    borderTopLeftRadius: 10,
  },
  focus: { position: 'absolute', width: 54, height: 54, borderWidth: 2, borderRadius: 8 },
  beam: {
    position: 'absolute',
    top: -10,
    width: 120,
    height: 210,
    borderRadius: 60,
    backgroundImage: 'linear-gradient(180deg, rgba(255,230,160,0.75), rgba(255,230,160,0))',
  },
  ring: {
    position: 'absolute',
    width: 150,
    height: 150,
    borderRadius: 75,
    borderWidth: 2,
    borderColor: 'rgba(245,198,107,0.8)',
  },
  halo: { position: 'absolute', width: 180, height: 180, borderRadius: 90, backgroundColor: 'rgba(245,198,107,0.25)' },
  card: {
    position: 'absolute',
    width: 120,
    height: 160,
    borderRadius: 18,
    borderWidth: 2,
    borderColor: 'rgba(255,255,255,0.35)',
  },
  canvas: { width: 180, height: 180, borderRadius: 20, overflow: 'hidden' },
  paper: { position: 'absolute', left: 0, top: 0, bottom: 0, width: 180, backgroundColor: '#F4EFE6' },
  layer: { position: 'absolute', width: 200, height: 170, borderRadius: 22 },
  hill: {
    position: 'absolute',
    bottom: 25,
    width: 230,
    height: 70,
    borderTopLeftRadius: 120,
    borderTopRightRadius: 120,
  },
  toonSide: { position: 'absolute', left: 90, top: 0, bottom: 0, width: 200 },
  divider: { position: 'absolute', left: 89, top: 0, bottom: 0, width: 3, backgroundColor: '#FFFFFF' },
  points: { marginTop: 18, padding: 16, gap: 10, alignSelf: 'stretch' },
  dots: { flexDirection: 'row', justifyContent: 'center', gap: 6, marginVertical: 14 },
  dot: { height: 8, borderRadius: 4 },
  actions: { paddingHorizontal: 20, gap: 8 },
});
