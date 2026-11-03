import { fireEvent, render, screen, waitFor } from '@testing-library/react-native';
import React from 'react';
import { Text } from 'react-native';

import App from '../src/app/App';
import { Button, ErrorBoundary, IconButton, ProgressBar, SegmentedControl } from '../src/components';
import { EmptyState, ErrorState } from '../src/components/States';
import { HomeScreen } from '../src/screens/HomeScreen';
import { SettingsScreen } from '../src/screens/SettingsScreen';
import { DeviceInfoScreen } from '../src/screens/DeviceInfoScreen';
import { MarketplaceScreen } from '../src/screens/MarketplaceScreen';
import { parseCapabilities } from '../src/services/capabilities';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { rawCapabilities } from './helpers/fixtures';

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const nav = () => ({ navigate: jest.fn(), goBack: jest.fn(), reset: jest.fn() }) as never;

describe('components', () => {
  it('Button exposes role, label and disabled/busy state', async () => {
    const onPress = jest.fn();
    await wrap(<Button label="Save" onPress={onPress} />);
    await fireEvent.press(screen.getByRole('button', { name: 'Save' }));
    expect(onPress).toHaveBeenCalled();
  });

  it('a loading Button is busy and does not fire', async () => {
    const onPress = jest.fn();
    await wrap(<Button label="Export" onPress={onPress} loading />);
    const button = screen.getByRole('button', { name: 'Export' });
    expect(button.props.accessibilityState).toMatchObject({ busy: true, disabled: true });
    await fireEvent.press(button);
    expect(onPress).not.toHaveBeenCalled();
  });

  it('IconButton is labelled and meets the 48dp minimum', async () => {
    await wrap(<IconButton icon="close" label="Close" size={32} />);
    const btn = screen.getByRole('button', { name: 'Close' });
    const style = Array.isArray(btn.props.style) ? Object.assign({}, ...btn.props.style) : btn.props.style;
    expect(style.width).toBeGreaterThanOrEqual(48);
  });

  it('SegmentedControl behaves as an accessible radio group', async () => {
    const onChange = jest.fn();
    await wrap(
      <SegmentedControl
        label="Theme"
        value="dark"
        onChange={onChange}
        options={[
          { value: 'dark', label: 'Dark' },
          { value: 'light', label: 'Light' },
        ]}
      />,
    );
    expect(screen.getByRole('radio', { name: 'Dark' }).props.accessibilityState.selected).toBe(true);
    await fireEvent.press(screen.getByRole('radio', { name: 'Light' }));
    expect(onChange).toHaveBeenCalledWith('light');
  });

  it('ProgressBar reports its value and clamps out-of-range input', async () => {
    await wrap(<ProgressBar value={1.7} label="Upload" />);
    expect(screen.getByLabelText('Upload').props.accessibilityValue).toEqual({ min: 0, max: 100, now: 100 });
  });

  it('Empty and Error states render messages and actions', async () => {
    const retry = jest.fn();
    await wrap(
      <>
        <EmptyState title="Nothing here" message="Add something" actionLabel="Add" onAction={jest.fn()} />
        <ErrorState title="Failed" message="Network down" onRetry={retry} />
      </>,
    );
    expect(screen.getByText('Nothing here')).toBeTruthy();
    await fireEvent.press(screen.getByRole('button', { name: 'Try again' }));
    expect(retry).toHaveBeenCalled();
  });

  it('ErrorBoundary contains a crash and recovers on retry', async () => {
    let shouldThrow = true;
    const Bomb = () => {
      if (shouldThrow) throw new Error('kaboom');
      return <Text>Recovered</Text>;
    };
    jest.spyOn(console, 'error').mockImplementation(() => undefined);
    await wrap(
      <ErrorBoundary area="Camera">
        <Bomb />
      </ErrorBoundary>,
    );
    expect(screen.getByText('Camera ran into a problem')).toBeTruthy();
    shouldThrow = false;
    await fireEvent.press(screen.getByRole('button', { name: 'Try again' }));
    expect(screen.getByText('Recovered')).toBeTruthy();
  });
});

describe('screens', () => {
  beforeEach(() => {
    useAppStore.setState({
      status: 'ready',
      settings: { ...DEFAULT_SETTINGS },
      projects: [],
      projectsStatus: 'ready',
      warnings: [],
      capabilities: parseCapabilities(rawCapabilities()),
      tier: 'performance',
      engine: null,
      repos: null,
    });
  });

  it('Home shows the four pillars, empty library and readiness', async () => {
    const navigation = nav();
    await wrap(<HomeScreen navigation={navigation} route={{} as never} />);
    for (const t of ['Camera Studio', 'AI Assistant', 'Replication Studio', 'Editing Studio']) {
      expect(screen.getByText(t)).toBeTruthy();
    }
    expect(screen.getByText('No projects yet')).toBeTruthy();
    await fireEvent.press(screen.getByRole('button', { name: 'Open camera' }));
    expect((navigation as { navigate: jest.Mock }).navigate).toHaveBeenCalledWith('Camera');
  });

  it('Home lists startup warnings and lets the user dismiss them', async () => {
    useAppStore.setState({ warnings: ['Device check was slow.'] });
    await wrap(<HomeScreen navigation={nav()} route={{} as never} />);
    expect(screen.getByText('Device check was slow.')).toBeTruthy();
    await fireEvent.press(screen.getByRole('button', { name: 'Dismiss notice' }));
    expect(useAppStore.getState().warnings).toEqual([]);
  });

  it('Settings changes theme through the store and survives missing native storage', async () => {
    await wrap(<SettingsScreen navigation={nav()} route={{} as never} />);
    await fireEvent.press(screen.getByRole('radio', { name: 'Light' }));
    expect(useAppStore.getState().settings.themePreference).toBe('light');
    await waitFor(() => expect(screen.getByText(/Storage info unavailable/)).toBeTruthy());
  });

  it('Device screen summarises tier, controls and model readiness', async () => {
    await wrap(<DeviceInfoScreen navigation={nav()} route={{} as never} />);
    expect(screen.getByText('Performance tier')).toBeTruthy();
    expect(screen.getByText('Manual ISO')).toBeTruthy();
    expect(screen.getByText('50–6400')).toBeTruthy();
    expect(screen.getAllByText('Ready').length).toBeGreaterThan(0);
  });

  it('Marketplace is browsable for free with clear coming-soon labelling', async () => {
    await wrap(<MarketplaceScreen />);
    expect(screen.getByText('Featured packs')).toBeTruthy();
    expect(screen.getByLabelText(/Golden Coast.*Included with Premium.*Coming soon/)).toBeTruthy();
  });
});

describe('App boot', () => {
  // Integration test over the whole app tree; generous timeout for cold
  // transform caches with coverage instrumentation.
  it('shows the splash then a retryable error when native storage is missing', async () => {
    useAppStore.setState({ status: 'idle', repos: null });
    await render(<App />);
    // Wait on the outcome, not a fixed delay: cold module loading can be slow.
    await waitFor(() => expect(useAppStore.getState().status).toBe('error'));
    expect(await screen.findByText('Something went wrong at startup')).toBeTruthy();
  }, 20000);
});
