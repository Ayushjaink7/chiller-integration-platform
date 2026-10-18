import { methodName, num } from '../format.js';

// Process mimic of the chilled-water loop: chiller -> supply pipe -> building
// coil -> return pipe -> pump -> chiller. Values come from the latest record.
// Colour is kept for process lines and alarms only, in the spirit of
// high-performance HMI practice (ISA-101): grey when normal, colour when not.

const W = 720;
const H = 300;

function Bar({ x, y, width, label, value, marker, unit = '%', max = 100, minMark }) {
  const clamped = Math.max(0, Math.min(max, value ?? 0));
  const fill = (clamped / max) * width;
  return (
    <g>
      {label && (
        <>
          <text x={x} y={y - 8} className="mimic-label">
            {label}
          </text>
          <text x={x + width} y={y - 8} className="mimic-value" textAnchor="end">
            {num(value, unit === '%' ? 0 : 1)} {unit}
          </text>
        </>
      )}
      <rect x={x} y={y} width={width} height={10} rx={2} className="bar-track" />
      <rect x={x} y={y} width={fill} height={10} rx={2} className="bar-fill" />
      {minMark !== undefined && (
        <line
          x1={x + (minMark / max) * width}
          x2={x + (minMark / max) * width}
          y1={y - 3}
          y2={y + 13}
          className="bar-limit"
        />
      )}
      {marker !== undefined && (
        <path
          d={`M ${x + (Math.max(0, Math.min(max, marker)) / max) * width} ${y + 12} l -4 6 h 8 z`}
          className="bar-marker"
        />
      )}
    </g>
  );
}

function ModelDetail({ type, latest, model }) {
  const extras = latest?.extras || {};
  const x = 54;
  const width = 252;
  if (type === 'scroll') {
    const active = Math.round(extras.active_stages ?? 0);
    return (
      <g>
        <text x={x} y={152} className="mimic-label">
          Compressors running
        </text>
        <text x={x + width} y={152} className="mimic-value" textAnchor="end">
          {active} of 4
        </text>
        {[0, 1, 2, 3].map((i) => (
          <g key={i}>
            <rect
              x={x + i * 66}
              y={162}
              width={56}
              height={30}
              rx={4}
              className={i < active ? 'stage is-on' : 'stage'}
            />
            <text x={x + i * 66 + 28} y={182} textAnchor="middle" className={i < active ? 'stage-text is-on' : 'stage-text'}>
              C{i + 1}
            </text>
          </g>
        ))}
        <Bar x={x} y={226} width={width} label="Discharge temperature" value={extras.discharge_temp_c} unit="°C" max={150} minMark={120} />
      </g>
    );
  }
  if (type === 'centrifugal') {
    return (
      <g>
        <Bar x={x} y={166} width={width} label="Inlet guide vanes" value={extras.guide_vane_position_pct} minMark={model?.min_capacity_pct} />
        <Bar x={x} y={226} width={width} label="Surge margin" value={extras.surge_margin_pct} max={30} minMark={5} />
      </g>
    );
  }
  return (
    <g>
      <Bar x={x} y={166} width={width} label="Slide valve" value={extras.slide_valve_position_pct} minMark={model?.min_capacity_pct} />
      <Bar x={x} y={226} width={width} label="Oil pressure" value={extras.oil_pressure_kpa} unit="kPa" max={400} minMark={150} />
    </g>
  );
}

