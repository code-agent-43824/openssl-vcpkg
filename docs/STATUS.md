# Состояние

На 2026-10-17 workflow `ci` собирает статический OpenSSL для `x86-windows-static`,
`x64-windows-static` и `arm64-windows-static` (Debug и Release), прогоняет smoke-тесты и
публикует релиз. Последний проверенный прогон —
[run 39040385332](https://github.com/code-agent-43824/openssl-vcpkg/actions/runs/39040385332)
на `c7ff420`: все 10 джоб зелёные.

- **Сборка:** порт `openssl` 3.6.5#2; по артефактам первого прогона проверены архитектура
  объектников и флаги `/MT` / `/MTd` / `/Zl` (`docs/JOURNAL.md`).
- **Smoke:** все 6 комбинаций прошли все проверки `smoke/smoke.c`; arm64 — нативно
  на `windows-11-arm`. У каждого exe нужная архитектура и импорт только системных DLL:
  `WS2_32`, `ADVAPI32`, `CRYPT32`, `KERNEL32`, плюс `USER32` у x86 и x64.
- **Релиз:** [`openssl-3.6.5-port2`](https://github.com/code-agent-43824/openssl-vcpkg/releases/tag/openssl-3.6.5-port2).
  Три zip и `SHA256SUMS`; хеши в заметках совпадают с дайджестами ассетов, которые посчитал
  GitHub. Коммит исходников в заметках сверен с тегом `openssl-3.6.5` в `openssl/openssl`.

Не проверено: пропуск публикации, когда релиз этой версии порта уже есть. Сработает
на следующем прогоне без смены версии.
