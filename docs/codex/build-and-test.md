# Сборка, тестирование и команды риска

Назначение: канонический реестр подтверждённых форм команд, окружения, side effects и минимальной проверки по компонентам.

Проверено по CMake/scripts/CI: 2026-07-22. Qt 5 wrapper `C:\programming\qt_build.ps1` и отдельный Qt 6 wrapper `C:\programming\qt6_build.ps1` подтверждены для Release + RelWithDebInfo с CEF ON и XR Auto ON: compile/link/runtime-copy успешны, manifest `success=true`, полный обязательный CEF runtime присутствует в обеих canonical конфигурациях. Qt 6 startup smoke выполнен с `--desktop` и `SUBSTRATA_ENABLE_CEF=false`; браузер не запускался. Windows server, Linux server и production не запускались. `build` ниже означает запись generated artifacts, даже если source не меняется.

Историческое примечание Phase 2: при документационной миграции существовавшие command blocks сохранялись без изменения и не выполнялись. Это ограничение не относится к последующему датированному build evidence ниже.

## Требования окружения

| Требование | Подтверждение / роль |
| --- | --- |
| CMake, Ruby, Git | root configure вызывает Ruby config; dependency/build scripts Ruby |
| C++17 | `cmake/shared_cxx_settings.cmake` |
| `GLARE_CORE_LIBS` | root внешних libraries/source trees |
| `WINTER_DIR` | Winter engine source/install |
| `GLARE_CORE_TRUNK_DIR` | glare-core source |
| `CYBERSPACE_OUTPUT` | runtime/build output root |
| Qt | Windows 5.15.16; macOS/Linux 5.15.10 в `scripts/config-lib.rb` |
| VS2022 x64 | canonical Windows generator/local wrapper |
| SDL2 | alternative native и обязательный Emscripten path |
| LLVM 15, LibreSSL, libjpeg-turbo | root CMake dependencies |
| Jolt 5.3.0, Luau 0.627 | fetched by `scripts/get_libs.rb` into external dependency root |
| Optional CEF | compile-time browser/webview helper; canonical Windows Qt wrapper default ON |
| Optional OpenXR SDK | `XR_SUPPORT=ON`; must contain `cmake/OpenXRConfig.cmake` |
| Emscripten/Ninja | webclient build only; separate SDL/libjpeg Emscripten builds |

Current host has `C:\programming\qt_build.ps1`, `substrata_build_qt`, `substrata_output_qt`, OpenXR SDK and a VS2022-ready CEF 139.0.40 binary distribution at `D:\cef\binary_distrib\cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64`. The Qt 6 launcher `C:\programming\qt6_build.ps1` delegates to the tracked `scripts/qt6_build.ps1`; its isolated tree/output are `substrata_build_qt6`/`substrata_output_qt6`. Qt 6.11.1 MSVC 2022 is installed at `C:\Qt\6.11.1\msvc2022_64` and matches the CEF/native ABI. The Qt 6 full build and CEF runtime staging are confirmed; `sdl_build.ps1` is absent.

Разделение веток и output зафиксировано в [qt-build-policy.md](qt-build-policy.md): `master` всегда Qt 5 через `qt_build.ps1`, `qt6-integration` всегда Qt 6 через отдельный `qt6_build.ps1` и `substrata_build_qt6`. Qt 5 workflow не переключается на Qt 6 автоматически.

## Быстрые read-only проверки

```powershell
git -C C:\programming\substrata status --short --branch
git -C C:\programming\substrata branch -vv
rg -n "SymbolOrRoute" C:\programming\substrata
python C:\programming\substrata\scripts\analyze_xr_pose_trace.py [optional-trace.csv]
```

Последняя Python-команда только читает trace и печатает анализ. Для Markdown-задачи минимальная проверка — relative links/path existence + `git diff --check`; C++ build не нужен.

## Windows native: канонический local wrapper

Wrapper находится вне Git repo и проверен чтением на audit host.

Статус последнего подтверждения: **CONFIRMED, 2026-07-22** для полной `Release` + `RelWithDebInfo`, CEF ON, XR Auto ON сборки.

### Жёсткий инвариант пользовательского runtime

Владелец всегда запускает `RelWithDebInfo` клиент только отсюда:

```text
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe
```

