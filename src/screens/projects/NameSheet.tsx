import React, { useState } from 'react';
import { Modal, Pressable, StyleSheet, TextInput } from 'react-native';

import { AppText, Button, GlassSurface } from '../../components';
import { useT } from '../../i18n';
import { useTheme } from '../../theme';

/** Names something (a new project, a renamed one, a work's title): a sheet with one line to type. */
type Props = {
  open: boolean;
  title: string;
  initial: string;
  placeholder?: string;
  confirm: string;
  onClose: () => void;
  onSubmit: (name: string) => void | Promise<void>;
  testID?: string;
};

export function NameSheet(props: Props) {
  // A fresh sheet (with its starting name) each time it opens.
  return props.open ? <Sheet {...props} /> : null;
}

function Sheet({ title, initial, placeholder, confirm, onClose, onSubmit, testID = 'name-sheet' }: Props) {
  const theme = useTheme();
  const t = useT();
  const [name, setName] = useState(initial);
  const [busy, setBusy] = useState(false);
  const ok = name.trim().length > 0;
  const submit = async () => {
    if (!ok || busy) return;
    setBusy(true);
    try {
      await onSubmit(name.trim());
    } finally {
      setBusy(false);
    }
  };
  return (
    <Modal
      transparent
      animationType="fade"
      visible
      onRequestClose={onClose}
      statusBarTranslucent
      navigationBarTranslucent
    >
      <Pressable style={styles.scrim} onPress={onClose} accessibilityRole="button" accessibilityLabel={t('sp_cancel')}>
        <Pressable onPress={() => undefined} style={styles.wrap}>
          <GlassSurface intensity="strong" radius={24} style={styles.sheet} testID={testID}>
            <AppText variant="title3" accessibilityRole="header">
              {title}
            </AppText>
            <TextInput
              value={name}
              onChangeText={setName}
              placeholder={placeholder}
              placeholderTextColor={theme.colors.textTertiary}
              maxLength={80}
              autoFocus
              selectTextOnFocus
              onSubmitEditing={() => void submit()}
              accessibilityLabel={title}
              testID={`${testID}-input`}
              style={{
                minHeight: 48,
                borderRadius: theme.radii.sm,
                paddingHorizontal: 14,
                color: theme.colors.textPrimary,
                backgroundColor: theme.colors.glassFill,
                borderWidth: 1,
                borderColor: theme.colors.glassStroke,
                fontSize: 16,
              }}
            />
            <Button
              label={confirm}
              size="lg"
              disabled={!ok}
              loading={busy}
              onPress={() => void submit()}
              testID={`${testID}-confirm`}
            />
            <Button label={t('sp_cancel')} variant="ghost" onPress={onClose} />
          </GlassSurface>
        </Pressable>
      </Pressable>
    </Modal>
  );
}

const styles = StyleSheet.create({
  scrim: { flex: 1, backgroundColor: 'rgba(5,5,10,0.6)', justifyContent: 'center', padding: 24 },
  wrap: { width: '100%', maxWidth: 440, alignSelf: 'center' },
  sheet: { padding: 20, gap: 12 },
});
