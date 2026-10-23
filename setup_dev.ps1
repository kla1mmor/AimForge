# AimForge — one-time developer setup on Windows.
# Turns this folder into the git repository (keeps your exe, settings, scores, admin token),
# installs Git / Python / Node.js if missing, fetches the code + raylib, builds AimForge.exe.
# Run:  double-click setup.bat   (or: powershell -ExecutionPolicy Bypass -File setup_dev.ps1)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$Repo = 'https://github.com/kla1mmor/AimForge.git'

function Say($m)  { Write-Host "`n== $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "   $m" -ForegroundColor Green }
function Has($c)  { $null -ne (Get-Command $c -ErrorAction SilentlyContinue) }
function RefreshPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
}
function WingetInstall($id, $name) {
    if (-not (Has winget)) { throw "Нет winget. Установи $name вручную и запусти скрипт снова." }
    Write-Host "   Устанавливаю $name..."
    winget install --id $id -e --silent --accept-package-agreements --accept-source-agreements | Out-Host
    RefreshPath
}
function PythonCmd {
    foreach ($c in @('py', 'python', 'python3')) {
        if (Has $c) {
            try { & $c -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" 2>$null; if ($LASTEXITCODE -eq 0) { return $c } } catch {}
        }
    }
    return $null
}

try {
    Say '1/5 Инструменты'
    if (-not (Has git)) { WingetInstall 'Git.Git' 'Git for Windows' }
    Ok ("git: " + (git --version))
    $py = PythonCmd
    if (-not $py) { WingetInstall 'Python.Python.3.12' 'Python 3.12'; $py = PythonCmd }
    if (-not $py) { throw 'Python не найден после установки. Перезапусти скрипт в новом окне.' }
    Ok ("python: " + (& $py --version))
    if (-not (Has node)) { WingetInstall 'OpenJS.NodeJS.LTS' 'Node.js LTS (для публикации сервера)' }
    if (Has node) { Ok ("node: " + (node --version)) }

    Say '2/5 Компилятор zig'
    & $py -m pip install --user --disable-pip-version-check -q ziglang==0.17.0
    Ok ("zig: " + (& $py -m ziglang version))

    Say '3/5 Репозиторий в этой папке'
    if (-not (Test-Path .git)) {
        git init -q -b main
        git remote add origin $Repo
    }
    Write-Host '   Если откроется окно входа GitHub — войди как kla1mmor (репозиторий приватный).'
    git fetch origin main
    # -f: файлы, которые уже лежат здесь (README.txt, server\*.js ...), заменяются версиями из репозитория.
    # Твои aimforge_*.cfg / *.csv, AimForge.exe и server\admin_token.txt не трогаются (они в .gitignore).
    git checkout -f -B main origin/main
    git branch --set-upstream-to=origin/main main | Out-Null
    git submodule update --init --recursive
    if (-not (git config user.name)) { git config user.name 'kla1mmor' }
    if (-not (git config user.email)) { git config user.email 'alikhan.tok@gmail.com' }
    Ok ('ветка ' + (git rev-parse --abbrev-ref HEAD) + ' @ ' + (git rev-parse --short HEAD))

    Say '4/5 Сборка AimForge.exe'
    $bash = @("$env:ProgramFiles\Git\bin\bash.exe", "${env:ProgramFiles(x86)}\Git\bin\bash.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $bash) { throw 'Не найден Git Bash (C:\Program Files\Git\bin\bash.exe).' }
    $env:PYTHON = $py
    & $bash -lc "cd '$($PWD.Path -replace '\\','/')' && sh build_windows.sh"
    if ($LASTEXITCODE -ne 0) { throw 'Сборка не удалась (см. вывод выше).' }
    if (Get-Process AimForge -ErrorAction SilentlyContinue) {
        Write-Host '   AimForge запущен — exe в корне не заменён. Закрой игру и скопируй build\win\AimForge.exe сам.' -ForegroundColor Yellow
    } else {
        Copy-Item build\win\AimForge.exe .\AimForge.exe -Force
        Ok 'AimForge.exe обновлён в корне папки'
    }

    Say '5/5 Готово'
    Write-Host @"

   Дальше:
   1. Открой приложение Claude -> вкладка Code.
   2. Окружение: Local. Папка: Open folder... -> $($PWD.Path)
   3. Напиши первое сообщение, например:
      «Прочитай CLAUDE.md и docs/USER_GUIDE.md. Проверь Alt+Tab в версии 1.2.1 на моём ПК и, если всё ок, выпусти релиз v1.2.1».

"@ -ForegroundColor Green
} catch {
    Write-Host "`nОшибка: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
