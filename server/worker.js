// AimForge leaderboard — Cloudflare Worker + D1 (fits the free plan).
// Responses are plain text, tab separated, UTF-8: easy to parse from C.
//
//   GET  /health                         -> ok\taimforge\t1
//   POST /register  pid,secret,name      -> ok\t<name>         | err\t<code>
//   POST /submit    pid,secret,name,s,r,mode,score,acc,kills,shots,hits,expired,ttk,dur,track_on,ver
//                                         -> ok\t<rank>\t<total>\t<best>\t<improved 0|1>
//   GET  /top?s=&r=&n=50&pid=            -> ok\t<total>\t<my rank>\t<my best>\n then rows:
//                                            rank\tname\tscore\tacc\tkills\tat\tis_me
//   GET  /summary?pid=                   -> ok\n then rows: scenario\truleset\trank\ttotal\tbest
//   GET  /web[?s=&r=]                    -> HTML page with the leaderboards (to share with friends)
//   GET  /version                        -> ok\t<latest version>\t<download url>   (vars LATEST_VERSION, DOWNLOAD_URL)
//   GET  /download                       -> 302 to DOWNLOAD_URL (one link to give to friends)
//   POST /admin/ban  pid=… or name=…  (Authorization: Bearer $ADMIN_TOKEN) — removes all scores, blocks the player

