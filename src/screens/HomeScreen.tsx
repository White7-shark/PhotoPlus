import React, { useState } from 'react';
import { Alert, Image, Pressable, View } from 'react-native';

import {
  AppText,
  Badge,
  Button,
  EmptyState,
  GlassSurface,
  Icon,
  IconButton,
  Logo,
  PremiumBadge,
  PressableCard,
  Screen,
  SectionHeader,
  Skeleton,
} from '../components';
import { DEVELOPER_TOOLS } from '../app/developer';
import { useResponsive } from '../hooks/useResponsive';
import type { TabScreenProps } from '../navigation/types';
import { errorCode } from '../native';
import { tierLabel } from '../services/capabilities';
import { friendlyMessage } from '../services/errors';
import { importPhoto } from '../services/library/importPhoto';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { greetingFor, PILLARS } from './pillars';

export function HomeScreen({ navigation }: TabScreenProps<'Studio'>) {
  const theme = useTheme();
  const r = useResponsive();
  const projects = useAppStore(s => s.projects);
  const projectsStatus = useAppStore(s => s.projectsStatus);
  const tier = useAppStore(s => s.tier);
  const warnings = useAppStore(s => s.warnings);
  const dismissWarning = useAppStore(s => s.dismissWarning);
  const loadProjects = useAppStore(s => s.loadProjects);
  const repos = useAppStore(s => s.repos);
  const [importing, setImporting] = useState(false);
  const greeting = greetingFor(new Date());

  const editPhoto = async () => {
    if (!repos) {
      return;
    }
    setImporting(true);
    try {
      const id = await importPhoto(repos);
      if (id) {
        void loadProjects();
        navigation.navigate('PhotoPreview', { imageId: id });
      }
    } catch (e) {
      Alert.alert('Couldn’t open that photo', friendlyMessage(errorCode(e)));
    } finally {
      setImporting(false);
    }
  };
  const gap = 12;
  const cardWidth = (r.contentWidth - gap * (r.columns - 1)) / r.columns;

  return (
    <Screen onRefresh={loadProjects} refreshing={projectsStatus === 'loading'}>
      <View style={{ flexDirection: 'row', alignItems: 'center', marginBottom: 28 }}>
        <View style={{ flex: 1 }}>
          <Logo size={34} />
        </View>
        {DEVELOPER_TOOLS ? (
          <IconButton icon="cpu" label="Developer tools" onPress={() => navigation.navigate('DeviceInfo')} size={44} />
        ) : null}
      </View>

      {warnings.map((w, i) => (
        <GlassSurface
          key={w}
          radius={theme.radii.md}
          style={{ flexDirection: 'row', alignItems: 'center', gap: 10, padding: 12, marginBottom: 12 }}
          accessibilityLiveRegion="polite"
        >
          <Icon name="info" size={18} color={theme.colors.warning} />
          <AppText variant="callout" tone="secondary" style={{ flex: 1 }}>
            {w}
          </AppText>
          <IconButton icon="close" label="Dismiss notice" size={36} onPress={() => dismissWarning(i)} />
        </GlassSurface>
      ))}

      <AppText variant="overline" tone="accent">
        {greeting.overline.toUpperCase()}
      </AppText>
      <AppText variant="display" style={{ marginTop: 6, marginBottom: 22 }} accessibilityRole="header">
        {greeting.title}
      </AppText>

      {/* Hero */}
      <GlassSurface radius={theme.radii.xl} glow="rgba(242,71,126,0.35)" style={{ padding: 0, marginBottom: 28 }}>
        <View
          style={{
            padding: 22,
            backgroundImage:
              'linear-gradient(120deg, rgba(255,122,69,0.30) 0%, rgba(242,71,126,0.18) 45%, rgba(139,92,246,0.10) 100%)',
          }}
        >
          <Badge label="New shoot" tone="accent" icon="sparkles" />
          <AppText variant="title2" style={{ marginTop: 14 }}>
            Start a photoshoot
          </AppText>
          <AppText variant="body" tone="secondary" style={{ marginTop: 6, marginBottom: 18, maxWidth: 420 }}>
            Shoot with Clarity, Spotlight and more — or bring in a photo from your gallery. Everything stays private on
            this device.
          </AppText>
          <View style={{ flexDirection: 'row', gap: 10, flexWrap: 'wrap' }}>
            <Button label="Open camera" icon="camera" size="lg" onPress={() => navigation.navigate('Camera')} />
            <Button
              label="Edit a photo"
              icon="image"
              variant="glass"
              loading={importing}
              onPress={() => void editPhoto()}
              accessibilityHint="Choose a photo from your gallery to make it shine"
            />
          </View>
        </View>
      </GlassSurface>

      <SectionHeader overline="The four pillars" title="Your studio" />
      <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap, marginBottom: 28 }}>
        {PILLARS.map(p => (
          <PressableCard
            key={p.id}
            label={`${p.title}. ${p.tagline}. ${p.premium ? 'Premium. ' : ''}${p.availability}.`}
            onPress={() => (p.id === 'camera' ? navigation.navigate('Camera') : navigation.navigate('Packs'))}
            style={{ width: cardWidth, minHeight: 170 }}
            radius={theme.radii.lg}
          >
            <View style={{ flex: 1, padding: 16, backgroundImage: p.gradient }}>
              <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'flex-start' }}>
                <View
                  style={{
                    width: 42,
                    height: 42,
                    borderRadius: 14,
                    alignItems: 'center',
                    justifyContent: 'center',
                    backgroundColor: 'rgba(255,255,255,0.14)',
                    borderWidth: 1,
                    borderColor: 'rgba(255,255,255,0.22)',
                  }}
                >
                  <Icon name={p.icon} size={22} color="#FFFFFF" />
                </View>
                {p.premium ? <PremiumBadge /> : null}
              </View>
              <View style={{ flex: 1 }} />
              <AppText variant="bodyStrong" style={{ marginTop: 18 }}>
                {p.title}
              </AppText>
              <AppText variant="caption" tone="secondary" style={{ marginTop: 4 }} numberOfLines={2}>
                {p.tagline}
              </AppText>
            </View>
          </PressableCard>
        ))}
      </View>

      <SectionHeader
        overline="Library"
        title="Recent projects"
        action={
          projects.length ? (
            <Button label="See all" variant="ghost" onPress={() => navigation.navigate('Projects')} />
          ) : undefined
        }
      />
      {projectsStatus === 'loading' && projects.length === 0 ? (
        <View style={{ flexDirection: 'row', gap }}>
          <Skeleton height={150} width="48%" radius={20} />
          <Skeleton height={150} width="48%" radius={20} />
        </View>
      ) : projects.length === 0 ? (
        <EmptyState
          icon="image"
          title="No projects yet"
          message="Your captures land in a Captures project automatically."
          actionLabel="Take your first photo"
          onAction={() => navigation.navigate('Camera')}
        />
      ) : (
        <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap }}>
          {projects.slice(0, r.columns * 2).map(p => (
            <Pressable
              key={p.id}
              onPress={() => navigation.navigate('Projects')}
              accessibilityRole="button"
              accessibilityLabel={`${p.name}, ${p.imageCount} photos`}
              style={{ width: cardWidth }}
            >
              <GlassSurface radius={theme.radii.lg} style={{ height: 150, justifyContent: 'flex-end' }}>
                {p.coverPath ? (
                  <Image
                    source={{ uri: `file://${p.coverPath}` }}
                    style={{ position: 'absolute', top: 0, left: 0, right: 0, bottom: 0 }}
                    resizeMode="cover"
                  />
                ) : null}
                <View style={{ padding: 12, backgroundImage: theme.gradients.scrim }}>
                  <AppText variant="bodyStrong" numberOfLines={1} style={{ color: '#FFFFFF' }}>
                    {p.name}
                  </AppText>
                  <AppText variant="caption" style={{ color: 'rgba(255,255,255,0.8)' }}>
                    {p.imageCount} {p.imageCount === 1 ? 'photo' : 'photos'}
                  </AppText>
                </View>
              </GlassSurface>
            </Pressable>
          ))}
        </View>
      )}

      {DEVELOPER_TOOLS ? (
        <Pressable
          onPress={() => navigation.navigate('DeviceInfo')}
          accessibilityRole="button"
          accessibilityLabel={`Device readiness: ${tierLabel[tier]}. View details`}
          style={{ marginTop: 28 }}
        >
          <GlassSurface
            radius={theme.radii.lg}
            style={{ flexDirection: 'row', alignItems: 'center', gap: 14, padding: 16 }}
          >
            <View
              style={{
                width: 44,
                height: 44,
                borderRadius: 22,
                alignItems: 'center',
                justifyContent: 'center',
                backgroundImage: theme.gradients.lagoon,
              }}
            >
              <Icon name="shield" size={22} color="#FFFFFF" />
            </View>
            <View style={{ flex: 1 }}>
              <AppText variant="bodyStrong">Device readiness</AppText>
              <AppText variant="caption" tone="secondary">
                {tier === 'unsupported'
                  ? 'Below the recommended specs — heavy features will be limited.'
                  : `${tierLabel[tier]} tier · all core features available offline`}
              </AppText>
            </View>
            <Icon name="chevronRight" size={18} color={theme.colors.textTertiary} />
          </GlassSurface>
        </Pressable>
      ) : null}
    </Screen>
  );
}
