# API contract (Member 2). Bodies are whitespace-separated integers; responses are JSON.

| Endpoint | Method | Body | Returns |
|---|---|---|---|
| `/api/state` | GET | none | full state (`{"ready":false}` before setup) |
| `/api/setup` | POST | `n m total[m] alloc[n*m] max[n*m]` | state, or `{"error":...}` |
| `/api/request` | POST | `p req[m]` | `{"result","code","state"}` (code: 0 granted, 1 exceeds need, 2 wait, 3 unsafe, 4 invalid) |
| `/api/pending` | POST | `p req[m]` | state (forces a blocked request, used to demo deadlock) |
| `/api/recover` | POST | `victim mode` (victim -1 = auto, mode 0 terminate / 1 preempt) | `{"victim","state"}` |
| `/api/clear` | POST | none | state (pending requests cleared) |
| `/api/history` | GET | none | `[{"label","state"}, ...]`, one entry per change (max 40) |
