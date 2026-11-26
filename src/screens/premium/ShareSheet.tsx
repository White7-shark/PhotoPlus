import React, { useEffect, useMemo, useState } from 'react';
import {
  ActivityIndicator,
  FlatList,
  Image,
  Modal,
  Pressable,
  StyleSheet,
  useWindowDimensions,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { AppText, Icon } from '../../components';
import { SHARE_APPS } from '../../services/rewards/campaign';
import { getRewardsService, type ShareTarget } from '../../services/rewards/RewardsService';
import { useT } from '../../i18n';
import { useTheme } from '../../theme';

const COLUMNS = 4;
const hoursLeft = (until: number, now: number) => Math.max(1, Math.ceil((until - now) / 3_600_000));

/** Popular apps first (in the campaign's order), then the rest by name. */
export function orderTargets(list: readonly ShareTarget[]): ShareTarget[] {
  const rank = (pkg: string) => {
    const i = SHARE_APPS.findIndex(a => a.pkg === pkg);
    return i < 0 ? SHARE_APPS.length : i;
  };
  // Within an app, its main share screen (named as the app) comes first.
  const main = (t: ShareTarget) => (t.label === t.app ? 0 : 1);
  return [...list].sort(
    (a, b) =>
      rank(a.pkg) - rank(b.pkg) || a.app.localeCompare(b.app) || main(a) - main(b) || a.label.localeCompare(b.label),
  );
}

/** The name under an icon: the app, and which of its share screens when it has several. */
export function targetName(t: ShareTarget, all: readonly ShareTarget[]): { name: string; detail: string | null } {
  const several = all.filter(x => x.pkg === t.pkg).length > 1;
  return { name: t.app || t.label, detail: several && t.label !== t.app ? t.label : null };
}

/**
 * The app's own share sheet: every app on the phone that takes a picture,
 * with its icon, in a grid that scrolls freely end to end (its own list in
 * its own window, so nothing clips it or holds the scroll). Each tile shows
 * how many times Photo Plus was shared there and, when it was shared in the
 * last 8 hours, when it opens again.
 */
export function ShareSheet({
  visible,
  onClose,
  onPick,
  locks,
  counts,
  now,
  busy,
}: {
  visible: boolean;
  onClose: () => void;
  onPick: (t: ShareTarget) => void;
  locks: Record<string, number>;
  counts: Record<string, number>;
  now: number;
  busy: boolean;
}) {
  const theme = useTheme();
  const t = useT();
  const insets = useSafeAreaInsets();
  const { height, width } = useWindowDimensions();
  const [targets, setTargets] = useState<ShareTarget[] | null>(null);

  useEffect(() => {
    if (!visible) return;
    let live = true;
    void getRewardsService()
      .shareTargets()
      .then(list => live && setTargets(orderTargets(list)));
    return () => {
      live = false;
    };
  }, [visible]);

  const tile = Math.floor((Math.min(width, 640) - 32) / COLUMNS);
  const list = useMemo(() => targets ?? [], [targets]);

  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose} statusBarTranslucent navigationBarTranslucent>
      <Pressable
        style={styles.scrim}
        onPress={onClose}
        accessibilityRole="button"
        accessibilityLabel={t('common_close')}
      />
      <View
        style={[
          styles.sheet,
          { height: height * 0.78, paddingBottom: insets.bottom + 8, backgroundColor: theme.colors.background },
        ]}
        testID="share-sheet"
      >
        <View style={styles.grip} />
        <View style={styles.head}>
          <AppText variant="bodyStrong" style={{ flex: 1 }}>
            {t('share_sheet_title')}
          </AppText>
          <Pressable
            onPress={onClose}
            accessibilityRole="button"
            accessibilityLabel={t('common_close')}
            hitSlop={10}
            testID="share-sheet-close"
          >
            <Icon name="close" size={18} color={theme.colors.textSecondary} />
          </Pressable>
        </View>
        <AppText variant="caption" tone="tertiary" style={styles.note}>
          {t('share_sheet_note')}
        </AppText>
        {targets === null ? (
          <ActivityIndicator style={{ marginTop: 40 }} color={theme.colors.accent} />
        ) : list.length === 0 ? (
          <AppText variant="body" tone="secondary" style={styles.note}>
            {t('share_sheet_empty')}
          </AppText>
        ) : (
          <FlatList
            data={list}
            numColumns={COLUMNS}
            keyExtractor={t => `${t.pkg}/${t.activity}`}
            style={{ flex: 1 }}
            contentContainerStyle={styles.grid}
            showsVerticalScrollIndicator
            nestedScrollEnabled
            initialNumToRender={24}
            testID="share-sheet-list"
            renderItem={({ item }) => {
              const opens = locks[item.pkg] ?? 0;
              const locked = opens > now;
              const n = counts[item.pkg] ?? 0;
              const { name, detail } = targetName(item, list);
              return (
                <Pressable
                  onPress={() => onPick(item)}
                  disabled={locked || busy}
                  accessibilityRole="button"
                  accessibilityLabel={
                    locked
                      ? t('share_sheet_locked', {
                          app: `${name}${detail ? ` ${detail}` : ''}`,
                          hours: hoursLeft(opens, now),
                        })
                      : t('share_sheet_to', { app: `${name}${detail ? ` ${detail}` : ''}` })
                  }
                  testID={`share-target-${item.pkg}-${item.activity}`}
                  style={[styles.tile, { width: tile, opacity: locked ? 0.4 : 1 }]}
                >
                  <View style={styles.iconWrap}>
                    {item.icon ? (
                      <Image source={{ uri: `file://${item.icon}` }} style={styles.icon} />
                    ) : (
                      <View style={[styles.icon, styles.blank]}>
                        <Icon name="share" size={20} color={theme.colors.textSecondary} />
                      </View>
                    )}
                    {n > 0 ? (
                      <View style={[styles.count, { backgroundColor: theme.colors.accent }]}>
                        <AppText variant="caption" style={styles.countText}>
                          {n}
                        </AppText>
                      </View>
                    ) : null}
                  </View>
                  <AppText variant="caption" align="center" numberOfLines={1} style={{ fontSize: 11 }}>
                    {name}
                  </AppText>
                  <AppText variant="caption" tone="tertiary" align="center" numberOfLines={1} style={{ fontSize: 10 }}>
                    {locked ? t('share_sheet_in_hours', { hours: hoursLeft(opens, now) }) : (detail ?? ' ')}
                  </AppText>
                </Pressable>
              );
            }}
          />
        )}
      </View>
    </Modal>
  );
}

