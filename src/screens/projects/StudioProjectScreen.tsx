import React, { useCallback, useEffect, useState } from 'react';
import { Alert, Image, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, IconButton, Screen } from '../../components';
import { useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import type { StudioItem, StudioProject } from '../../services/database/repositories/StudioProjectsRepository';
import { STUDIOS, addPhotos, deleteItem } from '../../services/projects/studios';
import { getStorageService } from '../../services/storage/StorageService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { NameSheet } from './NameSheet';

type Tab = 'photos' | 'gallery';

/**
 * One project: its photos (add many; tap one to create with it in the
 * studio) and its gallery of finished works (tap one to view, download,
 * edit again, rename or delete it).
 */
export function StudioProjectScreen({ navigation, route }: RootScreenProps<'StudioProject'>) {
  const theme = useTheme();
  const t = useT();
  const repos = useAppStore(s => s.repos);
  const { projectId } = route.params;
  const [project, setProject] = useState<StudioProject | null>(null);
  const [photos, setPhotos] = useState<StudioItem[]>([]);
  const [works, setWorks] = useState<StudioItem[]>([]);
  const [tab, setTab] = useState<Tab>(route.params.tab ?? 'photos');
  const [renaming, setRenaming] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    if (!repos) return;
    void Promise.all([
      repos.studioProjects.get(projectId),
      repos.studioProjects.items(projectId, 'photo'),
      repos.studioProjects.items(projectId, 'work'),
    ])
      .then(([p, ph, wk]) => {
        setProject(p);
        setPhotos(ph);
        setWorks(wk);
      })
      .catch(() => setError(t('sp_failed')));
  }, [repos, projectId, t]);
  useEffect(() => {
    load();
    return navigation.addListener?.('focus', load);
  }, [navigation, load]);

  const studio = project ? STUDIOS[project.studio] : null;

  const add = async () => {
    if (!repos) return;
    setBusy(true);
    try {
      const paths = await getStorageService().pickImages('imports', 30);
      if (paths.length) {
        await addPhotos(repos, projectId, paths);
        setTab('photos');
        load();
      }
    } catch {
      setError(t('sp_failed'));
    } finally {
      setBusy(false);
    }
  };

  const create = (p: StudioItem) => {
    if (studio) navigation.navigate(studio.route, { photo: p.path, projectId });
  };

  const removePhoto = (p: StudioItem) =>
    Alert.alert(t('sp_delete_item_title'), t('sp_delete_photo_body'), [
      { text: t('sp_cancel'), style: 'cancel' },
      {
        text: t('sp_delete'),
        style: 'destructive',
        onPress: () => {
          if (repos) void deleteItem(repos, p.id).then(load);
        },
      },
    ]);

  const rename = async (name: string) => {
    if (!repos) return;
    await repos.studioProjects.rename(projectId, name);
    setRenaming(false);
    load();
  };

  return (
    <Screen title={project?.name ?? ''} onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <View style={styles.headRow}>
          <View style={{ flex: 1 }}>
            {studio ? (
              <AppText variant="overline" tone="secondary">
                {studio.title.toUpperCase()}
              </AppText>
            ) : null}
            <AppText variant="title2" numberOfLines={2} testID="studio-project-name">
              {project?.name ?? ''}
            </AppText>
          </View>
          <IconButton
            icon="brush"
            label={t('sp_rename')}
            onPress={() => setRenaming(true)}
            testID="studio-project-rename"
          />
        </View>

        <View style={styles.tabs} accessibilityRole="tablist">
          {(
            [
              ['photos', 'image', t('sp_photos'), photos.length],
              ['gallery', 'grid', t('sp_gallery'), works.length],
            ] as const
          ).map(([id, icon, label, n]) => {
            const on = tab === id;
            return (
              <Pressable
                key={id}
                onPress={() => setTab(id)}
                accessibilityRole="tab"
                accessibilityState={{ selected: on }}
                testID={`studio-project-tab-${id}`}
                style={[styles.tab, on ? { backgroundImage: theme.gradients.action } : null]}
              >
                <Icon name={icon} size={16} color={on ? '#FFFFFF' : theme.colors.textSecondary} />
                <AppText
                  variant="callout"
                  style={{ fontWeight: '700', color: on ? '#FFFFFF' : theme.colors.textSecondary }}
                >
                  {`${label} · ${n}`}
                </AppText>
              </Pressable>
            );
          })}
        </View>

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }}>
            {error}
          </AppText>
        ) : null}

        {tab === 'photos' ? (
          <>
            <Button
              label={t('sp_add_photos')}
              icon="plus"
              size="lg"
              loading={busy}
              onPress={() => void add()}
              testID="studio-project-add"
            />
            {photos.length === 0 ? (
              <AppText variant="body" tone="secondary" align="center" style={{ paddingVertical: 24 }}>
                {t('sp_photos_empty')}
              </AppText>
            ) : (
              <>
                <AppText variant="caption" tone="tertiary">
                  {t('sp_tap_photo', { studio: studio?.title ?? '' })}
                </AppText>
                <View style={styles.grid}>
                  {photos.map(p => (
                    <Pressable
                      key={p.id}
                      onPress={() => create(p)}
                      onLongPress={() => removePhoto(p)}
                      accessibilityRole="button"
                      accessibilityLabel={`${p.title}. ${t('sp_create_with')}`}
                      accessibilityHint={t('sp_hold_to_delete')}
                      testID={`studio-photo-${p.id}`}
                      style={styles.photo}
                    >
                      <Image source={{ uri: `file://${p.path}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
                      <View style={styles.photoBadge}>
                        <Icon name={studio?.icon ?? 'brush'} size={13} color="#FFFFFF" />
                      </View>
                    </Pressable>
                  ))}
                </View>
              </>
            )}
          </>
        ) : works.length === 0 ? (
          <GlassSurface radius={20} style={styles.empty}>
            <Icon name="grid" size={26} color={theme.colors.accent} />
            <AppText variant="body" tone="secondary" align="center">
              {t('sp_gallery_empty')}
            </AppText>
          </GlassSurface>
        ) : (
          <View style={styles.grid}>
            {works.map(w => (
              <Pressable
                key={w.id}
                onPress={() => navigation.navigate('StudioItem', { itemId: w.id })}
                accessibilityRole="button"
                accessibilityLabel={w.title}
                testID={`studio-work-${w.id}`}
                style={styles.workCell}
              >
                <View style={[styles.work, { aspectRatio: w.width > 0 && w.height > 0 ? w.width / w.height : 0.8 }]}>
                  <Image
                    source={{ uri: `file://${w.posterPath ?? w.path}` }}
                    style={StyleSheet.absoluteFill}
                    resizeMode="cover"
                  />
                  {w.media === 'video' ? (
                    <View style={styles.play}>
                      <Icon name="play" size={14} color="#FFFFFF" />
                    </View>
                  ) : null}
                </View>
                <AppText variant="caption" numberOfLines={1} style={{ fontWeight: '700' }}>
                  {w.title}
                </AppText>
              </Pressable>
            ))}
          </View>
        )}
      </View>
      <NameSheet
        open={renaming}
        title={t('sp_rename_title')}
        initial={project?.name ?? ''}
        confirm={t('sp_save')}
        onClose={() => setRenaming(false)}
        onSubmit={rename}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  headRow: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  tabs: {
    flexDirection: 'row',
    padding: 4,
    gap: 4,
    borderRadius: 16,
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.1)',
  },
  tab: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
    paddingVertical: 10,
    borderRadius: 12,
  },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  photo: {
    width: '31.6%',
    aspectRatio: 1,
    borderRadius: 12,
    overflow: 'hidden',
    backgroundColor: 'rgba(255,255,255,0.06)',
  },
  photoBadge: {
    position: 'absolute',
    right: 6,
    bottom: 6,
    width: 24,
    height: 24,
    borderRadius: 12,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(0,0,0,0.55)',
  },
  workCell: { width: '48.5%', gap: 6 },
  work: { width: '100%', borderRadius: 14, overflow: 'hidden', backgroundColor: 'rgba(255,255,255,0.06)' },
  play: {
    position: 'absolute',
    top: 8,
    right: 8,
    width: 28,
    height: 28,
    borderRadius: 14,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(0,0,0,0.55)',
  },
  empty: { padding: 24, gap: 10, alignItems: 'center' },
});
