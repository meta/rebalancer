// Explorer design language tokens: the one source for color, type, space,
// shape, elevation and motion. The language is documented in
// https://artifacts.internalmeta.com/a/4269143179884086.
//
// Three tiers:
// - Primitives (hue.step): the palette. Only this file uses them.
// - Semantic tokens, named by role: what components use. Dark mode swaps only
//   these. app/globals.css declares them as `--color-*` CSS variables (light on
//   :root, dark on [data-theme="dark"]); ui-tokens.test.ts keeps both in sync.
//   Components read them through `COLOR`, which holds `var(--color-*)`
//   references, so one style serves both themes.
// - Component tokens, only where a component needs its own knob (`KPI_EDGE`).
//
// The older names further down (ACCENT, TONES, SURFACE_2, ...) are a legacy
// alias layer onto the semantic tokens, so existing call sites follow the
// language and both themes unchanged.

export const PRIMITIVES = {
  gray: {
    0: '#ffffff',
    25: '#fafbfc',
    50: '#f6f7f9',
    100: '#f2f4f7',
    200: '#e4e7ec',
    300: '#d0d5dd',
    400: '#98a2b3',
    450: '#848ea0',
    500: '#667085',
    600: '#475467',
    900: '#101828',
  },
  // Dark-theme base.
  ink: {
    950: '#0b0f17',
    900: '#111827',
    850: '#151d2c',
    800: '#1b2435',
    700: '#253046',
    600: '#33415c',
    500: '#6b7689',
    400: '#8590a3',
    300: '#98a2b3',
    200: '#c3cad6',
    50: '#f2f4f7',
  },
  blue: {
    50: '#eef4ff',
    100: '#dbe7ff',
    200: '#b2ccff',
    500: '#0866ff',
    600: '#0457d9',
    'd-950': '#13213d',
    'd-900': '#1a2c52',
    'd-400': '#4d8eff',
    'd-300': '#6aa1ff',
  },
  green: {
    50: '#ecfdf3',
    200: '#abefc6',
    500: '#12b76a',
    700: '#067647',
    'd-950': '#0c2a1c',
    'd-800': '#1f5135',
    'd-400': '#4ade80',
  },
  red: {
    50: '#fef3f2',
    200: '#fecdca',
    500: '#f04438',
    700: '#b42318',
    'd-950': '#3a1513',
    'd-800': '#6b2420',
    'd-400': '#ff7b72',
  },
  amber: {
    50: '#fffaeb',
    200: '#fedf89',
    500: '#f79009',
    700: '#b54708',
    'd-950': '#33240a',
    'd-800': '#5c3d0b',
    'd-400': '#fbbf24',
  },
  violet: {
    50: '#f4f3ff',
    200: '#d9d6fe',
    500: '#7a5af8',
    600: '#6938ef',
    'd-950': '#221d45',
    'd-800': '#3b3270',
    'd-400': '#a48afb',
  },
  // Categorical hues for categories with no meaning (charts, entity kinds).
  cat: {
    1: '#0866ff',
    2: '#7a5af8',
    3: '#0e9384',
    4: '#dd2590',
    5: '#f79009',
    6: '#2e90fa',
    7: '#66c61c',
    8: '#ef6820',
    other: '#98a2b3',
  },
} as const;

const P = PRIMITIVES;

