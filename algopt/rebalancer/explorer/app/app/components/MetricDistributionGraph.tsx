'use client';

import {useCallback, useEffect, useState} from 'react';

import {Typography} from '@mui/material';

import {ErrorState, LoadingState} from '@/app/components/ui/StateViews';
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

import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
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
} from '@/lib/ui-tokens';
import {fetchMetricDistribution} from '@/lib/rebalancer-explorer-api';
import type {MetricDistributionResponse} from '@/lib/rebalancer-explorer-types';

const MAX_POINTS = 1000;

interface MetricDistributionGraphProps {
  entityName: string;
  selectedMetrics: string[];
}

interface ChartDataPoint {
  index: number;
  [metricName: string]: number;
}

function buildChartData(
  metrics: string[],
  responses: MetricDistributionResponse[],
): ChartDataPoint[] {
  if (responses.length === 0) {
    return [];
  }

  // Match the www implementation: x = index, y = metric_value.
  // Since the API returns the same number of evenly-spaced points per metric,
  // we align by index position.
  const maxLen = Math.max(...responses.map(r => r.points.length));

  const chartData: ChartDataPoint[] = [];
  for (let idx = 0; idx < maxLen; idx++) {
    const point: ChartDataPoint = {index: idx};

    for (let i = 0; i < metrics.length; i++) {
      const points = responses[i]?.points ?? [];
      if (idx < points.length) {
        point[metrics[i]] = points[idx].metricValue;
      }
    }

    chartData.push(point);
  }

  return chartData;
}

export default function MetricDistributionGraph({
  entityName,
  selectedMetrics,
}: MetricDistributionGraphProps) {
  const {handle} = useRebalancerHandle();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [chartData, setChartData] = useState<ChartDataPoint[]>([]);
  const [loadedMetrics, setLoadedMetrics] = useState<string[]>([]);

  const fetchDistributions = useCallback(async () => {
    if (handle == null || selectedMetrics.length === 0) {
      setChartData([]);
      setLoadedMetrics([]);
      return;
    }

    setLoading(true);
    setError(null);

    try {
      const requests = selectedMetrics.map(metric => ({
        entity: entityName,
        metric,
        maxPoints: MAX_POINTS,
      }));

      const responses = await fetchMetricDistribution(handle, requests);
      setChartData(buildChartData(selectedMetrics, responses));
      setLoadedMetrics([...selectedMetrics]);
    } catch (err: unknown) {
      setError(
        err instanceof Error
          ? err.message
          : 'Failed to fetch metric distributions',
      );
    } finally {
      setLoading(false);
    }
  }, [handle, entityName, selectedMetrics]);

  useEffect(() => {
    void fetchDistributions();
  }, [fetchDistributions]);

  if (loading) {
    return <LoadingState compact label="Loading distribution..." />;
  }

  if (error != null) {
    return <ErrorState>{error}</ErrorState>;
  }

  if (chartData.length === 0) {
    return null;
  }

  return (
    <>
      <Typography variant="subtitle2" component="h4" sx={{mb: 1}}>
        Distribution of {entityName}
      </Typography>
      <ResponsiveContainer width="100%" height={400}>
        <LineChart
          data={chartData}
          margin={{top: 8, right: 16, bottom: 24, left: 8}}>
          <CartesianGrid
            vertical={false}
            strokeDasharray={CHART_GRID_DASH}
            stroke={CHART_GRID_STROKE}
          />
          <XAxis
            dataKey="index"
            type="number"
            domain={['dataMin', 'dataMax']}
            tick={CHART_TICK_STYLE}
            tickLine={false}
            axisLine={{stroke: CHART_AXIS_STROKE}}
            label={{
              value: `Rank (${entityName} sorted by value)`,
              position: 'insideBottom',
              offset: -12,
              style: CHART_AXIS_LABEL_STYLE,
            }}
          />
          <YAxis
            label={{
              value: loadedMetrics.length === 1 ? loadedMetrics[0] : 'Value',
              angle: -90,
              position: 'insideLeft',
              style: CHART_AXIS_LABEL_STYLE,
            }}
            domain={['auto', 'auto']}
            tick={CHART_TICK_STYLE}
            tickLine={false}
            axisLine={false}
            tickFormatter={(v: number) =>
              Number.isInteger(v) ? String(v) : v.toFixed(2)
            }
          />
          <Tooltip
            contentStyle={CHART_TOOLTIP_STYLE}
            labelStyle={CHART_TOOLTIP_LABEL_STYLE}
            itemStyle={CHART_TOOLTIP_ITEM_STYLE}
            formatter={(value: number | string) => Number(value).toFixed(2)}
            labelFormatter={(label: number) => `Rank ${label}`}
          />
          <Legend verticalAlign="bottom" align="left" content={ChartLegend} />
          {loadedMetrics.map((metric, i) => (
            <Line
              key={metric}
              type="monotone"
              dataKey={metric}
              stroke={CHART_PALETTE[i % CHART_PALETTE.length]}
              dot={false}
              strokeWidth={CHART_LINE_WIDTH}
            />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </>
  );
}
