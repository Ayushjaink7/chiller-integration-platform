import { useMemo } from 'react';
import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { clock, num } from '../format.js';

// Two charts, one y-axis each. Dashed ink = what the controller wants
// (setpoint / request); solid colour = what the plant does.
const CONFIG = {
  temperature: {
    title: 'Chilled-water temperature',
    subtitle: 'Last 10 simulated minutes',
    unit: '°C',
    digits: 2,
    series: [
      { key: 'supply', label: 'Supply', source: 'chw_supply_c', color: 'var(--series-1)' },
      { key: 'ret', label: 'Return', source: 'chw_return_c', color: 'var(--series-2)' },
      { key: 'target', label: 'Setpoint', source: 'setpoint_c', color: 'var(--ink-soft)', dashed: true, step: true, digits: 1 },
    ],
  },
  capacity: {
    title: 'Compressor capacity',
    subtitle: 'What the PID asked for, what the model commanded, what it delivered',
    unit: '%',
    digits: 0,
    domain: [0, 100],
    series: [
      { key: 'command', label: 'Commanded', source: 'capacity_command_pct', color: 'var(--series-1)', step: true },
      { key: 'actual', label: 'Delivered', source: 'capacity_actual_pct', color: 'var(--series-2)' },
      { key: 'request', label: 'PID request', source: 'capacity_request_pct', color: 'var(--ink-soft)', dashed: true },
    ],
  },
};

function niceDomain(rows, keys) {
  let lo = Infinity;
  let hi = -Infinity;
  for (const r of rows) {
    for (const k of keys) {
      const v = r[k];
      if (Number.isFinite(v) && v > -9 && v < 39) {
        lo = Math.min(lo, v);
        hi = Math.max(hi, v);
      }
    }
  }
  if (!Number.isFinite(lo)) return [0, 15];
  const a = Math.floor(lo - 0.5);
  let b = Math.ceil(hi + 0.5);
  if ((b - a) % 2 === 1 && b - a > 8) b += 1;
  return [a, b];
}

const CHART_HEIGHT = 240;
const MARGIN = { top: 8, right: 96, bottom: 4, left: 0 };
const X_AXIS_HEIGHT = 30;
const LABEL_GAP_PX = 15;

// Direct labels at the line ends, nudged apart so they never overlap.
function labelOffsets(rows, series, domain) {
  if (!rows.length) return {};
  const plotHeight = CHART_HEIGHT - MARGIN.top - MARGIN.bottom - X_AXIS_HEIGHT;
  const pxPerUnit = plotHeight / (domain[1] - domain[0] || 1);
  const last = rows[rows.length - 1];
  const items = series
    .map((s) => ({ key: s.key, y: (domain[1] - last[s.key]) * pxPerUnit }))
    .filter((i) => Number.isFinite(i.y))
    .sort((a, b) => a.y - b.y);
  const placed = [];
  for (const item of items) {
    const prev = placed[placed.length - 1];
    const y = prev ? Math.max(item.y, prev.y + LABEL_GAP_PX) : item.y;
    placed.push({ ...item, y });
  }
  // If the stack runs past the bottom of the plot, slide it up as a group.
  const overflow = placed.length ? Math.max(0, placed[placed.length - 1].y - plotHeight) : 0;
  return Object.fromEntries(placed.map((p, i) => [p.key, p.y - overflow - items[i].y]));
}

function niceTicks(domain) {
  const [lo, hi] = domain;
  const step = hi - lo > 8 ? 2 : 1;
  const ticks = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) ticks.push(v);
  return ticks;
}

// Time ticks on round minutes (or half-minutes for short windows).
function timeTicks(rows) {
  if (rows.length < 2) return undefined;
  const lo = rows[0].t;
  const hi = rows[rows.length - 1].t;
  const span = hi - lo;
  const step = [30, 60, 120, 300, 600, 1800].find((s) => span / s <= 6) || 3600;
  const ticks = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) ticks.push(v);
  return ticks;
}

