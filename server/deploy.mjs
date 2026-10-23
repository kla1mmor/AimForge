// One-shot deploy of the AimForge leaderboard to Cloudflare (free plan).
// Run:  node deploy.mjs      (deploy.bat does this on Windows)
import { execSync, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
process.chdir(here);
const WR = "npx --yes wrangler@4";
const say = (m) => console.log("\n\x1b[36m== " + m + "\x1b[0m");
const sh = (cmd, input) => execSync(cmd, { encoding: "utf8", stdio: ["pipe", "pipe", "pipe"], input });
const interactive = (cmd) => {
  const r = spawnSync(cmd, { stdio: "inherit", shell: true });
  if (r.status !== 0) throw new Error("Команда завершилась с ошибкой: " + cmd);
};
const UUID = /[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/;

try {
  say("1/5 Вход в Cloudflare");
  let who = "";
  try { who = sh(`${WR} whoami`); } catch (e) { who = String(e.stdout || "") + String(e.stderr || ""); }
  if (/not authenticated|You are not logged in/i.test(who)) {
    console.log("Откроется браузер — войди или зарегистрируйся (бесплатно, карта не нужна) и нажми Allow.");
    interactive(`${WR} login`);
  } else console.log("Уже вошли.");

  say("2/5 База данных D1 «aimforge»");
  let id = null;
  try {
    const list = JSON.parse(sh(`${WR} d1 list --json`));
    const db = list.find((d) => d.name === "aimforge");
    if (db) id = db.uuid || db.database_id;
  } catch { /* fall through and create */ }
  if (!id) {
    const out = sh(`${WR} d1 create aimforge`);
    const m = out.match(UUID);
    if (!m) throw new Error("Не удалось создать базу:\n" + out);
    id = m[0];
    console.log("Создана база " + id);
  } else console.log("База уже есть: " + id);
  const tomlPath = path.join(here, "wrangler.toml");
  fs.writeFileSync(tomlPath, fs.readFileSync(tomlPath, "utf8").replace(/database_id\s*=\s*"[^"]*"/, `database_id = "${id}"`));

  say("3/5 Таблицы");
  sh(`${WR} d1 execute aimforge --remote --file schema.sql --yes`);
  console.log("Готово.");

  say("4/5 Публикация сервера");
  let url = null, out = "";
  try {
    out = sh(`${WR} deploy`);
    process.stdout.write(out);
  } catch (e) {
    out = String(e.stdout || "") + String(e.stderr || "");
    // usually: first deploy needs a workers.dev subdomain — rerun interactively so wrangler can ask for it
    console.log("Нужен ответ на вопрос wrangler (например, имя поддомена workers.dev):");
    interactive(`${WR} deploy`);
  }
  const m = out.match(/https:\/\/[a-z0-9.-]+\.workers\.dev/i);
  if (m) url = m[0];

  say("5/5 Ключ администратора (для бана читеров)");
  const tokPath = path.join(here, "admin_token.txt");
  if (!fs.existsSync(tokPath)) {
    const tok = randomBytes(24).toString("hex");
    sh(`${WR} secret put ADMIN_TOKEN`, tok + "\n");
    fs.writeFileSync(tokPath, tok + "\n");
    console.log("Сохранён в admin_token.txt (никому не показывай).");
  } else console.log("Уже задан (admin_token.txt).");

  console.log("\n\x1b[32m============================================================\x1b[0m");
  if (url) {
    fs.writeFileSync(path.join(here, "server_url.txt"), url + "\n");
    console.log("  ГОТОВО! Адрес твоего сервера рейтинга:\n\n    " + url + "\n");
    console.log("  Вставь его в AimForge → Настройки → Онлайн → «Адрес сервера»");
    console.log("  и отправь друзьям. Веб-страница рейтинга: " + url + "/web");
  } else {
    console.log("  ГОТОВО! Скопируй адрес вида https://aimforge-leaderboard.<имя>.workers.dev");
    console.log("  из вывода выше и вставь его в AimForge → Настройки → Онлайн.");
  }
  console.log("\x1b[32m============================================================\x1b[0m");
} catch (e) {
  console.error("\n\x1b[31mОшибка:\x1b[0m " + (e.message || e));
  console.error("Можно развернуть вручную через сайт Cloudflare — см. DEPLOY.txt, вариант Б.");
  process.exit(1);
}
