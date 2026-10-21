/* UI + API calls. Owner: Member 3 */
const $ = id => document.getElementById(id);
let S = null;
const log = t => { $('log').textContent = new Date().toLocaleTimeString() + '  ' + t + '\n' + $('log').textContent; };
async function api(path, body) {
  const r = await fetch('/api/' + path, body === undefined ? {} : { method: 'POST', body });
  return r.json();
}
const dims = () => [Math.min(10, Math.max(1, +$('n').value || 1)), Math.min(10, Math.max(1, +$('m').value || 1))];

function grid(name, rows, cols, label, vals) {
  let h = `<div class="gb"><h3>${label}</h3><table><tr><th></th>` + [...Array(cols)].map((_, j) => `<th>R${j}</th>`).join('') + '</tr>';
  for (let i = 0; i < rows; i++) {
    h += `<tr><th>${name === 'total' ? '' : 'P' + i}</th>`;
    for (let j = 0; j < cols; j++) h += `<td><input type="number" min="0" id="${name}_${i}_${j}" value="${vals?.[i]?.[j] ?? 0}"></td>`;
    h += '</tr>';
  }
  return h + '</table></div>';
}
function build(v) {
  const [n, m] = v ? [v.n, v.m] : dims();
  $('n').value = n; $('m').value = m;
  $('grids').innerHTML = grid('total', 1, m, 'Total instances', v && [v.total]) + grid('alloc', n, m, 'Allocation', v?.alloc) + grid('max', n, m, 'Maximum', v?.max);
}
const read = (name, rows, cols) => [...Array(rows)].flatMap((_, i) => [...Array(cols)].map((_, j) => +$(`${name}_${i}_${j}`).value || 0));

async function apply() {
  const [n, m] = dims();
  const bad = validateInputs(n, m);
  if (bad) return log('Setup error: ' + bad);
  const body = [n, m, ...read('total', 1, m), ...read('alloc', n, m), ...read('max', n, m)].join(' ');
  const r = await api('setup', body);
  if (r.error) return log('Setup error: ' + r.error);
  log(`System set up: ${n} processes, ${m} resources`); show(r);
}

function tbl(title, M, S, vec) {
  const rows = vec ? [M] : M;
  let h = `<div class="tb"><h3>${title}</h3><table><tr><th></th>` + [...Array(S.m)].map((_, j) => `<th>R${j}</th>`).join('') + '</tr>';
  rows.forEach((r, i) => {
    const cls = vec ? '' : S.terminated[i] ? 'dead' : S.dead.includes(i) ? 'dl' : '';
    h += `<tr class="${cls}"><th>${vec ? '' : 'P' + i}</th>` + r.map(x => `<td>${x}</td>`).join('') + '</tr>';
  });
  return h + '</table></div>';
}

function show(s) {
  if (!s.ready) return;
  const rebuild = !S || S.n !== s.n || S.m !== s.m;
  S = s;
  $('tables').innerHTML = tbl('Available', S.available, S, true) + tbl('Allocation', S.alloc, S) + tbl('Maximum', S.max, S) +
                          tbl('Need', S.need, S) + tbl('Pending requests', S.request, S);
  $('util').innerHTML = '<h3>Resource utilization</h3>' + S.util.map((u, j) => `<div class="bar">R${j}<div><i style="width:${u}%"></i></div>${u}%</div>`).join('');
  $('badge').textContent = S.state;
  $('badge').className = 'badge ' + { SAFE: 'ok', UNSAFE: 'warn', DEADLOCKED: 'bad' }[S.state];
  $('status').innerHTML = `<b>State: ${S.state}</b> ` +
    (S.safe ? '&nbsp; Safe sequence: ' + S.seq.map(p => `<span class="chip">P${p}</span>`).join('→') : '&nbsp; No safe sequence') +
    (S.dead.length ? '<br><b style="color:#c62828">Deadlocked: ' + S.dead.map(p => 'P' + p).join(', ') + '</b>' : '');
  if (rebuild) {
    $('rp').innerHTML = [...Array(S.n)].map((_, i) => `<option value="${i}">P${i}</option>`).join('');
    $('rv').innerHTML = [...Array(S.m)].map((_, j) => `R${j}<input type="number" min="0" id="rv_${j}" value="0">`).join('');
  }
  renderConditions(S);
  loadHistory();
}

const reqBody = () => [$('rp').value, ...[...Array(S.m)].map((_, j) => +$('rv_' + j).value || 0)].join(' ');
const need = () => { if (!S) { log('Set up the system first.'); return false; } return true; };

$('build').onclick = () => build();
$('apply').onclick = apply;
$('sample1').onclick = async () => {
  build({ n: 5, m: 3, total: [10, 5, 7], alloc: [[0,1,0],[2,0,0],[3,0,2],[2,1,1],[0,0,2]], max: [[7,5,3],[3,2,2],[9,0,2],[2,2,2],[4,3,3]] });
  await apply();
};
$('sample2').onclick = async () => {
  build({ n: 2, m: 2, total: [1, 1], alloc: [[1,0],[0,1]], max: [[1,1],[1,1]] });
  await apply();
  show(await api('pending', '0 0 1')); show(await api('pending', '1 1 0'));
  log('P0 waits for R1 and P1 waits for R0 (circular wait)');
};
$('req').onclick = async () => {
  if (!need()) return;
  const r = await api('request', reqBody());
  if (r.error) return log(r.error);
  log(`Request by P${$('rp').value}: ${r.result}`); show(r.state);
};
$('pend').onclick = async () => {
  if (!need()) return;
  const r = await api('pending', reqBody());
  if (r.error) return log(r.error);
  log(`P${$('rp').value} is now blocked waiting for resources`); show(r);
};
$('bank').onclick = async () => {
  if (!need()) return;
  show(await api('state'));
  log(S.safe ? "Banker's: SAFE, sequence " + S.seq.map(p => 'P' + p).join(' → ') : "Banker's: UNSAFE, no safe sequence exists");
};
$('detect').onclick = async () => {
  if (!need()) return;
  show(await api('state'));
  log(S.dead.length ? 'Deadlock detected: ' + S.dead.map(p => 'P' + p).join(', ') : 'No deadlock detected');
};
$('clearreq').onclick = async () => { if (need()) { show(await api('clear', '')); log('Pending requests cleared'); } };
$('recover').onclick = async () => {
  if (!need()) return;
  const r = await api('recover', '-1 ' + $('mode').value);
  if (r.error) return log(r.error);
  log(`Recovery: ${$('mode').value === '0' ? 'terminated' : 'preempted'} P${r.victim}`); show(r.state);
};

build();
api('state').then(show);
