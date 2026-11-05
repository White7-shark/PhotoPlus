import React from 'react';
import { Image, Pressable, ScrollView, View } from 'react-native';

import { AppText, Badge, Button, Icon, SegmentedControl, type IconName } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { BLUR_LEVELS, GLOW_LEVELS, type BlurLevel, type GlowLevel } from '../../services/camera/model';
import {
  FRAMES,
  LOOKS,
  PROP_CATEGORIES,
  PROPS,
  propAsset,
  type PropCategory,
  type PropDef,
} from '../../services/edit/catalog';
import type { EditDocument } from '../../services/edit/document';
import { useTheme } from '../../theme';
import { CROP_ASPECTS } from './geometry';

export type ToolId =
  'adjust' | 'crop' | 'looks' | 'spotlight' | 'retouch' | 'focus' | 'transform' | 'props' | 'frames' | 'watermark';

export const EDITOR_TOOLS: readonly { id: ToolId; label: string; icon: IconName; premium?: boolean }[] = [
  { id: 'adjust', label: 'Adjust', icon: 'sliders' },
  { id: 'crop', label: 'Crop', icon: 'aspect' },
  { id: 'looks', label: 'Looks', icon: 'image' },
  { id: 'spotlight', label: 'Spotlight', icon: 'target' },
  { id: 'retouch', label: 'Retouch', icon: 'sparkles' },
  { id: 'focus', label: 'Focus', icon: 'exposure' },
  { id: 'transform', label: 'Transform', icon: 'aperture', premium: true },
  { id: 'props', label: 'Props', icon: 'wand' },
  { id: 'frames', label: 'Frames', icon: 'grid' },
  { id: 'watermark', label: 'Watermark', icon: 'layers' },
];

export type Update = (patch: Partial<EditDocument>, group?: string) => void;

const pct = (v: number) => `${v > 0 ? '+' : ''}${Math.round(v * 100)}`;

/** A row of selectable cards (looks, props, frames). */
function Chip({
  label,
  selected,
  premium,
  onPress,
  testID,
  children,
}: {
  label: string;
  selected: boolean;
  premium?: boolean;
  onPress: () => void;
  testID?: string;
  children?: React.ReactNode;
}) {
  const theme = useTheme();
  return (
    <Pressable
      onPress={onPress}
      testID={testID}
      accessibilityRole="button"
      accessibilityLabel={`${label}${premium ? ', premium' : ''}`}
      accessibilityState={{ selected }}
      style={{
        minWidth: 76,
        padding: 8,
        borderRadius: 14,
        alignItems: 'center',
        gap: 6,
        borderWidth: 1,
        borderColor: selected ? theme.colors.accent : theme.colors.glassStroke,
        backgroundColor: selected ? 'rgba(255,122,69,0.14)' : theme.colors.glassFill,
      }}
    >
      {children}
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 4 }}>
        <AppText variant="caption">{label}</AppText>
        {premium ? <Icon name="crown" size={12} color={theme.colors.premium} strokeWidth={2.2} /> : null}
      </View>
    </Pressable>
  );
}

export function AdjustPanel({ doc, update, commit }: { doc: EditDocument; update: Update; commit: () => void }) {
  const rows: { key: keyof EditDocument; label: string; min: number; max: number }[] = [
    { key: 'exposure', label: 'Exposure', min: -2, max: 2 },
    { key: 'brightness', label: 'Brightness', min: -1, max: 1 },
    { key: 'contrast', label: 'Contrast', min: -1, max: 1 },
    { key: 'saturation', label: 'Saturation', min: -1, max: 1 },
    { key: 'warmth', label: 'Warmth', min: -1, max: 1 },
    { key: 'sharpen', label: 'Sharpen', min: 0, max: 1 },
  ];
  return (
    <View style={{ gap: 10 }}>
      {rows.map(r => (
        <ValueSlider
          key={r.key}
          label={r.label}
          testID={`slider-${r.key}`}
          value={doc[r.key] as number}
          min={r.min}
          max={r.max}
          format={r.key === 'exposure' ? v => `${v > 0 ? '+' : ''}${v.toFixed(1)}` : pct}
          onChange={v => update({ [r.key]: Math.round(v * 100) / 100 }, `adjust-${r.key}`)}
          onEnd={commit}
        />
      ))}
    </View>
  );
}