`C:\programming\substrata_output` — legacy mirror/вспомогательный output. Наличие там более нового EXE не доказывает, что изменение доступно владельцу. Windows Qt build/UI verification считается успешной только если:

1. `C:\programming\substrata_output_qt\build_manifest.json` содержит `success: true` для требуемой конфигурации;
2. обновился timestamp/размер или SHA-256 именно канонического `RelWithDebInfo\gui_client.exe`;
3. runtime проверка запускает этот точный путь.

`CYBERSPACE_OUTPUT` применяется во время CMake configure и запекается в Visual Studio project. Поэтому после сомнения в output path нужно запускать `qt_build.ps1` без `-SkipConfigure`; один успешный `cmake --build` может обновить другой output и не завершает пользовательскую задачу.

CEF path должен указывать на leaf binary distribution с `include`, `Release`, `Resources` и собранным `libcef_dll_wrapper`, а не на полный Chromium checkout `D:\cef\chromium_git`. Canonical wrapper включает CEF по умолчанию, передаёт и environment, и CMake cache path, проверяет `/MD`-совместимый wrapper, фактический CMake cache/generated `OutDir`/configure stamp и весь runtime-набор. Qt/CEF/OpenXR и source resource copies сверяются по SHA-256. Явный opt-out: `-CEF Off`.

### Предстартовая проверка wrapper и защита build-tree

`qt_build.ps1` и CMake-конфигурация вызывают `ruby` по имени. На текущем компьютере подтверждён `C:\Ruby34-x64\bin\ruby.exe`, но каталог может отсутствовать в `PATH` нового PowerShell-процесса. Перед wrapper выполнить:

```powershell
if(-not (Get-Command ruby -ErrorAction SilentlyContinue)) {
    $rubyDir = 'C:\Ruby34-x64\bin'
    if(-not (Test-Path -LiteralPath (Join-Path $rubyDir 'ruby.exe'))) {
        throw 'Ruby is required, but C:\Ruby34-x64\bin\ruby.exe was not found.'
    }
    $env:Path = $rubyDir + ';' + $env:Path
}
ruby --version
```

Не интерпретировать ошибку `ruby is not recognized` как проблему C++-исходников или CMake. Не менять системный `PATH` ради одной сборки: достаточно окружения текущего процесса.

В одном `C:\programming\substrata_build_qt` одновременно может работать только одна сборка — включая wrapper, прямой `cmake --build` и диагностический запуск из другого агента/терминала. Если вызов инструмента вернул timeout или перешёл в фоновое ожидание, это не означает, что дочерние `cmake`/`MSBuild`/`cl`/`link` завершились. Нужно дождаться исходного запуска или проверить его process tree; второй build вслепую не запускать.

MSBuild может оставлять idle node-reuse workers после успешного завершения. Процесс `MSBuild.exe` с `/nodemode:` не является сам по себе доказательством продолжающейся сборки, если его coordinator/parent уже завершён и отсутствуют реальные `cmake`/`cl`/`link`. Проверять `Win32_Process.CommandLine` и parent process; такие workers не убивать и не использовать как ложную причину для clean/recovery.

Если после реального прерывания или пересечения сборок линкер сообщает `LNK1136`, сначала проверить конкретный `.obj`. Нулевой размер является признаком оборванной записи. Recovery допустим только для build artifacts:

1. убедиться, что активных compiler/linker процессов больше нет;
2. получить resolved build root `C:\programming\substrata_build_qt` и проверить, что каждый удаляемый `.obj` находится строго внутри него;
3. удалить только подтверждённые нулевые `.obj`;
4. снова запустить канонический `qt_build.ps1` без `-SkipConfigure` и проверить manifest/канонический EXE.

Ненулевой `.obj`, повторный `LNK1136` без истории прерывания или ошибка вне build root требуют отдельной диагностики. Не делать массовый clean и не трогать source/vendor trees как автоматический workaround.

### Разделение build и запуска

Успешные configure/compile/link/runtime-copy не разрешают запуск приложения. `gui_client.exe` — в обычном режиме и с любым runtime-smoke аргументом — запускать только по прямой просьбе пользователя. Без неё итог должен явно говорить: сборка подтверждена, runtime/UI не запускался и оставлен владельцу.

