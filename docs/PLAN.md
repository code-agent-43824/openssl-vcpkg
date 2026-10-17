# План — этап 3: DLL со статическим CRT

Цель этапа — из `docs/ROADMAP.md`: кроме статических библиотек собирать DLL, которые тоже
зависят только от системных DLL Windows, тестировать их и выпускать в релизе.

- [ ] Оверлейные триплеты `triplets/<arch>-windows-dll-mt.cmake` (DLL + статический CRT),
      подключены через `vcpkg-configuration` в `vcpkg.json`.
- [ ] `ci.yml`: сборка и smoke по двум вариантам линковки — `static` и `dll-mt`.
- [ ] `smoke/run.ps1 -Linkage`: для `dll-mt` — `applink.c` в exe, DLL рядом с exe; архитектура
      и импорты проверяются у exe и у каждой DLL (только системные DLL; exe обязан
      импортировать `libcrypto` и `libssl`).
- [ ] `smoke/smoke.c`: ошибки печатаются без передачи `FILE*` в библиотеку; новая проверка —
      BIO на stdout приложения (в DLL-варианте она проходит только через applink).
- [ ] Ревизия сборки `release/revision` в теге: `openssl-<версия>-port<N>-r<ревизия>`.
      Первый релиз с DLL — r2: релиз `openssl-3.6.5-port2` той же версии порта уже есть.
- [ ] Прогон зелёный, релиз с 6 архивами проверен.
- [ ] `CLAUDE.md`, `docs/STATUS.md`.
