# AimForge

Тренажёр аима для Windows в духе KovaaK's / Aim Lab: отдельный `.exe`, Raw Input без задержки, 15 сценариев (флики, рефлекс, переводка, трекинг), редактор прицела и целей, чувствительность 1:1 с Valorant / CS2 / Overwatch 2 и бесплатный онлайн-рейтинг на Cloudflare.

- **Скачать:** раздел [Releases](../../releases) → `AimForge-win64.zip`
- **Руководство пользователя:** [docs/USER_GUIDE.md](docs/USER_GUIDE.md)
- **Онлайн-рейтинг в браузере:** https://aimforge-leaderboard.kla1mmor.workers.dev/web

## Структура

| Путь | Что там |
|---|---|
| `src/` | игра на C (raylib 5.5, OpenGL 3.3): `main.c` — интерфейс и цикл, `game.c` — сценарии и попадания, `platform_win.c` — Raw Input и таймер, `net.c`/`online.c` — рейтинг |
| `server/` | сервер рейтинга: Cloudflare Worker (`worker.js`) + D1 (`schema.sql`) |
| `docs/` | руководство пользователя |
| `assets/fonts/` | шрифты (встраиваются в exe при сборке) |
| `deps/raylib/` | raylib 5.5 (git-подмодуль) |

## Сборка

Нужны Python 3 и [zig](https://ziglang.org) (`pip install ziglang`).

```sh
git clone --recursive https://github.com/kla1mmor/AimForge
cd AimForge
sh build_windows.sh          # → build/win/AimForge.exe (кросс-компиляция, работает и на Linux/macOS)
```

GitHub Actions собирает exe на каждый пуш (артефакт `AimForge-win64`), а на тег `v*` публикует релиз.

## Сервер

Официальный сервер уже развёрнут, его адрес вшивается в сборку из `server/server_url.txt`. Свой сервер: `server/DEPLOY.txt`.

Шрифты: Montserrat, JetBrains Mono (SIL OFL). Движок: raylib (zlib).
