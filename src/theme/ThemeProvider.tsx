import React, { createContext, useContext, useMemo } from 'react';
import { useColorScheme } from 'react-native';

import type { LucidLook } from './lucid';
import { createTheme, type Theme, type ThemeMode, type ThemePreference } from './tokens';

const ThemeContext = createContext<Theme>(createTheme('dark'));

export function resolveThemeMode(preference: ThemePreference, systemScheme: string | null | undefined): ThemeMode {
  if (preference === 'system' || preference === 'lucid') {
    return systemScheme === 'light' ? 'light' : 'dark';
  }
  return preference;
}

interface ThemeProviderProps {
  preference?: ThemePreference;
  /** The Lucid look (used when the preference is 'lucid'; until it is known, the phone's light or dark). */
  lucid?: LucidLook | null;
  children: React.ReactNode;
}

export function ThemeProvider({ preference = 'dark', lucid = null, children }: ThemeProviderProps) {
  const system = useColorScheme();
  const mode = resolveThemeMode(preference, system);
  const look = preference === 'lucid' ? lucid : null;
  const theme = useMemo(() => createTheme(mode, look), [mode, look]);
  return <ThemeContext.Provider value={theme}>{children}</ThemeContext.Provider>;
}

export function useTheme(): Theme {
  return useContext(ThemeContext);
}
