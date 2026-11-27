import { act, fireEvent, render, screen } from '@testing-library/react-native';
import React from 'react';
import { Alert } from 'react-native';

import { openDatabase, type Repositories } from '../src/services/database/Database';
import { normaliseTitle } from '../src/services/database/repositories/StudioProjectsRepository';
import { MIGRATIONS } from '../src/services/database/migrations';
import { nextProjectName, saveWork } from '../src/services/projects/studios';
import { StudioItemScreen } from '../src/screens/projects/StudioItemScreen';
import { StudioProjectScreen } from '../src/screens/projects/StudioProjectScreen';
import { StudioProjectsScreen } from '../src/screens/projects/StudioProjectsScreen';
import { useAppStore } from '../src/state/store';
import { ThemeProvider } from '../src/theme';
import { SqlJsExecutor } from './helpers/sqljsExecutor';

const mockStorage = {
  pickImages: jest.fn(async (_area: string, _max: number) => ['/files/imports/a.jpg', '/files/imports/b.jpg']),
  deleteFile: jest.fn(async () => true),
  shareImages: jest.fn(async () => true),
  saveVideoToGallery: jest.fn(async () => 'content://video/1'),
};
const mockStudio = { saveToGallery: jest.fn(async () => 'content://image/1') };
jest.mock('../src/native', () => {
  const actual = jest.requireActual('../src/native');
  return {
    ...actual,
    nativeModules: {
      ...actual.nativeModules,
      get storage() {
        return mockStorage;
      },
      get studio() {
        return mockStudio;
      },
    },
  };
});

async function freshRepos(): Promise<Repositories> {
  const db = await SqlJsExecutor.create();
  return openDatabase(async () => db);
}

const wrap = (ui: React.ReactElement) => render(<ThemeProvider preference="dark">{ui}</ThemeProvider>);
const tick = () => new Promise<void>(r => setTimeout(() => r(), 0));
const step = async (fn: () => unknown) =>
  act(async () => {
    await fn();
    for (let i = 0; i < 15; i++) await tick();
  });
const nav = () => ({
  navigate: jest.fn(),
  goBack: jest.fn(),
  setParams: jest.fn(),
  replace: jest.fn(),
  addListener: jest.fn(() => () => undefined),
});

beforeEach(() => {
  jest.clearAllMocks();
});

describe('studio projects in the database', () => {
  it('keeps each studio’s projects, their photos and works, with counts and a cover', async () => {
    const repos = await freshRepos();
    expect(MIGRATIONS.at(-1)!.name).toBe('studio_projects');
    const s = repos.studioProjects;
    const a = await s.create('arts', '  Wedding   portraits ');
    expect(a.name).toBe('Wedding portraits');
    await s.create('motion', 'Clips');
    expect((await s.list('arts')).map(p => p.name)).toEqual(['Wedding portraits']);
    await expect(s.create('arts', '   ')).rejects.toThrow();
    await expect(s.create('cooking' as never, 'x')).rejects.toThrow();

    const [p1, p2] = await s.addItems(a.id, [
      { kind: 'photo', path: '/p1.jpg', title: 'Photo 1' },
      { kind: 'photo', path: '/p2.jpg', title: 'Photo 2' },
    ]);
    const w = await saveWork(repos, a.id, {
      path: '/w1.jpg',
      sourcePath: '/p1.jpg',
      title: 'Rembrandt · Painting',
      width: 1500,
      height: 1900,
      settings: { artist: 6, level: 2 },
    });
    const got = (await s.get(a.id))!;
    expect(got).toMatchObject({ photoCount: 2, workCount: 1, coverPath: '/w1.jpg' });
    expect((await s.items(a.id, 'photo')).map(i => i.path)).toEqual(['/p2.jpg', '/p1.jpg']);
    expect(await s.item(w.id)).toMatchObject({
      kind: 'work',
      sourcePath: '/p1.jpg',
      settings: { artist: 6, level: 2 },
      media: 'image',
    });

    await s.renameItem(w.id, 'My Rembrandt');
    expect((await s.item(w.id))!.title).toBe('My Rembrandt');
    // A photo that a work was made from: its file stays (to edit the work again).
    expect(await s.deleteItem(p1.id)).toEqual([]);
    expect(await s.deleteItem(p2.id)).toEqual(['/p2.jpg']);
    // Deleting the project returns every file left.
    expect(await s.delete(a.id)).toEqual(['/w1.jpg']);
    expect(await s.get(a.id)).toBeNull();
    expect(await s.items(a.id)).toEqual([]);
    expect(normaliseTitle('x'.repeat(100))).toHaveLength(80);
  });

  it('names a new project with the next free number', () => {
    expect(nextProjectName([])).toBe('Project 1');
    const p = (name: string) => ({ name }) as never;
    expect(nextProjectName([p('Project 2')])).toBe('Project 3');
    expect(nextProjectName([p('Project 2'), p('Project 3')])).toBe('Project 4');
  });
});