export default function PlantMimic({ state }) {
  const latest = state?.latest;
  const model = state?.model;
  const tripped = state?.state === 'TRIPPED';
  const fault = state?.active_faults?.[0];
  const faultDetail = fault ? fault.description.split(': ').slice(1).join(': ') || fault.description : '';
  const flow = latest?.flow_pct ?? 100;
  const flowing = flow > 5;
  // Faster dash movement for higher flow; pauses when the pump has failed.
  const flowSeconds = flowing ? Math.max(0.6, 2.4 * (100 / Math.max(flow, 1))) : 0;
  const loadKw = model && state ? (state.load_pct / 100) * model.rated_capacity_kw : null;
  const sensorBroken = latest && (latest.chw_supply_c < -9 || latest.chw_supply_c > 39);

  return (
    <div className="mimic">
      <div className="panel-head">
        <h2>{model ? model.display_name : 'Chilled-water loop'}</h2>
        <p>{model ? `${methodName[model.capacity_method] || model.capacity_method}, rated COP ${model.cop}` : 'Waiting for data'}</p>
      </div>

      <div className="mimic-scroll">
      <svg viewBox={`0 0 ${W} ${H}`} className="mimic-svg" role="img" aria-label="Chiller, chilled-water pipes and building load with live values">
        {/* Chiller unit */}
        <rect x={30} y={20} width={300} height={260} rx={8} className={tripped ? 'unit is-tripped' : 'unit'} />
        <text x={54} y={52} className="mimic-label">
          Capacity
        </text>
        <text x={306} y={52} className="mimic-big" textAnchor="end">
          {num(latest?.capacity_actual_pct, 0)}%
        </text>
        <Bar x={54} y={76} width={252} label="" value={latest?.capacity_actual_pct} marker={latest?.capacity_command_pct} />
        <text x={54} y={112} className="mimic-small">
          Commanded {num(latest?.capacity_command_pct, 0)}%, controller asked {num(latest?.capacity_request_pct, 0)}%
        </text>
        <text x={54} y={130} className="mimic-small">
          Condenser {num(latest?.cond_pressure_kpa, 0)} kPa, power {num(latest?.power_kw, 0)} kW
        </text>
        <ModelDetail type={model?.type} latest={latest} model={model} />

        {/* Supply pipe: chiller -> building */}
        <line x1={330} y1={90} x2={540} y2={90} className="pipe pipe-supply" />
        {flowing && (
          <line x1={330} y1={90} x2={540} y2={90} className="pipe-flow" style={{ animationDuration: `${flowSeconds}s` }} />
        )}
        <path d="M 528 82 l 12 8 l -12 8" className="pipe-arrow pipe-arrow-supply" />
        <text x={350} y={70} className={sensorBroken ? 'mimic-big is-alarm' : 'mimic-big'}>
          {sensorBroken ? 'Bad signal' : `${num(latest?.chw_supply_c, 2)} °C`}
        </text>
        <text x={350} y={114} className="mimic-small">
          Supply, setpoint {num(state?.setpoint_c, 1)} °C
        </text>

        {/* Return pipe: building -> pump -> chiller */}
        <line x1={540} y1={230} x2={330} y2={230} className="pipe pipe-return" />
        {flowing && (
          <line x1={540} y1={230} x2={330} y2={230} className="pipe-flow" style={{ animationDuration: `${flowSeconds}s` }} />
        )}
        <path d="M 342 222 l -12 8 l 12 8" className="pipe-arrow pipe-arrow-return" />
        <circle cx={440} cy={230} r={14} className={flow < 50 ? 'pump is-alarm' : 'pump'} />
        <path d="M 447 222 L 431 230 L 447 238 Z" className="pump-glyph" />
        <text x={350} y={262} className="mimic-big">
          {num(latest?.chw_return_c, 2)} °C
        </text>
        <text x={350} y={282} className="mimic-small">
          Return, flow {num(flow, 0)}%
        </text>

        {/* Building coil */}
        <rect x={540} y={60} width={150} height={200} rx={8} className="building" />
        <path d="M 565 110 h 100 M 565 135 h 100 M 565 160 h 100 M 565 185 h 100" className="coil" />
        <text x={615} y={84} textAnchor="middle" className="mimic-label">
          Building load
        </text>
        <text x={615} y={222} textAnchor="middle" className="mimic-big">
          {num(state?.load_pct, 0)}%
        </text>
        <text x={615} y={242} textAnchor="middle" className="mimic-small">
          {loadKw === null ? '' : `${num(loadKw, 0)} kW`}
        </text>
      </svg>
      </div>

      {tripped && fault && (
        <div className="trip-callout" role="alert">
          <svg viewBox="0 0 20 20" className="trip-icon" aria-hidden="true">
            <path d="M10 2 L19 18 H1 Z" />
            <path d="M10 8 v4.5 M10 14.8 v0.4" className="trip-icon-mark" />
          </svg>
          <div>
            <strong>
              Tripped: {fault.code} {fault.name.toLowerCase()}
            </strong>
            <span>
              {faultDetail.charAt(0).toUpperCase() + faultDetail.slice(1)}. Model code {fault.native_code}. Fix the cause, then reset alarms.
            </span>
          </div>
        </div>
      )}
    </div>
  );
}
