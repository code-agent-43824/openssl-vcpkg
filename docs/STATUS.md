# Состояние

На 2026-10-17 workflow `ci` собирает OpenSSL в двух вариантах — статические библиотеки
(`<arch>-windows-static`) и DLL со статическим CRT (`<arch>-windows-dll-mt`) — для x86, x64
и arm64, Debug и Release; прогоняет smoke-тесты и публикует релиз. Последний проверенный
прогон — [run 39043013638](https://github.com/code-agent-43824/openssl-vcpkg/actions/runs/39043013638)
на `f63cd69`: все 13 джоб зелёные.

- **Схема:** у каждого из 6 вариантов своя пара «build → smoke»; smoke стартует через секунды
  после своей сборки, не дожидаясь остальных (сверено по времени джоб в run 39042518782).
- **Сборка:** порт `openssl` 3.6.5#2. Проверки vcpkg после сборки DLL со статическим CRT прошли
  без замечаний.
- **Smoke:** все 12 комбинаций прошли все проверки `smoke/smoke.c`. У каждого бинарника
  нужная архитектура; DLL импортируют только системные DLL (`libssl` и `legacy` — ещё
  `libcrypto`); BIO на stdout приложения в DLL-варианте работает через `applink.c`.
- **Релизы:** [`openssl-3.6.5-port2-r2`](https://github.com/code-agent-43824/openssl-vcpkg/releases/tag/openssl-3.6.5-port2-r2)
  — 6 архивов и `SHA256SUMS`, хеши в заметках совпадают с дайджестами ассетов, посчитанными
  GitHub. [`openssl-3.6.5-port2`](https://github.com/code-agent-43824/openssl-vcpkg/releases/tag/openssl-3.6.5-port2)
  — первый релиз, только статические библиотеки.

Пропуск публикации проверен: прогоны на `c5362d5` и `f63cd69` нашли `openssl-3.6.5-port2-r2`
и новый релиз не создали.

В работе: функциональные тесты (`docs/PLAN.md`).
