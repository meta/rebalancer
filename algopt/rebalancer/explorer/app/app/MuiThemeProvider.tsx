'use client';

import {createTheme, ThemeProvider} from '@mui/material/styles';

import Toaster from '@/app/components/ui/Toaster';

import {
  COLOR,
  CONTROL,
  DURATION,
  EYEBROW_TEXT,
  focusRing,
  LEADING,
  OVERLAY,
  radius,
  SEMANTIC_COLORS,
  SHADOW_LG,
  SHADOW_SM,
  TEXT,
  type ThemeMode,
  type Tone,
  TONES,
  TRACKING,
  WEIGHT,
} from '@/lib/ui-tokens';

// The MUI palette needs real colors (it derives channels and contrast from
// them), so it takes the semantic tokens' values per theme. Component
// overrides below use the `var(--color-*)` tokens directly, which follow the
// theme on their own.
function palette(mode: ThemeMode) {
  const c = (name: keyof typeof SEMANTIC_COLORS) => SEMANTIC_COLORS[name][mode];
  return {
    primary: {
      main: c('action'),
      dark: c('action-hover'),
      light: c('action-subtle'),
      contrastText: c('inverse-text'),
    },
    secondary: {
      main: c('alt'),
      dark: c('alt-text'),
      light: c('alt-bg'),
      contrastText: c('inverse-text'),
    },
    // `main` is also used for text (Typography color="error"), so it is the
    // tone's text color.
    success: {
      main: c('positive-text'),
      dark: c('positive-text'),
      light: c('positive-bg'),
    },
    error: {
      main: c('negative-text'),
      dark: c('negative-text'),
      light: c('negative-bg'),
    },
    warning: {
      main: c('caution-text'),
      dark: c('caution-text'),
      light: c('caution-bg'),
    },
    info: {
      main: c('action'),
      dark: c('action-hover'),
      light: c('action-subtle'),
    },
    background: {default: c('bg-canvas'), paper: c('bg-surface')},
    text: {
      primary: c('text-primary'),
      secondary: c('text-secondary'),
      disabled: c('text-decorative'),
    },
    divider: c('border-default'),
    action: {
      active: c('text-secondary'),
      hover: c('bg-subtle'),
      selected: c('action-subtle'),
    },
  };
}

function toneStyle(tone: Tone) {
  return {
    backgroundColor: tone.bg,
    color: COLOR.textSecondary,
    border: `1px solid ${tone.border}`,
  };
}

const FOCUS = {'&.Mui-focusVisible': focusRing()};

