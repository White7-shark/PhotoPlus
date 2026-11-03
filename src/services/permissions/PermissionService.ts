import { Linking, PermissionsAndroid, Platform, type Permission } from 'react-native';

/**
 * Runtime permission management.
 *
 * Android's permission model changed across our supported range (API 29+):
 * photo access moved from READ_EXTERNAL_STORAGE to READ_MEDIA_IMAGES (33)
 * and gained partial "selected photos" access (34). This service hides those
 * differences behind one status vocabulary.
 *
 * App-private files (captures, projects, exports) need no permission at all.
 */

export type AppPermission = 'camera' | 'photos';
export type PermissionStatus = 'granted' | 'limited' | 'denied' | 'blocked' | 'unavailable';

export const ANDROID_PERMISSIONS = {
  CAMERA: 'android.permission.CAMERA',
  READ_MEDIA_IMAGES: 'android.permission.READ_MEDIA_IMAGES',
  READ_MEDIA_VISUAL_USER_SELECTED: 'android.permission.READ_MEDIA_VISUAL_USER_SELECTED',
  READ_EXTERNAL_STORAGE: 'android.permission.READ_EXTERNAL_STORAGE',
} as const;

type RequestResult = 'granted' | 'denied' | 'never_ask_again';

export interface PermissionsBackend {
  sdkInt: number;
  check(permission: string): Promise<boolean>;
  request(permission: string, rationale?: Rationale): Promise<RequestResult>;
  requestMultiple(permissions: string[]): Promise<Record<string, RequestResult>>;
  openSettings(): Promise<void>;
}

export interface Rationale {
  title: string;
  message: string;
  buttonPositive: string;
}

export function permissionsFor(permission: AppPermission, sdkInt: number): string[] {
  if (permission === 'camera') {
    return [ANDROID_PERMISSIONS.CAMERA];
  }
  if (sdkInt >= 34) {
    return [ANDROID_PERMISSIONS.READ_MEDIA_IMAGES, ANDROID_PERMISSIONS.READ_MEDIA_VISUAL_USER_SELECTED];
  }
  if (sdkInt >= 33) {
    return [ANDROID_PERMISSIONS.READ_MEDIA_IMAGES];
  }
  return [ANDROID_PERMISSIONS.READ_EXTERNAL_STORAGE];
}

export const RATIONALES: Record<AppPermission, Rationale> = {
  camera: {
    title: 'Camera access',
    message: 'Photo Plus uses the camera to take photos. Nothing is uploaded — your shots stay on this device.',
    buttonPositive: 'Continue',
  },
  photos: {
    title: 'Photo library access',
    message: 'Allow access to import photos you want to edit. You can choose specific photos.',
    buttonPositive: 'Continue',
  },
};

export function createAndroidBackend(): PermissionsBackend {
  return {
    sdkInt: typeof Platform.Version === 'number' ? Platform.Version : parseInt(String(Platform.Version), 10),
    check: p => PermissionsAndroid.check(p as Permission),
    request: (p, rationale) => PermissionsAndroid.request(p as Permission, rationale) as Promise<RequestResult>,
    requestMultiple: ps =>
      PermissionsAndroid.requestMultiple(ps as Permission[]) as Promise<Record<string, RequestResult>>,
    openSettings: () => Linking.openSettings(),
  };
}

export class PermissionService {
  /** Remembers "never ask again" answers, which `check` cannot detect. */
  private blocked = new Set<AppPermission>();

  constructor(private readonly backend: PermissionsBackend = createAndroidBackend()) {}

  async check(permission: AppPermission): Promise<PermissionStatus> {
    const [primary, partial] = permissionsFor(permission, this.backend.sdkInt);
    if (await this.backend.check(primary)) {
      this.blocked.delete(permission);
      return 'granted';
    }
    if (partial && (await this.backend.check(partial))) {
      return 'limited';
    }
    return this.blocked.has(permission) ? 'blocked' : 'denied';
  }

  async request(permission: AppPermission): Promise<PermissionStatus> {
    const list = permissionsFor(permission, this.backend.sdkInt);
    if (list.length === 1) {
      const result = await this.backend.request(list[0], RATIONALES[permission]);
      return this.record(permission, result);
    }
    const results = await this.backend.requestMultiple(list);
    const [primary, partial] = list;
    if (results[primary] === 'granted') {
      return this.record(permission, 'granted');
    }
    if (results[partial] === 'granted') {
      return 'limited';
    }
    return this.record(permission, results[primary] ?? 'denied');
  }

  /** Request only if not already granted. */
  async ensure(permission: AppPermission): Promise<PermissionStatus> {
    const current = await this.check(permission);
    if (current === 'granted' || current === 'limited' || current === 'blocked') {
      return current;
    }
    return this.request(permission);
  }

  openSettings(): Promise<void> {
    return this.backend.openSettings();
  }

  private record(permission: AppPermission, result: RequestResult): PermissionStatus {
    if (result === 'granted') {
      this.blocked.delete(permission);
      return 'granted';
    }
    if (result === 'never_ask_again') {
      this.blocked.add(permission);
      return 'blocked';
    }
    return 'denied';
  }
}

let shared: PermissionService | null = null;
export function getPermissionService(): PermissionService {
  shared ??= new PermissionService();
  return shared;
}