/** Semantic color tokens: role → light and dark value. */
export const SEMANTIC_COLORS = {
  // Page background.
  'bg-canvas': {light: P.gray[50], dark: P.ink[950]},
  // Cards, panels, bars, inputs.
  'bg-surface': {light: P.gray[0], dark: P.ink[900]},
  // Table and group heads, row hover.
  'bg-surface-2': {light: P.gray[25], dark: P.ink[850]},
  // Tracks, chips, segmented background.
  'bg-subtle': {light: P.gray[100], dark: P.ink[800]},
  // Hairlines, card edges.
  'border-default': {light: P.gray[200], dark: P.ink[700]},
  // Inputs, buttons, axes.
  'border-strong': {light: P.gray[300], dark: P.ink[600]},
  // Titles, values.
  'text-primary': {light: P.gray[900], dark: P.ink[50]},
  // Body, labels.
  'text-secondary': {light: P.gray[600], dark: P.ink[200]},
  // Captions, meta, table heads: the faintest informative text (4.5:1).
  'text-tertiary': {light: P.gray[500], dark: P.ink[300]},
  // Arrows, carets, separators only (3:1).
  'text-decorative': {light: P.gray[450], dark: P.ink[500]},
  // Links, primary buttons, selection, focus.
  action: {light: P.blue[500], dark: P.blue['d-400']},
  // Hover of action; action text on tints.
  'action-hover': {light: P.blue[600], dark: P.blue['d-300']},
  // Selected rows, active nav, info tint.
  'action-subtle': {light: P.blue[50], dark: P.blue['d-950']},
  // Border of the info tint and of filter chips.
  'action-subtle-border': {light: P.blue[100], dark: P.blue['d-900']},
  // Better / ok.
  positive: {light: P.green[500], dark: P.green[500]},
  'positive-text': {light: P.green[700], dark: P.green['d-400']},
  'positive-bg': {light: P.green[50], dark: P.green['d-950']},
  'positive-border': {light: P.green[200], dark: P.green['d-800']},
  // Worse / broken.
  negative: {light: P.red[500], dark: P.red[500]},
  'negative-text': {light: P.red[700], dark: P.red['d-400']},
  'negative-bg': {light: P.red[50], dark: P.red['d-950']},
  'negative-border': {light: P.red[200], dark: P.red['d-800']},
  // Trade-off, limit, warning.
  caution: {light: P.amber[500], dark: P.amber[500]},
  'caution-text': {light: P.amber[700], dark: P.amber['d-400']},
  'caution-bg': {light: P.amber[50], dark: P.amber['d-950']},
  'caution-border': {light: P.amber[200], dark: P.amber['d-800']},
  // Alternative, what-if, structure, code operators.
  alt: {light: P.violet[500], dark: P.violet[500]},
  'alt-text': {light: P.violet[600], dark: P.violet['d-400']},
  'alt-bg': {light: P.violet[50], dark: P.violet['d-950']},
  'alt-border': {light: P.violet[200], dark: P.violet['d-800']},
  // The "before" and "after" sides of every comparison.
  baseline: {light: P.gray[400], dark: P.ink[500]},
  current: {light: P.blue[500], dark: P.blue['d-400']},
  // Tooltips, toasts and text on action or rank colors.
  'inverse-bg': {light: P.gray[900], dark: P.gray[900]},
  'inverse-text': {light: P.gray[0], dark: P.gray[0]},
  'cat-1': {light: P.cat[1], dark: P.cat[1]},
  'cat-2': {light: P.cat[2], dark: P.cat[2]},
  'cat-3': {light: P.cat[3], dark: P.cat[3]},
  'cat-4': {light: P.cat[4], dark: P.cat[4]},
  'cat-5': {light: P.cat[5], dark: P.cat[5]},
  'cat-6': {light: P.cat[6], dark: P.cat[6]},
  'cat-7': {light: P.cat[7], dark: P.cat[7]},
  'cat-8': {light: P.cat[8], dark: P.cat[8]},
  'cat-other': {light: P.cat.other, dark: P.cat.other},
} as const;

export type SemanticColorName = keyof typeof SEMANTIC_COLORS;
export type ThemeMode = 'light' | 'dark';

/** A semantic color as a CSS variable reference: follows the active theme. */
export function cssColor(name: SemanticColorName): string {
  return `var(--color-${name})`;
}