const theme = createTheme({
  cssVariables: {colorSchemeSelector: 'data-theme'},
  colorSchemes: {
    light: {palette: palette('light')},
    dark: {palette: palette('dark')},
  },
  shape: {borderRadius: 8},
  typography: {
    fontFamily: 'var(--font-sans)',
    fontSize: TEXT.sm,
    // Roles: h1 page title, h2 panel title, h3 section title, h4 KPI value,
    // h5 page title (landing), h6 and subtitle1 card title, subtitle2 strong
    // body, body1/body2 body, caption sub-line, overline eyebrow.
    h1: {
      fontSize: TEXT['2xl'],
      fontWeight: WEIGHT.heading,
      letterSpacing: TRACKING['2xl'],
      lineHeight: 1.4,
    },
    h2: {
      fontSize: TEXT.xl,
      fontWeight: WEIGHT.semibold,
      letterSpacing: TRACKING.xl,
      lineHeight: LEADING.title,
    },
    h3: {
      fontSize: TEXT.lg,
      fontWeight: WEIGHT.heading,
      lineHeight: LEADING.title,
    },
    h4: {
      fontSize: TEXT['3xl'],
      fontWeight: WEIGHT.heading,
      letterSpacing: TRACKING['3xl'],
      lineHeight: LEADING.title,
      fontVariantNumeric: 'tabular-nums',
    },
    h5: {
      fontSize: TEXT['2xl'],
      fontWeight: WEIGHT.heading,
      letterSpacing: TRACKING['2xl'],
      lineHeight: 1.4,
    },
    h6: {
      fontSize: TEXT.md,
      fontWeight: WEIGHT.semibold,
      letterSpacing: TRACKING.md,
      lineHeight: LEADING.title,
    },
    subtitle1: {
      fontSize: TEXT.md,
      fontWeight: WEIGHT.semibold,
      letterSpacing: TRACKING.md,
      lineHeight: LEADING.title,
    },
    subtitle2: {
      fontSize: TEXT.sm,
      fontWeight: WEIGHT.semibold,
      lineHeight: LEADING.body,
    },
    body1: {fontSize: TEXT.sm, lineHeight: LEADING.body},
    body2: {fontSize: TEXT.sm, lineHeight: LEADING.body},
    caption: {
      fontSize: TEXT.xs,
      lineHeight: LEADING.body,
      color: COLOR.textTertiary,
    },
    overline: {...EYEBROW_TEXT, lineHeight: LEADING.body},
    button: {
      fontSize: TEXT.sm,
      fontWeight: WEIGHT.semibold,
      textTransform: 'none',
      lineHeight: 1.2,
    },
  },
  components: {
    // No ripple: hover and the focus ring are the feedback.
    MuiButtonBase: {
      defaultProps: {disableRipple: true},
      styleOverrides: {root: FOCUS},
    },
    MuiButton: {
      defaultProps: {disableElevation: true},
      styleOverrides: {
        root: {
          minHeight: CONTROL.md,
          padding: '0 12px',
          borderRadius: radius('md'),
          whiteSpace: 'nowrap',
          gap: 6,
          transition: `background-color ${DURATION.fast}ms, border-color ${DURATION.fast}ms`,
          '&.Mui-disabled': {opacity: 0.55, cursor: 'not-allowed'},
          '& .MuiButton-startIcon, & .MuiButton-endIcon': {margin: 0},
        },
        sizeSmall: {
          minHeight: CONTROL.sm,
          padding: '0 8px',
          fontSize: TEXT.xs,
        },
        // Primary: one per view.
        contained: {
          backgroundColor: COLOR.action,
          border: `1px solid ${COLOR.action}`,
          color: COLOR.inverseText,
          '&:hover': {
            backgroundColor: COLOR.actionHover,
            borderColor: COLOR.actionHover,
          },
          '&.Mui-disabled': {
            backgroundColor: COLOR.action,
            color: COLOR.inverseText,
          },
        },
        // Default.
        outlined: {
          backgroundColor: COLOR.bgSurface,
          borderColor: COLOR.borderStrong,
          color: COLOR.textPrimary,
          '&:hover': {
            backgroundColor: COLOR.bgSubtle,
            borderColor: COLOR.borderStrong,
          },
          '&.Mui-disabled': {
            borderColor: COLOR.borderStrong,
            color: COLOR.textPrimary,
          },
        },
        // Ghost.
        text: {
          border: '1px solid transparent',
          color: COLOR.textSecondary,
          '&:hover': {backgroundColor: COLOR.bgSubtle},
          '&.Mui-disabled': {color: COLOR.textSecondary},
        },
      },
    },
    MuiIconButton: {
      styleOverrides: {
        root: {
          width: CONTROL.md,
          height: CONTROL.md,
          padding: 0,
          borderRadius: radius('md'),
          border: '1px solid transparent',
          color: COLOR.textSecondary,
          transition: `background-color ${DURATION.fast}ms`,
          '&:hover': {backgroundColor: COLOR.bgSubtle},
          '&.Mui-disabled': {opacity: 0.35, color: COLOR.textSecondary},
        },
        sizeSmall: {width: CONTROL.sm, height: CONTROL.sm},
      },
    },
    MuiPaper: {
      styleOverrides: {
        // No dark-mode elevation tint: surfaces are tokens.
        root: {backgroundImage: 'none'},
        outlined: {
          borderRadius: radius('lg'),
          borderColor: COLOR.borderDefault,
          boxShadow: SHADOW_SM,
        },
        // Raised surfaces (menus, popovers, dialogs, panels).
        elevation: {boxShadow: SHADOW_LG},
        elevation0: {boxShadow: 'none'},
      },
    },
    MuiCard: {
      defaultProps: {variant: 'outlined'},
    },
    MuiCardHeader: {
      styleOverrides: {
        root: {padding: '12px 16px 10px'},
        title: {
          fontSize: TEXT.md,
          fontWeight: WEIGHT.semibold,
          letterSpacing: TRACKING.md,
        },
        subheader: {fontSize: TEXT.xs, color: COLOR.textTertiary},
      },
    },
    MuiCardContent: {
      styleOverrides: {
        root: {padding: '0 16px 16px', '&:last-child': {paddingBottom: 16}},
      },
    },
    MuiBackdrop: {
      styleOverrides: {
        root: {
          '&:not(.MuiBackdrop-invisible)': {backgroundColor: OVERLAY.scrim},
        },
      },
    },
    MuiDialog: {
      styleOverrides: {
        root: {
          '& .MuiBackdrop-root:not(.MuiBackdrop-invisible)': {
            backgroundColor: OVERLAY.modal,
          },
        },
        paper: {
          borderRadius: radius('xl'),
          border: `1px solid ${COLOR.borderDefault}`,
          boxShadow: SHADOW_LG,
        },
        paperFullScreen: {borderRadius: 0},
      },
    },
    MuiDialogTitle: {
      styleOverrides: {
        root: {
          fontSize: TEXT.lg,
          fontWeight: WEIGHT.heading,
          lineHeight: LEADING.title,
          padding: '16px 20px 8px',
        },
      },
    },
    MuiDialogContent: {
      styleOverrides: {root: {padding: '8px 20px 20px'}},
    },
    MuiDialogActions: {
      styleOverrides: {
        root: {
          padding: '12px 20px',
          gap: 8,
          borderTop: `1px solid ${COLOR.borderDefault}`,
        },
      },
    },
    MuiDrawer: {
      styleOverrides: {
        paper: {
          backgroundColor: COLOR.bgSurface,
          borderLeft: `1px solid ${COLOR.borderDefault}`,
          boxShadow: SHADOW_LG,
        },
      },
    },
    MuiMenu: {
      styleOverrides: {
        paper: {
          borderRadius: radius('md'),
          border: `1px solid ${COLOR.borderDefault}`,
          boxShadow: SHADOW_LG,
        },
        list: {padding: 6},
      },
    },
    MuiMenuItem: {
      styleOverrides: {
        root: {
          minHeight: CONTROL.md,
          padding: '6px 10px',
          borderRadius: radius('sm'),
          fontSize: TEXT.sm,
          gap: 10,
          '&:hover': {backgroundColor: COLOR.bgSubtle},
          '&.Mui-selected, &.Mui-selected:hover': {
            backgroundColor: COLOR.actionSubtle,
          },
          '&.Mui-focusVisible': {
            backgroundColor: COLOR.bgSubtle,
            ...focusRing(-2),
          },
        },
      },
    },
    MuiListSubheader: {
      styleOverrides: {
        root: {
          ...EYEBROW_TEXT,
          lineHeight: LEADING.body,
          padding: '8px 10px 4px',
          backgroundColor: 'transparent',
        },
      },
    },
    MuiListItemIcon: {
      styleOverrides: {root: {minWidth: 0, color: COLOR.textSecondary}},
    },
    MuiPopover: {
      styleOverrides: {
        paper: {
          borderRadius: radius('md'),
          border: `1px solid ${COLOR.borderDefault}`,
          boxShadow: SHADOW_LG,
        },
      },
    },
    MuiAutocomplete: {
      styleOverrides: {
        paper: {
          borderRadius: radius('md'),
          border: `1px solid ${COLOR.borderDefault}`,
          boxShadow: SHADOW_LG,
        },
        listbox: {padding: 6},
        option: {
          minHeight: CONTROL.md,
          fontSize: TEXT.sm,
          borderRadius: radius('sm'),
          '&.Mui-focused': {backgroundColor: COLOR.bgSubtle},
          '&[aria-selected="true"], &[aria-selected="true"].Mui-focused': {
            backgroundColor: COLOR.actionSubtle,
          },
        },
        groupLabel: {...EYEBROW_TEXT, lineHeight: '28px'},
        noOptions: {fontSize: TEXT.sm, color: COLOR.textTertiary},
        tag: {margin: 2},
        inputRoot: {
          '&.MuiOutlinedInput-root': {padding: '3px 6px'},
          '&.MuiOutlinedInput-root .MuiAutocomplete-input': {
            padding: '3px 4px',
          },
        },
      },
    },
    MuiOutlinedInput: {
      styleOverrides: {
        root: {
          borderRadius: radius('md'),
          backgroundColor: COLOR.bgSurface,
          fontSize: TEXT.sm,
          '&:hover:not(.Mui-focused):not(.Mui-disabled) .MuiOutlinedInput-notchedOutline':
            {borderColor: COLOR.borderStrong},
          '&.Mui-focused': focusRing(1),
          '&.Mui-focused .MuiOutlinedInput-notchedOutline': {
            borderColor: COLOR.action,
            borderWidth: 1,
          },
        },
        input: {padding: '6px 10px', height: 20, lineHeight: '20px'},
        inputSizeSmall: {padding: '6px 10px'},
        notchedOutline: {
          borderColor: COLOR.borderStrong,
          '& legend': {maxWidth: '0.01px !important'},
        },
        adornedStart: {paddingLeft: 10},
        inputAdornedStart: {paddingLeft: 0},
      },
    },
    // The standard variant (the filter editor's selects) looks like the
    // outlined field: 32px, 1px strong border, 8px radius, no underline.
    MuiInput: {
      styleOverrides: {
        root: {
          minHeight: CONTROL.md,
          paddingLeft: 10,
          paddingRight: 4,
          border: `1px solid ${COLOR.borderStrong}`,
          borderRadius: radius('md'),
          backgroundColor: COLOR.bgSurface,
          fontSize: TEXT.sm,
          'label + &': {marginTop: 0},
          '&::before, &::after': {display: 'none'},
          '&.Mui-focused': {borderColor: COLOR.action, ...focusRing(1)},
        },
        input: {paddingTop: 6, paddingBottom: 6},
      },
    },
    MuiInputBase: {
      styleOverrides: {
        root: {fontSize: TEXT.sm},
        input: {
          '&::placeholder': {color: COLOR.textTertiary, opacity: 1},
          'label[data-shrink=false] + .MuiInputBase-formControl &::placeholder':
            {opacity: '1 !important'},
        },
      },
    },
    // Labels sit above the field (12px / 600, secondary), never inside it or
    // in a notch; the field below shows its placeholder.
    MuiInputLabel: {
      styleOverrides: {
        root: {
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.semibold,
          color: COLOR.textSecondary,
          '&.Mui-focused': {color: COLOR.textSecondary},
        },
        outlined: {
          transform: 'translate(0, -20px) scale(1)',
          '&.MuiInputLabel-shrink': {transform: 'translate(0, -20px) scale(1)'},
        },
      },
    },
    MuiFormControl: {
      styleOverrides: {
        root: {'&:has(> .MuiInputLabel-outlined)': {marginTop: 20}},
      },
    },
    MuiInputAdornment: {
      styleOverrides: {root: {color: COLOR.textTertiary}},
    },
    MuiFormControlLabel: {
      styleOverrides: {
        root: {marginLeft: -4, gap: 4},
        label: {fontSize: TEXT.sm, color: COLOR.textSecondary},
      },
    },
    MuiCheckbox: {
      defaultProps: {size: 'small'},
      styleOverrides: {
        root: {
          padding: 4,
          color: COLOR.borderStrong,
          '&.Mui-checked, &.MuiCheckbox-indeterminate': {color: COLOR.action},
        },
      },
    },
    MuiRadio: {
      defaultProps: {size: 'small'},
      styleOverrides: {
        root: {
          padding: 4,
          color: COLOR.borderStrong,
          '&.Mui-checked': {color: COLOR.action},
        },
      },
    },
    MuiTooltip: {
      defaultProps: {enterDelay: 300},
      styleOverrides: {
        tooltip: {
          backgroundColor: COLOR.inverseBg,
          color: COLOR.inverseText,
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.regular,
          lineHeight: LEADING.body,
          borderRadius: radius('md'),
          padding: '8px 10px',
          maxWidth: 380,
          boxShadow: SHADOW_LG,
        },
        arrow: {color: COLOR.inverseBg},
      },
    },
    MuiSnackbarContent: {
      styleOverrides: {
        root: {
          backgroundColor: COLOR.inverseBg,
          color: COLOR.inverseText,
          borderRadius: radius('md'),
          fontSize: TEXT.sm,
          fontWeight: WEIGHT.medium,
          padding: '4px 12px',
          boxShadow: SHADOW_LG,
        },
      },
    },
    // Segmented control: subtle track with 2px padding; the active segment
    // lifts to the surface.
    MuiToggleButtonGroup: {
      styleOverrides: {
        root: {
          backgroundColor: COLOR.bgSubtle,
          border: `1px solid ${COLOR.borderDefault}`,
          borderRadius: radius('md'),
          padding: 2,
          gap: 2,
        },
        grouped: {
          border: 0,
          // Same corners on every segment, overriding MUI's first/last rules.
          '&, &.MuiToggleButtonGroup-firstButton, &.MuiToggleButtonGroup-middleButton, &.MuiToggleButtonGroup-lastButton':
            {borderRadius: radius('sm'), margin: 0},
        },
      },
    },
    MuiToggleButton: {
      styleOverrides: {
        root: {
          height: CONTROL.sm,
          padding: '0 10px',
          border: 0,
          textTransform: 'none',
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.semibold,
          lineHeight: 1,
          whiteSpace: 'nowrap',
          color: COLOR.textSecondary,
          '&:hover': {backgroundColor: 'transparent', color: COLOR.textPrimary},
          '&.Mui-selected, &.Mui-selected:hover': {
            backgroundColor: COLOR.bgSurface,
            color: COLOR.textPrimary,
            boxShadow: SHADOW_SM,
          },
          '&.Mui-disabled': {border: 0, opacity: 0.55},
        },
      },
    },
    // Tabs: views of one thing. Active is action-hover text over a 2px action
    // underline.
    MuiTabs: {
      styleOverrides: {
        root: {minHeight: 40, borderBottom: `1px solid ${COLOR.borderDefault}`},
        flexContainer: {gap: 4},
        indicator: {height: 2, backgroundColor: COLOR.action},
      },
    },
    MuiTab: {
      styleOverrides: {
        root: {
          minHeight: 40,
          minWidth: 0,
          padding: '10px 12px',
          textTransform: 'none',
          fontSize: TEXT.sm,
          fontWeight: WEIGHT.semibold,
          color: COLOR.textTertiary,
          '&:hover': {color: COLOR.textPrimary},
          '&.Mui-selected': {color: COLOR.actionHover},
        },
      },
    },
    // Callouts: info, success, warning and error tints with a matching border.
    MuiAlert: {
      styleOverrides: {
        root: {
          borderRadius: radius('lg'),
          padding: '12px 16px',
          gap: 12,
          fontSize: TEXT.sm,
          lineHeight: LEADING.body,
          alignItems: 'flex-start',
        },
        icon: {padding: 0, margin: '1px 0 0', opacity: 1, fontSize: 16},
        message: {padding: 0, minWidth: 0, overflowWrap: 'anywhere'},
        action: {padding: 0, margin: '-4px -4px -4px auto'},
      },
      variants: [
        {
          props: {variant: 'standard', severity: 'success'},
          style: {
            ...toneStyle(TONES.good),
            '& .MuiAlert-icon': {color: COLOR.positive},
          },
        },
        {
          props: {variant: 'standard', severity: 'error'},
          style: {
            ...toneStyle(TONES.bad),
            '& .MuiAlert-icon': {color: COLOR.negative},
          },
        },
        {
          props: {variant: 'standard', severity: 'warning'},
          style: {
            ...toneStyle(TONES.warn),
            '& .MuiAlert-icon': {color: COLOR.caution},
          },
        },
        {
          props: {variant: 'standard', severity: 'info'},
          style: {
            ...toneStyle(TONES.info),
            '& .MuiAlert-icon': {color: COLOR.textPrimary},
          },
        },
      ],
    },
    MuiAlertTitle: {
      styleOverrides: {
        root: {
          margin: '0 0 2px',
          fontSize: TEXT.md,
          fontWeight: WEIGHT.semibold,
          lineHeight: LEADING.title,
          color: COLOR.textPrimary,
        },
      },
    },
    MuiLink: {
      defaultProps: {underline: 'hover'},
      styleOverrides: {root: {color: COLOR.action}},
    },
    // Chips say facts: 20px, radius 6, subtle.
    MuiChip: {
      defaultProps: {size: 'small'},
      styleOverrides: {
        root: {
          height: CONTROL.xs,
          borderRadius: radius('sm'),
          border: `1px solid ${COLOR.borderDefault}`,
          backgroundColor: COLOR.bgSubtle,
          color: COLOR.textSecondary,
          fontSize: TEXT.xs,
        },
        label: {padding: '0 6px'},
        deleteIcon: {
          fontSize: 14,
          margin: '0 4px 0 -2px',
          color: COLOR.textDecorative,
          '&:hover': {color: COLOR.textPrimary},
        },
      },
    },
    MuiTableCell: {
      styleOverrides: {
        root: {
          fontSize: TEXT.sm,
          padding: '8px 12px',
          borderBottom: `1px solid ${COLOR.borderDefault}`,
        },
        head: {
          backgroundColor: COLOR.bgSurface2,
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.semibold,
        },
      },
    },
    MuiDivider: {
      styleOverrides: {root: {borderColor: COLOR.borderDefault}},
    },
    MuiSkeleton: {
      styleOverrides: {root: {backgroundColor: COLOR.bgSubtle}},
    },
    MuiCircularProgress: {
      defaultProps: {thickness: 5},
    },
    // Row ranges with thousands separators: "1–25 of 5,225".
    MuiTablePagination: {
      defaultProps: {
        labelDisplayedRows: ({from, to, count}) =>
          `${from.toLocaleString('en-US')}–${to.toLocaleString('en-US')} of ${
            count === -1
              ? `more than ${to.toLocaleString('en-US')}`
              : count.toLocaleString('en-US')
          }`,
      },
    },
    MuiAvatar: {
      styleOverrides: {
        root: {
          width: 28,
          height: 28,
          fontSize: TEXT['2xs'],
          fontWeight: WEIGHT.bold,
          backgroundColor: COLOR.actionSubtleBorder,
          color: COLOR.actionHover,
        },
      },
    },
  },
});

export function MuiThemeProvider({children}: {children: React.ReactNode}) {
  return (
    <ThemeProvider theme={theme} defaultMode="light">
      {children}
      <Toaster />
    </ThemeProvider>
  );
}
