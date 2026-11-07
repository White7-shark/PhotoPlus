import type { CodegenTypes, ViewProps } from 'react-native';
import { codegenNativeComponent } from 'react-native';

type VideoReadyEvent = Readonly<{
  durationMs: CodegenTypes.Double;
  width: CodegenTypes.Int32;
  height: CodegenTypes.Int32;
}>;

type VideoErrorEvent = Readonly<{
  message: string;
}>;

/** Plays a local video file (Glide results), fitted inside the view. */
export interface NativeProps extends ViewProps {
  /** Absolute path of an app file. */
  source?: string;
  paused?: CodegenTypes.WithDefault<boolean, false>;
  loop?: CodegenTypes.WithDefault<boolean, true>;
  muted?: CodegenTypes.WithDefault<boolean, false>;
  onVideoReady?: CodegenTypes.DirectEventHandler<VideoReadyEvent>;
  onVideoError?: CodegenTypes.DirectEventHandler<VideoErrorEvent>;
}

export default codegenNativeComponent<NativeProps>('PhotoPlusVideoView');