/** The semantic colors components use, as theme-following CSS variables. */
export const COLOR = {
  bgCanvas: cssColor('bg-canvas'),
  bgSurface: cssColor('bg-surface'),
  bgSurface2: cssColor('bg-surface-2'),
  bgSubtle: cssColor('bg-subtle'),
  borderDefault: cssColor('border-default'),
  borderStrong: cssColor('border-strong'),
  textPrimary: cssColor('text-primary'),
  textSecondary: cssColor('text-secondary'),
  textTertiary: cssColor('text-tertiary'),
  textDecorative: cssColor('text-decorative'),
  action: cssColor('action'),
  actionHover: cssColor('action-hover'),
  actionSubtle: cssColor('action-subtle'),
  actionSubtleBorder: cssColor('action-subtle-border'),
  positive: cssColor('positive'),
  positiveText: cssColor('positive-text'),
  positiveBg: cssColor('positive-bg'),
  positiveBorder: cssColor('positive-border'),
  negative: cssColor('negative'),
  negativeText: cssColor('negative-text'),
  negativeBg: cssColor('negative-bg'),
  negativeBorder: cssColor('negative-border'),
  caution: cssColor('caution'),
  cautionText: cssColor('caution-text'),
  cautionBg: cssColor('caution-bg'),
  cautionBorder: cssColor('caution-border'),
  alt: cssColor('alt'),
  altText: cssColor('alt-text'),
  altBg: cssColor('alt-bg'),
  altBorder: cssColor('alt-border'),
  baseline: cssColor('baseline'),
  current: cssColor('current'),
  inverseBg: cssColor('inverse-bg'),
  inverseText: cssColor('inverse-text'),
} as const;

/** Scrims: under side panels, and under modal dialogs and the palette. */
export const OVERLAY = {
  scrim: 'rgba(16, 24, 40, 0.18)',
  modal: 'rgba(16, 24, 40, 0.28)',
} as const;

// ---------------------------------------------------------------------------
// Type: system sans for everything, system mono for identifiers. A dense 13px
// base; hierarchy from weight (400 → 500 → 600 → 650) and color before size.
// ---------------------------------------------------------------------------

export const FONT = {
  sans: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif',
  mono: 'ui-monospace, SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace',
} as const;

/** Font sizes in px. */
export const TEXT = {
  // Eyebrows (UPPERCASE), counts, keys, chart ticks.
  '2xs': 11,
  // Captions, sub-lines, pills, table heads, chips.
  xs: 12,
  // Body: the base size.
  sm: 13,
  // Card titles, callout titles, the brand.
  md: 14,
  // Section titles, verdicts, the palette input.
  lg: 15,
  // Panel titles.
  xl: 17,
  // Page titles, word KPIs.
  '2xl': 20,
  // KPI values.
  '3xl': 24,
} as const;

export const WEIGHT = {
  regular: 400,
  medium: 500,
  semibold: 600,
  // The signature heading weight.
  heading: 650,
  // Only for small numerals inside pills.
  bold: 700,
} as const;

/** Letter spacing: large text tightens, small uppercase labels widen. */
export const TRACKING = {
  '3xl': '-0.02em',
  '2xl': '-0.015em',
  xl: '-0.01em',
  md: '-0.005em',
  eyebrow: '0.06em',
} as const;

export const LEADING = {body: 1.45, title: 1.3} as const;

/** Identifiers (run ids, entity names, keys) and code. */
export const MONO_TEXT = {
  fontFamily: 'var(--font-mono)',
  fontSize: TEXT.xs,
} as const;

/** Uppercase labels above groups: nav sections, menu and palette groups. */
export const EYEBROW_TEXT = {
  fontSize: TEXT['2xs'],
  fontWeight: WEIGHT.semibold,
  letterSpacing: TRACKING.eyebrow,
  textTransform: 'uppercase',
  color: COLOR.textTertiary,
} as const;

/** Every number that lines up. */
export const TABULAR = {fontVariantNumeric: 'tabular-nums'} as const;

// ---------------------------------------------------------------------------
// Space, shape, elevation, motion
// ---------------------------------------------------------------------------

/**
 * Spacing scale in px: 2px steps up to 12, 4px steps after. In MUI `sx`
 * spacing props one unit is 8px, so `--space-N` is N / 2 units (space-3 = 1.5).
 */
export const SPACE = {
  '0.5': 2,
  '1': 4,
  '1.5': 6,
  '2': 8,
  '2.5': 10,
  '3': 12,
  '4': 16,
  '5': 20,
  '6': 24,
  '7': 28,
  '8': 32,
  '12': 48,
} as const;