function EndLabel({ x, y, index, count, text, color, offset = 0 }) {
  if (index !== count - 1 || !Number.isFinite(y)) return null;
  return (
    <g>
      <circle cx={x} cy={y} r={3} fill={color} />
      <text x={x + 8} y={y + 4 + offset} className="end-label">
        {text}
      </text>
    </g>
  );
}

function ChartTooltip({ active, payload, label, cfg }) {
  if (!active || !payload?.length) return null;
  return (
    <div className="chart-tooltip">
      <div className="chart-tooltip-time">t = {clock(label)}</div>
      {cfg.series.map((s) => {
        const p = payload.find((x) => x.dataKey === s.key);
        if (!p) return null;
        return (
          <div key={s.key} className="chart-tooltip-row">
            <span className={s.dashed ? 'swatch is-dashed' : 'swatch'} style={{ '--swatch': s.color }} />
            <span>{s.label}</span>
            <strong>
              {num(p.value, cfg.digits)} {cfg.unit}
            </strong>
          </div>
        );
      })}
    </div>
  );
}

export default function TrendChart({ kind, history }) {
  const cfg = CONFIG[kind];
  const rows = useMemo(() => {
    if (!history?.time_s) return [];
    return history.time_s.map((t, i) => {
      const row = { t };
      for (const s of cfg.series) row[s.key] = history[s.source][i];
      return row;
    });
  }, [history, cfg]);

  const domain = cfg.domain || niceDomain(rows, cfg.series.map((s) => s.key));
  const count = rows.length;
  const offsets = labelOffsets(rows, cfg.series, domain);

  return (
    <div className="trend">
      <div className="panel-head">
        <h2>{cfg.title}</h2>
        <p>{cfg.subtitle}</p>
      </div>
      <ul className="legend">
        {cfg.series.map((s) => (
          <li key={s.key}>
            <span className={s.dashed ? 'swatch is-dashed' : 'swatch'} style={{ '--swatch': s.color }} />
            {s.label}
          </li>
        ))}
      </ul>
      <div className="chart-box" style={{ height: CHART_HEIGHT }}>
        {count === 0 ? (
          <p className="empty">Waiting for the first readings.</p>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={rows} margin={MARGIN}>
              <CartesianGrid vertical={false} stroke="var(--grid)" />
              <XAxis
                dataKey="t"
                type="number"
                domain={['dataMin', 'dataMax']}
                tickFormatter={clock}
                ticks={timeTicks(rows)}
                tick={{ fill: 'var(--ink-muted)', fontSize: 12 }}
                stroke="var(--rule)"
              />
              <YAxis
                domain={domain}
                ticks={cfg.domain ? [0, 25, 50, 75, 100] : niceTicks(domain)}
                width={44}
                tick={{ fill: 'var(--ink-muted)', fontSize: 12 }}
                stroke="var(--rule)"
                tickFormatter={(v) => `${v}`}
                allowDataOverflow
              />
              <Tooltip content={<ChartTooltip cfg={cfg} />} cursor={{ stroke: 'var(--ink-muted)', strokeWidth: 1 }} />
              {cfg.series.map((s) => (
                <Line
                  key={s.key}
                  dataKey={s.key}
                  type={s.step ? 'stepAfter' : 'linear'}
                  stroke={s.color}
                  strokeWidth={s.dashed ? 1.5 : 2}
                  strokeDasharray={s.dashed ? '6 4' : undefined}
                  dot={false}
                  activeDot={{ r: 4, strokeWidth: 2, stroke: 'var(--panel)' }}
                  isAnimationActive={false}
                  label={(props) => (
                    <EndLabel
                      key={`${s.key}-end`}
                      {...props}
                      count={count}
                      color={s.color}
                      offset={offsets[s.key] || 0}
                      text={`${s.label} ${num(props.value, s.digits ?? cfg.digits)}`}
                    />
                  )}
                />
              ))}
            </LineChart>
          </ResponsiveContainer>
        )}
      </div>
    </div>
  );
}
