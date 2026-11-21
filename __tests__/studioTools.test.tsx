import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';

import { DownloadSize } from '../src/screens/studio/DownloadSize';
import { PhotoshootStudioScreen, STUDIO_TOOLS } from '../src/screens/studio/PhotoshootStudioScreen';
import { RelightScreen } from '../src/screens/studio/RelightScreen';
import { DEFAULT_SETTINGS } from '../src/services/database/repositories/SettingsRepository';
import { EXPORT_SIZES, defaultExportSize, exportPx } from '../src/services/studio/exportSize';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';

const report = {
  coverage: 0.2,
  edge: 0.01,
  hairEdge: 0.3,
  box: [0.25, 0.1, 0.75, 0.95],
  cutLeft: false,
  cutTop: false,
  cutRight: false,
  cutBottom: false,
  found: true,
};
const mockStudio = {
  startSession: jest.fn(async () => ({
    sessionId: 's1',
    photoPath: '/cache/photo.jpg',
    cutoutPath: '/cache/cut.png',
    width: 900,
    height: 1200,
    report,
    parts: true,
  })),
  renderRelight: jest.fn(async (_id: string, req: { quality: string }) => ({
    outputPath: req.quality === 'full' ? '/files/studio/Relight_1.jpg' : '/cache/relight.jpg',
    width: 900,
    height: 1200,
    sizeBytes: 1000,
    format: 'jpeg',
  })),
  saveToGallery: jest.fn(async () => 'content://gallery/1'),
  refineCutout: jest.fn(),
  renderScene: jest.fn(),
  renderFilm: jest.fn(),
  renderCollage: jest.fn(),
  endSession: jest.fn(),
  cancel: jest.fn(),
};
const mockStorage = { pickImage: jest.fn(async () => '/files/imports/me.jpg') };
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get studio() {
        return mockStudio;
      },
      get storage() {
        return mockStorage;
      },
    },
  };
});

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 6; i++) await tick();
  });
const navigation = { navigate: jest.fn(), goBack: jest.fn(), setParams: jest.fn() };
const images = { add: jest.fn(async (r: object) => ({ id: 'img1', ...r })) };
const projects = { ensureInbox: jest.fn(async () => ({ id: 'inbox' })) };

beforeEach(() => {
  jest.clearAllMocks();
  useAppStore.setState({
    repos: { images, projects } as never,
    settings: { ...DEFAULT_SETTINGS },
    loadProjects: jest.fn(async () => undefined),
  } as never);
});

describe('the studio as a grid of tools', () => {
  it('opens a tool on tap and tells what each is from its i', async () => {
    await wrap(
      <PhotoshootStudioScreen
        navigation={navigation as never}
        route={{ key: 'p', name: 'PhotoshootStudio' } as never}
      />,
    );
    for (const t of STUDIO_TOOLS) expect(screen.getByTestId(`studio-${t.id}`)).toBeTruthy();
    expect(screen.queryByTestId('studio-tip')).toBeNull();
    // The tooltip: what the tool is, its plan, and closing it.
    await fireEvent.press(screen.getByTestId('studio-info-film-collage'));
    expect(screen.getByTestId('studio-tip')).toBeTruthy();
    expect(screen.getByText(/film strip or a collage/)).toBeTruthy();
    expect(screen.getByText('Free')).toBeTruthy();
    await fireEvent.press(screen.getByTestId('studio-tip-close'));
    expect(screen.queryByTestId('studio-tip')).toBeNull();
    // A long press shows it too; a coming-soon tool explains itself instead of opening.
    await fireEvent(screen.getByTestId('studio-relight'), 'longPress');
    expect(screen.getByText(/Studio lighting on your own photo/)).toBeTruthy();
    await fireEvent.press(screen.getByTestId('studio-recreate'));
    expect(screen.getByText(/Premium · Coming soon/)).toBeTruthy();
    expect(navigation.navigate).not.toHaveBeenCalled();
    // Tools open on tap.
    await fireEvent.press(screen.getByTestId('studio-relight'));
    await fireEvent.press(screen.getByTestId('studio-film-collage'));
    expect(navigation.navigate.mock.calls.map(c => c[0])).toEqual(['Relight', 'FilmCollage']);
  });
});