Подтверждённая команда из PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File C:\programming\qt_build.ps1
```

Эквивалент при прямом запуске из PowerShell:

```powershell
& "C:\programming\qt_build.ps1"
```

Фактически подтверждённый запуск выполнялся из `C:\programming`; рабочий каталог не является contract, потому что script использует абсолютные defaults.

Подтверждённые параметры/результаты из `qt_build.ps1` и `C:\programming\substrata_output_qt\build_manifest.json`:

| Поле | Значение |
| --- | --- |
| Source dir | `C:\programming\substrata` |
| Build dir | `C:\programming\substrata_build_qt` |
| Output root | `C:\programming\substrata_output_qt` |
| Generator | Visual Studio 17 2022 x64 |
| Target | `gui_client` |
| Configs | `Release`, `RelWithDebInfo` |
| Jobs | 8 |
| Qt | 5.15.16 at `C:/programming/Qt/5.15.16-vs2022-64` |
| CEF | ON; CEF 139.0.40 at `D:\cef\binary_distrib\cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64` |
| XR | Auto; on 2026-07-22 resolved to `XR_SUPPORT=ON` with `C:\programming\OpenXR-SDK-1.1.57\install` |
| Runtime copy | enabled; wrapper validates 28 required Qt/platform, CEF and OpenXR paths and hashes Qt/CEF/OpenXR/resource source-to-output copies |
| Manifest | `C:\programming\substrata_output_qt\build_manifest.json` |

Confirmed client artifacts:

```text
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release\gui_client.exe
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe
```

CEF runtime evidence in both directories includes `browser_process.exe`, `libcef.dll`, `chrome_elf.dll`, ANGLE/Vulkan/DXC DLLs, context snapshot, pak/resources and `locales/en-US.pak`. On 2026-07-22 the canonical RelWithDebInfo EXE SHA-256 was `73528FDEEC734ABC6E49C77A2995BFD61BDE906521613D2BA9EDCA75C196420C`.

Подтверждение build относится к compile/link/runtime-copy artifact validation. Для Tree, Voxel Editor и PubChem есть отдельные narrow runtime smokes ниже; они не заменяют manual editor UI/server/reconnect test.

### Локальная Windows Release + NSIS installer без публикации

Этот workflow создаёт локальный Windows installer существующими scripts. Он не выполняет commit, tag, push, GitHub Release, установку или запуск клиента. `scripts/publish_update.ps1` для локальной упаковки не использовать: у него нет build-only режима, он требует clean worktree, меняет версию и выполняет Git/publish операции.

1. Установить целевую версию в строке `cyberspace_version` файла `shared/Version.h` и синхронно обновить `metasiberia_version` в `shared/MetasiberiaVersion.h`. До сборки проверить `git status --short --branch` и убедиться, что нет другой активной сборки в `C:\programming\substrata_build_qt`.
2. Проверить Ruby и NSIS:

   ```powershell
   ruby --version
   & "C:\Program Files (x86)\NSIS\makensis.exe" /VERSION
   ```

3. Собрать и подготовить полный Release runtime каноническим Qt 5 wrapper без `-SkipConfigure` и без `-SkipRuntimeCopy`:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass `
     -File C:\programming\qt_build.ps1 `
     -Configs Release `
     -Jobs 8
   ```

   Если wrapper сообщает о dirty worktree, продолжать только после ручной проверки перечисленных изменений. Готовый runtime должен находиться в `C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release`, а `C:\programming\substrata_output_qt\build_manifest.json` должен содержать `status: success`, `success: true` и `runtime_ready: true` для `Release`.

4. Прочитать версию из того же header и создать installer существующим NSIS pipeline:

   ```powershell
   $versionMatch = Select-String `
     -LiteralPath C:\programming\substrata\shared\Version.h `
     -Pattern 'cyberspace_version\s*=\s*"([^"]+)"' |
     Select-Object -First 1
   $releaseVersion = $versionMatch.Matches[0].Groups[1].Value

   powershell -NoProfile -ExecutionPolicy Bypass `
     -File C:\programming\substrata\scripts\create_simple_installer.ps1 `
     -SourceDir C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release `
     -OutputDir C:\programming\substrata_output\installers `
     -AppName "Metasiberia Beta" `
     -Version $releaseVersion `
     -Config Release
   ```

   Script должен вывести `Backend: NSIS`. Не использовать IExpress fallback для release: он не сохраняет полный набор вложенных runtime-файлов. Script перезаписывает одноимённый installer, если он уже существует.

5. Не запуская installer, проверить итоговые SHA-256:

   ```powershell
   $releaseExe = 'C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release\gui_client.exe'
   $installer = Join-Path 'C:\programming\substrata_output\installers' ("MetasiberiaBeta-Setup-v{0}.exe" -f $releaseVersion)
   Get-FileHash -Algorithm SHA256 -LiteralPath @($releaseExe, $installer)
   ```

Для воспроизводимой сборки Metasiberia Beta v0.0.22 внешний `glare-core` должен быть на commit `8142cd63403532726c068d18551383c54b8d7f66`, а его tracked worktree — чистым. `glare-core` подключается через `GLARE_CORE_TRUNK_DIR` и не является submodule этого репозитория, поэтому SHA зависимости нужно проверять отдельно до configure/build.

Изменения серверного кода не считаются проверенными без сборки сервера. После правок в `server/**`, общего протокола/сериализации или `shared/**`, который читается/пишется сервером, обязательно выполнить локальную сборку:

```powershell
cmake --build C:\\programming\\substrata_build_qt --config RelWithDebInfo --target server -j 8
```

Проверить код возврата и время/размер `C:\\programming\\substrata_output_qt\\vs2022\\cyberspace_x64\\RelWithDebInfo\\server.exe`. Эта проверка не запускает сервер; production Linux ELF-сборка, деплой и restart выполняются только после отдельного разрешения пользователя.

Критическое различие: production работает на Ubuntu/Linux `metasiberia-server`. Windows `server.exe` не является production-артефактом. Для серверной правки после локальной проверки обязательно выполнить Linux workflow из `docs/SERVERS_AND_EXCHANGE.md`, собрать `server` в `/srv/metasiberia/build/master`, скопировать ELF в release, переключить `/srv/metasiberia/releases/current` и перезапустить `metasiberia-server.service` только при явном разрешении пользователя.

### Native Voxel Editor smoke

Статус: **CONFIRMED, 2026-07-14** для clean-room metadata/tools/widget/mesher regression path.

После сборки smoke запускается из каждой canonical конфигурации:

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release\gui_client.exe `
  --voxel_editor_smoke C:\programming\voxel_editor_smoke_Release.json

C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe `
  --voxel_editor_smoke C:\programming\voxel_editor_smoke_RelWithDebInfo.json
```

Оба запуска завершились с exit code 0 и `ok=true`. Проверены metadata round-trip/limits и legacy-content escaping; Brush/Paint/Mirror/locked layer; Line; атомарный bounded Fill; selection copy/delete/paste/duplicate/move, overlap/collision rollback; прямые `VoxelEditCommand::undo/redo`; per-UID stack push/isolation/clear; QWidget round-trip; независимые generator sizes и area/perimeter/volume/surface metrics; determinism/hollow/clear/merge/cap всех семи generators; сохранение удаления слоя в compressed payload; legacy content migration; base material alpha после hide/show; shortcuts `B/L/F/S`/`]`; runtime-загрузка Lucide icons для tool/rebuild/generator/selection buttons; 12 Greedy против 20 Cubes triangles для двух соседних voxels.

Дополнительные compile/link проверки:

```powershell
cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo --target gui_client -j 8
cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo --target server -j 8
```

Scope boundary: smoke не вызывает `VoxelUndoStack::undo/redo` и `Ctrl+Z`/`Ctrl+Y` через `MainWindow`, не открывает реальное menu/toolbar действие, не проходит `CreateObject`/server confirmation/auto-selection, не эмулирует live scene mouse input или selection overlay, не проверяет визуальный render/physics, reconnect, second client, network persistence или production. Он также не является SDL/Web build. Canonical wrapper завершился успешно, несмотря на известное предупреждение об отсутствующем optional `C:\programming\SDL\sdl_2.30.9_build\Release\SDL2.dll`; Qt artifacts/runtime copy созданы.

### Scientific PubChem HTTPS smoke

Статус: **CONFIRMED, 2026-07-11** for headless client provider smoke only.

Запускать из output config directory или с абсолютным путём к report:

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe --scientific_pubchem_smoke C:\Users\densh\AppData\Local\Temp\metasiberia_pubchem_smoke.json
```

Фактически подтверждено 2026-07-11:

- transport: Windows WinHTTP/SChannel, not QtNetwork SSL;
- QtNetwork SSL in local Qt build: disabled by Qt config, so `QNetworkAccessManager` must not be used for PubChem HTTPS;
- TLS certificate validation: Windows default validation; certificate errors are not ignored and HTTPS is not downgraded to HTTP;
- `water`: search HTTP 200, CID 962, properties HTTP 200, SDF 3D HTTP 200, PNG HTTP 200;
- `nicotine`: search HTTP 200, CID 89594, properties HTTP 200, SDF 3D HTTP 200, PNG HTTP 200;
- invalid query: HTTP 404/no CID, no built-in sample fallback.

Side effects: writes normal client appdata log/cache and the requested JSON report. It exits before normal server connection/UI flow and is not a general test suite.

### Scientific PubChem object-application smoke

Статус: **CONFIRMED, 2026-07-11** for QWidget editor slot flow and local `WorldObject` application only.

Запускать из output config directory или с абсолютным путём к report:

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe --scientific_pubchem_apply_smoke C:\Users\densh\AppData\Local\Temp\metasiberia_pubchem_apply_smoke.json
```

Фактически подтверждено 2026-07-11:

- `water`: one result, selected CID 962, atom count 3, bond count 2, `source=PubChem`, lazy Images load/cache succeeds, local molecule OBJ assigned;
- `nicotine`: one result, selected CID 89594, atom count 26, bond count 27, `source=PubChem`, lazy Images load/cache succeeds, local molecule OBJ assigned;
- labels and legend are enabled and rendered for both molecules; PubChem PNG preview/cache succeeds; lazy PUG View Classification succeeds; serialized marker sizes are 7,616 bytes for Water and 9,098 bytes for Nicotine, below the 10,000-byte shared limit;
- invalid query: HTTP 404/no CID, object source stayed `manual`, no built-in sample fallback.

Scope boundary: this smoke instantiates the Qt widget and calls the same search/load slots, then calls `toObject()` on a local test `WorldObject`. It does **not** connect to a server, create/update production world state, run `GUIClient::objectEdited()` resource conversion/upload, or verify reconnect/second-client persistence.

### Scientific molecule information-layer smoke

Статус: **CONFIRMED, 2026-07-11** for native Qt information-layer logic and live resolver/cache requests.

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe --scientific_molecule_info_smoke C:\Users\densh\AppData\Local\Temp\metasiberia_molecule_info_smoke.json
```

Подтверждено:

- exact-first Russian resolver returns `вода` 962, `никотин` 89594, `аспирин` 2244, `кофеин` 2519, `этанол` 702, `глюкоза` 5793;
- seven label modes render with labels and legend enabled;
- atom, multiple-atoms, bond and molecule selected states round-trip in the native model;
- distance, angle and torsion produce three saved measurement records;
- PUG View Classification lazy load/cache succeeds;
- native periodic model contains 118 records.

Scope boundary: this smoke renders the Qt molecule viewport and tests model/actions; it does not verify human-readable visual layout, real mouse ergonomics, server persistence/reconnect, SDL/Web or production. Do not force `QT_QPA_PLATFORM=offscreen` for this runtime unless the offscreen platform plugin is present; on the current Windows Qt output it makes `QApplication` fail during platform integration.

```powershell
# Узкая актуальная конфигурация
powershell -ExecutionPolicy Bypass -File C:\programming\qt_build.ps1 -Configs RelWithDebInfo -XR Off

# Полный default wrapper: Release + RelWithDebInfo, XR Auto, CEF on
powershell -ExecutionPolicy Bypass -File C:\programming\qt_build.ps1
```

Класс: medium/expensive, пишет build/output/runtime files и `build_manifest.json`. `-SkipConfigure` допустим только для уже согласованного tree. `-XR On` обязан падать без SDK; default `-CEF On` обязан падать без полного binary distribution/wrapper/runtime.

Narrow Procedural Tree generator smoke после успешной Qt build:

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe `
  --tree_generator_smoke C:\programming\substrata_build_qt\tree_generator_smoke.json
```

Smoke проверяет determinism, seed/parameter changes, Z-up bounds, сохранение метрического масштаба, branch/leaf/trellis geometry, непустые LOD1/LOD2, точные canonical значения после clamp и непустой base mesh для всех 16 imported EZ-Tree presets. Он не подключается к server и не заменяет manual create/reconnect/second-client check.

Regression smoke для заполнения Tree Editor и восстановления старой невидимой заглушки:

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe `
  --tree_editor_smoke C:\programming\substrata_build_qt\tree_editor_smoke.json
```

Smoke проверяет, что программное заполнение controls не превращает `ash_medium` в `custom`, level-0/1 aliases синхронизированы, сохранённые placeholder-параметры `0.25 / 0.02 / 0.02` восстанавливаются, schema 1 получает geometry v2, а абсолютные custom radii мигрируют в parent-relative multipliers.

На Windows Release `gui_client.exe` является GUI process, поэтому PowerShell может вернуть управление до окончания smoke. Для строгой проверки использовать `Start-Process -Wait -PassThru` и затем читать report JSON. Не задавать `QT_QPA_PLATFORM=offscreen/minimal`: текущий runtime output не содержит соответствующей platform integration.

Ручная конфигурация Qt path:

```powershell
$env:GLARE_CORE_LIBS = "C:/programming"
$env:WINTER_DIR = "C:/programming/winter"
$env:GLARE_CORE_TRUNK_DIR = "C:/programming/glare-core"
$env:CYBERSPACE_OUTPUT = "C:/programming/substrata_output_qt"
$env:CEF_BINARY_DISTRIB_DIR = "D:/cef/binary_distrib/cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64"

cmake -S C:\programming\substrata `
  -B C:\programming\substrata_build_qt `
  -G "Visual Studio 17 2022" -A x64 `
  -DUSE_SDL=OFF -DCEF_SUPPORT=ON `
  -DCEF_BINARY_DISTRIB_DIR=D:/cef/binary_distrib/cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64 `
  -DXR_SUPPORT=OFF
```

## Узкие target builds

```powershell
cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo --target gui_client -j 8
cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo --target server -j 8
cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo --target screenshot_bot -j 8
```

- Fast/medium incremental, expensive clean; writes artifacts.
- Single-file standard command отсутствует; incremental affected target — минимальная compile check.
- Full root build: `cmake --build C:\programming\substrata_build_qt --config RelWithDebInfo -j 8`; включает root-wired targets и conditional CEF helper.

Runtime staging из `scripts/`:

```powershell
ruby copy_files_to_output.rb --no_bugsplat --config RelWithDebInfo
```

Команда пишет runtime output. Запускать только с корректными `CYBERSPACE_OUTPUT` и `CEF_BINARY_DISTRIB_DIR`; `--no_cef` использовать только для явно сконфигурированного `CEF_SUPPORT=OFF` build.

## Встроенные tests

CTest/add_test в repo нет. `BUILD_TESTS=1` автоматически задаётся только Debug и RelWithDebInfo.

```powershell
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe --test
C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\server.exe --test
```

- Release `--test` не поддерживает compiled test suite.
- Suites не полностью hermetic: включают file/codec/model/database, TLS/HTTP/SMTP-related tests. Перед запуском оценить network/local-state side effects.
- `WebServerRequestHandlerTests`, `WorkerThreadTests` и некоторые terrain tests существуют, но отдельный wired runner не подтверждён.
- Debug полезен для leak detection, но дороже.

## Linux configure/build

CI подтверждает только configure shape:

```bash
cmake -S . -B build \
  -DCEF_SUPPORT=OFF \
  -DUSE_SDL=ON \
  -DSDL_BUILD_DIR=/usr \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target server -j 4
```

Требуются корректные external dependency trees/env. CI не выполняет вторую строку и не подтверждает link/runtime.

## Компонентная матрица проверок

| Компонент | Минимум | Локальная сборка/test | Интеграция | Ограничения |
| --- | --- | --- | --- | --- |
| Qt client/UI/editor | affected symbol/CMake list | `gui_client` target + `--test` | manual UI flow; server при protocol | default client run пишет appdata/может подключаться к production |
| Scientific Object Editor WIP | marker/schema/CMake/10 KB bound | тот же `gui_client` target после отдельного разрешения | create/edit/reconnect/resource/permission flow | current implementation Qt-based, untracked WIP; Web/SDL parity и adapters/AI execution не считать реализованными |
| Procedural Tree Editor WIP | marker/params/preset/assets/CMake | `gui_client` target + `--tree_generator_smoke` + `--tree_editor_smoke` | Add Tree, realtime edit, server-confirm, reconnect, second client | Qt-only editor; smokes не проверяют final live-world render/permissions |
| SDL client | compile guards/search | configure `USE_SDL=ON`, build `gui_client` | manual SDL flow | отдельный current wrapper отсутствует |
| XR | CMake/guard + target | wrapper `-XR On/Off`, client test | `--desktop` и `--vr`, HMD/runtime | runtime/HMD external; не запускать как generic smoke |
| Server | affected target | `server` + `--test` | local copy state + client; route smoke | обычный run меняет state/listens ports |
| Embedded web/admin/API | route/handler/assets | build/test `server` | localhost HTTP/auth/permission flow | отдельного target/server нет |
| Shared protocol/model | readers/writers matrix | client + server builds/tests | old fixture/cross-version peers | architecture-level risk |
| Screenshot pipeline | target build | `screenshot_bot` build | isolated server + GUI slave | production/default run external и long-running |
| Webclient | HTML/script read | Emscripten `gui_client` | local server `/webclient` + WSS | expensive toolchain; generated outputs |
| Site CSS/JS/fragments | link/path/syntax read | server build only if C++ changed | local WebDataStore/site route | publish/deploy separate |
| Node site capture | `package.json`/script | `npm ci`; explicit capture | local/non-sensitive URL | network/output; auth state sensitive |
| Optional bots/installer | source/CMake audit | command не подтверждён | none | root-disabled/stale dependencies |

## Emscripten/webclient

### Обязательный platform-boundary gate: Qt vs SDL/Emscripten

`gui_client` имеет два compile contracts: Qt desktop (`USE_SDL=OFF`) и SDL/Emscripten (`USE_SDL=ON`). Успешная Windows Qt-сборка не доказывает, что Web-client собирается или работает.

Перед Web-затрагивающей правкой в `gui_client/**`, общем коде или public headers обязательно:

1. Проверить, что в SDL/common path не попали Qt-only headers, типы или API (`QString`, Qt JSON/image classes, QWidget editor/adapter и т. п.).
2. Если Qt-only поведение необходимо, вынести его на Qt boundary. Когда guard оправдан, `#if !defined(USE_SDL)` должен окружать include, declaration/signature, member/inline reference, definition и call site. Guard только вокруг тела функции недостаточен.
3. Выполнить изолированную Emscripten-сборку `gui_client` до запуска preload/hash scripts.
4. Если configure/build не успешен, остановиться: не использовать предыдущий JS/WASM/data bundle, не считать функцию Web-ready и не выполнять Web deploy.

Это обязательный regression gate после инцидента с Qt-зависимостью в общем `GUIClient` path. Публиковать можно только артефакты, созданные успешной Emscripten-сборкой из текущего source tree; после cache-busting сверить hashes HTML, JS, WASM и data.

Source command shape из `docs/building.txt`:

```powershell
cmake -S C:\programming\substrata -B <isolated-emscripten-build> `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=<emsdk>/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_SDL=ON -DCEF_SUPPORT=OFF `
  -DSDL_BUILD_DIR=<emscripten-sdl-build>

Set-Location <isolated-emscripten-build>
ruby C:\programming\substrata\scripts\make_emscripten_preload_data.rb C:\programming\substrata
cmake --build . --target gui_client
ruby C:\programming\substrata\scripts\update_webclient_cache_busting_hashes.rb C:\programming\substrata
```

Текущий канонический pipeline этого workspace:

- source: `C:\programming\substrata`;
- build: `C:\programming\substrata_emscripten_build_deploy_20260817`;
- output: `C:\programming\substrata_output_emscripten_deploy_20260817\test_builds`;
- browser entrypoint: `/webclient/` через junction `webclient -> test_builds` в output root.

`update_webclient_cache_busting_hashes.rb` под per-output exclusive lock сначала создаёт оба entrypoint во временных файлах из одного tracked template с одинаковыми hash-pinned bytes, затем строго инвалидирует старый `index.html` и публикует новый `index.html` последним через rename в том же каталоге. После успешной invalidation любой последующий сбой оставляет entrypoint отсутствующим, а не частично обновлённым; параллельные updater-процессы не могут смешать поколения файлов. Preload script также удаляет старый generated `index.html` перед пересборкой, поэтому canonical pipeline не сохраняет stale entrypoint при прерывании.

Класс: expensive/data-changing. Важно: preload script удаляет/создаёт `./data`, пишет в output и touch-ит внешний `GLARE_CORE_TRUNK_DIR/graphics/TextureData.cpp`; запускать только из изолированного build dir. Tracked `emscripten_build*` не считать portable source.

## Site/Figma и media helpers

```powershell
Set-Location C:\programming\substrata\tools\site_capture
npm ci
node capture.mjs --base http://127.0.0.1:<port> --out <safe-output-dir>
```

- Medium/network/output-writing. Для authenticated pages capture может сохранить sensitive `storageState.json`, cookies и screenshots.
- `capture_site_for_figma.ps1` может выполнить `npm install` автоматически.
- `snapshot_site_pages.ps1` пишет HTML/assets/manifest.
- VIVE sync — long-running ADB media pull; task-only, private output.

## Lint, format и type checks

- Repo-level clang-format/clang-tidy/editorconfig/ESLint/Prettier/typecheck config не найден.
- Не придумывать «каноническую» formatting command и не форматировать unrelated code.
- Ruby syntax: `ruby -c <script.rb>`; Python syntax/import-safe check и PowerShell parser допустимы только если не выполняют side effects.
- C++ correctness минимум подтверждает affected target compile, не text-only checker.

## CI: что реально проверяется

- `.github/workflows/windows-main-ci.yml` и `cmake-configure-linux.yml` выполняют checkout dependencies, Jolt/Luau fetch и CMake configure.
- Compile, tests, lint, packaging и deploy отсутствуют.
- Windows создаёт dummy SDL header только для configure; real headers/link не проверены.
- `webserver/**`, `screenshot_bot/**`, assets и systemd отсутствуют в path filters, хотя webserver входит в server.
- External `glare-core`/`winter` refs не pinned; configure не полностью reproducible.

## Опасные и publishing команды

| Команда/flow | Класс | Почему не запускать без явного задания |
| --- | --- | --- |
| `server` без `--test` | dangerous/data-changing | создаёт/читает/пишет state, directories, listeners |
| `server --compact_database_in_place` | destructive | меняет binary DB in-place |
| `gui_client --desktop/--vr` | external/data-changing | default production URL, appdata/cache/settings/logs |
| screenshot/lightmapper/stress bots | external/long-running | подключение, upload/render/load на server |
| production release symlink + `systemctl restart` | deploy | переключает live binary/service |
| `deploy_web_to_metasiberia_v2.ps1` | remote publish/destructive | old host/paths, `rsync --delete` |
| `publish_update.ps1` | publish | version/commit/tag/push/build/release; historical remote/policy |
| `publish_wiki_to_github.ps1` | publish | clone/delete/commit/push Wiki |
| `metasiberia_backup_pull.ps1` | sensitive/data-changing | SSH/SCP archives и retention deletion |
| map maintenance `sample/regen` | production/sensitive | читает/caches admin session; regen меняет queue/state |

Production workflow допустимо **описать**, но build/deploy/restart/DNS/restore выполнять только после отдельного подтверждения пользователя.

## Неподтверждённые команды/границы

- Disabled `cv_bot`, `lightmapper_bot`, `backup_bot`, `stress_test` не имеют current root build command без изменения build graph.
- `installer/CMakeLists.txt` root integration отсутствует.
- Staging `server_dist_resources/` → runtime `dist_resources/` не найден.
- Public `metasiberia.com`, TheRift и Android wrapper не имеют build source/command в repo.
- Fuzzing требует manual source/build-property changes и не является turnkey suite.

## Scientific Object Editor regression smoke

`gui_client.exe --scientific_molecule_info_smoke <report.json>` теперь дополнительно проверяет replacement path: создать distance/angle/torsion measurements, заменить molecule structure и убедиться, что stale atom-index-dependent measurements очищены (`stale_measurements_cleared_after_molecule_replace=true`). Это покрывает crash path, найденный 2026-07-11 при загрузке новой PubChem molecule в объект.
