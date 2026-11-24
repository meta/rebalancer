'use client';

import {useMemo} from 'react';

import {Box, Typography} from '@mui/material';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';

import type {LocalSearchProfile} from '@/lib/rebalancer-explorer-types';
import ChartLegend from '@/app/components/ui/ChartLegend';
import {
  CHART_AXIS_LABEL_STYLE,
  CHART_AXIS_STROKE,
  CHART_GRID_DASH,
  CHART_GRID_STROKE,
  CHART_LINE_WIDTH,
  CHART_TOOLTIP_ITEM_STYLE,
  CHART_TOOLTIP_LABEL_STYLE,
  CHART_PALETTE,
  CHART_TICK_STYLE,
  CHART_TOOLTIP_STYLE,
  COLOR,
} from '@/lib/ui-tokens';

import {EmptyState} from '@/app/components/ui/StateViews';

interface LineRow {
  time: number;
  [moveTypeName: string]: number | null;
}

interface PieDatum {
  name: string;
  value: number;
}

/**
 * Build the line-chart dataset by walking events in order.
 *
 * Mirrors the legacy Plotly trace builder: when the move type changes between
 * consecutive events we inject a point at `(t, initialValue)` for the new
 * trace so the series for that move type begins at the transition point
 * rather than at the end of its first event. With `connectNulls={false}`,
 * recharts then draws gaps wherever a series has no value.
 */
function buildObjectiveOverTime(profile: LocalSearchProfile): LineRow[] {
  const {moveTypeNames, moveTypeEvents} = profile;
  const rows: LineRow[] = [];
  let totalDuration = 0;
  let lastIndex = -1;

  for (const event of moveTypeEvents) {
    const name =
      moveTypeNames[event.moveTypeIndex] ?? `unknown_${event.moveTypeIndex}`;
    if (event.moveTypeIndex !== lastIndex) {
      rows.push({time: totalDuration, [name]: event.initialValue});
      lastIndex = event.moveTypeIndex;
    }
    totalDuration += event.duration;
    rows.push({time: totalDuration, [name]: event.finalValue});
  }
  return rows;
}

export function buildTimeByMoveType(profile: LocalSearchProfile): PieDatum[] {
  const totals = new Map<number, number>();
  for (const event of profile.moveTypeEvents) {
    totals.set(
      event.moveTypeIndex,
      (totals.get(event.moveTypeIndex) ?? 0) + event.duration,
    );
  }
  return Array.from(totals.entries())
    .map(([index, value]) => ({
      name: profile.moveTypeNames[index] ?? `unknown_${index}`,
      value,
    }))
    .filter(d => d.value > 0);
}

export function buildDecreaseByMoveType(
  profile: LocalSearchProfile,
): PieDatum[] {
  const totals = new Map<number, number>();
  for (const event of profile.moveTypeEvents) {
    const decrease = Math.max(0, event.initialValue - event.finalValue);
    totals.set(
      event.moveTypeIndex,
      (totals.get(event.moveTypeIndex) ?? 0) + decrease,
    );
  }
  return Array.from(totals.entries())
    .map(([index, value]) => ({
      name: profile.moveTypeNames[index] ?? `unknown_${index}`,
      value,
    }))
    .filter(d => d.value > 0);
}

export function formatNumber(value: number | string): string {
  const n = Number(value);
  if (!Number.isFinite(n)) return String(value);
  if (Number.isInteger(n)) return String(n);
  // Keep tiny values readable instead of rounding them to 0.0000.
  if (Math.abs(n) < 1e-3) return n.toExponential(2);
  return n.toFixed(4);
}

/**
 * Each move type's share of a total as a labeled horizontal bar, largest
 * first: name, bar, value and percent. Every number is printed, so a single
 * move type reads as one full bar with its value instead of a solid disc.
 */
function ShareBars({
  data,
  colorFor,
  formatValue,
  label,
}: {
  data: PieDatum[];
  colorFor: (name: string) => string;
  formatValue: (value: number) => string;
  label: string;
}) {
  const total = data.reduce((sum, d) => sum + d.value, 0);
  const rows = [...data].sort((a, b) => b.value - a.value);
  return (
    <Box
      component="ul"
      aria-label={label}
      sx={{
        listStyle: 'none',
        m: 0,
        p: 0,
        display: 'grid',
        gridTemplateColumns: 'minmax(80px, max-content) minmax(0, 1fr) auto',
        alignItems: 'center',
        columnGap: 1.5,
        rowGap: 1,
      }}>
      {rows.map(d => {
        const share = total > 0 ? d.value / total : 0;
        return (
          <Box component="li" key={d.name} sx={{display: 'contents'}}>
            <Typography
              variant="body2"
              component="span"
              title={d.name}
              sx={{
                fontFamily: 'var(--font-mono)',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                whiteSpace: 'nowrap',
              }}>
              {d.name}
            </Typography>
            <Box
              aria-hidden
              sx={{height: 8, borderRadius: 999, bgcolor: COLOR.bgSubtle}}>
              <Box
                sx={{
                  width: `${share * 100}%`,
                  minWidth: 2,
                  height: '100%',
                  borderRadius: 999,
                  bgcolor: colorFor(d.name),
                }}
              />
            </Box>
            <Typography
              variant="body2"
              component="span"
              sx={{fontVariantNumeric: 'tabular-nums', textAlign: 'right'}}>
              {formatValue(d.value)}
              <Typography
                variant="caption"
                component="span"
                sx={{ml: 1, display: 'inline-block', minWidth: 44}}>
                {`${(share * 100).toFixed(1)}%`}
              </Typography>
            </Typography>
          </Box>
        );
      })}
    </Box>
  );
}

