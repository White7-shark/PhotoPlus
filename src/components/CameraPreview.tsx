import React from 'react';
import { StyleSheet, View, type ViewStyle } from 'react-native';

import NativeCameraPreview from '../native/specs/PhotoPlusCameraPreviewNativeComponent';

export interface CameraPreviewProps {
  lensFacing: 'back' | 'front';
  active: boolean;
  onReady?: (lensFacing: string) => void;
  onError?: (code: string, message: string) => void;
  style?: ViewStyle;
}

/** Live CameraX preview (Kotlin: com.photoplus.camera.CameraPreviewView). */
export function CameraPreview({ lensFacing, active, onReady, onError, style }: CameraPreviewProps) {
  return (
    <View style={[StyleSheet.absoluteFill, style]} accessibilityLabel="Camera viewfinder" accessible>
      <NativeCameraPreview
        style={StyleSheet.absoluteFill}
        lensFacing={lensFacing}
        active={active}
        onCameraReady={e => onReady?.(e.nativeEvent.lensFacing)}
        onCameraError={e => onError?.(e.nativeEvent.code, e.nativeEvent.message)}
      />
    </View>
  );
}
