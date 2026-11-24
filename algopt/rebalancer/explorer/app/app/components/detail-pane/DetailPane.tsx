'use client';

import {useCallback, useEffect, type ReactNode} from 'react';

import {
  Box,
  Button,
  Drawer,
  IconButton,
  Tab,
  Tabs,
  Tooltip,
  Typography,
} from '@mui/material';
import {
  ArrowLeft,
  ChevronDown,
  ChevronUp,
  Maximize2,
  Minimize2,
  X,
} from 'lucide-react';

import ExpressionTreeView from '@/app/components/evaluation/ExpressionTreeView';
import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import {formatPercentChange} from '@/app/components/evaluation/evaluation-rows';
import SpecViewer from '@/app/components/evaluation/SpecViewer';
import CopyButton from '@/app/components/ui/CopyButton';
import Pill from '@/app/components/ui/Pill';
import Tag from '@/app/components/ui/Tag';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {setPref, usePref} from '@/lib/prefs';
import {
  ACCENT_50,
  COLOR,
  CONTROL,
  DURATION,
  radius,
  SHELL,
  TABULAR,
  TEXT,
  WEIGHT,
  Z,
} from '@/lib/ui-tokens';

import {
  InPaneContext,
  paneTitle,
  useDetailPane,
  type EntityPanePayload,
  type ExpressionPanePayload,
  type ExpressionPaneTab,
} from './DetailPaneContext';

const WIDE_PREF = 'rbx-panel-wide';
import EntityDetail from './EntityDetail';

/**
 * A mini KPI of the panel: a 12px label (with the side's dot for A and B)
 * over the value.
 */
function Value({
  label,
  side,
  children,
}: {
  label: string;
  side?: 'A' | 'B';
  children: ReactNode;
}) {
  return (
    <Box
      sx={{
        minWidth: 0,
        px: 1.5,
        py: 1.25,
        border: `1px solid ${COLOR.borderDefault}`,
        borderRadius: radius('lg'),
        bgcolor: COLOR.bgSurface,
      }}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 0.75,
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.medium,
        }}>
        {side != null && (
          <Box
            component="span"
            aria-hidden
            sx={{
              width: 8,
              height: 8,
              borderRadius: '50%',
              flexShrink: 0,
              bgcolor: side === 'A' ? COLOR.baseline : COLOR.current,
            }}
          />
        )}
        <Box
          component="span"
          sx={{
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
          }}>
          {label}
        </Box>
      </Box>
      <Box
        sx={{
          mt: 0.5,
          fontSize: TEXT.lg,
          fontWeight: WEIGHT.heading,
          lineHeight: 1.3,
          overflowWrap: 'anywhere',
          ...TABULAR,
        }}>
        {children}
      </Box>
    </Box>
  );
}

/** Panel header actions: step through the page's rows, widen, close. */
function HeaderActions({
  stepper,
  wide,
  onToggleWide,
  onClose,
}: {
  stepper?: ReactNode;
  wide: boolean;
  onToggleWide: () => void;
  onClose: () => void;
}) {
  return (
    <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5, flexShrink: 0}}>
      {stepper}
      <Tooltip title={wide ? 'Narrow the panel' : 'Widen the panel'}>
        <IconButton
          aria-label={wide ? 'Narrow the panel' : 'Widen the panel'}
          aria-pressed={wide}
          onClick={onToggleWide}>
          {wide ? <Minimize2 size={16} /> : <Maximize2 size={16} />}
        </IconButton>
      </Tooltip>
      <Tooltip title="Close (Esc)">
        <IconButton aria-label="Close details" onClick={onClose}>
          <X size={18} />
        </IconButton>
      </Tooltip>
    </Box>
  );
}

/** "← Back to …" when this panel was opened from a link in another. */
function BackLink({to, onBack}: {to: string | null; onBack: () => void}) {
  if (to == null) {
    return null;
  }
  return (
    <Box sx={{flexBasis: '100%', mt: -1, ml: -1, minWidth: 0}}>
      <Button
        size="small"
        variant="text"
        startIcon={<ArrowLeft size={14} />}
        onClick={onBack}
        sx={{
          maxWidth: '100%',
          color: COLOR.actionHover,
          '& .label': {overflow: 'hidden', textOverflow: 'ellipsis'},
        }}>
        <span className="label">Back to {to}</span>
      </Button>
    </Box>
  );
}

