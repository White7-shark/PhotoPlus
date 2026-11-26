import React, { useState } from 'react';
import { View } from 'react-native';

import { AppText, Button, SegmentedControl } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { GLOW_LEVELS, type GlowLevel } from '../../services/camera/model';
import {
  NEUTRAL_PORTRAIT,
  portraitFromGlow,
  type EditDocument,
  type PortraitAdvanced,
} from '../../services/edit/document';
import { useTheme } from '../../theme';
import type { Update } from './panels';
import { t as tx } from '../../i18n';

const glowOf = (v: number): GlowLevel =>
  GLOW_LEVELS.reduce((a, b) => (Math.abs(b.retouch - v) < Math.abs(a.retouch - v) ? b : a)).id;
const pct = (v: number) => `${Math.round(v * 100)}`;
const signed = (v: number) => `${v > 0 ? '+' : ''}${Math.round(v * 100)}`;

type Key = keyof PortraitAdvanced;
const SECTIONS: readonly { title: string; rows: readonly { key: Key; label: string }[] }[] = [
  {
    title: tx('portrait_skin'),
    rows: [
      { key: 'smooth', label: tx('portrait_smooth') },
      { key: 'blemish', label: tx('portrait_blemishes') },
      { key: 'even', label: tx('portrait_even_tone') },
      { key: 'shine', label: tx('portrait_shine') },
      { key: 'underEye', label: tx('portrait_under_eyes') },
    ],
  },
  {
    title: tx('portrait_face'),
    rows: [
      { key: 'eyes', label: tx('portrait_eyes') },
      { key: 'teeth', label: tx('portrait_teeth') },
      { key: 'lips', label: tx('portrait_lips') },
      { key: 'slim', label: tx('portrait_slim') },
    ],
  },
  {
    title: 'Light',
    rows: [
      { key: 'faceLight', label: tx('portrait_face_light') },
      { key: 'studio', label: tx('portrait_studio_light') },
    ],
  },
];

function Section({ title }: { title: string }) {
  const theme = useTheme();
  return (
    <AppText variant="caption" style={{ color: theme.colors.textSecondary, fontWeight: '700', letterSpacing: 0.6 }}>
      {title.toUpperCase()}
    </AppText>
  );
}

/** A Premium area shown to free users: what it does, and the way in. */
function LockedCard({ text, onPremium, testID }: { text: string; onPremium: () => void; testID: string }) {
  return (
    <View style={{ gap: 8 }}>
      <AppText variant="caption" tone="secondary">
        {text}
      </AppText>
      <Button
        label={tx('portrait_unlock_with_premium')}
        icon="crown"
        variant="glass"
        onPress={onPremium}
        testID={testID}
      />
    </View>
  );
}

/**
 * Portrait: Glow presets for everyone, and (Premium) every control on its
 * own: skin, face and light, each with its own intensity. Changes preview
 * live, every drag is one undo step, and Restore original returns the face
 * to how it was.
 */
