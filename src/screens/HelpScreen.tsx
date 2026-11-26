import React, { useMemo, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, TextInput, View } from 'react-native';

import { AppText, GlassSurface, Icon, Screen } from '../components';
import { t, useI18n } from '../i18n';
import type { RootScreenProps } from '../navigation/types';
import { faqTopics, searchFaq, type FaqItem } from '../services/help/faq';
import { useTheme } from '../theme';

/**
 * Help & FAQ: every tool, plan and setting explained, by topic, with a
 * search that finds words in questions and answers (in the app's language).
 */
export function HelpScreen({ navigation }: RootScreenProps<'Help'>) {
  const theme = useTheme();
  const strings = useI18n(s => s.strings);
  const topics = useMemo(() => faqTopics(), [strings]); // eslint-disable-line react-hooks/exhaustive-deps
  const [query, setQuery] = useState('');
  const [topic, setTopic] = useState<string>('all');
  const [open, setOpen] = useState<Set<string>>(new Set());

  const searching = query.trim().length > 0;
  const results = useMemo(() => (searching ? searchFaq(topics, query) : []), [topics, query, searching]);
  const current = topics.find(x => x.id === topic);
  const toggle = (id: string) =>
    setOpen(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  const titleOf = (id: string) => topics.find(x => x.id === id)?.title ?? '';

  return (
    <Screen title={t('help_title')} overline={t('help_overline')} onBack={() => navigation.goBack()} bottomInset={32}>
      <GlassSurface style={styles.search} radius={16}>
        <Icon name="search" size={18} color={theme.colors.textSecondary} />
        <TextInput
          value={query}
          onChangeText={setQuery}
          placeholder={t('help_search')}
          placeholderTextColor={theme.colors.textTertiary}
          accessibilityLabel={t('help_search')}
          returnKeyType="search"
          autoCorrect={false}
          style={[styles.input, { color: theme.colors.textPrimary }]}
          testID="help-search"
        />
        {searching ? (
          <Pressable
            onPress={() => setQuery('')}
            accessibilityRole="button"
            accessibilityLabel={t('help_clear')}
            hitSlop={10}
            testID="help-clear"
          >
            <Icon name="close" size={16} color={theme.colors.textSecondary} />
          </Pressable>
        ) : null}
      </GlassSurface>

      {!searching ? (
        <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.chips}>
          {[{ id: 'all', title: t('help_all'), icon: 'help' as const }, ...topics].map(x => {
            const on = topic === x.id;
            return (
              <Pressable
                key={x.id}
                onPress={() => setTopic(x.id)}
                accessibilityRole="tab"
                accessibilityState={{ selected: on }}
                testID={`help-topic-${x.id}`}
                style={[
                  styles.chip,
                  {
                    backgroundColor: on ? theme.colors.accent : theme.colors.glassFill,
                    borderColor: on ? theme.colors.accent : theme.colors.glassStroke,
                  },
                ]}
              >
                <Icon name={x.icon} size={14} color={on ? '#FFFFFF' : theme.colors.textSecondary} />
                <AppText variant="callout" style={{ color: on ? '#FFFFFF' : theme.colors.textPrimary }}>
                  {x.title}
                </AppText>
              </Pressable>
            );
          })}
        </ScrollView>
      ) : null}

      {searching ? (
        <>
          <AppText variant="caption" tone="tertiary" style={styles.count} testID="help-count">
            {results.length === 1 ? t('help_one_result') : t('help_results', { count: results.length })}
          </AppText>
          {results.length ? (
            <GlassSurface style={styles.list}>
              {results.map((item, i) => (
                <Question
                  key={item.id}
                  item={item}
                  topic={titleOf(item.topic)}
                  open={open.has(item.id)}
                  onToggle={() => toggle(item.id)}
                  last={i === results.length - 1}
                />
              ))}
            </GlassSurface>
          ) : (
            <GlassSurface style={styles.empty}>
              <Icon name="search" size={26} color={theme.colors.textTertiary} />
              <AppText variant="bodyStrong" align="center">
                {t('help_none_title')}
              </AppText>
              <AppText variant="callout" tone="secondary" align="center">
                {t('help_none_body')}
              </AppText>
            </GlassSurface>
          )}
        </>
      ) : current ? (
        <>
          <View style={styles.sectionHead}>
            <View style={[styles.badge, { backgroundImage: theme.gradients.spectrum }]}>
              <Icon name={current.icon} size={16} color="#FFFFFF" />
            </View>
            <AppText variant="title3" accessibilityRole="header">
              {current.title}
            </AppText>
          </View>
          <GlassSurface style={styles.list}>
            {current.items.map((item, i) => (
              <Question
                key={item.id}
                item={item}
                open={open.has(item.id)}
                onToggle={() => toggle(item.id)}
                last={i === current.items.length - 1}
              />
            ))}
          </GlassSurface>
        </>
      ) : (
        <>
          <AppText variant="overline" tone="tertiary" style={styles.count}>
            {t('help_browse').toLocaleUpperCase()}
          </AppText>
          <View style={styles.grid}>
            {topics.map(x => (
              <Pressable
                key={x.id}
                onPress={() => setTopic(x.id)}
                accessibilityRole="button"
                testID={`help-tile-${x.id}`}
                style={styles.tileWrap}
              >
                <GlassSurface radius={18} style={styles.tile}>
                  <View style={[styles.badge, { backgroundImage: theme.gradients.spectrum }]}>
                    <Icon name={x.icon} size={16} color="#FFFFFF" />
                  </View>
                  <AppText variant="bodyStrong" numberOfLines={2}>
                    {x.title}
                  </AppText>
                  <AppText variant="caption" tone="tertiary">
                    {t('help_questions', { count: x.items.length })}
                  </AppText>
                </GlassSurface>
              </Pressable>
            ))}
          </View>
        </>
      )}

      <GlassSurface style={styles.contact} glow="rgba(116,69,224,0.25)">
        <Icon name="help" size={22} color={theme.colors.accent} />
        <View style={{ flex: 1, gap: 2 }}>
          <AppText variant="bodyStrong">{t('help_contact_title')}</AppText>
          <AppText variant="callout" tone="secondary">
            {t('help_contact_body')}
          </AppText>
        </View>
      </GlassSurface>
    </Screen>
  );
}