const PANE_ROW_SELECTOR = '[data-pane-row]';

/** Rows of the page that can open in the pane, in on-screen order. */
function paneRows(): HTMLElement[] {
  if (typeof document === 'undefined') {
    return [];
  }
  return Array.from(document.querySelectorAll<HTMLElement>(PANE_ROW_SELECTOR));
}

/**
 * Opens the row `offset` places from the expression shown, keeping the tab,
 * by pressing that row's own Spec or Tree control. Returns false at either end
 * of the list.
 */
export function stepPaneRow(
  expressionId: number,
  tab: ExpressionPaneTab,
  offset: number,
): boolean {
  const rows = paneRows();
  const i = rows.findIndex(r => r.dataset.paneRow === String(expressionId));
  const next = i < 0 ? null : rows[i + offset];
  if (next == null) {
    return false;
  }
  const control = next.querySelector<HTMLElement>(`[data-pane-open="${tab}"]`);
  if (control == null) {
    return false;
  }
  control.click();
  next.scrollIntoView({block: 'nearest'});
  return true;
}

/**
 * "2 of 7" with previous / next buttons: steps through the rows shown on the
 * page without closing the pane. It reads the rows when the pane shows a new
 * expression, so the count follows the page's current filters.
 */
function PaneStepper({
  expressionId,
  tab,
}: {
  expressionId: number;
  tab: ExpressionPaneTab;
}) {
  const rows = paneRows();
  const index = rows.findIndex(r => r.dataset.paneRow === String(expressionId));
  if (index < 0 || rows.length < 2) {
    return null;
  }
  return (
    <Box
      role="group"
      aria-label="Step through the rows on the page"
      sx={{
        display: 'inline-flex',
        alignItems: 'center',
        border: `1px solid ${COLOR.borderDefault}`,
        borderRadius: radius('md'),
        height: CONTROL.md,
        px: '1px',
        flexShrink: 0,
      }}>
      <Tooltip title="Previous row (k)">
        <span>
          <IconButton
            size="small"
            aria-label="Previous row"
            disabled={index === 0}
            onClick={() => stepPaneRow(expressionId, tab, -1)}>
            <ChevronUp size={16} />
          </IconButton>
        </span>
      </Tooltip>
      <Typography
        variant="caption"
        component="span"
        sx={{
          minWidth: 58,
          textAlign: 'center',
          fontVariantNumeric: 'tabular-nums',
          whiteSpace: 'nowrap',
          color: COLOR.textTertiary,
        }}>
        {`${index + 1} of ${rows.length}`}
      </Typography>
      <Tooltip title="Next row (j)">
        <span>
          <IconButton
            size="small"
            aria-label="Next row"
            disabled={index === rows.length - 1}
            onClick={() => stepPaneRow(expressionId, tab, 1)}>
            <ChevronDown size={16} />
          </IconButton>
        </span>
      </Tooltip>
    </Box>
  );
}

