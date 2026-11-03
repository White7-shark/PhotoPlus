module.exports = {
  root: true,
  extends: '@react-native',
  rules: {
    // Styles are derived from runtime theme tokens (light/dark, density,
    // breakpoints), so most cannot live in a static StyleSheet. Static
    // structural styles still use StyleSheet.create where it matters.
    'react-native/no-inline-styles': 'off',
  },
};
