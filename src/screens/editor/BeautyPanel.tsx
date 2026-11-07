import React from 'react';
import { ScrollView, View } from 'react-native';

import { AppText, PreviewTile } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { BEAUTY_PRESETS, findPreset, presetCover } from '../../services/beauty/catalog';
import { DEFAULT_ADJUST, GROUP_MAX, resolveRecipe, type BeautyAdjust } from '../../services/beauty/recipe';
import type { EditDocument } from '../../services/edit/document';
import type { Update } from './panels';

const pct = (v: number) => `${Math.round(v * 100)}%`;

/**
 * Beauty on a saved or imported photo: the same 13 filters as the camera,
 * Intensity and the Skin / Face / Lighting / Background sliders.
 */
export function BeautyEditPanel({
  doc,
  update,
  commit,
  premiumAllowed,
  onPremium,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  premiumAllowed: boolean;
  onPremium: () => void;
}) {
  const current = doc.beauty;
  const set = (preset: string | null, adjust: BeautyAdjust, group?: string) => {
    const p = findPreset(preset);
    update({ beauty: p ? { preset: p.id, adjust, recipe: resolveRecipe(p, adjust) } : null }, group);
  };
  const adjust = current?.adjust ?? DEFAULT_ADJUST;
  return (
    <View style={{ gap: 12 }} testID="beauty-panel">
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
        <PreviewTile
          label="None"
          placeholderIcon="close"
          width={68}
          height={68}
          selected={!current}
          onPress={() => set(null, adjust)}
        />
        {BEAUTY_PRESETS.map(p => {
          const locked = p.tier === 'premium' && !premiumAllowed;
          return (
            <PreviewTile
              key={p.id}
              label={p.name}
              source={presetCover(p.id)}
              width={68}
              height={68}
              locked={locked}
              selected={current?.preset === p.id}
              onPress={() => (locked ? onPremium() : set(p.id, adjust))}
              testID={`edit-beauty-${p.id}`}
            />
          );
        })}
      </ScrollView>
      {current ? (
        <>
          <AppText variant="caption" tone="secondary">
            {findPreset(current.preset)?.description}
          </AppText>
          <ValueSlider
            label="Intensity"
            value={adjust.intensity}
            onChange={v => set(current.preset, { ...adjust, intensity: v }, 'beauty-intensity')}
            onEnd={commit}
            format={pct}
            testID="edit-beauty-intensity"
          />
          {(
            [
              ['skin', 'Skin'],
              ['face', 'Face'],
              ['lighting', 'Lighting'],
              ['background', 'Background'],
            ] as const
          ).map(([key, label]) => (
            <ValueSlider
              key={key}
              label={label}
              value={adjust[key]}
              max={GROUP_MAX}
              onChange={v => set(current.preset, { ...adjust, [key]: v }, `beauty-${key}`)}
              onEnd={commit}
              format={pct}
              testID={`edit-beauty-${key}`}
            />
          ))}
        </>
      ) : null}
    </View>
  );
}