function ExpressionPane({
  payload,
  tab,
  onTabChange,
  onClose,
  backTo,
  onBack,
  wide,
  onToggleWide,
}: {
  payload: ExpressionPanePayload;
  tab: ExpressionPaneTab;
  onTabChange: (tab: ExpressionPaneTab) => void;
  onClose: () => void;
  backTo: string | null;
  onBack: () => void;
  wide: boolean;
  onToggleWide: () => void;
}) {
  const aLabel = payload.srcLabel != null ? `A · ${payload.srcLabel}` : 'A';
  const bLabel = payload.dstLabel != null ? `B · ${payload.dstLabel}` : 'B';
  const isConstraint = payload.expressionType === 'CONSTRAINT';
  const hasPenalty = payload.srcPenalty != null || payload.dstPenalty != null;
  const treeKey = [
    payload.expressionId,
    JSON.stringify(payload.sourceAssignment),
    JSON.stringify(payload.destinationAssignment),
  ].join('|');

  return (
    <Box sx={{display: 'flex', flexDirection: 'column', height: '100%'}}>
      <Box
        sx={{
          px: 2.5,
          pt: 2,
          pb: 1.5,
          borderBottom: `1px solid ${COLOR.borderDefault}`,
        }}>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'flex-start',
            flexWrap: 'wrap',
            columnGap: 1.5,
            rowGap: 0.5,
          }}>
          <BackLink to={backTo} onBack={onBack} />
          <Box sx={{flex: 1, minWidth: 0}}>
            {/* Identity pills: kind and rank. */}
            <Box
              sx={{
                display: 'flex',
                alignItems: 'center',
                gap: 0.75,
                mb: 0.5,
                flexWrap: 'wrap',
              }}>
              {payload.tupleIndex != null &&
                (payload.maxTupleIndex != null && payload.maxTupleIndex > 0 ? (
                  <TupleBadge
                    tupleIndex={payload.tupleIndex}
                    maxTupleIndex={payload.maxTupleIndex}
                  />
                ) : (
                  <Pill>{`tuple ${payload.tupleIndex}`}</Pill>
                ))}
              <Pill tone={isConstraint ? 'warn' : 'info'}>
                {payload.expressionType}
              </Pill>
            </Box>
            <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5}}>
              <Typography
                variant="h2"
                component="h2"
                sx={{overflowWrap: 'anywhere'}}>
                {payload.name}
              </Typography>
              <CopyButton value={payload.name} label="Copy name" />
            </Box>
            {payload.description !== '' && (
              <Typography
                variant="body2"
                sx={{
                  mt: 0.25,
                  color: COLOR.textTertiary,
                  overflowWrap: 'anywhere',
                }}>
                {payload.description}
              </Typography>
            )}
          </Box>
          <HeaderActions
            stepper={
              <PaneStepper expressionId={payload.expressionId} tab={tab} />
            }
            wide={wide}
            onToggleWide={onToggleWide}
            onClose={onClose}
          />
        </Box>
        <Box
          sx={{
            display: 'grid',
            gridTemplateColumns: hasPenalty
              ? 'repeat(3, minmax(0, 1fr))'
              : 'repeat(4, minmax(0, 1fr))',
            gap: 1,
            mt: 1.5,
            '@media (max-width: 760px)': {
              gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
            },
          }}>
          <Value label={aLabel} side="A">
            <PreciseNumber value={payload.srcValue} highlight={isConstraint} />
          </Value>
          <Value label={bLabel} side="B">
            <PreciseNumber value={payload.dstValue} highlight={isConstraint} />
          </Value>
          <Value
            label={`B − A (${formatPercentChange(payload.srcValue, payload.dstValue, payload.percentage).replace(/-/g, '\u2212')})`}>
            <PreciseNumber
              value={payload.delta}
              highlight={!isConstraint}
              signed
            />
          </Value>
          <Value label="Direction">
            <Box
              component="span"
              sx={{fontSize: TEXT.sm, fontWeight: WEIGHT.semibold}}>
              Lower is better
            </Box>
          </Value>
          {hasPenalty && (
            <>
              <Value label="Penalty A" side="A">
                <PreciseNumber
                  value={payload.srcPenalty ?? 0}
                  highlight={false}
                />
              </Value>
              <Value label="Penalty B" side="B">
                <PreciseNumber
                  value={payload.dstPenalty ?? 0}
                  highlight={false}
                />
              </Value>
            </>
          )}
        </Box>
      </Box>
      <Tabs
        value={tab}
        onChange={(_e, value: ExpressionPaneTab) => onTabChange(value)}
        sx={{px: 1.5}}>
        <Tab value="spec" label="Spec" />
        <Tab value="tree" label="Tree" />
      </Tabs>
      <Box sx={{flex: 1, overflow: 'auto', px: 2.5, pt: 2, pb: 5}}>
        {tab === 'spec' ? (
          <SpecViewer
            key={payload.name}
            name={payload.name}
            handle={payload.handle}
            expressionType={payload.expressionType}
          />
        ) : (
          <ExpressionTreeView
            key={treeKey}
            expressionId={payload.expressionId}
            handle={payload.handle}
            sourceAssignment={payload.sourceAssignment}
            destinationAssignment={payload.destinationAssignment}
          />
        )}
      </Box>
    </Box>
  );
}