const styles = StyleSheet.create({
  scrim: { flex: 1, backgroundColor: 'rgba(5,5,10,0.55)' },
  sheet: {
    position: 'absolute',
    left: 0,
    right: 0,
    bottom: 0,
    borderTopLeftRadius: 26,
    borderTopRightRadius: 26,
    paddingTop: 8,
    overflow: 'hidden',
  },
  grip: {
    alignSelf: 'center',
    width: 44,
    height: 5,
    borderRadius: 3,
    backgroundColor: 'rgba(255,255,255,0.25)',
    marginBottom: 8,
  },
  head: { flexDirection: 'row', alignItems: 'center', paddingHorizontal: 18 },
  note: { paddingHorizontal: 18, marginTop: 2, marginBottom: 8 },
  grid: { paddingHorizontal: 16, paddingBottom: 24 },
  tile: { alignItems: 'center', paddingVertical: 10, gap: 4 },
  iconWrap: { width: 52, height: 52 },
  icon: { width: 52, height: 52, borderRadius: 14 },
  blank: { backgroundColor: 'rgba(255,255,255,0.08)', alignItems: 'center', justifyContent: 'center' },
  count: {
    position: 'absolute',
    top: -4,
    right: -6,
    minWidth: 18,
    height: 18,
    borderRadius: 9,
    paddingHorizontal: 4,
    alignItems: 'center',
    justifyContent: 'center',
  },
  countText: { color: '#FFFFFF', fontSize: 10, fontWeight: '800' },
});
