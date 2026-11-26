import React, { useState } from 'react';
import {
  Image,
  Pressable,
  ScrollView,
  TextInput,
  View,
  type GestureResponderEvent,
  type LayoutChangeEvent,
} from 'react-native';

import { AppText, Button, GlassSurface, Icon, Screen, SegmentedControl } from '../components';
import { PremiumSheet } from '../components/PremiumSheet';
import { useFeatureCheck } from '../hooks/useFeature';
import type { RootScreenProps } from '../navigation/types';
import { errorCode } from '../native';
import { CUSTOM_PROP_ID, MAX_CUSTOM_PROPS, type CustomProp, type PropAnchor } from '../services/edit/catalog';
import { getProcessingService } from '../services/processing/ImageProcessingService';
import { getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { SpotlightMarkers } from './camera/SpotlightControls';
import { containRect } from './editor/geometry';
import { t as tx } from '../i18n';

const PLACES: { value: PropAnchor; label: string }[] = [
  { value: 'head', label: tx('create_prop_head') },
  { value: 'eyes', label: tx('create_prop_eyes') },
  { value: 'neck', label: tx('create_prop_neck') },
  { value: 'free', label: tx('create_prop_anywhere') },
];

const MAX_TAPS = 4;

/**
 * Premium: make a prop from any photo. Tap the object (a hat, glasses, a
 * pet, a logo…), it is cut out with soft edges, then named and given a place
 * (head, eyes, neck or anywhere) so it fits faces like the built-in props.
 */
export function CreatePropScreen({ navigation }: RootScreenProps<'CreateProp'>) {
  const theme = useTheme();
  const can = useFeatureCheck();
  const settings = useAppStore(s => s.settings);
  const updateSetting = useAppStore(s => s.updateSetting);
  const allowed = can('customProps').allowed;
  const [photo, setPhoto] = useState<{ path: string; width: number; height: number } | null>(null);
  const [taps, setTaps] = useState<{ x: number; y: number }[]>([]);
  const [view, setView] = useState({ width: 0, height: 0 });
  const [cut, setCut] = useState<{ id: string; path: string; width: number; height: number } | null>(null);
  const [name, setName] = useState('');
  const [place, setPlace] = useState<PropAnchor>('head');
  const [busy, setBusy] = useState<'pick' | 'cut' | 'save' | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [premiumOpen, setPremiumOpen] = useState(!allowed);
  const mine = settings.customProps;

  const pick = async () => {
    setBusy('pick');
    setError(null);
    try {
      const path = await getStorageService().pickImage('imports');
      if (!path) return;
      Image.getSize(
        `file://${path}`,
        (width, height) => {
          setPhoto({ path, width, height });
          setTaps([]);
          setCut(null);
        },
        () => setError(tx('create_prop_that_photo_couldn_t_be')),
      );
    } finally {
      setBusy(null);
    }
  };

  const area = photo ? containRect(view.width, view.height, photo.width, photo.height) : null;

  const onTap = (e: GestureResponderEvent) => {
    if (!area || area.w === 0 || cut) return;
    const x = (e.nativeEvent.locationX - area.x) / area.w;
    const y = (e.nativeEvent.locationY - area.y) / area.h;
    if (x < 0 || x > 1 || y < 0 || y > 1) return;
    setTaps(t => [...t, { x, y }].slice(-MAX_TAPS));
  };

  const cutOut = async () => {
    if (!photo) return;
    setBusy('cut');
    setError(null);
    try {
      const r = await getProcessingService().createCustomProp(
        photo.path,
        taps.length > 0 ? taps : [{ x: 0.5, y: 0.5 }],
      );
      setCut(r);
      setName(n => n || `My prop ${mine.length + 1}`);
    } catch (e) {
      if (errorCode(e) === 'PREMIUM_REQUIRED') {
        setPremiumOpen(true);
      } else {
        setError(tx('create_prop_nothing_could_be_cut_out'));
      }
    } finally {
      setBusy(null);
    }
  };

  const save = async () => {
    if (!cut || !CUSTOM_PROP_ID.test(cut.id)) return;
    setBusy('save');
    const prop: CustomProp = {
      id: cut.id,
      name: name.trim().slice(0, 40) || tx('create_prop_my_prop', { value: mine.length + 1 }),
      anchor: place,
      path: cut.path,
      aspect: cut.height / Math.max(1, cut.width),
      createdAt: Date.now(),
    };
    try {
      await updateSetting('customProps', [...mine, prop].slice(-MAX_CUSTOM_PROPS));
      navigation.goBack();
    } catch {
      setError(tx('create_prop_your_prop_couldn_t_be'));
      setBusy(null);
    }
  };

  const remove = async (p: CustomProp) => {
    await getProcessingService().deleteCustomProp(p.id);
    await updateSetting(
      'customProps',
      mine.filter(m => m.id !== p.id),
    ).catch(() => undefined);
  };

  return (
    <Screen title={tx('create_prop_make_a_prop')} onBack={() => navigation.goBack()} bottomInset={32}>
      <View style={{ gap: 14 }}>
        <AppText variant="body" tone="secondary">
          {tx('create_prop_turn_anything_in_a_photo')}
        </AppText>

        {!photo ? (
          <Button
            label={tx('create_prop_choose_a_photo')}
            icon="image"
            onPress={() => void pick()}
            loading={busy === 'pick'}
            disabled={!allowed}
            testID="create-prop-pick"
          />
        ) : (
          <>
            <View
              onLayout={(e: LayoutChangeEvent) =>
                setView({ width: e.nativeEvent.layout.width, height: e.nativeEvent.layout.height })
              }
              onStartShouldSetResponder={() => !cut}
              onResponderRelease={onTap}
              accessibilityLabel={tx('create_prop_photo_tap_the_object_to')}
              testID="create-prop-photo"
              style={{ width: '100%', aspectRatio: 0.9, borderRadius: 18, overflow: 'hidden', backgroundColor: '#000' }}
            >
              {cut ? (
                <View
                  style={{
                    flex: 1,
                    alignItems: 'center',
                    justifyContent: 'center',
                    backgroundColor: 'rgba(10,9,17,0.9)',
                  }}
                >
                  <Image
                    source={{ uri: `file://${cut.path}` }}
                    style={{ width: '80%', height: '80%' }}
                    resizeMode="contain"
                  />
                </View>
              ) : (
                <>
                  <Image
                    source={{ uri: `file://${photo.path}` }}
                    style={{ width: '100%', height: '100%' }}
                    resizeMode="contain"
                  />
                  {area ? (
                    <View
                      pointerEvents="none"
                      style={{ position: 'absolute', left: area.x, top: area.y, width: area.w, height: area.h }}
                    >
                      <SpotlightMarkers points={taps} frame={{ width: area.w, height: area.h }} />
                    </View>
                  ) : null}
                </>
              )}
            </View>

            {!cut ? (
              <>
                <AppText variant="caption" tone="secondary" align="center">
                  {taps.length === 0
                    ? tx('create_prop_tap_the_object_you_want')
                    : tx('create_prop_tap_again_to_add_more')}
                </AppText>
                <View style={{ flexDirection: 'row', gap: 10 }}>
                  <Button
                    label={tx('create_prop_other_photo')}
                    icon="image"
                    variant="glass"
                    onPress={() => void pick()}
                    style={{ flex: 1 }}
                  />
                  <Button
                    label={tx('create_prop_cut_it_out')}
                    icon="sparkles"
                    onPress={() => void cutOut()}
                    loading={busy === 'cut'}
                    disabled={taps.length === 0 || busy !== null}
                    style={{ flex: 1 }}
                    testID="create-prop-cut"
                  />
                </View>
              </>
            ) : (
              <GlassSurface style={{ padding: 16, gap: 12 }}>
                <TextInput
                  value={name}
                  onChangeText={t => setName(t.slice(0, 40))}
                  placeholder={tx('create_prop_name')}
                  placeholderTextColor={theme.colors.textSecondary}
                  accessibilityLabel={tx('create_prop_prop_name')}
                  testID="create-prop-name"
                  style={{
                    borderWidth: 1,
                    borderColor: theme.colors.glassStroke,
                    borderRadius: 12,
                    paddingHorizontal: 12,
                    paddingVertical: 10,
                    color: theme.colors.textPrimary,
                  }}
                />
                <SegmentedControl
                  label={tx('create_prop_where_it_goes')}
                  value={place}
                  onChange={setPlace}
                  options={PLACES}
                />
                <View style={{ flexDirection: 'row', gap: 10 }}>
                  <Button
                    label={tx('create_prop_try_again')}
                    icon="refresh"
                    variant="glass"
                    onPress={() => setCut(null)}
                    style={{ flex: 1 }}
                  />
                  <Button
                    label={tx('create_prop_save_prop')}
                    icon="check"
                    onPress={() => void save()}
                    loading={busy === 'save'}
                    style={{ flex: 1 }}
                    testID="create-prop-save"
                  />
                </View>
              </GlassSurface>
            )}
          </>
        )}

        {error ? (
          <AppText variant="caption" tone="secondary" align="center">
            {error}
          </AppText>
        ) : null}

        {mine.length > 0 ? (
          <View style={{ gap: 8 }}>
            <AppText variant="bodyStrong">{tx('create_prop_your_props')}</AppText>
            <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
              {mine.map(p => (
                <View key={p.id} style={{ width: 76, alignItems: 'center', gap: 6 }}>
                  <View
                    style={{
                      width: 76,
                      height: 76,
                      borderRadius: 14,
                      backgroundColor: 'rgba(10,9,17,0.55)',
                      alignItems: 'center',
                      justifyContent: 'center',
                      borderWidth: 1,
                      borderColor: theme.colors.glassStroke,
                    }}
                  >
                    <Image
                      source={{ uri: `file://${p.path}` }}
                      style={{ width: 66, height: 66 }}
                      resizeMode="contain"
                    />
                    <Pressable
                      onPress={() => void remove(p)}
                      accessibilityRole="button"
                      accessibilityLabel={tx('create_prop_delete', { name: p.name })}
                      hitSlop={8}
                      style={{
                        position: 'absolute',
                        right: 4,
                        top: 4,
                        width: 22,
                        height: 22,
                        borderRadius: 11,
                        alignItems: 'center',
                        justifyContent: 'center',
                        backgroundColor: 'rgba(10,9,17,0.75)',
                      }}
                    >
                      <Icon name="trash" size={12} color="#fff" />
                    </Pressable>
                  </View>
                  <AppText variant="caption" numberOfLines={1}>
                    {p.name}
                  </AppText>
                </View>
              ))}
            </ScrollView>
          </View>
        ) : null}
      </View>
      <PremiumSheet
        feature={premiumOpen ? 'customProps' : null}
        onClose={() => {
          setPremiumOpen(false);
          if (!allowed) navigation.goBack();
        }}
      />
    </Screen>
  );
}
