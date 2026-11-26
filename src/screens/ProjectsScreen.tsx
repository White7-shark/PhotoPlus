import React, { useState } from 'react';
import { Alert, Image, Pressable, TextInput, View } from 'react-native';

import {
  AppText,
  Button,
  EmptyState,
  ErrorState,
  GlassSurface,
  Icon,
  IconButton,
  LoadingState,
  Screen,
} from '../components';
import { useResponsive } from '../hooks/useResponsive';
import type { ProjectKind } from '../services/database/repositories/ProjectsRepository';
import { Choice } from './product/controls';
import type { TabScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import { getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { t as tx } from '../i18n';

export function ProjectsScreen({ navigation }: TabScreenProps<'Projects'>) {
  const theme = useTheme();
  const r = useResponsive();
  const { projects, projectsStatus, projectsError, loadProjects, createProject, deleteProject } = useAppStore();
  const [creating, setCreating] = useState(false);
  const [name, setName] = useState('');
  const [kind, setKind] = useState<ProjectKind>('mixed');
  const [saving, setSaving] = useState(false);

  const submit = async () => {
    if (!name.trim()) {
      return;
    }
    setSaving(true);
    try {
      await createProject(name, kind);
      setName('');
      setCreating(false);
    } catch (e) {
      Alert.alert(tx('projects_could_not_create_project'), errorMessage(e));
    } finally {
      setSaving(false);
    }
  };

  const confirmDelete = (id: string, projectName: string) =>
    Alert.alert(tx('projects_delete', { projectName }), tx('projects_photos_in_this_project_will'), [
      { text: tx('projects_cancel'), style: 'cancel' },
      {
        text: tx('projects_delete_2'),
        style: 'destructive',
        onPress: async () => {
          try {
            const paths = await deleteProject(id);
            const storage = getStorageService();
            await Promise.allSettled(paths.map(p => storage.deleteFile(p)));
          } catch (e) {
            Alert.alert(tx('projects_could_not_delete_project'), errorMessage(e));
          }
        },
      },
    ]);

  return (
    <Screen
      title="Projects"
      overline={tx('projects_library')}
      onRefresh={loadProjects}
      refreshing={projectsStatus === 'loading'}
      headerRight={
        <IconButton
          icon="plus"
          label={tx('projects_new_project')}
          onPress={() => setCreating(c => !c)}
          active={creating}
        />
      }
    >
      {creating ? (
        <GlassSurface style={{ padding: 16, marginBottom: 16, gap: 12 }}>
          <AppText variant="bodyStrong">{tx('projects_new_project')}</AppText>
          <TextInput
            value={name}
            onChangeText={setName}
            placeholder={tx('projects_e_g_studio_portraits_march')}
            placeholderTextColor={theme.colors.textTertiary}
            maxLength={120}
            autoFocus
            onSubmitEditing={submit}
            accessibilityLabel={tx('projects_project_name')}
            style={{
              minHeight: 48,
              borderRadius: theme.radii.sm,
              paddingHorizontal: 14,
              color: theme.colors.textPrimary,
              backgroundColor: theme.colors.glassFill,
              borderWidth: 1,
              borderColor: theme.colors.glassStroke,
              fontSize: 15,
            }}
          />
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }} accessibilityRole="radiogroup">
            {(
              [
                ['product', 'Products'],
                ['portrait', 'Portraits'],
                ['mixed', 'Both'],
              ] as const
            ).map(([k, label]) => (
              <Choice
                key={k}
                label={label}
                selected={kind === k}
                onPress={() => setKind(k)}
                testID={`new-project-${k}`}
              />
            ))}
          </View>
          <View style={{ flexDirection: 'row', gap: 10, justifyContent: 'flex-end' }}>
            <Button label={tx('projects_cancel')} variant="ghost" onPress={() => setCreating(false)} />
            <Button
              label={tx('projects_create')}
              icon="check"
              onPress={submit}
              loading={saving}
              disabled={!name.trim()}
            />
          </View>
        </GlassSurface>
      ) : null}

      {projectsStatus === 'loading' && projects.length === 0 ? (
        <LoadingState label={tx('projects_loading_projects')} />
      ) : projectsStatus === 'error' ? (
        <ErrorState
          title={tx('projects_couldn_t_load_projects')}
          message={projectsError ?? undefined}
          onRetry={loadProjects}
        />
      ) : projects.length === 0 ? (
        <EmptyState
          icon="folder"
          title={tx('projects_your_library_is_empty')}
          message={tx('projects_create_a_project_to_organise')}
          actionLabel={tx('projects_open_camera')}
          onAction={() => navigation.navigate('Camera')}
          secondaryLabel="Create a project"
          onSecondary={() => setCreating(true)}
        />
      ) : (
        <View style={{ gap: 12 }}>
          {projects.map(p => (
            <GlassSurface
              key={p.id}
              radius={theme.radii.lg}
              style={{ flexDirection: 'row', alignItems: 'center', gap: 14, padding: 12 }}
            >
              <View
                style={{
                  width: r.breakpoint === 'compact' ? 64 : 84,
                  aspectRatio: 1,
                  borderRadius: 14,
                  overflow: 'hidden',
                  backgroundImage: theme.gradients.spectrumSoft,
                  alignItems: 'center',
                  justifyContent: 'center',
                }}
              >
                {p.coverPath ? (
                  <Image source={{ uri: `file://${p.coverPath}` }} style={{ width: '100%', height: '100%' }} />
                ) : (
                  <Icon name="image" size={24} color="#FFFFFF" />
                )}
              </View>
              <Pressable
                style={{ flex: 1 }}
                onPress={() => navigation.navigate('Project', { projectId: p.id })}
                accessibilityRole="button"
                accessibilityLabel={tx('projects_photos_updated', {
                  name: p.name,
                  imageCount: p.imageCount,
                  value: new Date(p.updatedAt).toLocaleDateString(),
                })}
                accessibilityHint={tx('projects_opens_the_project')}
                testID={`project-open-${p.id}`}
              >
                <AppText variant="bodyStrong" numberOfLines={1}>
                  {p.name}
                </AppText>
                <AppText variant="caption" tone="secondary">
                  {p.imageCount} {p.imageCount === 1 ? 'photo' : 'photos'} ·{' '}
                  {new Date(p.updatedAt).toLocaleDateString()}
                </AppText>
              </Pressable>
              <IconButton
                icon="trash"
                label={tx('projects_delete_3', { name: p.name })}
                size={44}
                tint={theme.colors.danger}
                onPress={() => confirmDelete(p.id, p.name)}
              />
            </GlassSurface>
          ))}
        </View>
      )}
    </Screen>
  );
}