/**
 * Y-axis tick labels for one line chart. formatNumber keeps 4 decimals, which
 * prints the same label on every tick when the objective moves by less than
 * 0.01 (tw minimize_servers spans 7e-7 around 3.11). For such a span this
 * prints two digits past the first one the ticks differ in.
 */
export function valueTickFormatter(
  rows: readonly LineRow[],
): (value: number | string) => string {
  let min = Infinity;
  let max = -Infinity;
  for (const row of rows) {
    for (const [key, v] of Object.entries(row)) {
      if (key !== 'time' && typeof v === 'number') {
        min = Math.min(min, v);
        max = Math.max(max, v);
      }
    }
  }
  const span = max - min;
  if (!(span > 0) || span >= 1e-2) {
    return formatNumber;
  }
  const decimals = Math.min(12, Math.ceil(-Math.log10(span)) + 2);
  return value => {
    const n = Number(value);
    return Number.isFinite(n) ? n.toFixed(decimals) : String(value);
  };
}

interface LocalSearchProfileCardProps {
  profile: LocalSearchProfile;
}

/** The charts of one local search profile (shown when its row is expanded). */
export default function LocalSearchProfileCard({
  profile,
}: LocalSearchProfileCardProps) {
  const lineData = useMemo(() => buildObjectiveOverTime(profile), [profile]);
  const valueTick = useMemo(() => valueTickFormatter(lineData), [lineData]);
  const timeData = useMemo(() => buildTimeByMoveType(profile), [profile]);
  const decreaseData = useMemo(
    () => buildDecreaseByMoveType(profile),
    [profile],
  );

  const moveTypeNames = profile.moveTypeNames;
  const eventCount = profile.moveTypeEvents.length;

  const colorFor = (name: string): string => {
    const idx = moveTypeNames.indexOf(name);
    return CHART_PALETTE[(idx >= 0 ? idx : 0) % CHART_PALETTE.length];
  };

  return (
    <Box>
      {eventCount === 0 ? (
        <EmptyState compact title="No events recorded for this objective." />
      ) : (
        <>
          <Typography variant="subtitle2" component="h4" sx={{mt: 2, mb: 1}}>
            Objective value over time
          </Typography>
          <ResponsiveContainer width="100%" height={400}>
            <LineChart
              data={lineData}
              margin={{top: 8, right: 16, bottom: 24, left: 8}}>
              <CartesianGrid
                vertical={false}
                strokeDasharray={CHART_GRID_DASH}
                stroke={CHART_GRID_STROKE}
              />
              <XAxis
                dataKey="time"
                type="number"
                domain={['dataMin', 'dataMax']}
                label={{
                  value: 'Time (seconds)',
                  position: 'insideBottom',
                  offset: -12,
                  style: CHART_AXIS_LABEL_STYLE,
                }}
                tick={CHART_TICK_STYLE}
                tickLine={false}
                axisLine={{stroke: CHART_AXIS_STROKE}}
                tickFormatter={formatNumber}
              />
              <YAxis
                domain={['auto', 'auto']}
                label={{
                  value: 'Objective value',
                  angle: -90,
                  position: 'insideLeft',
                  style: CHART_AXIS_LABEL_STYLE,
                }}
                tick={CHART_TICK_STYLE}
                tickLine={false}
                axisLine={false}
                tickFormatter={valueTick}
                width={valueTick === formatNumber ? 60 : 100}
              />
              <Tooltip
                contentStyle={CHART_TOOLTIP_STYLE}
                labelStyle={CHART_TOOLTIP_LABEL_STYLE}
                itemStyle={CHART_TOOLTIP_ITEM_STYLE}
                formatter={(value: number | string) => valueTick(value)}
                labelFormatter={(label: number) =>
                  `Time: ${formatNumber(label)} s`
                }
              />
              <Legend
                verticalAlign="bottom"
                align="left"
                content={ChartLegend}
              />
              {moveTypeNames.map((name, i) => (
                <Line
                  key={name}
                  type="monotone"
                  dataKey={name}
                  stroke={CHART_PALETTE[i % CHART_PALETTE.length]}
                  dot={false}
                  strokeWidth={CHART_LINE_WIDTH}
                  connectNulls={false}
                  isAnimationActive={false}
                />
              ))}
            </LineChart>
          </ResponsiveContainer>

          <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mt-4">
            <div>
              <Typography variant="subtitle2" gutterBottom>
                Time spent by move type
              </Typography>
              <ShareBars
                data={timeData}
                colorFor={colorFor}
                formatValue={v => `${formatNumber(v)} s`}
                label="Time spent by move type"
              />
            </div>

            <div>
              <Typography variant="subtitle2" gutterBottom>
                Objective decrease by move type
              </Typography>
              {decreaseData.length === 0 ? (
                <EmptyState
                  compact
                  title="No positive objective decreases recorded."
                />
              ) : (
                <ShareBars
                  data={decreaseData}
                  colorFor={colorFor}
                  formatValue={formatNumber}
                  label="Objective decrease by move type"
                />
              )}
            </div>
          </div>
        </>
      )}
    </Box>
  );
}
