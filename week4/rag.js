/* Resource Allocation Graph (SVG) */
function drawRAG(S) {
  const W = 700, PY = 70, RY = 250;
  const px = i => (i + 1) * W / (S.n + 1), rx = j => (j + 1) * W / (S.m + 1);
  const mk = (id, c) => `<marker id="${id}" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8z" fill="${c}"/></marker>`;
  let h = `<defs>${mk('ag', '#2e7d32')}${mk('ar', '#ef6c00')}</defs>`;
  const edge = (x1, y1, x2, y2, cls, m, label) => {
    const dx = x2 - x1, dy = y2 - y1, L = Math.hypot(dx, dy), ux = dx / L, uy = dy / L, ox = -uy * 6, oy = ux * 6;
    const ax = x1 + ux * 24 + ox, ay = y1 + uy * 24 + oy, bx = x2 - ux * 26 + ox, by = y2 - uy * 26 + oy;
    return `<line x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}" class="${cls}" marker-end="url(#${m})"/>` +
           `<text x="${(ax + bx) / 2 + ox}" y="${(ay + by) / 2 + oy}" class="el">${label}</text>`;
  };
  for (let i = 0; i < S.n; i++) for (let j = 0; j < S.m; j++) {
    if (S.alloc[i][j] > 0) h += edge(rx(j), RY, px(i), PY, 'ea', 'ag', S.alloc[i][j]);
    if (S.request[i][j] > 0) h += edge(px(i), PY, rx(j), RY, 'er', 'ar', S.request[i][j]);
  }
  for (let i = 0; i < S.n; i++) {
    const cls = S.terminated[i] ? 'gone' : S.dead.includes(i) ? 'dead' : '';
    h += `<circle cx="${px(i)}" cy="${PY}" r="22" class="np ${cls}"/><text x="${px(i)}" y="${PY + 5}" class="nt">P${i}</text>`;
  }
  for (let j = 0; j < S.m; j++)
    h += `<rect x="${rx(j) - 24}" y="${RY - 24}" width="48" height="48" class="nr"/>` +
         `<text x="${rx(j)}" y="${RY + 5}" class="nt">R${j}</text>` +
         `<text x="${rx(j)}" y="${RY + 42}" class="el">${S.available[j]}/${S.total[j]} free</text>`;
  document.getElementById('rag').innerHTML = h;
}

/* Before / during / after stepper over the server's history. Owner: Member 4 */
let H = [], hi = 0;
async function loadHistory() {
  H = await (await fetch('/api/history')).json();
  hi = H.length - 1;
  showStep();
}
function showStep() {
  if (!H.length) return;
  drawRAG(H[hi].state);
  document.getElementById('steptxt').textContent = `Step ${hi + 1}/${H.length}: ${H[hi].label}`;
}
document.getElementById('sprev').onclick = () => { if (hi > 0) { hi--; showStep(); } };
document.getElementById('snext').onclick = () => { if (hi < H.length - 1) { hi++; showStep(); } };