function EntityPane({
  payload,
  onClose,
  backTo,
  onBack,
  wide,
  onToggleWide,
}: {
  payload: EntityPanePayload;
  onClose: () => void;
  backTo: string | null;
  onBack: () => void;
  wide: boolean;
  onToggleWide: () => void;
}) {
  return (
    <Box sx={{display: 'flex', flexDirection: 'column', height: '100%'}}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'flex-start',
          flexWrap: 'wrap',
          columnGap: 1.5,
          rowGap: 0.5,
          px: 2.5,
          pt: 2,
          pb: 1.5,
          borderBottom: `1px solid ${COLOR.borderDefault}`,
        }}>
        <BackLink to={backTo} onBack={onBack} />
        <Box sx={{flex: 1, minWidth: 0}}>
          <Box sx={{mb: 0.5}}>
            <Tag>{payload.entity}</Tag>
          </Box>
          <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5}}>
            <Typography
              component="h2"
              variant="h2"
              sx={{
                fontFamily: 'var(--font-mono)',
                fontSize: TEXT.lg,
                overflowWrap: 'anywhere',
              }}>
              {payload.id}
            </Typography>
            <CopyButton value={payload.id} label="Copy name" />
          </Box>
        </Box>
        <HeaderActions
          wide={wide}
          onToggleWide={onToggleWide}
          onClose={onClose}
        />
      </Box>
      <Box sx={{flex: 1, overflow: 'auto', px: 2.5, pt: 2, pb: 5}}>
        <EntityDetail
          key={`${payload.entity}|${payload.id}`}
          handle={payload.handle}
          runId={payload.runId}
          entity={payload.entity}
          id={payload.id}
        />
      </Box>
    </Box>
  );
}

/**
 * The run's side panel: all detail opens in it, over an 18% scrim. It takes
 * focus, Esc closes it and focus returns; it steps through the page's rows
 * (j / k), widens (remembered), and stacks with "Back to …".
 */
export default function DetailPane() {
  const {content, stack, close, back, setTab} = useDetailPane();
  const isOpen = content != null;
  const wide = usePref(WIDE_PREF, 'false') === 'true';
  const toggleWide = useCallback(
    () => setPref(WIDE_PREF, String(!wide)),
    [wide],
  );
  const backTo = stack.length > 0 ? paneTitle(stack[stack.length - 1]) : null;

  // j / k step to the next / previous row while an expression is open.
  const expression = content?.kind === 'expression' ? content : null;
  const stepRow = useCallback(
    (offset: number) => {
      if (expression != null) {
        stepPaneRow(expression.payload.expressionId, expression.tab, offset);
      }
    },
    [expression],
  );

  useEffect(() => {
    if (!isOpen) {
      return;
    }
    const onKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement | null;
      const typing =
        target != null &&
        (target.isContentEditable ||
          ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName));
      if (
        typing ||
        e.metaKey ||
        e.ctrlKey ||
        e.altKey ||
        (e.key !== 'j' && e.key !== 'k')
      ) {
        return;
      }
      e.preventDefault();
      stepRow(e.key === 'j' ? 1 : -1);
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [isOpen, stepRow]);

  return (
    <Drawer
      anchor="right"
      open={isOpen}
      onClose={close}
      sx={{zIndex: Z.drawer}}
      transitionDuration={DURATION.panel}
      PaperProps={{
        component: 'aside',
        'aria-label': 'Details',
        sx: {
          width: wide
            ? `min(${SHELL.panelWide}px, 96vw)`
            : `min(${SHELL.panel}px, 94vw)`,
          transition: `width ${DURATION.panel}ms ease`,
          '@media (max-width: 760px)': {width: '100vw'},
        },
      }}>
      <InPaneContext.Provider value={true}>
        {expression != null && (
          // The row shown in the pane stays highlighted in its table.
          <style>{`[data-pane-row="${expression.payload.expressionId}"] > td { background-color: ${ACCENT_50}; }`}</style>
        )}
        {content?.kind === 'expression' && (
          <ExpressionPane
            payload={content.payload}
            tab={content.tab}
            onTabChange={setTab}
            onClose={close}
            backTo={backTo}
            onBack={back}
            wide={wide}
            onToggleWide={toggleWide}
          />
        )}
        {content?.kind === 'entity' && (
          <EntityPane
            payload={content.payload}
            onClose={close}
            backTo={backTo}
            onBack={back}
            wide={wide}
            onToggleWide={toggleWide}
          />
        )}
      </InPaneContext.Provider>
    </Drawer>
  );
}
