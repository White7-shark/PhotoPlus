import React, { useCallback, useEffect, useState } from 'react';
import { Alert, Image, Pressable, StyleSheet, TextInput, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon, IconButton, Screen } from '../../components';
import { PremiumSheet } from '../../components/PremiumSheet';
import { useFeatureCheck } from '../../hooks/useFeature';
import type { RootScreenProps } from '../../navigation/types';
import { errorMessage } from '../../native';
import type { ImageRecord } from '../../services/database/repositories/ImagesRepository';
import type { Project, ProjectKind } from '../../services/database/repositories/ProjectsRepository';
import type { FeatureKey } from '../../services/entitlements/features';
import { importPhoto } from '../../services/library/importPhoto';
import { filesToDelete } from '../../services/processing/quality';
import { CATALOG_PRESETS, EXPORT_PROFILES, LOOK_PRESETS, PACKS, packSuits } from '../../services/product/setup';
import { exportSize, type ExportSizeId } from '../../services/studio/exportSize';
import { getStorageService } from '../../services/storage/StorageService';
import { useAppStore } from '../../state/store';
import { useTheme } from '../../theme';

import { Choice, Tabs } from './controls';

type Filter = 'all' | 'originals' | 'edited';
const KINDS: readonly { value: ProjectKind; label: string }[] = [
  { value: 'product', label: 'Products' },
  { value: 'portrait', label: 'Portraits' },
  { value: 'mixed', label: 'Both' },
];

const isEdited = (r: ImageRecord) => typeof r.metadata.sourceImageId === 'string';

/**
 * A project (Phase 12): a shoot's photos, originals and every edit made from
 * them, with what the project reuses: a pack that suits it, a setup or look,
 * an export profile and size, and a brand mark. Photos can be picked to
 * batch edit, make a product shot, move to another project or delete.
 */
