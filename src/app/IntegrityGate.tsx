import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ActivityIndicator,
  AppState,
  BackHandler,
  PermissionsAndroid,
  Platform,
  Pressable,
  StyleSheet,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { AppText, Button, GlassSurface, Icon } from '../components';
import { AuroraBackground } from '../components/AuroraBackground';
import type { IconName } from '../components/Icon';
import {
  formatDay,
  getIntegrityService,
  OK_STATE,
  skewWords,
  type BlockReason,
  type IntegrityState,
} from '../services/integrity/IntegrityService';
import { useTheme } from '../theme';

/** How often the app checks online while it is open (it also checks on opening and in the background). */
export const CHECK_EVERY_MS = 30 * 60_000;

const COPY: Record<BlockReason, { icon: IconName; title: string }> = {
  time: { icon: 'timer', title: 'Your phone’s date and time are wrong' },
  offline: { icon: 'shield', title: 'Connect to the internet' },
  update: { icon: 'download', title: 'Update Photo Plus' },
};

function reasonText(s: IntegrityState): string {
  switch (s.reason) {
    case 'time':
      return `Photo Plus needs your phone’s correct date and time. Your clock is ${skewWords(s.skewMinutes)}. Set the date and time to automatic, then tap Check time.`;
    case 'offline':
      return `Photo Plus needs to go online at least once every ${s.maxOfflineDays} days to check the app’s integrity. It has been ${Math.floor(s.offlineDays)} days. Turn on Wi-Fi or mobile data, then tap Check connection.`;
    case 'update':
      return 'This version of Photo Plus is out of date. The update period has ended: update now to keep using Photo Plus. Your photos and projects stay as they are.';
    default:
      return '';
  }
}

/**
 * The app's integrity gate, over everything: when the phone's time is
 * wrong, the app hasn't been online for a week, or an update is overdue, a
 * blocking screen covers the app (back does nothing) and says why, with
 * the way out — a check online that must succeed before the app opens
 * again. While an update waits, a banner asks (Update / Later) with its
 * deadline. Checks run on opening, on return to the app, every 30 minutes
 * while open and in the background.
 */
export function IntegrityGate({ children }: { children: React.ReactNode }) {
  const integrity = getIntegrityService();
  const [state, setState] = useState<IntegrityState>(OK_STATE);
  const [checking, setChecking] = useState(false);
  const [result, setResult] = useState<string | null>(null);
  const asked = useRef(false);

  const check = useCallback(
    async (force: boolean, manual = false) => {
      if (manual) {
        setChecking(true);
        setResult(null);
      }
      try {
        const next = await integrity.check(force);
        setState(next);
        if (manual) {
          if (!next.online) setResult('No internet connection. Connect to Wi-Fi or mobile data and try again.');
          else if (next.reason === 'time')
            setResult('Your phone’s time is still wrong. Set it to automatic and try again.');
          else if (next.reason === 'update') setResult('An update is still needed.');
        }
      } catch {
        if (manual) setResult('The check couldn’t run. Please try again.');
      } finally {
        if (manual) setChecking(false);
      }
    },
    [integrity],
  );

  useEffect(() => {
    let live = true;
    void integrity.state().then(s => {
      if (!live) return;
      setState(s);
      return check(false);
    });
    const off = integrity.onChange(s => live && setState(s));
    const app = AppState.addEventListener('change', st => {
      if (st === 'active') void check(false);
    });
    const timer = setInterval(() => void check(false), CHECK_EVERY_MS);
    return () => {
      live = false;
      off();
      app.remove();
      clearInterval(timer);
    };
  }, [integrity, check]);

  // Blocked: the back button stays on this screen.
  useEffect(() => {
    if (!state.blocked) return;
    const sub = BackHandler.addEventListener('hardwareBackPress', () => true);
    return () => sub.remove();
  }, [state.blocked]);

  // Update reminders are notifications: ask once, when an update is waiting.
  useEffect(() => {
    if (!state.update.pending || asked.current || Platform.OS !== 'android' || Number(Platform.Version) < 33) return;
    asked.current = true;
    void PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.POST_NOTIFICATIONS).catch(() => undefined);
  }, [state.update.pending]);

  const update = async () => {
    try {
      await integrity.startUpdate();
    } catch {
      setResult('Google Play couldn’t be opened. Please try again.');
    }
  };

  return (
    <View style={{ flex: 1 }}>
      <View style={{ flex: 1 }} importantForAccessibility={state.blocked ? 'no-hide-descendants' : 'auto'}>
        {children}
      </View>
      {!state.blocked && state.update.banner ? (
        <UpdateBanner
          deadline={state.update.deadline}
          now={state.now}
          onUpdate={update}
          onLater={() =>
            void integrity
              .later()
              .then(setState)
              .catch(() => undefined)
          }
        />
      ) : null}
      {state.blocked && state.reason ? (
        <BlockScreen
          state={state}
          checking={checking}
          result={result}
          onCheck={() => void check(true, true)}
          onSettings={() => integrity.openDateSettings()}
          onUpdate={update}
        />
      ) : null}
    </View>
  );
}

