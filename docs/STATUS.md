# Состояние

На 2026-10-17 workflow `ci` собирает OpenSSL в двух вариантах — статические библиотеки
(`<arch>-windows-static`) и DLL со статическим CRT (`<arch>-windows-dll-mt`) — для x86, x64
и arm64, Debug и Release; прогоняет smoke-тесты и публикует релиз. Последний проверенный
прогон — [run 39041642886](https://github.com/code-agent-43824/openssl-vcpkg/actions/runs/39041642886)
на `de4f9a9`: все 19 джоб зелёные.

- **Сборка:** порт `openssl` 3.6.5#2. Проверки vcpkg после сборки DLL со статическим CRT прошли
  без замечаний.
- **Smoke:** все 12 комбинаций прошли все проверки `smoke/smoke.c`. У каждого бинарника
  нужная архитектура; DLL импортируют только системные DLL (`libssl` и `legacy` — ещё
  `libcrypto`); BIO на stdout приложения в DLL-варианте работает через `applink.c`.
- **Релизы:** [`openssl-3.6.5-port2-r2`](https://github.com/code-agent-43824/openssl-vcpkg/releases/tag/openssl-3.6.5-port2-r2)
  — 6 архивов и `SHA256SUMS`, хеши в заметках совпадают с дайджестами ассетов, посчитанными
  GitHub. [`openssl-3.6.5-port2`](https://github.com/code-agent-43824/openssl-vcpkg/releases/tag/openssl-3.6.5-port2)
  — первый релиз, только статические библиотеки.

В работе: каждая smoke-джоба ждёт только свою сборку (`docs/PLAN.md`). Не проверено: пропуск
публикации, когда релиз с этой версией порта и ревизией уже есть.
