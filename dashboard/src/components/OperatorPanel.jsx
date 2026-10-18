import { useEffect, useRef, useState } from 'react';
import { api } from '../api.js';
import { faultLabel, num } from '../format.js';

const SPEEDS = [1, 5, 10, 25];

export default function OperatorPanel({ state, onCommand }) {
  const [load, setLoad] = useState(50);
  const editingLoad = useRef(false);
  const loadTimer = useRef(null);

  // Follow the server's load value unless the operator is dragging the slider.
  useEffect(() => {
    if (state && !editingLoad.current) setLoad(Math.round(state.load_pct));
  }, [state]);

  const changeLoad = (value) => {
    editingLoad.current = true;
    setLoad(value);
    clearTimeout(loadTimer.current);
    loadTimer.current = setTimeout(async () => {
      await onCommand(() => api.setLoad(value));
      editingLoad.current = false;
    }, 250);
  };

  const setpoint = state?.setpoint_c ?? 7;
  const changeSetpoint = (delta) => {
    const next = Math.round((setpoint + delta) * 10) / 10;
    onCommand(() => api.setSetpoint(next));
  };

  const faults = state?.active_faults ?? [];
  const injected = new Set(state?.injected ?? []);
  const injectable = state?.injectable_faults ?? [];
  const modelType = state?.model?.type;
  const tripped = state?.state === 'TRIPPED';

  return (
    <div className="operator">
      <div className="panel-head">
        <h2>Operator</h2>
        <p>Changes apply to the running chiller right away.</p>
      </div>

      <div className="control">
        <span className="control-label" id="sp-label">
          Supply water setpoint
        </span>
        <div className="stepper" role="group" aria-labelledby="sp-label">
          <button type="button" onClick={() => changeSetpoint(-0.5)} aria-label="Lower setpoint by 0.5 degrees">
            −
          </button>
          <output aria-live="polite">{num(setpoint, 1)} °C</output>
          <button type="button" onClick={() => changeSetpoint(0.5)} aria-label="Raise setpoint by 0.5 degrees">
            +
          </button>
        </div>
      </div>

      <div className="control">
        <label className="control-label" htmlFor="load">
          Building load <span className="control-value">{load}%</span>
        </label>
        <input
          id="load"
          type="range"
          min="0"
          max="100"
          step="5"
          value={load}
          onChange={(e) => changeLoad(Number(e.target.value))}
        />
        <div className="range-scale" aria-hidden="true">
          <span>0%</span>
          <span>50%</span>
          <span>100%</span>
        </div>
      </div>

      <div className="control">
        <span className="control-label" id="speed-label">
          Simulation speed
        </span>
        <div className="segmented" role="group" aria-labelledby="speed-label">
          {SPEEDS.map((s) => (
            <button
              key={s}
              type="button"
              className={state?.speed === s ? 'is-active' : ''}
              aria-pressed={state?.speed === s}
              onClick={() => onCommand(() => api.setSpeed(s))}
            >
              {s}×
            </button>
          ))}
        </div>
      </div>

      <div className="control">
        <span className="control-label">Inject a fault</span>
        <p className="control-help">Each one should be caught and trip the chiller within a minute.</p>
        <ul className="fault-list">
          {injectable.map((f) => (
            <li key={f}>
              <button type="button" className="fault-button" onClick={() => onCommand(() => api.inject(f))}>
                <span>{faultLabel(f, modelType)}</span>
                {injected.has(f) && <span className="tag">Injected</span>}
              </button>
            </li>
          ))}
        </ul>
      </div>

      <div className="control actions">
        <button
          type="button"
          className={faults.length || tripped ? 'action is-primary' : 'action'}
          onClick={() => onCommand(api.resetAlarms)}
        >
          {faults.length ? `Repair and reset ${faults.length} alarm${faults.length > 1 ? 's' : ''}` : 'Reset alarms'}
        </button>
        <button type="button" className="action" onClick={() => onCommand(api.restart)}>
          Restart run
        </button>
      </div>
    </div>
  );
}
