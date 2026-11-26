import React from 'react';
import { ScrollView, View } from 'react-native';

import { AppText, PreviewTile } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import type { EditDocument } from '../../services/edit/document';
import { findShade, SKIN_SHADES, shadeCover } from '../../services/skintones/catalog';
import type { Update } from './panels';
import { t as tx } from '../../i18n';

/** Skin Tone: a shade for the person's skin (face and body), and how strongly. */
export function SkinTonePanel({
  doc,
  update,
  commit,
  premiumAllowed,
  onPremium,
  noFace,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  premiumAllowed: boolean;
  onPremium: () => void;
  /** The last render found no face to measure: nothing was changed. */
  noFace?: boolean;
}) {
  const current = doc.skinTone;
  const strength = current?.strength ?? 0.8;
  return (
    <View style={{ gap: 12 }} testID="skintone-panel">
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
        <PreviewTile
          label={tx('skin_tone_none')}
          placeholderIcon="close"
          width={68}
          height={68}
          selected={!current}
          onPress={() => update({ skinTone: null })}
        />
        {SKIN_SHADES.map(s => {
          const locked = s.plan === 'premium' && !premiumAllowed;
          return (
            <PreviewTile
              key={s.id}
              label={s.name}
              source={shadeCover(s.id)}
              width={68}
              height={68}
              locked={locked}
              selected={current?.id === s.id}
              onPress={() => (locked ? onPremium() : update({ skinTone: { id: s.id, strength } }))}
              testID={`skintone-${s.id}`}
            />
          );
        })}
      </ScrollView>
      {current ? (
        <>
          <AppText variant="caption" tone="secondary">
            {noFace ? tx('skin_tone_no_face_found_in_this') : findShade(current.id)?.note}
          </AppText>
          <ValueSlider
            label={tx('skin_tone_strength')}
            value={strength}
            onChange={v => update({ skinTone: { id: current.id, strength: v } }, 'skintone-strength')}
            onEnd={commit}
            format={v => `${Math.round(v * 100)}%`}
            testID="skintone-strength"
          />
        </>
      ) : (
        <AppText variant="caption" tone="secondary">
          {tx('skin_tone_changes_the_colour_of_the')}
        </AppText>
      )}
    </View>
  );
}
