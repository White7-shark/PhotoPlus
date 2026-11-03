import React, { createContext, useContext, useMemo } from 'react';
import { useColorScheme } from 'react-native';

import { createTheme, type Theme, type ThemeMode, type ThemePreference } from './tokens';

const ThemeContext = createContext<Theme>(createTheme('dark'));

export function resolveThemeMode(preference: ThemePreference, systemScheme: string | null | undefined): ThemeMode {
  if (preference === 'system') {
    return systemScheme === 'light' ? 'light' : 'dark';
  }
  return preference;
}

interface ThemeProviderProps {
  preference?: ThemePreference;
  children: React.ReactNode;
}

export function ThemeProvider({ preference = 'dark', children }: ThemeProviderProps) {
  const system = useColorScheme();
  const mode = resolveThemeMode(preference, system);
  const theme = useMemo(() => createTheme(mode), [mode]);
  return <ThemeContext.Provider value={theme}>{children}</ThemeContext.Provider>;
}

export function useTheme(): Theme {
  return useContext(ThemeContext);
}
