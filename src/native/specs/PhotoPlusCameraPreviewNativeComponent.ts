import type { CodegenTypes, ViewProps } from 'react-native';
import { codegenNativeComponent } from 'react-native';

type CameraReadyEvent = Readonly<{
  lensFacing: string;
}>;

type CameraErrorEvent = Readonly<{
  code: string;
  message: string;
}>;

/** Normalised (0..1) position of a tap-to-focus within the preview. */
type FocusTapEvent = Readonly<{
  x: CodegenTypes.Double;
  y: CodegenTypes.Double;
}>;

export interface NativeProps extends ViewProps {
  lensFacing?: CodegenTypes.WithDefault<'back' | 'front', 'back'>;
  active?: CodegenTypes.WithDefault<boolean, true>;
  /** Native pinch-to-zoom and tap-to-focus (camera latency, no JS round trip). */
  gesturesEnabled?: CodegenTypes.WithDefault<boolean, true>;
  onCameraReady?: CodegenTypes.DirectEventHandler<CameraReadyEvent>;
  onCameraError?: CodegenTypes.DirectEventHandler<CameraErrorEvent>;
  onFocusTap?: CodegenTypes.DirectEventHandler<FocusTapEvent>;
}

export default codegenNativeComponent<NativeProps>('PhotoPlusCameraPreview');