export function CropPanel({
  doc,
  update,
  commit,
  aspect,
  onAspect,
  cropSize,
  onCropSize,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  aspect: string;
  onAspect: (id: string) => void;
  cropSize: number;
  onCropSize: (v: number) => void;
}) {
  return (
    <View style={{ gap: 12 }}>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        {CROP_ASPECTS.map(a => (
          <Chip
            key={a.id}
            label={a.label}
            selected={aspect === a.id}
            onPress={() => onAspect(a.id)}
            testID={`crop-${a.id}`}
          />
        ))}
      </ScrollView>
      <ValueSlider
        label="Crop size"
        value={cropSize}
        min={0.3}
        max={1}
        onChange={onCropSize}
        onEnd={commit}
        format={pct}
      />
      <ValueSlider
        label="Straighten"
        value={doc.straighten}
        min={-45}
        max={45}
        step={1}
        format={v => `${v > 0 ? '+' : ''}${v.toFixed(0)}°`}
        onChange={v => update({ straighten: Math.round(v * 2) / 2 }, 'straighten')}
        onEnd={commit}
      />
      <View style={{ flexDirection: 'row', gap: 10 }}>
        <Button
          label="Rotate"
          icon="refresh"
          variant="glass"
          onPress={() =>
            update({ quarterTurns: ((doc.quarterTurns + 1) % 4) as EditDocument['quarterTurns'], crop: null })
          }
          style={{ flex: 1 }}
          testID="rotate"
        />
        <Button
          label="Mirror"
          icon="flip"
          variant="glass"
          onPress={() => update({ flipH: !doc.flipH })}
          style={{ flex: 1 }}
        />
      </View>
      <AppText variant="caption" tone="secondary">
        Drag on the photo to move the crop.
      </AppText>
    </View>
  );
}

export function LooksPanel({
  doc,
  update,
  commit,
  onPremium,
  locked,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  onPremium: () => void;
  locked: boolean;
}) {
  return (
    <View style={{ gap: 12 }}>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        <Chip label="None" selected={!doc.look} onPress={() => update({ look: null })} />
        {LOOKS.map(l => (
          <Chip
            key={l.id}
            label={l.name}
            premium={l.premium}
            selected={doc.look?.id === l.id}
            testID={`look-${l.id}`}
            onPress={() =>
              l.premium && locked ? onPremium() : update({ look: { id: l.id, strength: doc.look?.strength ?? 0.8 } })
            }
          />
        ))}
      </ScrollView>
      {doc.look ? (
        <ValueSlider
          label="Strength"
          value={doc.look.strength}
          onChange={v =>
            doc.look && update({ look: { ...doc.look, strength: Math.round(v * 100) / 100 } }, 'look-strength')
          }
          onEnd={commit}
        />
      ) : null}
    </View>
  );
}

const blurOf = (v: number): BlurLevel | 'off' =>
  v <= 0 ? 'off' : BLUR_LEVELS.reduce((a, b) => (Math.abs(b.value - v) < Math.abs(a.value - v) ? b : a)).id;
const glowOf = (v: number): GlowLevel =>
  GLOW_LEVELS.reduce((a, b) => (Math.abs(b.retouch - v) < Math.abs(a.retouch - v) ? b : a)).id;

