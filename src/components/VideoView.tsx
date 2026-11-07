import React from 'react';
import { StyleSheet, View, type ViewStyle } from 'react-native';

import NativeVideoView from '../native/specs/PhotoPlusVideoViewNativeComponent';

export interface VideoViewProps {
  /** Absolute path of a clip in app storage. */
  source: string;
  paused?: boolean;
  loop?: boolean;
  muted?: boolean;
  onReady?: (info: { durationMs: number; width: number; height: number }) => void;
  onError?: (message: string) => void;
  accessibilityLabel?: string;
  style?: ViewStyle;
  testID?: string;
}

/** Plays a local clip, fitted inside the view (Kotlin: com.photoplus.video.VideoPlayerView). */
export function VideoView({
  source,
  paused = false,
  loop = true,
  muted = false,
  onReady,
  onError,
  accessibilityLabel = 'Video',
  style,
  testID = 'video-view',
}: VideoViewProps) {
  return (
    <View style={[styles.box, style]} accessible accessibilityLabel={accessibilityLabel}>
      <NativeVideoView
        testID={testID}
        style={StyleSheet.absoluteFill}
        source={source}
        paused={paused}
        loop={loop}
        muted={muted}
        onVideoReady={e =>
          onReady?.({
            durationMs: e.nativeEvent.durationMs,
            width: e.nativeEvent.width,
            height: e.nativeEvent.height,
          })
        }
        onVideoError={e => onError?.(e.nativeEvent.message)}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  box: { backgroundColor: '#000', overflow: 'hidden' },
});