export function PortraitPanel({
  doc,
  update,
  commit,
  locked,
  onPremium,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  locked: boolean;
  onPremium: () => void;
}) {
  const [mode, setMode] = useState<'basic' | 'advanced'>(doc.portrait ? 'advanced' : 'basic');
  const adv = doc.portrait;
  const current = adv ?? portraitFromGlow(doc.retouch);
  const set = (patch: Partial<PortraitAdvanced>, group: string) =>
    update({ portrait: { ...current, ...patch } }, group);
  const touched = doc.retouch > 0 || adv !== null;

  return (
    <View style={{ gap: 12 }}>
      <SegmentedControl
        label={tx('portrait_controls')}
        value={mode}
        onChange={m => {
          if (m === 'advanced' && locked) {
            onPremium();
            return;
          }
          setMode(m);
        }}
        options={[
          { value: 'basic', label: tx('portrait_basic') },
          { value: 'advanced', label: tx('portrait_advanced') },
        ]}
      />
      {mode === 'basic' ? (
        <>
          <AppText variant="body" tone="secondary">
            {tx('portrait_glow_smooths_skin_clears_blemishes')}
          </AppText>
          <SegmentedControl
            label="Glow"
            value={glowOf(doc.retouch)}
            // A preset replaces any fine-tuning (undo brings it back).
            onChange={id => update({ retouch: GLOW_LEVELS.find(g => g.id === id)?.retouch ?? 0, portrait: null })}
            options={GLOW_LEVELS.map(g => ({ value: g.id, label: g.label }))}
          />
          {adv ? (
            <AppText variant="caption" tone="secondary" testID="portrait-finetuned">
              {tx('portrait_fine_tuned_in_advanced_choosing')}
            </AppText>
          ) : null}
        </>
      ) : locked ? (
        <LockedCard text={tx('portrait_shape_every_detail_on_its')} onPremium={onPremium} testID="portrait-unlock" />
      ) : (
        <>
          {SECTIONS.map(section => (
            <View key={section.title} style={{ gap: 10 }}>
              <Section title={section.title} />
              {section.rows.map(r => (
                <ValueSlider
                  key={r.key}
                  label={r.label}
                  value={current[r.key]}
                  format={pct}
                  onChange={v => set({ [r.key]: Math.round(v * 100) / 100 }, `portrait-${r.key}`)}
                  onEnd={commit}
                  testID={`portrait-${r.key}`}
                />
              ))}
              {section.title === 'Light' && current.studio > 0 ? (
                <ValueSlider
                  label={tx('portrait_light_from')}
                  value={current.lightAngle}
                  min={-1}
                  max={1}
                  format={v => (Math.abs(v) < 0.05 ? 'Front' : v < 0 ? `Left ${pct(-v)}` : `Right ${pct(v)}`)}
                  onChange={v => set({ lightAngle: Math.round(v * 100) / 100 }, 'portrait-lightAngle')}
                  onEnd={commit}
                  testID="portrait-lightAngle"
                />
              ) : null}
            </View>
          ))}
          <View style={{ flexDirection: 'row', gap: 10 }}>
            <Button
              label={tx('portrait_reset_to_glow')}
              icon="refresh"
              variant="glass"
              style={{ flex: 1 }}
              onPress={() =>
                update({ portrait: adv ? { ...portraitFromGlow(doc.retouch), ...backgroundOf(adv) } : null })
              }
              testID="portrait-reset"
            />
          </View>
        </>
      )}
      {touched ? (
        <Button
          label={tx('portrait_restore_original')}
          icon="undo"
          variant="ghost"
          onPress={() => update({ retouch: 0, portrait: adv ? backgroundOnly(adv) : null })}
          testID="portrait-restore"
        />
      ) : null}
    </View>
  );
}

const backgroundOf = (p: PortraitAdvanced): Pick<PortraitAdvanced, 'bgLight' | 'bgColor' | 'edge'> => ({
  bgLight: p.bgLight,
  bgColor: p.bgColor,
  edge: p.edge,
});

/** Face and skin back to how they were; background choices (Spotlight's) stay. */
function backgroundOnly(p: PortraitAdvanced): PortraitAdvanced | null {
  const keep = { ...NEUTRAL_PORTRAIT, ...backgroundOf(p) };
  return keep.bgLight !== 0 || keep.bgColor !== 0 || keep.edge !== 0 ? keep : null;
}

/**
 * Spotlight's advanced background controls (Premium): any blur amount, the
 * background's own light and colour (down to black and white), and finer
 * edges around hair with no fringe of the old background.
 */
export function SpotlightAdvanced({
  doc,
  update,
  commit,
  locked,
  onPremium,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  locked: boolean;
  onPremium: () => void;
}) {
  const s = doc.spotlight;
  const current = doc.portrait ?? portraitFromGlow(doc.retouch);
  const set = (patch: Partial<PortraitAdvanced>, group: string) =>
    update({ portrait: { ...current, ...patch } }, group);
  if (locked) {
    return (
      <LockedCard text={tx('portrait_set_any_blur_amount_relight')} onPremium={onPremium} testID="spotlight-unlock" />
    );
  }
  return (
    <View style={{ gap: 10 }}>
      <Section title={tx('portrait_background')} />
      {s ? (
        <ValueSlider
          label={tx('portrait_blur_amount')}
          value={s.blur}
          min={0.1}
          max={1}
          format={pct}
          onChange={v => update({ spotlight: { ...s, blur: Math.round(v * 100) / 100 } }, 'spotlight-blur')}
          onEnd={commit}
          testID="spotlight-blur"
        />
      ) : null}
      <ValueSlider
        label={tx('portrait_background_light')}
        value={current.bgLight}
        min={-1}
        max={1}
        format={signed}
        onChange={v => set({ bgLight: Math.round(v * 100) / 100 }, 'portrait-bgLight')}
        onEnd={commit}
        testID="portrait-bgLight"
      />
      <ValueSlider
        label={tx('portrait_background_colour')}
        value={current.bgColor}
        min={-1}
        max={1}
        format={v => (v <= -0.99 ? 'B&W' : signed(v))}
        onChange={v => set({ bgColor: Math.round(v * 100) / 100 }, 'portrait-bgColor')}
        onEnd={commit}
        testID="portrait-bgColor"
      />
      <ValueSlider
        label={tx('portrait_edge_refinement')}
        value={current.edge}
        format={pct}
        onChange={v => set({ edge: Math.round(v * 100) / 100 }, 'portrait-edge')}
        onEnd={commit}
        testID="portrait-edge"
      />
    </View>
  );
}
