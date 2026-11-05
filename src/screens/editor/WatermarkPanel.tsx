import React, { useState } from 'react';
import { Image, Pressable, ScrollView, TextInput, View } from 'react-native';

import { AppText, Button, Icon, SegmentedControl } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { FONTS, WATERMARK_STYLES } from '../../services/edit/catalog';
import {
  DEFAULT_POSITION,
  DEFAULT_WATERMARK,
  POSITIONS,
  type BrandingPreset,
  type WatermarkPosition,
  type WatermarkSpec,
} from '../../services/edit/watermark';
import { useTheme } from '../../theme';

const SWATCHES = ['#FFFFFF', '#111111', '#F5C66B', '#FF7A45', '#E8DCCB', '#8B5CF6', '#3DDC97', '#4F8BFF'];

const POSITION_NAMES: Record<WatermarkPosition, string> = {
  tl: 'Top left',
  tc: 'Top',
  tr: 'Top right',
  ml: 'Left',
  mc: 'Centre',
  mr: 'Right',
  bl: 'Bottom left',
  bc: 'Bottom',
  br: 'Bottom right',
};

/**
 * Watermark design. Free: the Photo Plus mark in any predefined style, font
 * and layout, within size and opacity limits, anywhere around the photo.
 * Premium: custom text, colour, logo, alignment, saved presets, or none.
 * The native export applies the plan whatever is chosen here.
 */
