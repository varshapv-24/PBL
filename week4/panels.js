/* Info panels and input validation. Owner: Member 3 */
function renderConditions(S) {
  const sum = r => r.reduce((a, b) => a + b, 0);
  const holdWait = S.alloc.some((r, i) => !S.terminated[i] && sum(r) > 0 && sum(S.request[i]) > 0);
  const rows = [
    ['Mutual exclusion', true, 'Each resource instance is used by one process at a time'],
    ['Hold and wait', holdWait, 'A process holds resources while waiting for more'],
    ['No preemption', true, 'Resources are released only voluntarily (until recovery forces it)'],
    ['Circular wait', S.dead.length > 0, 'A cycle of processes, each waiting for the next']
  ];
  const all = rows.every(r => r[1]);
  $('cond').innerHTML = '<table>' + rows.map(([n, on, d]) =>
    `<tr><td style="text-align:left"><b>${n}</b></td><td><span class="chip" style="background:${on ? '#ffd7d7' : '#dff3e2'}">${on ? 'PRESENT' : 'absent'}</span></td><td style="text-align:left">${d}</td></tr>`).join('') +
    `</table><p class="hint">${all ? 'All four conditions hold, so deadlock exists.' : 'Deadlock needs all four conditions at once.'}</p>`;
}

function validateInputs(n, m) {
  const v = id => +$(id).value || 0;
  for (let i = 0; i < n; i++) for (let j = 0; j < m; j++)
    if (v(`alloc_${i}_${j}`) > v(`max_${i}_${j}`)) return `P${i} holds more of R${j} than its maximum`;
  for (let j = 0; j < m; j++) {
    let used = 0;
    for (let i = 0; i < n; i++) used += v(`alloc_${i}_${j}`);
    if (used > v(`total_0_${j}`)) return `Allocated R${j} (${used}) exceeds its total`;
  }
  return '';
}