/** Corner radii in px. Nested: inner radius = outer radius − padding. */
export const RADIUS = {
  // Keys, small chips, toggles.
  xs: 4,
  // Chips, rank pills, segments.
  sm: 6,
  // Buttons, inputs, nav items, menus, code.
  md: 8,
  // Cards, callouts, panels.
  lg: 10,
  // Command palette, dialogs.
  xl: 14,
  // Pills, counts, tracks.
  full: 999,
  // Older names.
  control: 8,
  card: 10,
  tag: 6,
  dialog: 14,
} as const;

/** A radius as a CSS length, for `sx` (where a bare number is a multiple). */
export function radius(r: keyof typeof RADIUS): string {
  return `${RADIUS[r]}px`;
}

/** Control heights in px. */
export const CONTROL = {xs: 20, sm: 26, md: 32, lg: 36} as const;

export const SHADOW_SM = '0 1px 2px rgba(16, 24, 40, 0.05)';
export const SHADOW_MD =
  '0 4px 8px -2px rgba(16, 24, 40, 0.08), 0 2px 4px -2px rgba(16, 24, 40, 0.04)';
export const SHADOW_LG =
  '0 20px 24px -4px rgba(16, 24, 40, 0.1), 0 8px 8px -4px rgba(16, 24, 40, 0.04)';
/** Resting cards and KPI tiles; hover lift; panels, menus, tooltips, toasts. */
export const SHADOW = {sm: SHADOW_SM, md: SHADOW_MD, lg: SHADOW_LG} as const;

/** Motion in ms: hover; carets, fades and scrims; panels and toasts. */
export const DURATION = {fast: 120, base: 150, panel: 200} as const;
export const EASE = 'ease';

export const Z = {
  sticky: 1,
  contextbar: 20,
  topbar: 30,
  scrim: 40,
  drawer: 41,
  tooltip: 60,
  menu: 65,
  palette: 70,
  toast: 80,
  progress: 90,
} as const;

/** The app shell: top bar, side navigation, page and side panel. */
export const SHELL = {
  topBar: 52,
  nav: 232,
  navRail: 64,
  pageMax: 1480,
  pageMaxWide: 1640,
  panel: 760,
  panelWide: 1180,
} as const;

/**
 * The 2px focus ring of everything interactive: offset 2px, or −2px inside
 * rows and cards.
 */
export function focusRing(offset = 2) {
  return {
    outline: `2px solid ${COLOR.action}`,
    outlineOffset: offset,
  } as const;
}

// ---------------------------------------------------------------------------
// Status tones. `solid` for icons and graphics, `text` for text on the surface
// or on `bg`, `bg` and `border` for pills and callouts.
// ---------------------------------------------------------------------------

export interface Tone {
  solid: string;
  text: string;
  bg: string;
  border: string;
}
export const TONES = {
  good: {
    solid: COLOR.positive,
    text: COLOR.positiveText,
    bg: COLOR.positiveBg,
    border: COLOR.positiveBorder,
  },
  bad: {
    solid: COLOR.negative,
    text: COLOR.negativeText,
    bg: COLOR.negativeBg,
    border: COLOR.negativeBorder,
  },
  warn: {
    solid: COLOR.caution,
    text: COLOR.cautionText,
    bg: COLOR.cautionBg,
    border: COLOR.cautionBorder,
  },
  info: {
    solid: COLOR.action,
    text: COLOR.actionHover,
    bg: COLOR.actionSubtle,
    border: COLOR.actionSubtleBorder,
  },
  violet: {
    solid: COLOR.alt,
    text: COLOR.altText,
    bg: COLOR.altBg,
    border: COLOR.altBorder,
  },
  neutral: {
    solid: COLOR.textTertiary,
    text: COLOR.textSecondary,
    bg: COLOR.bgSubtle,
    border: COLOR.borderDefault,
  },
} as const satisfies Record<string, Tone>;
export type ToneName = keyof typeof TONES;

/** KPI card status edge (component token). */
export const KPI_EDGE = {
  good: COLOR.positive,
  warn: COLOR.caution,
  bad: COLOR.negative,
} as const;