export function ProjectScreen({ navigation, route }: RootScreenProps<'Project'>) {
  const theme = useTheme();
  const can = useFeatureCheck();
  const packsAllowed = can('packAccess').allowed;
  const repos = useAppStore(st => st.repos);
  const projects = useAppStore(st => st.projects);
  const loadProjects = useAppStore(st => st.loadProjects);
  const brandings = useAppStore(st => st.settings.brandingPresets);
  const projectId = route.params.projectId;

  const [project, setProject] = useState<Project | null>(null);
  const [images, setImages] = useState<ImageRecord[]>([]);
  const [filter, setFilter] = useState<Filter>('all');
  const [selecting, setSelecting] = useState(false);
  const [selected, setSelected] = useState<string[]>([]);
  const [renaming, setRenaming] = useState(false);
  const [name, setName] = useState('');
  const [moving, setMoving] = useState(false);
  const [premiumOpen, setPremiumOpen] = useState<FeatureKey | null>(null);
  const [error, setError] = useState<string | null>(null);

  // The project and its photos, loaded again after each change and on return.
  const [version, setVersion] = useState(0);
  const reload = useCallback(async () => setVersion(v => v + 1), []);
  useEffect(() => navigation.addListener('focus', () => setVersion(v => v + 1)), [navigation]);
  useEffect(() => {
    if (!repos) return;
    let live = true;
    (async () => {
      const p = await repos.projects.get(projectId);
      const list = p ? await repos.images.listByProject(p.id, 500) : [];
      if (!live) return;
      setProject(p);
      setImages(list);
    })().catch(() => {
      if (live) setError('This project couldn’t be loaded.');
    });
    return () => {
      live = false;
    };
  }, [repos, projectId, version]);

  if (!project) {
    return (
      <Screen title="Project" onBack={() => navigation.goBack()}>
        <AppText variant="body" tone="secondary">
          {error ?? 'Loading…'}
        </AppText>
      </Screen>
    );
  }

  const originals = images.filter(r => !isEdited(r)).length;
  const shown = images.filter(r => (filter === 'all' ? true : filter === 'edited' ? isEdited(r) : !isEdited(r)));
  const d = project.defaults;
  const toggle = (id: string) => setSelected(s => (s.includes(id) ? s.filter(x => x !== id) : [...s, id]));

  const setKind = async (kind: ProjectKind) => {
    await repos?.projects.setKind(project.id, kind);
    // A pack that no longer suits the project is taken off it.
    const current = PACKS.find(p => p.id === d.packId);
    if (current && !packSuits(current, kind)) await repos?.projects.setDefaults(project.id, { packId: null });
    await reload();
    void loadProjects();
  };
  const applyPack = async (id: string | null) => {
    const p = PACKS.find(x => x.id === id);
    if (p?.premium && !packsAllowed) return setPremiumOpen('packAccess');
    await repos?.projects.setDefaults(
      project.id,
      p
        ? p.kind === 'product'
          ? { packId: p.id, setupId: p.items[0] }
          : { packId: p.id, lookId: p.items[0] }
        : { packId: null },
    );
    await reload();
  };
  const clearDefault = async (key: 'setupId' | 'lookId' | 'profileId' | 'sizeId' | 'brandingId') => {
    await repos?.projects.setDefaults(project.id, { [key]: null });
    await reload();
  };
  const rename = async () => {
    if (!name.trim()) return;
    try {
      await repos?.projects.rename(project.id, name);
      setRenaming(false);
      await reload();
      void loadProjects();
    } catch (e) {
      Alert.alert('Couldn’t rename', errorMessage(e));
    }
  };
  const add = async () => {
    if (!repos) return;
    await importPhoto(repos, project.id);
    await reload();
    void loadProjects();
  };
  const moveTo = async (target: string) => {
    await repos?.images.moveToProject(selected, target);
    setMoving(false);
    setSelected([]);
    setSelecting(false);
    await reload();
    void loadProjects();
  };
  const remove = () =>
    Alert.alert(
      `Delete ${selected.length} ${selected.length === 1 ? 'photo' : 'photos'}?`,
      'They will be removed from this device. Edits made from them stay.',
      [
        { text: 'Cancel', style: 'cancel' },
        {
          text: 'Delete',
          style: 'destructive',
          onPress: async () => {
            const storage = getStorageService();
            for (const id of selected) {
              const r = images.find(x => x.id === id);
              if (!r) continue;
              await repos?.images.remove(id);
              await Promise.allSettled(filesToDelete(r).map(p => storage.deleteFile(p)));
            }
            setSelected([]);
            setSelecting(false);
            await reload();
            void loadProjects();
          },
        },
      ],
    );

  const setupName = CATALOG_PRESETS.find(c => c.id === d.setupId)?.name ?? (d.setupId ? 'Your setup' : null);
  const lookName = LOOK_PRESETS.find(l => l.id === d.lookId)?.name ?? (d.lookId ? 'Your look' : null);
  const profileName = EXPORT_PROFILES.find(p => p.id === d.profileId)?.name ?? null;
  const sizeName = d.sizeId ? exportSize(d.sizeId as ExportSizeId).label : null;
  const brandName = brandings.find(b => b.id === d.brandingId)?.name ?? null;
  const defaults = [
    ['Setup', setupName, 'setupId'],
    ['Look', lookName, 'lookId'],
    ['Shape', profileName, 'profileId'],
    ['Size', sizeName, 'sizeId'],
    ['Brand mark', brandName, 'brandingId'],
  ] as const;

  return (
    <Screen
      title={project.name}
      overline="Project"
      onBack={() => navigation.goBack()}
      bottomInset={32}
      headerRight={
        <IconButton
          icon="sliders"
          label="Rename project"
          onPress={() => (setName(project.name), setRenaming(r => !r))}
          active={renaming}
        />
      }
    >
      <View style={{ gap: 14 }}>
        {renaming ? (
          <GlassSurface radius={16} style={styles.section}>
            <View style={styles.row}>
              <TextInput
                value={name}
                onChangeText={setName}
                maxLength={120}
                autoFocus
                onSubmitEditing={rename}
                accessibilityLabel="Project name"
                testID="project-name"
                style={[styles.input, { color: theme.colors.textPrimary, borderColor: theme.colors.glassStroke }]}
              />
              <Button label="Rename" onPress={rename} disabled={!name.trim()} testID="project-rename" />
            </View>
          </GlassSurface>
        ) : null}

        <AppText variant="caption" tone="secondary">
          {originals} {originals === 1 ? 'original' : 'originals'} · {images.length - originals} edited · originals are
          never changed
        </AppText>

        <View style={styles.row}>
          <Button
            label="Product Studio"
            icon="bag"
            onPress={() => navigation.navigate('ProductStudio', { projectId: project.id })}
            testID="project-product"
            style={{ flex: 1 }}
          />
          <Button
            label="Batch edit"
            icon="wand"
            variant="glass"
            onPress={() => navigation.navigate('Batch', { projectId: project.id })}
            testID="project-batch"
            style={{ flex: 1 }}
          />
        </View>

        <GlassSurface radius={18} style={styles.section}>
          <AppText variant="bodyStrong">What this project holds</AppText>
          <View style={styles.wrap}>
            {KINDS.map(k => (
              <Choice
                key={k.value}
                label={k.label}
                selected={project.kind === k.value}
                onPress={() => setKind(k.value)}
                testID={`project-kind-${k.value}`}
              />
            ))}
          </View>
          <AppText variant="bodyStrong">Pack</AppText>
          <AppText variant="caption" tone="secondary">
            A pack brings ready-made setups or looks to every photo in the project.
          </AppText>
          {PACKS.map(p => {
            const suits = packSuits(p, project.kind);
            const on = d.packId === p.id;
            return (
              <Pressable
                key={p.id}
                onPress={() => (suits ? applyPack(on ? null : p.id) : undefined)}
                disabled={!suits}
                accessibilityRole="radio"
                accessibilityState={{ selected: on, disabled: !suits }}
                accessibilityLabel={`${p.name}${p.premium ? ', Premium' : ''}${suits ? '' : ', made for product photos'}`}
                testID={`project-pack-${p.id}`}
                style={[
                  styles.pack,
                  { borderColor: on ? theme.colors.accent : 'rgba(255,255,255,0.12)', opacity: suits ? 1 : 0.45 },
                ]}
              >
                <Icon name={p.kind === 'product' ? 'bag' : 'drop'} size={18} color={theme.colors.accent} />
                <View style={{ flex: 1 }}>
                  <View style={styles.row}>
                    <AppText variant="caption" style={{ fontWeight: '700' }}>
                      {p.name}
                    </AppText>
                    {p.premium ? <Icon name="crown" size={11} color={theme.colors.accent} /> : null}
                  </View>
                  <AppText variant="caption" tone="tertiary">
                    {suits ? p.about : 'Made for product photos'}
                  </AppText>
                </View>
                {on ? <Icon name="check" size={16} color={theme.colors.accent} /> : null}
              </Pressable>
            );
          })}
          {defaults.some(([, v]) => v) ? (
            <>
              <AppText variant="bodyStrong">Reused for every photo</AppText>
              {defaults.map(([label, value, key]) =>
                value ? (
                  <View key={key} style={styles.row}>
                    <AppText variant="caption" tone="secondary" style={{ width: 84 }}>
                      {label}
                    </AppText>
                    <AppText variant="caption" style={{ flex: 1 }}>
                      {value}
                    </AppText>
                    <Pressable
                      onPress={() => clearDefault(key)}
                      accessibilityRole="button"
                      accessibilityLabel={`Clear ${label}`}
                      hitSlop={8}
                      testID={`project-clear-${key}`}
                    >
                      <Icon name="close" size={14} color={theme.colors.textSecondary} />
                    </Pressable>
                  </View>
                ) : null,
              )}
            </>
          ) : null}
        </GlassSurface>

        <View style={styles.head}>
          <Tabs
            tabs={[
              { value: 'all', label: 'All' },
              { value: 'originals', label: 'Originals' },
              { value: 'edited', label: 'Edited' },
            ]}
            value={filter}
            onChange={setFilter}
            testPrefix="project-filter"
          />
          <View style={{ flex: 1 }} />
          <Choice
            label={selecting ? 'Done' : 'Select'}
            selected={selecting}
            onPress={() => (setSelecting(s => !s), setSelected([]))}
            testID="project-select"
          />
        </View>
        <View style={styles.grid} testID="project-photos">
          <Pressable
            onPress={add}
            accessibilityRole="button"
            accessibilityLabel="Add photos"
            testID="project-add"
            style={[styles.thumb, styles.addTile]}
          >
            <Icon name="plus" size={22} color={theme.colors.textSecondary} />
          </Pressable>
          {shown.map(r => {
            const on = selected.includes(r.id);
            return (
              <Pressable
                key={r.id}
                onPress={() => (selecting ? toggle(r.id) : navigation.navigate('PhotoPreview', { imageId: r.id }))}
                onLongPress={() => (setSelecting(true), toggle(r.id))}
                accessibilityRole={selecting ? 'checkbox' : 'button'}
                accessibilityState={selecting ? { checked: on } : undefined}
                accessibilityLabel={isEdited(r) ? 'Edited photo' : 'Original photo'}
                testID={`project-photo-${r.id}`}
                style={[styles.thumb, { borderColor: on ? theme.colors.accent : 'transparent' }]}
              >
                <Image source={{ uri: `file://${r.path}` }} style={StyleSheet.absoluteFill} />
                {isEdited(r) ? (
                  <View style={styles.badge}>
                    <Icon name="wand" size={10} color="#FFFFFF" />
                  </View>
                ) : null}
                {on ? (
                  <View style={[styles.tick, { backgroundColor: theme.colors.accent }]}>
                    <Icon name="check" size={12} color="#FFFFFF" />
                  </View>
                ) : null}
              </Pressable>
            );
          })}
        </View>

        {selecting && selected.length > 0 ? (
          <GlassSurface radius={18} style={styles.section} testID="project-actions">
            <AppText variant="bodyStrong">{selected.length} selected</AppText>
            <View style={styles.wrap}>
              <Choice
                label="Batch edit"
                icon="wand"
                selected={false}
                onPress={() => navigation.navigate('Batch', { projectId: project.id, imageIds: selected })}
                testID="project-action-batch"
              />
              {selected.length === 1 ? (
                <Choice
                  label="Product shot"
                  icon="bag"
                  selected={false}
                  onPress={() => navigation.navigate('ProductStudio', { projectId: project.id, imageId: selected[0] })}
                  testID="project-action-product"
                />
              ) : null}
              <Choice
                label="Move to…"
                icon="folder"
                selected={moving}
                onPress={() => setMoving(m => !m)}
                testID="project-action-move"
              />
              <Choice label="Delete" icon="trash" selected={false} onPress={remove} testID="project-action-delete" />
            </View>
            {moving ? (
              <View style={styles.wrap}>
                {projects
                  .filter(p => p.id !== project.id)
                  .map(p => (
                    <Choice
                      key={p.id}
                      label={p.name}
                      icon="folder"
                      selected={false}
                      onPress={() => moveTo(p.id)}
                      testID={`project-move-${p.id}`}
                    />
                  ))}
              </View>
            ) : null}
          </GlassSurface>
        ) : null}
      </View>
      <PremiumSheet feature={premiumOpen} onClose={() => setPremiumOpen(null)} />
    </Screen>
  );
}

const styles = StyleSheet.create({
  section: { padding: 14, gap: 10 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  head: { flexDirection: 'row', alignItems: 'center', gap: 8, flexWrap: 'wrap' },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  input: { flex: 1, minHeight: 44, borderRadius: 12, borderWidth: 1, paddingHorizontal: 12, fontSize: 15 },
  pack: { flexDirection: 'row', alignItems: 'center', gap: 10, padding: 10, borderRadius: 14, borderWidth: 1 },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 6 },
  thumb: { width: 96, height: 96, borderRadius: 12, overflow: 'hidden', borderWidth: 2 },
  addTile: {
    alignItems: 'center',
    justifyContent: 'center',
    borderStyle: 'dashed',
    borderColor: 'rgba(255,255,255,0.25)',
  },
  badge: {
    position: 'absolute',
    left: 4,
    bottom: 4,
    width: 18,
    height: 18,
    borderRadius: 9,
    backgroundColor: 'rgba(0,0,0,0.55)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  tick: {
    position: 'absolute',
    top: 4,
    right: 4,
    width: 20,
    height: 20,
    borderRadius: 10,
    alignItems: 'center',
    justifyContent: 'center',
  },
});
