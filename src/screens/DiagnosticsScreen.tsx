import React, { useEffect, useRef, useState } from 'react';
import { View } from 'react-native';

import {
  AppText,
  Badge,
  Button,
  ErrorState,
  GlassSurface,
  ListRow,
  ProgressBar,
  Screen,
  SectionHeader,
  StatTile,
} from '../components';
import type { RootScreenProps } from '../navigation/types';
import { errorMessage } from '../native';
import { createLogger } from '../services/logging/logger';
import { getProcessingService, ProcessingError } from '../services/processing/ImageProcessingService';
import { runSelfTest, type SelfTestReport } from '../services/processing/selfTest';
import { getStorageService } from '../services/storage/StorageService';
import { useAppStore } from '../state/store';
import { useTheme } from '../theme';
import { formatBytes } from '../services/storage/StorageService';

const log = createLogger('diagnostics');

/**
 * Engine Lab — runs a synthetic photo through the full native pipeline many
 * times and reports timings and memory, demonstrating the Phase 1
 * acceptance criteria on the user's own device.
 */
export function DiagnosticsScreen({ navigation }: RootScreenProps<'Diagnostics'>) {
  const theme = useTheme();
  const engine = useAppStore(s => s.engine);
  const repos = useAppStore(s => s.repos);
  const [running, setRunning] = useState(false);
  const [progress, setProgress] = useState(0);
  const [report, setReport] = useState<SelfTestReport | null>(null);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  // Leaving the screen cancels the run — no orphaned native work.
  useEffect(() => () => abortRef.current?.abort(), []);

  const start = async () => {
    const controller = new AbortController();
    abortRef.current = controller;
    setRunning(true);
    setError(null);
    setReport(null);
    setProgress(0);
    try {
      const paths = await getStorageService().getPaths();
      const result = await runSelfTest({
        service: getProcessingService(),
        outputDir: paths.processingCache,
        iterations: 12,
        signal: controller.signal,
        onProgress: setProgress,
        history: repos?.history,
      });
      setReport(result);
    } catch (e) {
      if (e instanceof ProcessingError && e.cancelled) {
        setError('Self-test cancelled.');
      } else {
        log.error('self-test failed', e);
        setError(errorMessage(e));
      }
    } finally {
      setRunning(false);
      abortRef.current = null;
    }
  };

  return (
    <Screen title="Engine Lab" overline="Diagnostics" onBack={navigation.goBack} bottomInset={24}>
      {!engine ? (
        <ErrorState
          title="Processing engine unavailable"
          message="This device could not load the native engine. Camera capture still works."
        />
      ) : (
        <>
          <GlassSurface style={{ padding: 18, gap: 12, marginBottom: 20 }} glow="rgba(91,91,247,0.25)">
            <View style={{ flexDirection: 'row', gap: 8, flexWrap: 'wrap' }}>
              <Badge label={`Engine v${engine.version}`} tone="accent" icon="cpu" />
              <Badge label={`${engine.workers} worker${engine.workers > 1 ? 's' : ''}`} />
              <Badge label={`Budget ${formatBytes(engine.memoryBudgetBytes, 0)}`} />
            </View>
            <AppText variant="body" tone="secondary">
              Generates a test photo and runs a full edit pipeline — tone, rotate, crop, resize, sharpen, colour —
              repeatedly in C++, then checks that memory returns to baseline.
            </AppText>
            {running ? <ProgressBar value={progress} label="Self-test progress" /> : null}
            <View style={{ flexDirection: 'row', gap: 10 }}>
              {running ? (
                <Button label="Cancel" variant="danger" icon="stop" onPress={() => abortRef.current?.abort()} />
              ) : (
                <Button label={report ? 'Run again' : 'Run self-test'} icon="play" onPress={start} />
              )}
            </View>
          </GlassSurface>

          {error ? <ErrorState title="Self-test stopped" message={error} onRetry={start} /> : null}

          {report ? (
            <>
              <View style={{ flexDirection: 'row', gap: 10, marginBottom: 10 }}>
                <StatTile icon="play" label="Avg pipeline" value={`${report.avgProcessMs.toFixed(0)} ms`} />
                <StatTile icon="refresh" label="Iterations" value={`${report.iterations}`} tint={theme.colors.info} />
              </View>
              <View style={{ flexDirection: 'row', gap: 10, marginBottom: 20 }}>
                <StatTile
                  icon="memory"
                  label="Peak engine memory"
                  value={formatBytes(report.memory.peakBytes)}
                  tint={theme.colors.premium}
                />
                <StatTile
                  icon={report.memoryReturnedToBaseline ? 'check' : 'alert'}
                  label="Memory after run"
                  value={report.memoryReturnedToBaseline ? 'Baseline' : formatBytes(report.memory.leakedBytes)}
                  tint={report.memoryReturnedToBaseline ? theme.colors.success : theme.colors.danger}
                />
              </View>
              <SectionHeader title="Per-operation timings" />
              <GlassSurface>
                {report.operations.map((op, i) => (
                  <ListRow
                    key={op.name}
                    title={op.name}
                    value={`${op.avgMs.toFixed(1)} ms avg · ${op.calls}×`}
                    last={i === report.operations.length - 1}
                  />
                ))}
              </GlassSurface>
            </>
          ) : null}
        </>
      )}
    </Screen>
  );
}
