import React from 'react';
import { View } from 'react-native';

import { createLogger } from '../services/logging/logger';
import { ErrorState } from './States';

const log = createLogger('ui');

interface Props {
  children: React.ReactNode;
  /** Shown in the fallback title, e.g. "Camera". */
  area?: string;
  onReset?: () => void;
}

interface State {
  error: Error | null;
}

/**
 * Contains render crashes to one area of the app (a screen or panel) so the
 * rest keeps working; the error is logged to the diagnostics buffer.
 */
export class ErrorBoundary extends React.Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo) {
    log.error(`Render error in ${this.props.area ?? 'app'}: ${error.message}`, info.componentStack);
  }

  reset = () => {
    this.setState({ error: null });
    this.props.onReset?.();
  };

  render() {
    if (this.state.error) {
      return (
        <View style={{ flex: 1, justifyContent: 'center', padding: 24 }}>
          <ErrorState
            title={`${this.props.area ?? 'This screen'} ran into a problem`}
            message="Your photos and projects are safe. You can try again, and the issue has been noted in diagnostics."
            onRetry={this.reset}
          />
        </View>
      );
    }
    return this.props.children;
  }
}
