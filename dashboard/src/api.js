// Thin client for the C++ REST API (see apps/chiller_server.cpp).

async function request(path, options = {}) {
  const res = await fetch(`/api${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  const body = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(body.error || `Request failed (${res.status})`);
  return body;
}

const post = (path, payload = {}) => request(path, { method: 'POST', body: JSON.stringify(payload) });

export const api = {
  models: () => request('/models'),
  state: () => request('/state'),
  history: (seconds = 600) => request(`/history?seconds=${seconds}`),
  faults: (limit = 40) => request(`/faults?limit=${limit}`),
  selectModel: (modelId) => post('/select', { model_id: modelId }),
  setSetpoint: (value) => post('/setpoint', { value }),
  setLoad: (value) => post('/load', { value }),
  setSpeed: (value) => post('/speed', { value }),
  inject: (fault) => post('/inject', { fault }),
  resetAlarms: () => post('/reset'),
  restart: () => post('/restart'),
};
