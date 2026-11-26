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
import { t as tx } from '../i18n';

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
        setError(tx('diagnostics_self_test_cancelled'));
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
    <Screen title={tx('diagnostics_engine_lab')} overline="Diagnostics" onBack={navigation.goBack} bottomInset={24}>
      {!engine ? (
        <ErrorState
          title={tx('diagnostics_processing_engine_unavailable')}
          message={tx('diagnostics_this_device_could_not_load')}
        />
      ) : (
        <>
          <GlassSurface style={{ padding: 18, gap: 12, marginBottom: 20 }} glow="rgba(91,91,247,0.25)">
            <View style={{ flexDirection: 'row', gap: 8, flexWrap: 'wrap' }}>
              <Badge label={tx('diagnostics_engine_v', { version: engine.version })} tone="accent" icon="cpu" />
              <Badge label={`${engine.workers} worker${engine.workers > 1 ? 's' : ''}`} />
              <Badge label={tx('diagnostics_budget', { bytes: formatBytes(engine.memoryBudgetBytes, 0) })} />
            </View>
            <AppText variant="body" tone="secondary">
              {tx('diagnostics_generates_a_test_photo_and')}
            </AppText>
            {running ? <ProgressBar value={progress} label={tx('diagnostics_self_test_progress')} /> : null}
            <View style={{ flexDirection: 'row', gap: 10 }}>
              {running ? (
                <Button
                  label={tx('diagnostics_cancel')}
                  variant="danger"
                  icon="stop"
                  onPress={() => abortRef.current?.abort()}
                />
              ) : (
                <Button
                  label={report ? tx('diagnostics_run_again') : tx('diagnostics_run_self_test')}
                  icon="play"
                  onPress={start}
                />
              )}
            </View>
          </GlassSurface>

          {error ? <ErrorState title={tx('diagnostics_self_test_stopped')} message={error} onRetry={start} /> : null}

          {report ? (
            <>
              <View style={{ flexDirection: 'row', gap: 10, marginBottom: 10 }}>
                <StatTile
                  icon="play"
                  label={tx('diagnostics_avg_pipeline')}
                  value={`${report.avgProcessMs.toFixed(0)} ms`}
                />
                <StatTile
                  icon="refresh"
                  label={tx('diagnostics_iterations')}
                  value={`${report.iterations}`}
                  tint={theme.colors.info}
                />
              </View>
              <View style={{ flexDirection: 'row', gap: 10, marginBottom: 20 }}>
                <StatTile
                  icon="memory"
                  label={tx('diagnostics_peak_engine_memory')}
                  value={formatBytes(report.memory.peakBytes)}
                  tint={theme.colors.premium}
                />
                <StatTile
                  icon={report.memoryReturnedToBaseline ? 'check' : 'alert'}
                  label={tx('diagnostics_memory_after_run')}
                  value={
                    report.memoryReturnedToBaseline
                      ? tx('diagnostics_baseline')
                      : formatBytes(report.memory.leakedBytes)
                  }
                  tint={report.memoryReturnedToBaseline ? theme.colors.success : theme.colors.danger}
                />
              </View>
              <SectionHeader title={tx('diagnostics_per_operation_timings')} />
              <GlassSurface>
                {report.operations.map((op, i) => (
                  <ListRow
                    key={op.name}
                    title={op.name}
                    value={tx('diagnostics_ms_avg', { value: op.avgMs.toFixed(1), calls: op.calls })}
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
