import React, { useState } from 'react';
import { Alert, Image, TextInput, View } from 'react-native';

import { AppText, Button, EmptyState, ErrorState, GlassSurface, Icon, IconButton, LoadingState, Screen } from '../components';
import { useResponsive } from '../hooks/useResponsive';
import type { TabScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import { getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';

export function ProjectsScreen({ navigation }: TabScreenProps<'Projects'>) {
  const theme = useTheme();
  const r = useResponsive();
  const { projects, projectsStatus, projectsError, loadProjects, createProject, deleteProject } = useAppStore();
  const [creating, setCreating] = useState(false);
  const [name, setName] = useState('');
  const [saving, setSaving] = useState(false);

  const submit = async () => {
    if (!name.trim()) {
      return;
    }
    setSaving(true);
    try {
      await createProject(name);
      setName('');
      setCreating(false);
    } catch (e) {
      Alert.alert('Could not create project', errorMessage(e));
    } finally {
      setSaving(false);
    }
  };

  const confirmDelete = (id: string, projectName: string) =>
    Alert.alert(`Delete "${projectName}"?`, 'Photos in this project will be removed from this device.', [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Delete',
        style: 'destructive',
        onPress: async () => {
          try {
            const paths = await deleteProject(id);
            const storage = getStorageService();
            await Promise.allSettled(paths.map(p => storage.deleteFile(p)));
          } catch (e) {
            Alert.alert('Could not delete project', errorMessage(e));
          }
        },
      },
    ]);

  return (
    <Screen
      title="Projects"
      overline="Library"
      onRefresh={loadProjects}
      refreshing={projectsStatus === 'loading'}
      headerRight={<IconButton icon="plus" label="New project" onPress={() => setCreating(c => !c)} active={creating} />}
    >
      {creating ? (
        <GlassSurface style={{ padding: 16, marginBottom: 16, gap: 12 }}>
          <AppText variant="bodyStrong">New project</AppText>
          <TextInput
            value={name}
            onChangeText={setName}
            placeholder="e.g. Studio portraits — March"
            placeholderTextColor={theme.colors.textTertiary}
            maxLength={120}
            autoFocus
            onSubmitEditing={submit}
            accessibilityLabel="Project name"
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
          <View style={{ flexDirection: 'row', gap: 10, justifyContent: 'flex-end' }}>
            <Button label="Cancel" variant="ghost" onPress={() => setCreating(false)} />
            <Button label="Create" icon="check" onPress={submit} loading={saving} disabled={!name.trim()} />
          </View>
        </GlassSurface>
      ) : null}

      {projectsStatus === 'loading' && projects.length === 0 ? (
        <LoadingState label="Loading projects…" />
      ) : projectsStatus === 'error' ? (
        <ErrorState title="Couldn't load projects" message={projectsError ?? undefined} onRetry={loadProjects} />
      ) : projects.length === 0 ? (
        <EmptyState
          icon="folder"
          title="Your library is empty"
          message="Create a project to organise a shoot, or just start capturing — photos go to Captures."
          actionLabel="Open camera"
          onAction={() => navigation.navigate('Camera')}
          secondaryLabel="Create a project"
          onSecondary={() => setCreating(true)}
        />
      ) : (
        <View style={{ gap: 12 }}>
          {projects.map(p => (
            <GlassSurface key={p.id} radius={theme.radii.lg} style={{ flexDirection: 'row', alignItems: 'center', gap: 14, padding: 12 }}>
              <View style={{ width: r.breakpoint === 'compact' ? 64 : 84, aspectRatio: 1, borderRadius: 14, overflow: 'hidden', backgroundImage: theme.gradients.spectrumSoft, alignItems: 'center', justifyContent: 'center' }}>
                {p.coverPath ? <Image source={{ uri: `file://${p.coverPath}` }} style={{ width: '100%', height: '100%' }} /> : <Icon name="image" size={24} color="#FFFFFF" />}
              </View>
              <View style={{ flex: 1 }} accessible accessibilityLabel={`${p.name}, ${p.imageCount} photos, updated ${new Date(p.updatedAt).toLocaleDateString()}`}>
                <AppText variant="bodyStrong" numberOfLines={1}>
                  {p.name}
                </AppText>
                <AppText variant="caption" tone="secondary">
                  {p.imageCount} {p.imageCount === 1 ? 'photo' : 'photos'} · {new Date(p.updatedAt).toLocaleDateString()}
                </AppText>
              </View>
              <IconButton icon="trash" label={`Delete ${p.name}`} size={44} tint={theme.colors.danger} onPress={() => confirmDelete(p.id, p.name)} />
            </GlassSurface>
          ))}
        </View>
      )}
    </Screen>
  );
}