export function BlockScreen({
  state,
  checking,
  result,
  onCheck,
  onSettings,
  onUpdate,
}: {
  state: IntegrityState;
  checking: boolean;
  result: string | null;
  onCheck: () => void;
  onSettings: () => void;
  onUpdate: () => void;
}) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const reason = state.reason ?? 'offline';
  const copy = COPY[reason];
  return (
    <View
      style={[
        StyleSheet.absoluteFill,
        { backgroundColor: theme.colors.background, paddingTop: insets.top + 24, paddingBottom: insets.bottom + 24 },
      ]}
      accessibilityViewIsModal
      testID="integrity-block"
      onStartShouldSetResponder={() => true}
    >
      <AuroraBackground />
      <View style={styles.blockBody}>
        <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
          <Icon name={copy.icon} size={30} color="#1A1206" strokeWidth={2} />
        </View>
        <AppText variant="title2" align="center" accessibilityRole="header" testID="integrity-title">
          {copy.title}
        </AppText>
        <AppText variant="body" tone="secondary" align="center" testID="integrity-reason">
          {reasonText(state)}
        </AppText>
        <GlassSurface radius={16} style={styles.why}>
          <AppText variant="caption" tone="tertiary" align="center">
            {reason === 'update'
              ? 'Updates keep your photos safe and the app working as it should.'
              : 'Photo Plus checks the time and its integrity online to keep rewards, Premium time and your account fair and secure.'}
          </AppText>
        </GlassSurface>
        {result ? (
          <AppText
            variant="callout"
            align="center"
            style={{ color: theme.colors.warning }}
            accessibilityLiveRegion="assertive"
            testID="integrity-result"
          >
            {result}
          </AppText>
        ) : null}
        {checking ? <ActivityIndicator color={theme.colors.accent} /> : null}
      </View>
      <View style={styles.actions}>
        {reason === 'update' ? (
          <>
            <Button
              label="Update now"
              icon="download"
              variant="premium"
              size="lg"
              onPress={onUpdate}
              testID="integrity-update"
            />
            <Button
              label="I’ve updated · check again"
              variant="ghost"
              onPress={onCheck}
              disabled={checking}
              testID="integrity-check"
            />
          </>
        ) : reason === 'time' ? (
          <>
            <Button
              label="Check time"
              icon="timer"
              size="lg"
              onPress={onCheck}
              loading={checking}
              disabled={checking}
              testID="integrity-check"
            />
            <Button
              label="Open date & time settings"
              variant="glass"
              onPress={onSettings}
              testID="integrity-settings"
            />
          </>
        ) : (
          <Button
            label="Check connection"
            icon="shield"
            size="lg"
            onPress={onCheck}
            loading={checking}
            disabled={checking}
            testID="integrity-check"
          />
        )}
      </View>
    </View>
  );
}

export function UpdateBanner({
  deadline,
  now,
  onUpdate,
  onLater,
}: {
  deadline: number;
  /** The true time (from the device). */
  now: number;
  onUpdate: () => void;
  onLater: () => void;
}) {
  const theme = useTheme();
  const insets = useSafeAreaInsets();
  const days = Math.max(0, Math.ceil((deadline - now) / 86_400_000));
  return (
    <View style={[styles.bannerWrap, { top: insets.top + 6 }]} pointerEvents="box-none" testID="update-banner">
      <GlassSurface intensity="strong" radius={18} style={styles.banner} glow="rgba(245,198,107,0.25)">
        <View style={[styles.bannerIcon, { backgroundImage: theme.gradients.premium }]}>
          <Icon name="download" size={16} color="#1A1206" />
        </View>
        <View style={{ flex: 1 }}>
          <AppText variant="caption" style={{ fontWeight: '700' }}>
            A new version of Photo Plus is ready
          </AppText>
          <AppText variant="caption" tone="secondary">
            Update by {formatDay(deadline)}
            {days <= 2 ? ` · ${days === 0 ? 'last day' : `${days} ${days === 1 ? 'day' : 'days'} left`}` : ''}
          </AppText>
        </View>
        <Pressable
          onPress={onLater}
          accessibilityRole="button"
          accessibilityLabel="Later"
          hitSlop={8}
          testID="update-later"
          style={styles.later}
        >
          <AppText variant="caption" tone="secondary">
            Later
          </AppText>
        </Pressable>
        <Pressable
          onPress={onUpdate}
          accessibilityRole="button"
          accessibilityLabel="Update"
          testID="update-now"
          style={[styles.updateBtn, { backgroundImage: theme.gradients.premium }]}
        >
          <AppText variant="caption" style={{ color: '#1A1206', fontWeight: '800' }}>
            Update
          </AppText>
        </Pressable>
      </GlassSurface>
    </View>
  );
}

const styles = StyleSheet.create({
  blockBody: { flex: 1, justifyContent: 'center', alignItems: 'center', gap: 16, paddingHorizontal: 28 },
  badge: { width: 68, height: 68, borderRadius: 34, alignItems: 'center', justifyContent: 'center' },
  why: { padding: 12, maxWidth: 420 },
  actions: { paddingHorizontal: 24, gap: 10 },
  bannerWrap: { position: 'absolute', left: 12, right: 12 },
  banner: { flexDirection: 'row', alignItems: 'center', gap: 10, padding: 10 },
  bannerIcon: { width: 32, height: 32, borderRadius: 10, alignItems: 'center', justifyContent: 'center' },
  later: { paddingHorizontal: 8, paddingVertical: 8 },
  updateBtn: { paddingHorizontal: 14, paddingVertical: 8, borderRadius: 12 },
});