export function SpotlightEditPanel({ doc, update }: { doc: EditDocument; update: Update }) {
  const s = doc.spotlight;
  const count = (s?.points.length ?? 0) / 2;
  return (
    <View style={{ gap: 12 }}>
      <AppText variant="body" tone="secondary">
        {count > 0
          ? `${count} chosen · tap again to remove`
          : 'Tap what should shine. Untapped, the people (or the main subject) stand out.'}
      </AppText>
      <SegmentedControl
        label="Background blur"
        value={s ? blurOf(s.blur) : 'off'}
        onChange={id =>
          update({
            spotlight:
              id === 'off' ? null : { points: s?.points ?? [], blur: BLUR_LEVELS.find(b => b.id === id)?.value ?? 0.6 },
          })
        }
        options={[{ value: 'off', label: 'Off' }, ...BLUR_LEVELS.map(b => ({ value: b.id, label: b.label }))]}
      />
      <SegmentedControl
        label="Glow"
        value={glowOf(doc.retouch)}
        onChange={id => update({ retouch: GLOW_LEVELS.find(g => g.id === id)?.retouch ?? 0 })}
        options={GLOW_LEVELS.map(g => ({ value: g.id, label: g.label }))}
      />
      {count > 0 && s ? (
        <Button
          label="Clear choices"
          variant="ghost"
          icon="close"
          onPress={() => update({ spotlight: { ...s, points: [] } })}
        />
      ) : null}
    </View>
  );
}

export function RetouchPanel({ doc, update }: { doc: EditDocument; update: Update }) {
  return (
    <View style={{ gap: 12 }}>
      <AppText variant="body" tone="secondary">
        Glow smooths skin, clears blemishes and evens tone, keeping each person’s own colour.
      </AppText>
      <SegmentedControl
        label="Glow"
        value={glowOf(doc.retouch)}
        onChange={id => update({ retouch: GLOW_LEVELS.find(g => g.id === id)?.retouch ?? 0 })}
        options={GLOW_LEVELS.map(g => ({ value: g.id, label: g.label }))}
      />
    </View>
  );
}

export function FocusPanel({ doc, update, commit }: { doc: EditDocument; update: Update; commit: () => void }) {
  const f = doc.focus;
  return (
    <View style={{ gap: 12 }}>
      <SegmentedControl
        label="Focus"
        value={f?.kind ?? 'off'}
        onChange={k =>
          update({
            focus:
              k === 'off'
                ? null
                : {
                    kind: k,
                    x: f?.x ?? 0.5,
                    y: f?.y ?? 0.5,
                    size: f?.size ?? 0.25,
                    angle: 0,
                    strength: f?.strength ?? 0.6,
                  },
          })
        }
        options={[
          { value: 'off', label: 'Off' },
          { value: 'radial', label: 'Round' },
          { value: 'tilt', label: 'Band' },
        ]}
      />
      {f ? (
        <>
          <ValueSlider
            label="Sharp area"
            value={f.size}
            min={0.05}
            max={0.8}
            onChange={v => update({ focus: { ...f, size: v } }, 'focus-size')}
            onEnd={commit}
          />
          <ValueSlider
            label="Softness"
            value={f.strength}
            onChange={v => update({ focus: { ...f, strength: v } }, 'focus-strength')}
            onEnd={commit}
          />
          <AppText variant="caption" tone="secondary">
            Tap the photo to choose what stays sharp.
          </AppText>
        </>
      ) : null}
    </View>
  );
}

export function TransformEditPanel({
  doc,
  update,
  allowance,
  onPremium,
}: {
  doc: EditDocument;
  update: Update;
  allowance: string | null;
  onPremium: () => void;
}) {
  return (
    <View style={{ gap: 12 }}>
      <AppText variant="body" tone="secondary">
        The look of a high-end camera: restores fine detail, sharpens soft edges, removes colour fringes and grain. Your
        other edits are applied on top.
      </AppText>
      <SegmentedControl
        label="Transform"
        value={doc.transform ? 'on' : 'off'}
        onChange={v => (v === 'on' && allowance === 'none' ? onPremium() : update({ transform: v === 'on' }))}
        options={[
          { value: 'off', label: 'Off' },
          { value: 'on', label: 'On' },
        ]}
      />
      {allowance && allowance !== 'none' ? <Badge label={allowance} tone="premium" icon="crown" /> : null}
    </View>
  );
}

