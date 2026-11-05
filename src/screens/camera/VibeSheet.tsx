import React, { useState } from 'react';
import { Image, Modal, Pressable, StyleSheet, View } from 'react-native';

import { AppText, Button, GlassSurface, Icon } from '../../components';
import { errorCode } from '../../native';
import { friendlyMessage } from '../../services/errors';
import { lookDescription, pickVibeLook, VIBE_STRENGTHS, type VibeLook } from '../../services/looks/LookService';
import { useTheme } from '../../theme';

/**
 * Vibe Match: pick any photo whose style you love; the camera adjusts its
 * light and colour toward it, and every shot is finished in that style.
 */
export function VibeSheet({
  visible,
  look,
  onChange,
  onClose,
}: {
  visible: boolean;
  look: VibeLook | null;
  onChange: (look: VibeLook | null) => void;
  onClose: () => void;
}) {
  const theme = useTheme();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const pick = async () => {
    setBusy(true);
    setError(null);
    try {
      const picked = await pickVibeLook(look?.strength ?? 0.8);
      if (picked) {
        onChange(picked);
      }
    } catch (e) {
      setError(friendlyMessage(errorCode(e), 'That photo couldn’t be used. Try another one.'));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Modal transparent animationType="fade" visible={visible} onRequestClose={onClose} statusBarTranslucent>
      <Pressable
        style={styles.scrim}
        onPress={onClose}
        accessibilityRole="button"
        accessibilityLabel="Close Vibe Match"
      >
        <Pressable onPress={() => undefined} style={styles.wrap}>
          <GlassSurface intensity="strong" radius={28} style={{ padding: 20, gap: 14 }}>
            <View style={styles.header}>
              <Icon name="wand" size={20} color={theme.colors.premium} />
              <AppText variant="title3" accessibilityRole="header" style={{ flex: 1 }}>
                Vibe Match
              </AppText>
              <View style={styles.crown}>
                <Icon name="crown" size={12} color={theme.colors.premium} strokeWidth={2.2} />
                <AppText variant="caption" style={{ color: theme.colors.premium, fontWeight: '700' }}>
                  Premium
                </AppText>
              </View>
            </View>
            {look ? (
              <>
                <View style={styles.lookRow}>
                  <Image
                    source={{ uri: `file://${look.referencePath}` }}
                    style={styles.thumb}
                    accessibilityLabel="Your style photo"
                  />
                  <View style={{ flex: 1, gap: 4 }}>
                    <AppText variant="bodyStrong">{lookDescription(look).text}</AppText>
                    <AppText variant="caption" tone="secondary">
                      Your camera now shoots in this style.
                    </AppText>
                  </View>
                </View>
                <View style={styles.strengths} accessibilityRole="radiogroup" accessibilityLabel="Strength">
                  {VIBE_STRENGTHS.map(s => {
                    const selected = Math.abs(look.strength - s.value) < 0.01;
                    return (
                      <Pressable
                        key={s.id}
                        onPress={() => onChange({ ...look, strength: s.value })}
                        accessibilityRole="radio"
                        accessibilityLabel={s.label}
                        accessibilityState={{ selected }}
                        style={[
                          styles.pill,
                          selected
                            ? { borderColor: theme.colors.premium, backgroundColor: 'rgba(245,198,107,0.16)' }
                            : null,
                        ]}
                      >
                        <AppText
                          variant="caption"
                          style={{ color: selected ? theme.colors.premium : '#FFFFFF', fontWeight: '700' }}
                        >
                          {s.label}
                        </AppText>
                      </Pressable>
                    );
                  })}
                </View>
                <View style={{ flexDirection: 'row', gap: 10 }}>
                  <Button
                    label="Change photo"
                    variant="glass"
                    icon="image"
                    onPress={() => void pick()}
                    loading={busy}
                    style={{ flex: 1 }}
                  />
                  <Button label="Turn off" variant="ghost" onPress={() => onChange(null)} />
                </View>
              </>
            ) : (
              <>
                <AppText variant="body" tone="secondary">
                  Pick any photo whose style you love. Your camera adjusts its light and colour to match, and every shot
                  is finished in that style.
                </AppText>
                <Button label="Choose a style photo" icon="image" onPress={() => void pick()} loading={busy} />
              </>
            )}
            {error ? (
              <AppText variant="caption" tone="danger" accessibilityLiveRegion="polite">
                {error}
              </AppText>
            ) : null}
          </GlassSurface>
        </Pressable>
      </Pressable>
    </Modal>
  );
}

const styles = StyleSheet.create({
  scrim: { flex: 1, backgroundColor: 'rgba(5,5,10,0.6)', justifyContent: 'flex-end', padding: 16, paddingBottom: 40 },
  wrap: { maxWidth: 460, width: '100%', alignSelf: 'center' },
  header: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  crown: { flexDirection: 'row', alignItems: 'center', gap: 4 },
  lookRow: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  thumb: { width: 64, height: 64, borderRadius: 14, backgroundColor: 'rgba(255,255,255,0.08)' },
  strengths: { flexDirection: 'row', gap: 8 },
  pill: {
    height: 34,
    paddingHorizontal: 14,
    borderRadius: 17,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.22)',
    alignItems: 'center',
    justifyContent: 'center',
  },
});
