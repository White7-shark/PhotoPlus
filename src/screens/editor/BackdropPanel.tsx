import React from 'react';
import { ScrollView, View } from 'react-native';

import { AppText, PreviewTile } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { BACKDROPS, backdropCover, findBackdrop } from '../../services/backdrops/catalog';
import type { EditDocument } from '../../services/edit/document';
import type { Update } from './panels';

/** Backdrops: a new background behind the person, and how strongly it shows. */
export function BackdropEditPanel({
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
  const current = doc.backdrop;
  const strength = current?.strength ?? 1;
  return (
    <View style={{ gap: 12 }} testID="backdrop-panel">
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 10 }}>
        <PreviewTile
          label="None"
          placeholderIcon="close"
          width={68}
          height={68}
          selected={!current}
          onPress={() => update({ backdrop: null })}
        />
        {BACKDROPS.map(b => {
          const locked = b.tier === 'premium' && !premiumAllowed;
          return (
            <PreviewTile
              key={b.id}
              label={b.name}
              source={backdropCover(b.id)}
              width={68}
              height={68}
              locked={locked}
              selected={current?.id === b.id}
              onPress={() => (locked ? onPremium() : update({ backdrop: { id: b.id, strength } }))}
              testID={`backdrop-${b.id}`}
            />
          );
        })}
      </ScrollView>
      {current ? (
        <>
          <AppText variant="caption" tone="secondary">
            {findBackdrop(current.id)?.description}
          </AppText>
          <ValueSlider
            label="Strength"
            value={strength}
            onChange={v => update({ backdrop: { id: current.id, strength: v } }, 'backdrop-strength')}
            onEnd={commit}
            format={v => `${Math.round(v * 100)}%`}
            testID="backdrop-strength"
          />
        </>
      ) : (
        <AppText variant="caption" tone="secondary">
          Works best with a person in the photo. Nothing leaves your phone.
        </AppText>
      )}
    </View>
  );
}
