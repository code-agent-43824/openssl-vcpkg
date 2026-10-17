# Состояние

На 2026-10-17 workflow `build` собирает статический OpenSSL для `x86-windows-static`,
`x64-windows-static` и `arm64-windows-static`, Debug и Release. Последний проверенный прогон —
[run 1](https://github.com/code-agent-43824/openssl-vcpkg/actions/runs/39038950692) на `37461ec`:
зелёный, около 6 минут на триплет, порт `openssl` 3.6.5#2.

Проверено по скачанным артефактам: все объектники в `.lib` — нужной архитектуры; флаги
`/MT` (Release), `/MTd` (Debug) и `/Zl`; DLL нет. Артефакты этого прогона хранятся до 2027-01-15.

Smoke-тестов нет — это этап 2 из `docs/ROADMAP.md`, он ждёт проверки этапа 1 владельцем.
