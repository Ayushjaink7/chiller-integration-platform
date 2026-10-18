import { clock } from '../format.js';

export default function EventLog({ events }) {
  return (
    <div className="events">
      <div className="panel-head">
        <h2>Fault events</h2>
        <p>Read from the SQLite black box, newest first. Standard codes are the same on every model.</p>
      </div>
      {events.length === 0 ? (
        <p className="empty">No faults yet. Inject one from the operator panel to see it caught here.</p>
      ) : (
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th scope="col">Sim time</th>
                <th scope="col">Run</th>
                <th scope="col">Model</th>
                <th scope="col">Code</th>
                <th scope="col">Model code</th>
                <th scope="col">What happened</th>
                <th scope="col">Event</th>
              </tr>
            </thead>
            <tbody>
              {events.map((e) => (
                <tr key={e.id} className={e.event === 'RAISED' ? 'is-raised' : ''}>
                  <td className="num">{clock(e.time_s)}</td>
                  <td className="num">{e.run_id}</td>
                  <td>{e.model_id}</td>
                  <td>
                    <span className="code">{e.fault_code}</span>
                  </td>
                  <td>{e.native_code}</td>
                  <td>{e.description}</td>
                  <td>{e.event === 'RAISED' ? 'Raised' : 'Cleared'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
