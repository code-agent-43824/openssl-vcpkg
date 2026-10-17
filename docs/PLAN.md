# План — этап 1: сборка

Цель этапа — из `docs/ROADMAP.md`: workflow собирает OpenSSL для x86, x64 и arm64 в Debug
и Release, результат — артефакты workflow.

- [ ] `vcpkg.json`: зависимость `openssl` и `builtin-baseline` — коммит `microsoft/vcpkg`.
      Тем же коммитом CI пинует сам vcpkg, так что версия задаётся в одном месте.
- [ ] `.github/workflows/build.yml`: матрица из триплетов `<arch>-windows-static` на x64-раннере
      (arm64 — кросс-компиляцией); один `vcpkg install` даёт Debug и Release; артефакт
      на триплет; при сбое — хвосты логов сборки vcpkg в лог джобы.
- [ ] Прогон CI зелёный; по логам и составу артефактов видно, что собраны обе конфигурации
      всех трёх архитектур (`lib/` и `debug/lib/`).
- [ ] `CLAUDE.md`: разделы Commands и Map of the code.