describe('the studio project screens', () => {
  it('creates a project from a studio, then adds photos and opens one in the studio', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const n = nav();
    await wrap(
      <StudioProjectsScreen
        navigation={n as never}
        route={{ key: 'p', name: 'StudioProjects', params: { studio: 'arts' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByTestId('studio-projects-empty')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('studio-projects-new')));
    expect(screen.getByTestId('name-sheet-input').props.value).toBe('Project 1');
    await step(() => fireEvent.changeText(screen.getByTestId('name-sheet-input'), 'Studio portraits'));
    await step(() => fireEvent.press(screen.getByTestId('name-sheet-confirm')));
    const [project] = await repos.studioProjects.list('arts');
    expect(project.name).toBe('Studio portraits');
    expect(n.navigate).toHaveBeenCalledWith('StudioProject', { projectId: project.id });

    // The project: add many photos, then create with one.
    const pn = nav();
    await wrap(
      <StudioProjectScreen
        navigation={pn as never}
        route={{ key: 'q', name: 'StudioProject', params: { projectId: project.id } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByTestId('studio-project-name').props.children).toBe('Studio portraits');
    await step(() => fireEvent.press(screen.getByTestId('studio-project-add')));
    expect(mockStorage.pickImages).toHaveBeenCalledWith('imports', 30);
    const photos = await repos.studioProjects.items(project.id, 'photo');
    expect(photos.map(p => p.title).sort()).toEqual(['Photo 1', 'Photo 2']);
    await step(() => fireEvent.press(screen.getByTestId(`studio-photo-${photos[0].id}`)));
    expect(pn.navigate).toHaveBeenCalledWith('Masters', { photo: photos[0].path, projectId: project.id });

    // Its gallery: a work, opened.
    const w = await saveWork(repos, project.id, {
      path: '/w.jpg',
      sourcePath: photos[0].path,
      title: 'Klimt · Painting',
    });
    await step(() => fireEvent.press(screen.getByTestId('studio-project-tab-gallery')));
    await step(() => fireEvent.press(screen.getByTestId('studio-project-tab-photos')));
    await step(() => fireEvent.press(screen.getByTestId('studio-project-tab-gallery')));
    await step(() => fireEvent.press(screen.getByTestId('studio-project-rename')));
    await step(() => fireEvent.changeText(screen.getByTestId('name-sheet-input'), 'Gallery show'));
    await step(() => fireEvent.press(screen.getByTestId('name-sheet-confirm')));
    expect((await repos.studioProjects.get(project.id))!.name).toBe('Gallery show');
    await step(() => fireEvent.press(screen.getByTestId(`studio-work-${w.id}`)));
    expect(pn.navigate).toHaveBeenCalledWith('StudioItem', { itemId: w.id });
  });

  it('puts a photo just taken into the project chosen for it, and opens it in the studio', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const p = await repos.studioProjects.create('photoshoot', 'Shoot');
    const n = nav();
    await wrap(
      <StudioProjectsScreen
        navigation={n as never}
        route={{ key: 'p', name: 'StudioProjects', params: { studio: 'photoshoot', photo: '/shot.jpg' } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByTestId('studio-projects-photo')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId(`studio-project-${p.id}`)));
    expect((await repos.studioProjects.items(p.id, 'photo')).map(i => i.path)).toEqual(['/shot.jpg']);
    expect(n.navigate).toHaveBeenCalledWith('SceneSwap', { photo: '/shot.jpg', projectId: p.id });
  });

  it('renames and deletes projects', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const p = await repos.studioProjects.create('motion', 'Clips');
    await repos.studioProjects.addItems(p.id, [{ kind: 'photo', path: '/c.jpg' }]);
    const n = nav();
    await wrap(
      <StudioProjectsScreen
        navigation={n as never}
        route={{ key: 'p', name: 'StudioProjects', params: { studio: 'motion' } } as never}
      />,
    );
    await step(() => undefined);
    await step(() => fireEvent.press(screen.getByTestId(`studio-project-rename-${p.id}`)));
    await step(() => fireEvent.changeText(screen.getByTestId('name-sheet-input'), 'Summer clips'));
    await step(() => fireEvent.press(screen.getByTestId('name-sheet-confirm')));
    expect((await repos.studioProjects.get(p.id))!.name).toBe('Summer clips');
    const alert = jest.spyOn(Alert, 'alert').mockImplementation((_t, _m, buttons) => buttons?.[1]?.onPress?.());
    await step(() => fireEvent.press(screen.getByTestId(`studio-project-delete-${p.id}`)));
    expect(await repos.studioProjects.get(p.id)).toBeNull();
    expect(mockStorage.deleteFile).toHaveBeenCalledWith('/c.jpg');
    alert.mockRestore();
  });

  it('shows a finished work: download, share, edit again, rename and delete', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const p = await repos.studioProjects.create('arts', 'Portraits');
    const w = await saveWork(repos, p.id, {
      path: '/studio/PencilArt_1.jpg',
      sourcePath: '/me.jpg',
      title: 'Dürer · Pencil Art',
      width: 2048,
      height: 2560,
      settings: { artist: 3, mode: 'pencil', inner: [0, 0, 1, 1] },
    });
    const n = nav();
    await wrap(
      <StudioItemScreen
        navigation={n as never}
        route={{ key: 'i', name: 'StudioItem', params: { itemId: w.id, fresh: true } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByText('Your work is ready')).toBeTruthy();
    expect(screen.getByTestId('studio-item-fresh')).toBeTruthy();
    expect(screen.getByText(/2048 × 2560/)).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('studio-item-download')));
    expect(mockStudio.saveToGallery).toHaveBeenCalledWith('/studio/PencilArt_1.jpg');
    expect(screen.getByText('Saved to your gallery')).toBeTruthy();
    await step(() => fireEvent.press(screen.getByTestId('studio-item-share')));
    expect(mockStorage.shareImages).toHaveBeenCalledWith(['/studio/PencilArt_1.jpg'], 'Dürer · Pencil Art');
    await step(() => fireEvent.press(screen.getByTestId('studio-item-edit')));
    expect(n.navigate).toHaveBeenCalledWith('Masters', {
      photo: '/me.jpg',
      projectId: p.id,
      edit: { artist: 3, mode: 'pencil', inner: [0, 0, 1, 1] },
    });
    await step(() => fireEvent.press(screen.getByTestId('studio-item-gallery')));
    expect(n.navigate).toHaveBeenCalledWith('StudioProject', { projectId: p.id, tab: 'gallery' });
    await step(() => fireEvent.press(screen.getByTestId('studio-item-rename')));
    await step(() => fireEvent.changeText(screen.getByTestId('name-sheet-input'), 'For the hallway'));
    await step(() => fireEvent.press(screen.getByTestId('name-sheet-confirm')));
    expect((await repos.studioProjects.item(w.id))!.title).toBe('For the hallway');
    const alert = jest.spyOn(Alert, 'alert').mockImplementation((_t, _m, buttons) => buttons?.[1]?.onPress?.());
    await step(() => fireEvent.press(screen.getByTestId('studio-item-delete')));
    expect(await repos.studioProjects.item(w.id)).toBeNull();
    expect(n.goBack).toHaveBeenCalled();
    alert.mockRestore();
  });

  it('downloads a video work to the phone’s videos', async () => {
    const repos = await freshRepos();
    useAppStore.setState({ repos });
    const p = await repos.studioProjects.create('motion', 'Clips');
    const w = await saveWork(repos, p.id, {
      path: '/exports/PhotoMotion_1.mp4',
      posterPath: '/exports/p.jpg',
      media: 'video',
      title: 'Orbit · 5 s',
    });
    await wrap(
      <StudioItemScreen
        navigation={nav() as never}
        route={{ key: 'i', name: 'StudioItem', params: { itemId: w.id } } as never}
      />,
    );
    await step(() => undefined);
    expect(screen.getByTestId('studio-item-video')).toBeTruthy();
    // No photo kept with it: nothing to edit again.
    expect(screen.queryByTestId('studio-item-edit')).toBeNull();
    await step(() => fireEvent.press(screen.getByTestId('studio-item-download')));
    expect(mockStorage.saveVideoToGallery).toHaveBeenCalledWith('/exports/PhotoMotion_1.mp4');
  });
});
