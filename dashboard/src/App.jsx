import { useCallback, useEffect, useRef, useState } from 'react';
import { api } from './api.js';
import { clock, typeName } from './format.js';
import PlantMimic from './components/PlantMimic.jsx';
import OperatorPanel from './components/OperatorPanel.jsx';
import TrendChart from './components/TrendChart.jsx';
import Readings from './components/Readings.jsx';
import EventLog from './components/EventLog.jsx';

const POLL_MS = 1000;
const HISTORY_SECONDS = 600;

export default function App() {
  const [models, setModels] = useState([]);
  const [state, setState] = useState(null);
  const [history, setHistory] = useState(null);
  const [events, setEvents] = useState([]);
  const [offline, setOffline] = useState(false);
  const [notice, setNotice] = useState('');
  const inFlight = useRef(false);

  const refresh = useCallback(async () => {
    if (inFlight.current) return;
    inFlight.current = true;
    try {
      const [s, h, f] = await Promise.all([api.state(), api.history(HISTORY_SECONDS), api.faults(40)]);
      setState(s);
      setHistory(h);
      setEvents(f);
      setOffline(false);
    } catch {
      setOffline(true);
    } finally {
      inFlight.current = false;
    }
  }, []);

  useEffect(() => {
    api.models().then(setModels).catch(() => setOffline(true));
    refresh();
    const id = setInterval(refresh, POLL_MS);
    return () => clearInterval(id);
  }, [refresh]);

  // Runs an operator command, shows its error if any, then refreshes the view.
  const command = useCallback(
    async (fn) => {
      try {
        const s = await fn();
        if (s && s.model) setState(s);
        setNotice('');
      } catch (e) {
        setNotice(e.message);
      }
      refresh();
    },
    [refresh],
  );

  const selectModel = async (id) => {
    await command(() => api.selectModel(id));
    api.models().then(setModels).catch(() => {});
  };

  const activeId = state?.model?.model_id;
  const tripped = state?.state === 'TRIPPED';

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <h1>Chiller integration platform</h1>
          <p>One C++ control application running three chiller models</p>
        </div>

        <nav className="model-switch" aria-label="Chiller model">
          {models.map((m) => (
            <button
              key={m.model_id}
              type="button"
              className={m.model_id === activeId ? 'model-option is-active' : 'model-option'}
              aria-pressed={m.model_id === activeId}
              onClick={() => selectModel(m.model_id)}
            >
              <span className="model-option-type">{typeName[m.type] || m.type}</span>
              <span className="model-option-id">{m.model_id}</span>
            </button>
          ))}
        </nav>

        <div className="run-status">
          <span className={tripped ? 'state-pill is-tripped' : 'state-pill'}>
            <span className="state-dot" aria-hidden="true" />
            {tripped ? 'Tripped' : state ? 'Running' : 'Connecting'}
          </span>
          <span className="sim-clock" title="Simulated time since this run started">
            {clock(state?.time_s)} <small>at {state?.speed ?? '–'}× speed</small>
          </span>
        </div>
      </header>

      {offline && (
        <div className="banner banner-error" role="alert">
          The dashboard can't reach the chiller server. Start it from the project folder with{' '}
          <code>build/bin/chiller_server</code>, then this page reconnects by itself.
        </div>
      )}
      {notice && (
        <div className="banner banner-warn" role="status">
          {notice}
        </div>
      )}

      <main className="layout">
        <section className="panel area-mimic" aria-label="Plant overview">
          <PlantMimic state={state} />
        </section>

        <aside className="panel area-operator" aria-label="Operator controls">
          <OperatorPanel state={state} onCommand={command} />
        </aside>

        <section className="panel area-temp" aria-label="Water temperature trend">
          <TrendChart kind="temperature" history={history} />
        </section>

        <section className="panel area-capacity" aria-label="Capacity trend">
          <TrendChart kind="capacity" history={history} />
        </section>

        <section className="panel area-readings" aria-label="Readings">
          <Readings state={state} />
        </section>

        <section className="panel area-events" aria-label="Fault events">
          <EventLog events={events} />
        </section>
      </main>

      <footer className="footer">
        Simulated plant. The control app talks only to the <code>ChillerModel</code> interface; each model is a C++
        class plus a JSON config. Records are stored in SQLite.
      </footer>
    </div>
  );
}
