export const num = (value, digits = 1) =>
  value === null || value === undefined || Number.isNaN(value) ? '–' : Number(value).toFixed(digits);

// Simulation time as m:ss (or h:mm:ss after an hour).
export const clock = (seconds) => {
  const s = Math.max(0, Math.floor(seconds || 0));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = String(s % 60).padStart(2, '0');
  return h > 0 ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`;
};

export const typeName = {
  screw: 'Screw',
  centrifugal: 'Centrifugal',
  scroll: 'Scroll',
};

export const methodName = {
  slide_valve: 'Slide valve',
  inlet_guide_vanes: 'Inlet guide vanes',
  compressor_staging: 'Compressor staging',
};

// Plain-language names for the faults the operator can inject.
export const faultLabel = (fault, modelType) => {
  const protection = {
    screw: 'Lose oil pressure',
    centrifugal: 'Push compressor into surge',
    scroll: 'Overheat discharge gas',
  };
  return (
    {
      sensor_failure: 'Break supply temperature sensor',
      condenser_fan_failure: 'Fail condenser fan',
      pump_failure: 'Fail chilled-water pump',
      capacity_stuck_high: 'Stick capacity at 100%',
      compressor_protection: protection[modelType] || 'Trip compressor protection',
    }[fault] || fault
  );
};