export function PropsPanel({
  category,
  onCategory,
  selected,
  onAdd,
  onPremium,
  locked,
  placed,
  onSize,
  onRotate,
  onFlip,
  onFit,
  onRemove,
  commit,
}: {
  category: PropCategory;
  onCategory: (c: PropCategory) => void;
  selected: { def: PropDef; width: number; rotation: number } | null;
  onAdd: (p: PropDef) => void;
  onPremium: () => void;
  locked: boolean;
  placed: number;
  onSize: (v: number) => void;
  onRotate: (v: number) => void;
  onFlip: () => void;
  onFit: () => void;
  onRemove: () => void;
  commit: () => void;
}) {
  return (
    <View style={{ gap: 12 }}>
      <SegmentedControl
        label="Category"
        value={category}
        onChange={onCategory}
        options={PROP_CATEGORIES.map(c => ({ value: c.id, label: c.label }))}
      />
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        {PROPS.filter(p => p.category === category).map(p => (
          <Chip
            key={p.id}
            label={p.name}
            premium={p.premium}
            selected={selected?.def.id === p.id}
            testID={`prop-${p.id}`}
            onPress={() => (p.premium && locked ? onPremium() : onAdd(p))}
          >
            <Image source={{ uri: propAsset(p.id) }} style={{ width: 64, height: 40 }} resizeMode="contain" />
          </Chip>
        ))}
      </ScrollView>
      {selected ? (
        <View style={{ gap: 10 }}>
          <ValueSlider
            label="Size"
            value={selected.width}
            min={0.05}
            max={1.2}
            onChange={onSize}
            onEnd={commit}
            format={pct}
          />
          <ValueSlider
            label="Turn"
            value={selected.rotation}
            min={-45}
            max={45}
            step={1}
            onChange={onRotate}
            onEnd={commit}
            format={v => `${v > 0 ? '+' : ''}${v.toFixed(0)}°`}
          />
          <View style={{ flexDirection: 'row', gap: 8 }}>
            <Button
              label="Fit to face"
              icon="target"
              variant="glass"
              onPress={onFit}
              style={{ flex: 1 }}
              testID="prop-fit"
            />
            <Button label="Mirror" icon="flip" variant="glass" onPress={onFlip} style={{ flex: 1 }} />
            <Button
              label="Remove"
              icon="trash"
              variant="glass"
              onPress={onRemove}
              style={{ flex: 1 }}
              testID="prop-remove"
            />
          </View>
          <AppText variant="caption" tone="secondary">
            Drag on the photo to move it.
          </AppText>
        </View>
      ) : (
        <AppText variant="caption" tone="secondary">
          {placed > 0 ? 'Tap a prop on the photo to adjust it.' : 'Choose a prop: it fits itself to the face.'}
        </AppText>
      )}
    </View>
  );
}

export function FramesPanel({
  doc,
  update,
  commit,
  onPremium,
  locked,
}: {
  doc: EditDocument;
  update: Update;
  commit: () => void;
  onPremium: () => void;
  locked: boolean;
}) {
  return (
    <View style={{ gap: 12 }}>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={{ gap: 8 }}>
        <Chip label="None" selected={!doc.frame} onPress={() => update({ frame: null })} />
        {FRAMES.map(f => (
          <Chip
            key={f.id}
            label={f.name}
            premium={f.premium}
            selected={doc.frame?.id === f.id}
            testID={`frame-${f.id}`}
            onPress={() =>
              f.premium && locked
                ? onPremium()
                : update({ frame: { id: f.id, zoom: doc.frame?.zoom ?? 1, x: 0, y: 0 } })
            }
          />
        ))}
      </ScrollView>
      {doc.frame ? (
        <>
          <ValueSlider
            label="Zoom"
            value={doc.frame.zoom}
            min={1}
            max={2.5}
            onChange={v => doc.frame && update({ frame: { ...doc.frame, zoom: v } }, 'frame-zoom')}
            onEnd={commit}
            format={v => `${v.toFixed(1)}×`}
          />
          <AppText variant="caption" tone="secondary">
            Drag on the photo to move it inside the frame.
          </AppText>
        </>
      ) : null}
    </View>
  );
}
