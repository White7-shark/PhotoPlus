import React from 'react';
import { StyleSheet, View, type ViewStyle } from 'react-native';

import NativeCameraPreview from '../native/specs/PhotoPlusCameraPreviewNativeComponent';

export interface CameraPreviewProps {
  lensFacing: 'back' | 'front';
  active: boolean;
  /** Native pinch-to-zoom and tap-to-focus. */
  gesturesEnabled?: boolean;
  onReady?: (lensFacing: string) => void;
  onError?: (code: string, message: string) => void;
  /** Normalised tap position; the native view has already started focusing there. */
  onFocusTap?: (x: number, y: number) => void;
  style?: ViewStyle;
}

/** Live CameraX preview (Kotlin: com.photoplus.camera.CameraPreviewView). */
export function CameraPreview({
  lensFacing,
  active,
  gesturesEnabled = true,
  onReady,
  onError,
  onFocusTap,
  style,
}: CameraPreviewProps) {
  return (
    <View
      style={[StyleSheet.absoluteFill, style]}
      accessibilityLabel="Camera viewfinder. Pinch to zoom, tap to focus."
      accessible
    >
      <NativeCameraPreview
        testID="camera-preview"
        style={StyleSheet.absoluteFill}
        lensFacing={lensFacing}
        active={active}
        gesturesEnabled={gesturesEnabled}
        onCameraReady={e => onReady?.(e.nativeEvent.lensFacing)}
        onCameraError={e => onError?.(e.nativeEvent.code, e.nativeEvent.message)}
        onFocusTap={e => onFocusTap?.(e.nativeEvent.x, e.nativeEvent.y)}
      />
    </View>
  );
}