function Question({
  item,
  topic,
  open,
  onToggle,
  last,
}: {
  item: FaqItem;
  topic?: string;
  open: boolean;
  onToggle: () => void;
  last: boolean;
}) {
  const theme = useTheme();
  return (
    <View
      style={[
        styles.q,
        !last && { borderBottomWidth: StyleSheet.hairlineWidth, borderBottomColor: theme.colors.divider },
      ]}
    >
      <Pressable
        onPress={onToggle}
        accessibilityRole="button"
        accessibilityState={{ expanded: open }}
        accessibilityHint={open ? t('help_collapse') : t('help_expand')}
        style={styles.qRow}
        testID={`faq-${item.id}`}
      >
        <View style={{ flex: 1, gap: 2 }}>
          {topic ? (
            <AppText variant="caption" tone="accent">
              {topic}
            </AppText>
          ) : null}
          <AppText variant="bodyStrong">{item.question}</AppText>
        </View>
        <View style={{ transform: [{ rotate: open ? '90deg' : '0deg' }] }}>
          <Icon name="chevronRight" size={18} color={theme.colors.textTertiary} />
        </View>
      </Pressable>
      {open ? (
        <AppText variant="body" tone="secondary" style={styles.answer} testID={`faq-${item.id}-answer`}>
          {item.answer}
        </AppText>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  search: { flexDirection: 'row', alignItems: 'center', gap: 10, paddingHorizontal: 14, marginBottom: 12 },
  input: { flex: 1, fontSize: 15, paddingVertical: 12 },
  chips: { gap: 8, paddingBottom: 16 },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderRadius: 999,
    borderWidth: 1,
  },
  count: { marginBottom: 10 },
  list: { marginBottom: 20 },
  empty: { alignItems: 'center', gap: 8, padding: 24, marginBottom: 20 },
  sectionHead: { flexDirection: 'row', alignItems: 'center', gap: 10, marginBottom: 12 },
  badge: { width: 32, height: 32, borderRadius: 10, alignItems: 'center', justifyContent: 'center' },
  grid: { flexDirection: 'row', flexWrap: 'wrap', marginHorizontal: -5, marginBottom: 14 },
  tileWrap: { width: '50%', padding: 5 },
  tile: { padding: 14, gap: 8, minHeight: 124 },
  q: { paddingHorizontal: 16 },
  qRow: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingVertical: 14 },
  answer: { paddingBottom: 16 },
  contact: { flexDirection: 'row', gap: 12, padding: 16, alignItems: 'flex-start' },
});