export function WatermarkPanel({
  spec,
  onChange,
  premium,
  onPremium,
  presets,
  onSavePreset,
  onDeletePreset,
  onPickLogo,
}: {
  spec: WatermarkSpec;
  onChange: (s: WatermarkSpec) => void;
  /** The native plan: custom branding only does anything on Premium. */
  premium: boolean;
  onPremium: () => void;
  presets: readonly BrandingPreset[];
  onSavePreset: (name: string) => void;
  onDeletePreset: (id: string) => void;
  onPickLogo: () => void;
}) {
  const theme = useTheme();
  const [presetName, setPresetName] = useState('');
  const set = (patch: Partial<WatermarkSpec>) => onChange({ ...spec, ...patch });
  const custom = spec.custom ?? { text: '', color: '#FFFFFF', logoPath: null, align: 'center' as const };
  const setCustom = (patch: Partial<typeof custom>) =>
    premium ? set({ custom: { ...custom, ...patch } }) : onPremium();
  return (
    <View style={{ gap: 14 }}>
      <AppText variant="caption" tone="secondary">
        {premium
          ? 'Your branding goes on every photo you share.'
          : 'Every shared photo carries the Photo Plus mark. Make it yours:'}
      </AppText>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        {WATERMARK_STYLES.map(s => (
          <Pressable
            key={s.id}
            onPress={() => set({ style: s.id })}
            accessibilityRole="button"
            accessibilityLabel={`${s.name} style`}
            accessibilityState={{ selected: spec.style === s.id }}
            testID={`wm-style-${s.id}`}
            style={{
              paddingHorizontal: 14,
              paddingVertical: 10,
              borderRadius: 14,
              borderWidth: 1,
              borderColor: spec.style === s.id ? theme.colors.accent : theme.colors.glassStroke,
              backgroundColor: spec.style === s.id ? 'rgba(255,122,69,0.14)' : theme.colors.glassFill,
            }}
          >
            <AppText variant="caption">{s.name}</AppText>
          </Pressable>
        ))}
      </ScrollView>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        {FONTS.map(f => (
          <Pressable
            key={f.id}
            onPress={() => set({ font: f.id })}
            accessibilityRole="button"
            accessibilityLabel={`${f.name} font`}
            accessibilityState={{ selected: spec.font === f.id }}
            style={{
              paddingHorizontal: 14,
              paddingVertical: 8,
              borderRadius: 14,
              borderWidth: 1,
              borderColor: spec.font === f.id ? theme.colors.accent : theme.colors.glassStroke,
            }}
          >
            <AppText variant="body" style={{ fontFamily: f.family }}>
              {f.name}
            </AppText>
          </Pressable>
        ))}
      </ScrollView>
      <SegmentedControl
        label="Layout"
        value={spec.layout}
        onChange={layout => set({ layout })}
        options={[
          { value: 'inline', label: 'Side by side' },
          { value: 'stacked', label: 'Stacked' },
        ]}
      />
      <ValueSlider label="Size" value={spec.size} onChange={size => set({ size })} testID="wm-size" />
      <ValueSlider label="Opacity" value={spec.opacity} onChange={opacity => set({ opacity })} testID="wm-opacity" />
      <View style={{ gap: 8 }}>
        <AppText variant="caption" tone="secondary">
          Position
        </AppText>
        <View style={{ flexDirection: 'row', flexWrap: 'wrap', width: 156, gap: 6 }}>
          {POSITIONS.map(p => (
            <Pressable
              key={p}
              onPress={() => set({ position: p })}
              accessibilityRole="button"
              accessibilityLabel={POSITION_NAMES[p]}
              accessibilityState={{ selected: spec.position === p }}
              testID={`wm-pos-${p}`}
              style={{
                width: 46,
                height: 34,
                borderRadius: 8,
                alignItems: 'center',
                justifyContent: 'center',
                borderWidth: 1,
                borderColor: spec.position === p ? theme.colors.accent : theme.colors.glassStroke,
                backgroundColor: spec.position === p ? 'rgba(255,122,69,0.2)' : theme.colors.glassFill,
              }}
            >
              <View
                style={{
                  width: 8,
                  height: 8,
                  borderRadius: 4,
                  backgroundColor: spec.position === p ? theme.colors.accent : theme.colors.textSecondary,
                }}
              />
            </Pressable>
          ))}
        </View>
        <Button
          label="Restore default"
          variant="ghost"
          icon="refresh"
          onPress={() =>
            set({ position: DEFAULT_POSITION, size: DEFAULT_WATERMARK.size, opacity: DEFAULT_WATERMARK.opacity })
          }
          testID="wm-restore"
        />
      </View>

      {/* Premium branding */}
      <View style={{ gap: 10, paddingTop: 6, borderTopWidth: 1, borderTopColor: theme.colors.glassStroke }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
          <Icon name="crown" size={14} color={theme.colors.premium} strokeWidth={2.2} />
          <AppText variant="bodyStrong">Your branding</AppText>
        </View>
        <TextInput
          value={custom.text}
          onChangeText={text => setCustom({ text: text.slice(0, 40) })}
          onFocus={() => !premium && onPremium()}
          editable={premium}
          placeholder="Your name or studio"
          placeholderTextColor={theme.colors.textSecondary}
          accessibilityLabel="Watermark text"
          testID="wm-text"
          style={{
            borderWidth: 1,
            borderColor: theme.colors.glassStroke,
            borderRadius: 12,
            paddingHorizontal: 12,
            paddingVertical: 10,
            color: theme.colors.textPrimary,
          }}
        />
        <View style={{ flexDirection: 'row', gap: 8, flexWrap: 'wrap' }}>
          {SWATCHES.map(c => (
            <Pressable
              key={c}
              onPress={() => setCustom({ color: c })}
              accessibilityRole="button"
              accessibilityLabel={`Colour ${c}`}
              accessibilityState={{ selected: custom.color === c }}
              style={{
                width: 30,
                height: 30,
                borderRadius: 15,
                backgroundColor: c,
                borderWidth: custom.color === c ? 3 : 1,
                borderColor: custom.color === c ? theme.colors.accent : theme.colors.glassStroke,
              }}
            />
          ))}
        </View>
        <View style={{ flexDirection: 'row', gap: 8, alignItems: 'center' }}>
          {custom.logoPath ? (
            <Image source={{ uri: `file://${custom.logoPath}` }} style={{ width: 36, height: 36, borderRadius: 8 }} />
          ) : null}
          <Button
            label={custom.logoPath ? 'Change logo' : 'Add your logo'}
            variant="glass"
            icon="image"
            onPress={() => (premium ? onPickLogo() : onPremium())}
            style={{ flex: 1 }}
            testID="wm-logo"
          />
          {custom.logoPath ? (
            <Button label="Remove" variant="ghost" onPress={() => setCustom({ logoPath: null })} />
          ) : null}
        </View>
        <SegmentedControl
          label="Alignment"
          value={custom.align}
          onChange={align => setCustom({ align })}
          options={[
            { value: 'left', label: 'Left' },
            { value: 'center', label: 'Centre' },
            { value: 'right', label: 'Right' },
          ]}
        />
        <SegmentedControl
          label="Watermark"
          value={spec.enabled ? 'on' : 'off'}
          onChange={v => (premium ? set({ enabled: v === 'on' }) : onPremium())}
          options={[
            { value: 'on', label: 'On' },
            { value: 'off', label: 'No watermark' },
          ]}
        />
        {presets.length ? (
          <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
            {presets.map(p => (
              <View key={p.id} style={{ flexDirection: 'row', alignItems: 'center' }}>
                <Button label={p.name} variant="glass" onPress={() => onChange(p.spec)} />
                <Pressable
                  onPress={() => onDeletePreset(p.id)}
                  accessibilityRole="button"
                  accessibilityLabel={`Delete ${p.name}`}
                  style={{ padding: 6 }}
                >
                  <Icon name="close" size={14} color={theme.colors.textSecondary} />
                </Pressable>
              </View>
            ))}
          </ScrollView>
        ) : null}
        <View style={{ flexDirection: 'row', gap: 8 }}>
          <TextInput
            value={presetName}
            onChangeText={t => setPresetName(t.slice(0, 30))}
            editable={premium}
            onFocus={() => !premium && onPremium()}
            placeholder="Preset name"
            placeholderTextColor={theme.colors.textSecondary}
            accessibilityLabel="Preset name"
            style={{
              flex: 1,
              borderWidth: 1,
              borderColor: theme.colors.glassStroke,
              borderRadius: 12,
              paddingHorizontal: 12,
              color: theme.colors.textPrimary,
            }}
          />
          <Button
            label="Save preset"
            variant="glass"
            icon="check"
            onPress={() => {
              if (!premium) {
                onPremium();
                return;
              }
              onSavePreset(presetName.trim() || `Brand ${presets.length + 1}`);
              setPresetName('');
            }}
            testID="wm-save-preset"
          />
        </View>
      </View>
    </View>
  );
}