const API_VERSION = "1";
const MAX_ROWS = 100;
const PID_RE = /^[0-9a-f]{32}$/;
const SECRET_RE = /^[0-9a-f]{64}$/;
const SCEN_RE = /^[a-z0-9_]{1,23}$/;
const RULE_RE = /^[a-z0-9]{1,16}$/;
const NAME_RE = /^[A-Za-zА-Яа-яЁё0-9 _\-.\[\]()!?#*+@~^]{2,16}$/u;
const PID_COOLDOWN_S = 5;         // one submission per player every 5 s
const IP_WINDOW_S = 600, IP_MAX = 40;
const VER_RE = /^\d{1,3}(\.\d{1,3}){0,2}$/;
const DEFAULT_DOWNLOAD = "https://github.com/kla1mmor/AimForge/releases/latest";

const TEXT = { "content-type": "text/plain; charset=utf-8", "access-control-allow-origin": "*", "cache-control": "no-store" };
const ok = (body) => new Response("ok" + (body ? "\t" + body : "") + "\n", { headers: TEXT });
const okLines = (head, rows) => new Response("ok" + (head ? "\t" + head : "") + "\n" + rows.map(r => r.join("\t") + "\n").join(""), { headers: TEXT });
const err = (code, status = 400) => new Response("err\t" + code + "\n", { status, headers: TEXT });
const now = () => Math.floor(Date.now() / 1000);

async function sha256(s) {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map(b => b.toString(16).padStart(2, "0")).join("");
}

function cleanName(raw) {
  if (typeof raw !== "string") return null;
  const n = raw.normalize("NFC").replace(/[\u0000-\u001f\u007f\t]/g, "").replace(/\s+/g, " ").trim();
  return NAME_RE.test(n) ? n : null;
}

function num(p, k) {
  const v = Number(p.get(k));
  return Number.isFinite(v) ? v : NaN;
}

async function readForm(req) {
  const ct = req.headers.get("content-type") || "";
  const body = await req.text();
  if (body.length > 4096) return null;
  if (ct.includes("json")) {
    try { return new URLSearchParams(Object.entries(JSON.parse(body)).map(([k, v]) => [k, String(v)])); } catch { return null; }
  }
  return new URLSearchParams(body);
}

async function rateLimitIp(env, req) {
  const ip = req.headers.get("cf-connecting-ip") || "local";
  const key = (await sha256("aimforge:" + ip)).slice(0, 32);
  const t = now(), win = t - (t % IP_WINDOW_S);
  const row = await env.DB.prepare("SELECT win, n FROM ip_rate WHERE ip = ?").bind(key).first();
  if (row && row.win === win && row.n >= IP_MAX) return false;
  if (!row || row.win !== win) await env.DB.prepare("INSERT OR REPLACE INTO ip_rate (ip, win, n) VALUES (?, ?, 1)").bind(key, win).run();
  else await env.DB.prepare("UPDATE ip_rate SET n = n + 1 WHERE ip = ?").bind(key).run();
  return true;
}

// Creates the player on first contact, checks the secret afterwards, applies nickname changes.
async function authPlayer(env, p) {
  const pid = p.get("pid") || "", secret = p.get("secret") || "";
  if (!PID_RE.test(pid) || !SECRET_RE.test(secret)) return { e: err("bad_request") };
  const name = cleanName(p.get("name"));
  if (!name) return { e: err("bad_name") };
  const key = name.toLowerCase();
  const hash = await sha256(secret);
  const pl = await env.DB.prepare("SELECT * FROM players WHERE pid = ?").bind(pid).first();
  if (!pl) {
    const taken = await env.DB.prepare("SELECT pid FROM players WHERE name_key = ?").bind(key).first();
    if (taken) return { e: err("name_taken", 409) };
    try {
      await env.DB.prepare("INSERT INTO players (pid, secret_hash, name, name_key, created) VALUES (?, ?, ?, ?, ?)")
        .bind(pid, hash, name, key, now()).run();
    } catch { return { e: err("name_taken", 409) }; }
    return { pl: { pid, name, last_submit: 0, banned: 0 } };
  }
  if (pl.secret_hash !== hash) return { e: err("auth", 403) };
  if (pl.banned) return { e: err("banned", 403) };
  if (pl.name !== name) {
    const taken = await env.DB.prepare("SELECT pid FROM players WHERE name_key = ? AND pid <> ?").bind(key, pid).first();
    if (taken) return { e: err("name_taken", 409) };
    await env.DB.prepare("UPDATE players SET name = ?, name_key = ? WHERE pid = ?").bind(name, key, pid).run();
    pl.name = name;
  }
  return { pl };
}

// Server-side sanity checks. A native client can always be tampered with; these make
// forged numbers at least internally consistent and humanly possible.
function plausible(p) {
  const mode = p.get("mode");
  const score = num(p, "score"), acc = num(p, "acc"), kills = num(p, "kills"), shots = num(p, "shots"),
        hits = num(p, "hits"), ttk = num(p, "ttk"), dur = num(p, "dur"), trackOn = num(p, "track_on");
  if ([score, acc, kills, dur].some(Number.isNaN)) return "bad_request";
  if (dur < 10 || dur > 300 || score < 0 || acc < 0 || acc > 1 || kills < 0) return "implausible";
  if (kills / dur > 12) return "implausible";
  if (mode === "click") {
    if ([shots, hits, ttk].some(Number.isNaN)) return "bad_request";
    if (hits > shots || kills > hits || shots / dur > 25) return "implausible";
    if (shots > 0 && Math.abs(acc - hits / shots) > 0.002) return "implausible";
    if (score > 200 * kills * acc + 1 || score < 100 * kills * acc - 1) return "implausible";
    if (kills >= 5 && ttk < 30) return "implausible";
  } else if (mode === "track") {
    if (Number.isNaN(trackOn) || trackOn < 0 || trackOn > dur + 0.05) return "implausible";
    if (score > 100 * dur + 1 || Math.abs(score - 100 * trackOn) > 2) return "implausible";
  } else return "bad_request";
  return null;
}

async function rankOf(env, s, r, score) {
  const row = await env.DB.prepare(
    "SELECT (SELECT COUNT(*) FROM best WHERE scenario = ?1 AND ruleset = ?2 AND score > ?3) + 1 AS rank, " +
    "(SELECT COUNT(*) FROM best WHERE scenario = ?1 AND ruleset = ?2) AS total").bind(s, r, score).first();
  return row;
}

async function submit(env, req) {
  const p = await readForm(req);
  if (!p) return err("bad_request");
  const s = p.get("s") || "", r = p.get("r") || "";
  if (!SCEN_RE.test(s) || !RULE_RE.test(r)) return err("bad_request");
  const bad = plausible(p);
  if (bad) return err(bad);
  if (!(await rateLimitIp(env, req))) return err("rate_limited", 429);
  const { pl, e } = await authPlayer(env, p);
  if (e) return e;
  const t = now();
  if (t - pl.last_submit < PID_COOLDOWN_S) return err("rate_limited", 429);

  const score = Math.round(num(p, "score") * 10) / 10;
  const acc = num(p, "acc"), kills = Math.round(num(p, "kills")), ttk = num(p, "ttk") || 0;
  const ver = (p.get("ver") || "").slice(0, 16);
  const old = await env.DB.prepare("SELECT score FROM best WHERE scenario = ? AND ruleset = ? AND pid = ?").bind(s, r, pl.pid).first();
  const improved = !old || score > old.score;
  const stmts = [env.DB.prepare("UPDATE players SET last_submit = ?, runs = runs + 1 WHERE pid = ?").bind(t, pl.pid)];
  if (!old) {
    stmts.push(env.DB.prepare("INSERT INTO best (scenario, ruleset, pid, score, acc, kills, ttk, runs, at, ver) VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?, ?)")
      .bind(s, r, pl.pid, score, acc, kills, ttk, t, ver));
  } else if (improved) {
    stmts.push(env.DB.prepare("UPDATE best SET score = ?, acc = ?, kills = ?, ttk = ?, runs = runs + 1, at = ?, ver = ? WHERE scenario = ? AND ruleset = ? AND pid = ?")
      .bind(score, acc, kills, ttk, t, ver, s, r, pl.pid));
  } else {
    stmts.push(env.DB.prepare("UPDATE best SET runs = runs + 1 WHERE scenario = ? AND ruleset = ? AND pid = ?").bind(s, r, pl.pid));
  }
  await env.DB.batch(stmts);
  const best = improved ? score : old.score;
  const rk = await rankOf(env, s, r, best);
  return ok([rk.rank, rk.total, best, improved ? 1 : 0].join("\t"));
}

async function top(env, url) {
  const s = url.searchParams.get("s") || "", r = url.searchParams.get("r") || "", pid = url.searchParams.get("pid") || "";
  if (!SCEN_RE.test(s) || !RULE_RE.test(r)) return err("bad_request");
  const n = Math.max(1, Math.min(MAX_ROWS, parseInt(url.searchParams.get("n") || "50", 10) || 50));
  const res = await env.DB.prepare(
    "SELECT b.pid, p.name, b.score, b.acc, b.kills, b.at FROM best b JOIN players p ON p.pid = b.pid " +
    "WHERE b.scenario = ? AND b.ruleset = ? ORDER BY b.score DESC, b.at ASC LIMIT ?").bind(s, r, n).all();
  const rows = (res.results || []);
  let total = 0, myRank = 0, myBest = 0;
  const tot = await env.DB.prepare("SELECT COUNT(*) AS c FROM best WHERE scenario = ? AND ruleset = ?").bind(s, r).first();
  total = tot ? tot.c : 0;
  if (PID_RE.test(pid)) {
    const mine = await env.DB.prepare("SELECT score FROM best WHERE scenario = ? AND ruleset = ? AND pid = ?").bind(s, r, pid).first();
    if (mine) { myBest = mine.score; myRank = (await rankOf(env, s, r, mine.score)).rank; }
  }
  // ties share a rank
  let rank = 0, prev = null;
  const out = rows.map((x, i) => {
    if (x.score !== prev) { rank = i + 1; prev = x.score; }
    return [rank, x.name, x.score, x.acc.toFixed(4), x.kills, x.at, x.pid === pid ? 1 : 0];
  });
  return okLines([total, myRank, myBest].join("\t"), out);
}

async function summary(env, url) {
  const pid = url.searchParams.get("pid") || "";
  if (!PID_RE.test(pid)) return err("bad_request");
  const res = await env.DB.prepare(
    "SELECT b.scenario, b.ruleset, b.score, " +
    "(SELECT COUNT(*) FROM best x WHERE x.scenario = b.scenario AND x.ruleset = b.ruleset AND x.score > b.score) + 1 AS rank, " +
    "(SELECT COUNT(*) FROM best y WHERE y.scenario = b.scenario AND y.ruleset = b.ruleset) AS total " +
    "FROM best b WHERE b.pid = ?").bind(pid).all();
  return okLines("", (res.results || []).map(x => [x.scenario, x.ruleset, x.rank, x.total, x.score]));
}

async function ban(env, req) {
  const auth = req.headers.get("authorization") || "";
  if (!env.ADMIN_TOKEN || auth !== "Bearer " + env.ADMIN_TOKEN) return err("forbidden", 403);
  const p = await readForm(req);
  let pid = p && p.get("pid");
  const name = p && p.get("name");
  if (!pid && name) {
    const row = await env.DB.prepare("SELECT pid FROM players WHERE name_key = ?").bind(name.trim().toLowerCase()).first();
    pid = row && row.pid;
  }
  if (!pid || !PID_RE.test(pid)) return err("not_found", 404);
  await env.DB.batch([
    env.DB.prepare("UPDATE players SET banned = 1 WHERE pid = ?").bind(pid),
    env.DB.prepare("DELETE FROM best WHERE pid = ?").bind(pid),
  ]);
  return ok("banned");
}

function downloadUrl(env) {
  const u = String(env.DOWNLOAD_URL || "").trim();
  return /^https?:\/\/[^\s\t]+$/.test(u) ? u : DEFAULT_DOWNLOAD;
}

function version(env) {
  const v = String(env.LATEST_VERSION || "").trim();
  return ok([VER_RE.test(v) ? v : "", downloadUrl(env)].join("\t"));
}

const esc = (s) => String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

async function web(env, url) {
  const boards = await env.DB.prepare(
    "SELECT scenario, ruleset, COUNT(*) AS n, MAX(at) AS last FROM best GROUP BY scenario, ruleset ORDER BY last DESC LIMIT 60").all();
  const list = boards.results || [];
  let s = url.searchParams.get("s"), r = url.searchParams.get("r");
  if ((!s || !r) && list.length) { s = list[0].scenario; r = list[0].ruleset; }
  let rows = [];
  if (s && r && SCEN_RE.test(s) && RULE_RE.test(r)) {
    rows = (await env.DB.prepare(
      "SELECT p.name, b.score, b.acc, b.kills, b.at FROM best b JOIN players p ON p.pid = b.pid WHERE b.scenario = ? AND b.ruleset = ? ORDER BY b.score DESC, b.at ASC LIMIT 100")
      .bind(s, r).all()).results || [];
  }
  const nav = list.map(b => `<a class="${b.scenario === s && b.ruleset === r ? "on" : ""}" href="?s=${esc(b.scenario)}&r=${esc(b.ruleset)}">${esc(b.scenario)}<span>${b.n}</span></a>`).join("");
  const body = rows.map((x, i) => `<tr><td>${i + 1}</td><td>${esc(x.name)}</td><td class="n">${Math.round(x.score).toLocaleString("ru-RU")}</td><td class="n">${(x.acc * 100).toFixed(1)}%</td><td class="n">${x.kills}</td><td class="d">${new Date(x.at * 1000).toLocaleDateString("ru-RU")}</td></tr>`).join("");
  const html = `<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AimForge — рейтинг</title><style>
:root{color-scheme:dark}body{margin:0;background:#0b0d12;color:#e8ecf3;font:15px/1.4 system-ui,Segoe UI,sans-serif}
.w{max-width:980px;margin:0 auto;padding:28px 16px}h1{font-size:28px;margin:0 0 4px}h1 b{color:#36dcff}p{color:#8c96a8;margin:0 0 20px}
nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:18px}nav a{color:#8c96a8;text-decoration:none;background:#161a23;padding:6px 10px;border-radius:8px}
nav a.on{background:#145c70;color:#fff}nav span{margin-left:6px;opacity:.6;font-size:12px}
table{width:100%;border-collapse:collapse;background:#161a23;border-radius:12px;overflow:hidden}td,th{padding:10px 12px;text-align:left}
th{font-size:12px;color:#58617a;text-transform:uppercase}tr:nth-child(even) td{background:#12151c}.n{font-family:ui-monospace,Consolas,monospace;text-align:right}
.d{color:#8c96a8}tr:nth-child(-n+3) td:first-child{color:#ffbe46;font-weight:700}</style></head><body><div class="w">
<h1>AIM <b>FORGE</b></h1><p>Онлайн-рейтинг · лучший результат каждого игрока</p><nav>${nav || "<span>Пока пусто</span>"}</nav>
${s ? `<table><tr><th>#</th><th>Игрок</th><th class="n">Счёт</th><th class="n">Точность</th><th class="n">Убийств</th><th>Дата</th></tr>${body}</table>` : ""}
</div></body></html>`;
  return new Response(html, { headers: { "content-type": "text/html; charset=utf-8" } });
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";
    try {
      if (req.method === "OPTIONS") return new Response(null, { headers: { ...TEXT, "access-control-allow-methods": "GET, POST", "access-control-allow-headers": "content-type" } });
      if (req.method === "GET" && (path === "/" || path === "/health")) return ok("aimforge\t" + API_VERSION);
      if (req.method === "GET" && path === "/top") return await top(env, url);
      if (req.method === "GET" && path === "/summary") return await summary(env, url);
      if (req.method === "GET" && path === "/web") return await web(env, url);
      if (req.method === "GET" && path === "/version") return version(env);
      if (req.method === "GET" && path === "/download") return Response.redirect(downloadUrl(env), 302);
      if (req.method === "POST" && path === "/submit") return await submit(env, req);
      if (req.method === "POST" && path === "/register") {
        const p = await readForm(req);
        if (!p) return err("bad_request");
        if (!(await rateLimitIp(env, req))) return err("rate_limited", 429);
        const { pl, e } = await authPlayer(env, p);
        return e || ok(pl.name);
      }
      if (req.method === "POST" && path === "/admin/ban") return await ban(env, req);
      return err("not_found", 404);
    } catch (x) {
      console.error(x);
      return err("server", 500);
    }
  },
};
