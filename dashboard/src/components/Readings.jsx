import { num } from '../format.js';

const EXTRA_LABELS = {
  slide_valve_position_pct: ['Slide valve position', '%', 0],
  oil_pressure_kpa: ['Oil pressure', 'kPa', 0],
  guide_vane_position_pct: ['Guide vane position', '%', 0],
  surge_margin_pct: ['Surge margin', '%', 1],
  active_stages: ['Compressors running', '', 0],
  discharge_temp_c: ['Discharge temperature', '°C', 1],
};

export default function Readings({ state }) {
  const latest = state?.latest;
  const model = state?.model;
  const extras = latest?.extras || {};
  const pid = state?.pid;

  const common = [
    ['Chilled-water flow', `${num(latest?.flow_pct, 0)} %`],
    ['Condenser pressure', `${num(latest?.cond_pressure_kpa, 0)} kPa`],
    ['Compressor power', `${num(latest?.power_kw, 1)} kW`],
    ['Compressor starts this run', state ? String(state.compressor_starts) : '–'],
  ];

  return (
    <div className="readings">
      <div className="panel-head">
        <h2>Readings</h2>
        <p>Common to every model, then specific to {model?.model_id ?? 'this model'}</p>
      </div>
      <dl className="reading-list">
        {common.map(([k, v]) => (
          <div key={k}>
            <dt>{k}</dt>
            <dd>{v}</dd>
          </div>
        ))}
      </dl>
      <dl className="reading-list is-specific">
        {Object.entries(extras).map(([key, value]) => {
          const [label, unit, digits] = EXTRA_LABELS[key] || [key, '', 2];
          return (
            <div key={key}>
              <dt>{label}</dt>
              <dd>
                {num(value, digits)} {unit}
              </dd>
            </div>
          );
        })}
      </dl>
      <p className="readings-note">
        PID Kp {pid?.kp ?? '–'}, Ki {pid?.ki ?? '–'}, Kd {pid?.kd ?? '–'}, the same for every model. Run{' '}
        {state?.run_id ?? '–'} in SQLite.
      </p>
    </div>
  );
}
