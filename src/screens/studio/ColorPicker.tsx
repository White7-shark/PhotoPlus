import React, { useRef, useState } from 'react';
import { StyleSheet, TextInput, View } from 'react-native';

import { AppText } from '../../components';
import { ValueSlider } from '../../components/ValueSlider';
import { hexToHsv, hsvToHex, normalizeHex, type Hsv } from '../../services/studio/colors';
import { useTheme } from '../../theme';
import { t as tx } from '../../i18n';

const HUES = ['#FF0000', '#FFFF00', '#00FF00', '#00FFFF', '#0000FF', '#FF00FF', '#FF0000'];

/**
 * Pick any backdrop colour: colour (hue), richness (saturation) and
 * brightness, each on a track that shows what it does, or a colour code.
 * The swatch follows the finger; `onChange` fires when a slider is let go
 * or a code is entered, so the photo is not redrawn on every move. Give it
 * a `key` of the chosen colour, so a colour chosen elsewhere resets it.
 */
export function ColorPicker({ value, onChange }: { value: string; onChange: (hex: string) => void }) {
  const theme = useTheme();
  const [hsv, setHsvState] = useState<Hsv>(() => hexToHsv(value));
  // The latest colour, for a commit in the same event as the change (a screen reader's step).
  const latest = useRef(hsv);
  const setHsv = (next: Hsv) => {
    latest.current = next;
    setHsvState(next);
  };
  const [code, setCode] = useState(value.toUpperCase());
  const hex = hsvToHex(hsv);
  const move = (next: Hsv) => {
    setHsv(next);
    setCode(hsvToHex(next));
  };
  const commit = () => onChange(hsvToHex(latest.current));
  return (
    <View style={styles.wrap} testID="color-picker">
      <View style={styles.head}>
        <View style={[styles.swatch, { backgroundColor: hex }]} testID="color-swatch" />
        <View style={{ flex: 1, gap: 2 }}>
          <AppText variant="caption" tone="secondary">
            {tx('color_picker_colour_code')}
          </AppText>
          <TextInput
            value={code}
            onChangeText={setCode}
            onEndEditing={() => {
              const n = normalizeHex(code);
              if (n) {
                setHsv(hexToHsv(n));
                setCode(n);
                onChange(n);
              } else {
                setCode(hex);
              }
            }}
            autoCapitalize="characters"
            autoCorrect={false}
            maxLength={7}
            accessibilityLabel={tx('color_picker_colour_code')}
            testID="color-code"
            style={[styles.code, { color: theme.colors.textPrimary, borderColor: theme.colors.glassStroke }]}
          />
        </View>
      </View>
      <ValueSlider
        label={tx('color_picker_colour')}
        value={hsv.h}
        min={0}
        max={359}
        step={5}
        track={HUES}
        thumbColor={hsvToHex({ h: hsv.h, s: 1, v: 1 })}
        format={() => ''}
        onChange={h => move({ ...hsv, h })}
        onEnd={commit}
        testID="color-hue"
      />
      <ValueSlider
        label={tx('color_picker_richness')}
        value={hsv.s}
        track={[hsvToHex({ ...hsv, s: 0 }), hsvToHex({ ...hsv, s: 1 })]}
        thumbColor={hex}
        onChange={s => move({ ...hsv, s })}
        onEnd={commit}
        testID="color-richness"
      />
      <ValueSlider
        label={tx('color_picker_brightness')}
        value={hsv.v}
        track={['#000000', hsvToHex({ ...hsv, v: 1 })]}
        thumbColor={hex}
        onChange={v => move({ ...hsv, v })}
        onEnd={commit}
        testID="color-brightness"
      />
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { gap: 10, padding: 12, borderRadius: 16, backgroundColor: 'rgba(255,255,255,0.06)' },
  head: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  swatch: { width: 48, height: 48, borderRadius: 12, borderWidth: 2, borderColor: 'rgba(255,255,255,0.6)' },
  code: { borderWidth: 1, borderRadius: 10, paddingHorizontal: 10, paddingVertical: 6, fontSize: 16, fontWeight: '700', letterSpacing: 1 },
});
