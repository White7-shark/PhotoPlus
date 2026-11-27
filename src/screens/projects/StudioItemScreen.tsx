import React, { useCallback, useEffect, useState } from 'react';
import { Alert, Image, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen } from '../../components';
import { VideoView } from '../../components/VideoView';
import { useT } from '../../i18n';
import type { RootScreenProps } from '../../navigation/types';
import type { StudioItem, StudioProject } from '../../services/database/repositories/StudioProjectsRepository';
import { STUDIOS, deleteItem } from '../../services/projects/studios';
import { getStorageService } from '../../services/storage/StorageService';
import { getStudioService } from '../../services/studio/StudioService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';
import { CompareSlider } from '../studio/CompareSlider';
import { NameSheet } from './NameSheet';
import { formatDay } from './StudioProjectsScreen';

/**
 * A finished work from a project's gallery (and the preview straight after
 * a work is made): view it — a painting or drawing beside the photo it was
 * made from — and download, share, edit it again, rename or delete it.
 */
export function StudioItemScreen({ navigation, route }: RootScreenProps<'StudioItem'>) {
  const theme = useTheme();
  const t = useT();
  const repos = useAppStore(s => s.repos);
  const { itemId, fresh } = route.params;
  const [item, setItem] = useState<StudioItem | null>(null);
  const [project, setProject] = useState<StudioProject | null>(null);
  const [saved, setSaved] = useState(false);
  const [busy, setBusy] = useState(false);
  const [renaming, setRenaming] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    if (!repos) return;
    void repos.studioProjects.item(itemId).then(async it => {
      setItem(it);
      if (it) setProject(await repos.studioProjects.get(it.projectId));
    });
  }, [repos, itemId]);
  useEffect(load, [load]);

  if (!item) {
    return (
      <Screen title="" onBack={() => navigation.goBack()}>
        <View />
      </Screen>
    );
  }
  const studio = project ? STUDIOS[project.studio] : null;
  const inner = Array.isArray(item.settings.inner) ? (item.settings.inner as number[]) : null;

  const download = async () => {
    setBusy(true);
    setError(null);
    try {
      if (item.media === 'video') await getStorageService().saveVideoToGallery(item.path);
      else await getStudioService().saveToGallery(item.path);
      setSaved(true);
    } catch {
      setError(t('sp_failed'));
    } finally {
      setBusy(false);
    }
  };
  const share = () => void getStorageService().share([item.path], item.title);
  const editAgain = () => {
    if (!studio || !item.sourcePath) return;
    navigation.navigate(studio.route, { photo: item.sourcePath, projectId: item.projectId, edit: item.settings });
  };
  const remove = () =>
    Alert.alert(t('sp_delete_item_title'), t('sp_delete_item_body'), [
      { text: t('sp_cancel'), style: 'cancel' },
      {
        text: t('sp_delete'),
        style: 'destructive',
        onPress: () => {
          if (!repos) return;
          void deleteItem(repos, item.id).then(() => navigation.goBack());
        },
      },
    ]);
  const rename = async (title: string) => {
    if (!repos) return;
    await repos.studioProjects.renameItem(item.id, title);
    setRenaming(false);
    load();
  };

  const aspect = item.width > 0 && item.height > 0 ? item.width / item.height : 0.8;
  return (
    <Screen title={fresh ? t('sp_ready') : item.title} onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        {fresh && project ? (
          <View style={styles.banner} accessibilityLiveRegion="polite" testID="studio-item-fresh">
            <Icon name="check" size={16} color={theme.colors.accent} />
            <AppText variant="callout" style={{ flex: 1 }}>
              {t('sp_saved_in', { project: project.name })}
            </AppText>
          </View>
        ) : null}
        <GlassSurface radius={22} style={styles.stage}>
          {item.media === 'video' ? (
            <VideoView
              source={item.path}
              loop
              muted
              style={{ width: '100%', aspectRatio: aspect, borderRadius: 16 }}
              testID="studio-item-video"
            />
          ) : item.sourcePath ? (
            <CompareSlider
              painting={{ uri: `file://${item.path}`, width: item.width || 800, height: item.height || 1000 }}
              photo={`file://${item.sourcePath}`}
              inset={inner ? { x: inner[0], y: inner[1] } : { x: 0, y: 0 }}
              label={item.title}
              hint={t('masters_compare')}
              testID="studio-item-compare"
            />
          ) : (
            <Image
              source={{ uri: `file://${item.path}` }}
              style={{ width: '100%', aspectRatio: aspect, borderRadius: 16 }}
              testID="studio-item-image"
            />
          )}
        </GlassSurface>

        <View style={styles.titleRow}>
          <View style={{ flex: 1, gap: 2 }}>
            <AppText variant="title3" numberOfLines={2} testID="studio-item-title">
              {item.title}
            </AppText>
            <AppText variant="caption" tone="tertiary">
              {[item.width && item.height ? `${item.width} × ${item.height}` : null, formatDay(item.createdAt)]
                .filter(Boolean)
                .join(' · ')}
            </AppText>
          </View>
          <Button
            label={t('sp_rename')}
            icon="brush"
            variant="ghost"
            onPress={() => setRenaming(true)}
            testID="studio-item-rename"
          />
        </View>

        {error ? (
          <AppText variant="callout" style={{ color: theme.colors.warning }}>
            {error}
          </AppText>
        ) : null}

        <View style={styles.row}>
          <View style={{ flex: 1 }}>
            <Button
              label={saved ? t('sp_downloaded') : t('sp_download')}
              icon={saved ? 'check' : 'download'}
              size="lg"
              loading={busy}
              disabled={busy || saved}
              onPress={() => void download()}
              testID="studio-item-download"
            />
          </View>
          <Button
            label={t('sp_share')}
            icon="share"
            variant="glass"
            size="lg"
            onPress={share}
            testID="studio-item-share"
          />
        </View>
        {item.sourcePath ? (
          <Button
            label={t('sp_edit_again')}
            icon="sliders"
            variant="glass"
            onPress={editAgain}
            testID="studio-item-edit"
          />
        ) : null}
        {fresh ? (
          <Button
            label={t('sp_open_gallery')}
            icon="grid"
            variant="ghost"
            onPress={() => navigation.navigate('StudioProject', { projectId: item.projectId, tab: 'gallery' })}
            testID="studio-item-gallery"
          />
        ) : null}
        <Button label={t('sp_delete')} icon="trash" variant="ghost" onPress={remove} testID="studio-item-delete" />
      </View>
      <NameSheet
        open={renaming}
        title={t('sp_rename_title')}
        initial={item.title}
        confirm={t('sp_save')}
        onClose={() => setRenaming(false)}
        onSubmit={rename}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  banner: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    padding: 12,
    borderRadius: 14,
    backgroundColor: 'rgba(245,198,107,0.12)',
  },
  stage: { padding: 10 },
  titleRow: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  row: { flexDirection: 'row', gap: 10 },
});