describe('download sizes', () => {
  it('offers 360p to 4K, with 720p and up for Premium', async () => {
    expect(EXPORT_SIZES.map(s => s.label)).toEqual(['360p', '480p', '720p', '1080p', 'HD', '4K']);
    expect(EXPORT_SIZES.filter(s => s.premium).map(s => s.px)).toEqual([720, 1080, 1440, 2160]);
    expect(defaultExportSize(false)).toBe('480p');
    expect(exportPx('4k')).toBe(2160);
    const onChange = jest.fn();
    const onLocked = jest.fn();
    await wrap(<DownloadSize value="480p" onChange={onChange} premium={false} onLocked={onLocked} />);
    await fireEvent.press(screen.getByTestId('size-360p'));
    await fireEvent.press(screen.getByTestId('size-4k'));
    expect(onChange).toHaveBeenCalledWith('360p');
    expect(onLocked).toHaveBeenCalledTimes(1);
    expect(screen.getByLabelText('4K, Premium')).toBeTruthy();
  });
});

describe('Relight', () => {
  it('lights the person in their own photo, changes the background and saves at the size chosen', async () => {
    await wrap(<RelightScreen navigation={navigation as never} route={{ key: 'r', name: 'Relight' } as never} />);
    await step(() => fireEvent.press(screen.getByTestId('relight-pick')));
    expect(mockStudio.startSession).toHaveBeenCalledTimes(1);
    // Soft light by default, the background as it is.
    const first = mockStudio.renderRelight.mock.calls.at(-1)![1] as unknown as {
      light?: { preset: string };
      background: object;
      quality: string;
    };
    expect(first).toMatchObject({
      sessionId: 's1',
      quality: 'preview',
      background: { brightness: 0, colour: 0, blur: 0 },
    });
    expect(first.light).toBeTruthy();
    expect(screen.getByTestId('relight-preview')).toBeTruthy();
    // A darker, softer background.
    await step(() => fireEvent.press(screen.getByTestId('relight-tab-background')));
    await step(() =>
      fireEvent(screen.getByLabelText('Brightness'), 'accessibilityAction', {
        nativeEvent: { actionName: 'decrement' },
      }),
    );
    await step(() =>
      fireEvent(screen.getByLabelText('Soften'), 'accessibilityAction', { nativeEvent: { actionName: 'increment' } }),
    );
    const bg = (
      mockStudio.renderRelight.mock.calls.at(-1)![1] as unknown as { background: { brightness: number; blur: number } }
    ).background;
    expect(bg.brightness).toBeCloseTo(-0.05, 5);
    expect(bg.blur).toBeCloseTo(0.05, 5);
    // Natural pink lips go with it.
    await step(() => fireEvent.press(screen.getByTestId('relight-tab-lips')));
    await step(() => fireEvent.press(screen.getByTestId('pink-lips-on')));
    expect(mockStudio.renderRelight.mock.calls.at(-1)![1]).toMatchObject({ lips: { natural: true } });
    // Saved at 4K, into the gallery and the library.
    await step(() => fireEvent.press(screen.getByTestId('size-4k')));
    await step(() => fireEvent.press(screen.getByTestId('relight-save')));
    expect(mockStudio.renderRelight.mock.calls.at(-1)![1]).toMatchObject({ quality: 'full', resolution: 2160 });
    expect(mockStudio.saveToGallery).toHaveBeenCalledWith('/files/studio/Relight_1.jpg');
    expect(navigation.navigate).toHaveBeenCalledWith('PhotoPreview', { imageId: 'img1' });
  });

  it('takes the photo with the camera', async () => {
    await wrap(<RelightScreen navigation={navigation as never} route={{ key: 'r', name: 'Relight' } as never} />);
    await fireEvent.press(screen.getByTestId('relight-shoot'));
    expect(navigation.navigate).toHaveBeenCalledWith('Main', { screen: 'Camera', params: { returnTo: 'Relight' } });
  });
});
