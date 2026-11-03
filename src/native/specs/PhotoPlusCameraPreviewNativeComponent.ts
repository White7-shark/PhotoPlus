import type { CodegenTypes, ViewProps } from 'react-native';
import { codegenNativeComponent } from 'react-native';

type CameraReadyEvent = Readonly<{
  lensFacing: string;
}>;

type CameraErrorEvent = Readonly<{
  code: string;
  message: string;
}>;

export interface NativeProps extends ViewProps {
  lensFacing?: CodegenTypes.WithDefault<'back' | 'front', 'back'>;
  active?: CodegenTypes.WithDefault<boolean, true>;
  onCameraReady?: CodegenTypes.DirectEventHandler<CameraReadyEvent>;
  onCameraError?: CodegenTypes.DirectEventHandler<CameraErrorEvent>;
}

export default codegenNativeComponent<NativeProps>('PhotoPlusCameraPreview');
