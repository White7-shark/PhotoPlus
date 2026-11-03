import {
  ANDROID_PERMISSIONS as P,
  PermissionService,
  permissionsFor,
  type PermissionsBackend,
} from '../src/services/permissions/PermissionService';

function backend(
  sdkInt: number,
  initial: Record<string, boolean> = {},
  answers: Record<string, 'granted' | 'denied' | 'never_ask_again'> = {},
): PermissionsBackend & { granted: Record<string, boolean> } {
  const granted = { ...initial };
  return {
    sdkInt,
    granted,
    check: async p => !!granted[p],
    request: async p => {
      const a = answers[p] ?? 'denied';
      if (a === 'granted') granted[p] = true;
      return a;
    },
    requestMultiple: async ps => Object.fromEntries(ps.map(p => [p, answers[p] ?? 'denied'])),
    openSettings: jest.fn(async () => undefined),
  };
}

describe('permission mapping across Android versions', () => {
  it('uses granular media permissions on 13+, partial access on 14+, legacy below', () => {
    expect(permissionsFor('photos', 29)).toEqual([P.READ_EXTERNAL_STORAGE]);
    expect(permissionsFor('photos', 33)).toEqual([P.READ_MEDIA_IMAGES]);
    expect(permissionsFor('photos', 34)).toEqual([P.READ_MEDIA_IMAGES, P.READ_MEDIA_VISUAL_USER_SELECTED]);
    expect(permissionsFor('camera', 29)).toEqual([P.CAMERA]);
  });
});

describe('PermissionService', () => {
  it('reports granted without prompting', async () => {
    const b = backend(34, { [P.CAMERA]: true });
    const spy = jest.spyOn(b, 'request');
    expect(await new PermissionService(b).ensure('camera')).toBe('granted');
    expect(spy).not.toHaveBeenCalled();
  });

  it('prompts when denied and returns the answer', async () => {
    const svc = new PermissionService(backend(30, {}, { [P.CAMERA]: 'granted' }));
    expect(await svc.ensure('camera')).toBe('granted');
  });

  it('remembers "never ask again" as blocked and does not re-prompt', async () => {
    const b = backend(30, {}, { [P.CAMERA]: 'never_ask_again' });
    const svc = new PermissionService(b);
    expect(await svc.request('camera')).toBe('blocked');
    const spy = jest.spyOn(b, 'request');
    expect(await svc.ensure('camera')).toBe('blocked');
    expect(spy).not.toHaveBeenCalled();
  });

  it('recovers from blocked once the user enables it in settings', async () => {
    const b = backend(30, {}, { [P.CAMERA]: 'never_ask_again' });
    const svc = new PermissionService(b);
    await svc.request('camera');
    b.granted[P.CAMERA] = true;
    expect(await svc.check('camera')).toBe('granted');
  });

  it('reports limited photo access on Android 14 partial grants', async () => {
    const svc = new PermissionService(backend(34, {}, { [P.READ_MEDIA_VISUAL_USER_SELECTED]: 'granted' }));
    expect(await svc.request('photos')).toBe('limited');
    const svc2 = new PermissionService(backend(34, { [P.READ_MEDIA_VISUAL_USER_SELECTED]: true }));
    expect(await svc2.check('photos')).toBe('limited');
  });

  it('opens system settings', async () => {
    const b = backend(34);
    await new PermissionService(b).openSettings();
    expect(b.openSettings).toHaveBeenCalled();
  });
});