// ---------------------------------------------------------------------------
// Data visualization: categorical palette assigned by rank (largest first),
// the last slot always "other"; never for status.
// ---------------------------------------------------------------------------

export const CHART_PALETTE = [
  cssColor('cat-1'),
  cssColor('cat-2'),
  cssColor('cat-3'),
  cssColor('cat-4'),
  cssColor('cat-5'),
  cssColor('cat-6'),
  cssColor('cat-7'),
  cssColor('cat-8'),
  cssColor('cat-other'),
] as const;

// Recharts draws its own SVG and tooltip, outside the MUI theme. Tooltips use
// the inverse surface; ticks are 11px tertiary, axis titles 11px / 600.
export const CHART_TOOLTIP_STYLE = {
  backgroundColor: COLOR.inverseBg,
  color: COLOR.inverseText,
  border: 0,
  borderRadius: RADIUS.md,
  boxShadow: SHADOW_LG,
  fontSize: TEXT.xs,
  lineHeight: LEADING.body,
  padding: '8px 10px',
} as const;
export const CHART_TOOLTIP_LABEL_STYLE = {
  color: COLOR.inverseText,
  fontWeight: WEIGHT.semibold,
  marginBottom: 2,
} as const;
export const CHART_TOOLTIP_ITEM_STYLE = {
  color: COLOR.inverseText,
  padding: 0,
} as const;
export const CHART_TICK_STYLE = {
  fontSize: TEXT['2xs'],
  fill: COLOR.textTertiary,
} as const;
export const CHART_AXIS_LABEL_STYLE = {
  fontSize: TEXT['2xs'],
  fontWeight: WEIGHT.semibold,
  fill: COLOR.textTertiary,
} as const;
// First gridline solid, the rest dashed 2 3.
export const CHART_GRID_STROKE = COLOR.borderDefault;
export const CHART_GRID_DASH = '2 3';
export const CHART_LINE_WIDTH = 1.8;
export const CHART_AXIS_STROKE = COLOR.borderStrong;

// ---------------------------------------------------------------------------
// Legacy alias layer: the names used before the design language, now reading
// the semantic tokens.
// ---------------------------------------------------------------------------

export const ACCENT = COLOR.action;
export const ACCENT_DARK = COLOR.actionHover;
export const ACCENT_50 = COLOR.actionSubtle;
export const ACCENT_100 = COLOR.actionSubtleBorder;
export const PAGE_BACKGROUND = COLOR.bgCanvas;
export const SURFACE = COLOR.bgSurface;
export const SURFACE_2 = COLOR.bgSurface2;
export const SURFACE_3 = COLOR.bgSubtle;
export const DIVIDER = COLOR.borderDefault;
export const BORDER_STRONG = COLOR.borderStrong;
export const TEXT_PRIMARY = COLOR.textPrimary;
export const TEXT_SECONDARY = COLOR.textSecondary;
export const TEXT_TERTIARY = COLOR.textTertiary;
// Decorative glyphs only; informative text uses TEXT_TERTIARY.
export const TEXT_DISABLED = COLOR.textDecorative;
// Small icons that are controls (help, pin, hide): 3:1 on every surface.
export const ICON_MUTED = COLOR.textDecorative;
// Text on action and rank colors.
export const ON_ACCENT = COLOR.inverseText;
// Right edge of a pinned table column: a hairline, not a shadow.
export const PINNED_SHADOW = `inset -1px 0 0 ${COLOR.borderDefault}`;
// Plain HTML tables.
export const LINE_COLOR = COLOR.borderDefault;
export const TOTAL_LINE_COLOR = COLOR.borderStrong;
export const ROW_HOVER_COLOR = COLOR.bgSurface2;
export const ROW_HOVER_OPAQUE = COLOR.bgSurface2;
export const MUTED_TEXT_COLOR = COLOR.textSecondary;
export const IMPROVING_COLOR = COLOR.positiveText;
export const WORSENING_COLOR = COLOR.negativeText;
export const CARD_SHADOW = SHADOW_SM;
