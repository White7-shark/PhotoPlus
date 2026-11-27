import React, { useCallback, useEffect, useState } from 'react';
import { Alert, Image, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, IconButton, Screen } from '../../components';
import { useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import type { StudioProject } from '../../services/database/repositories/StudioProjectsRepository';
import { STUDIOS, addPhotos, deleteProject, nextProjectName, studioAbout } from '../../services/projects/studios';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { NameSheet } from './NameSheet';

export const formatDay = (ms: number) => {
  try {
    return new Date(ms).toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' });
  } catch {
    return new Date(ms).toDateString();
  }
};

/**
 * A studio's projects: every Photoshoot, Photo Arts and Photo Motion
 * creation lives in a project. Create one, open it, rename or delete it.
 * A photo just taken (params.photo) goes into the project chosen for it.
 */
export function StudioProjectsScreen({ navigation, route }: RootScreenProps<'StudioProjects'>) {
  const theme = useTheme();
  const t = useT();
  const repos = useAppStore(s => s.repos);
  const studio = STUDIOS[route.params.studio];
  const photo = route.params.photo;
  const [projects, setProjects] = useState<StudioProject[] | null>(null);
  const [naming, setNaming] = useState<null | { kind: 'new' } | { kind: 'rename'; project: StudioProject }>(null);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    if (!repos) return;
    repos.studioProjects
      .list(studio.kind)
      .then(setProjects)
      .catch(() => setError(t('sp_failed')));
  }, [repos, studio.kind, t]);
  // Fresh each time the screen comes back into view (a project may have changed).
  useEffect(() => {
    load();
    return navigation.addListener?.('focus', load);
  }, [navigation, load]);

  /** Opens a project; with a photo waiting, it goes in and opens in the studio straight away. */
  const open = async (p: StudioProject) => {
    if (photo && repos) {
      navigation.setParams({ photo: undefined });
      await addPhotos(repos, p.id, [photo]);
      navigation.navigate(studio.route, { photo, projectId: p.id });
      return;
    }
    navigation.navigate('StudioProject', { projectId: p.id });
  };

  const create = async (name: string) => {
    if (!repos) return;
    try {
      const p = await repos.studioProjects.create(studio.kind, name);
      setNaming(null);
      await open(p);
    } catch {
      setError(t('sp_failed'));
    }
  };

  const rename = async (p: StudioProject, name: string) => {
    if (!repos) return;
    try {
      await repos.studioProjects.rename(p.id, name);
      setNaming(null);
      load();
    } catch {
      setError(t('sp_failed'));
    }
  };

  const remove = (p: StudioProject) =>
    Alert.alert(t('sp_delete_project_title'), t('sp_delete_project_body', { name: p.name }), [
      { text: t('sp_cancel'), style: 'cancel' },
      {
        text: t('sp_delete'),
        style: 'destructive',
        onPress: () => {
          if (!repos) return;
          void deleteProject(repos, p.id)
            .then(load)
            .catch(() => setError(t('sp_failed')));
        },
      },
    ]);

  return (
    <Screen title={studio.title} onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 16 }}>
        <GlassSurface radius={22} style={styles.head} glow="rgba(245,198,107,0.18)">
          <View style={styles.headRow}>
            <View style={[styles.badge, { backgroundImage: theme.gradients.premium }]}>
              <Icon name={studio.icon} size={22} color="#1A1206" strokeWidth={2} />
            </View>
            <View style={{ flex: 1, gap: 2 }}>
              <AppText variant="overline" tone="secondary">
                {t('sp_projects').toUpperCase()}
              </AppText>
              <AppText variant="callout" tone="secondary">
                {studioAbout(studio.kind)}
              </AppText>
            </View>
          </View>
          {photo ? (
            <AppText variant="callout" style={{ fontWeight: '700' }} testID="studio-projects-photo">
              {t('sp_pick_for_photo')}
            </AppText>
          ) : null}
          <Button
            label={t('sp_new')}
            icon="plus"
            size="lg"
            onPress={() => setNaming({ kind: 'new' })}
            testID="studio-projects-new"
          />
        </GlassSurface>

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }} accessibilityLiveRegion="polite">
            {error}
          </AppText>
        ) : null}

        {projects && projects.length === 0 ? (
          <GlassSurface radius={20} style={styles.empty} testID="studio-projects-empty">
            <Icon name="folder" size={28} color={theme.colors.accent} />
            <AppText variant="title3" align="center">
              {t('sp_empty_title')}
            </AppText>
            <AppText variant="body" tone="secondary" align="center">
              {t('sp_empty_body', { studio: studio.title })}
            </AppText>
          </GlassSurface>
        ) : null}

        {(projects ?? []).map(p => (
          <Pressable
            key={p.id}
            onPress={() => void open(p)}
            accessibilityRole="button"
            accessibilityLabel={`${p.name}, ${t('sp_counts', { photos: p.photoCount, works: p.workCount })}`}
            testID={`studio-project-${p.id}`}
          >
            <GlassSurface radius={20} style={styles.card}>
              <View style={styles.cover}>
                {p.coverPath ? (
                  <Image source={{ uri: `file://${p.coverPath}` }} style={StyleSheet.absoluteFill} resizeMode="cover" />
                ) : (
                  <Icon name={studio.icon} size={26} color={theme.colors.textTertiary} />
                )}
              </View>
              <View style={{ flex: 1, gap: 3 }}>
                <AppText variant="bodyStrong" numberOfLines={1}>
                  {p.name}
                </AppText>
                <AppText variant="caption" tone="secondary">
                  {t('sp_counts', { photos: p.photoCount, works: p.workCount })}
                </AppText>
                <AppText variant="caption" tone="tertiary">
                  {t('sp_updated', { date: formatDay(p.updatedAt) })}
                </AppText>
              </View>
              <View style={{ gap: 4 }}>
                <IconButton
                  icon="brush"
                  label={t('sp_rename')}
                  size={40}
                  onPress={() => setNaming({ kind: 'rename', project: p })}
                  testID={`studio-project-rename-${p.id}`}
                />
                <IconButton
                  icon="trash"
                  label={t('sp_delete')}
                  size={40}
                  onPress={() => remove(p)}
                  testID={`studio-project-delete-${p.id}`}
                />
              </View>
            </GlassSurface>
          </Pressable>
        ))}
      </View>
      <NameSheet
        open={naming !== null}
        title={naming?.kind === 'rename' ? t('sp_rename_title') : t('sp_new_title')}
        initial={naming?.kind === 'rename' ? naming.project.name : nextProjectName(projects ?? [])}
        placeholder={t('sp_name_placeholder')}
        confirm={naming?.kind === 'rename' ? t('sp_save') : t('sp_create')}
        onClose={() => setNaming(null)}
        onSubmit={name => (naming?.kind === 'rename' ? rename(naming.project, name) : create(name))}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  head: { padding: 16, gap: 12 },
  headRow: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  badge: { width: 46, height: 46, borderRadius: 23, alignItems: 'center', justifyContent: 'center' },
  empty: { padding: 22, gap: 10, alignItems: 'center' },
  card: { flexDirection: 'row', alignItems: 'center', gap: 12, padding: 10 },
  cover: {
    width: 76,
    height: 76,
    borderRadius: 14,
    overflow: 'hidden',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: 'rgba(255,255,255,0.06)',
  },
});
