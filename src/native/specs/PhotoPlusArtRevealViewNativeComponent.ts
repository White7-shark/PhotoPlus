import type { CodegenTypes, ViewProps } from 'react-native';
import { codegenNativeComponent } from 'react-native';

type RevealReadyEvent = Readonly<{
  strokes: CodegenTypes.Int32;
}>;

type RevealDoneEvent = Readonly<{
  done: boolean;
}>;

type RevealErrorEvent = Readonly<{
  message: string;
}>;

/**
 * The live studio (Kotlin: com.photoplus.studio.ArtRevealView): the blank
 * sheet and a pencil or brush moving over it, revealing the finished work
 * stroke by stroke until it is complete.
 */
export interface NativeProps extends ViewProps {
  /** The finished work (an app file). */
  work?: string;
  /** The blank sheet or primed ground it starts from, the same size. */
  blank?: string;
  /** 0 a pencil, 1 a brush. */
  tool?: CodegenTypes.WithDefault<CodegenTypes.Int32, 0>;
  /** The master (0..19): their hatching direction. */
  artist?: CodegenTypes.WithDefault<CodegenTypes.Int32, 0>;
  /** The picture inside a frame: x0, y0, x1, y1 (0..1). */
  inner?: ReadonlyArray<CodegenTypes.Float>;
  durationMs?: CodegenTypes.WithDefault<CodegenTypes.Int32, 7000>;
  playing?: CodegenTypes.WithDefault<boolean, true>;
  /** Jump to the finished work. */
  finish?: CodegenTypes.WithDefault<boolean, false>;
  onRevealReady?: CodegenTypes.DirectEventHandler<RevealReadyEvent>;
  onRevealDone?: CodegenTypes.DirectEventHandler<RevealDoneEvent>;
  onRevealError?: CodegenTypes.DirectEventHandler<RevealErrorEvent>;
}

export default codegenNativeComponent<NativeProps>('PhotoPlusArtRevealView');
