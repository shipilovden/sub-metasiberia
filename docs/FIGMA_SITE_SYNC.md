# Figma: Синхронизация Макетов Сайта Metasiberia

Цель: чтобы в Figma-файле **Metasiberia** было отражено текущее состояние server site/портала `vr.metasiberia.com` (root, auth, account, admin и т.д.), и далее дизайн в Figma стал источником требований для правок `webserver_public_files/` и `webserver_fragments/`.

Важно: на 2026-02-15 MCP-интеграция Figma может упираться в лимит вызовов. Если автоматическое создание слоёв недоступно, используем один из fallback-подходов ниже.

## Источник истины
- Production server site: `https://vr.metasiberia.com/`
- Сервер: `metasiberia-server`, public IP `87.103.196.229`, LAN IP `192.168.0.30` (обновлено 2026-06-21)

Публичный маркетинговый сайт `https://metasiberia.com/` размещён отдельно; его source of truth — `C:\programming\Metasiberia official\Metasiberia one`. Не смешивать его макеты с C++ server site и отдельными `/admin*` routes; границы и deploy rules зафиксированы в [`METASIBERIA_WEBSITES.md`](METASIBERIA_WEBSITES.md).

## Что должно быть в Figma (минимум)
Одна страница Figma = один “раздел” сайта. На каждом фрейме сверху подпись URL.

Public:
- `/` (landing)
- `/map`
- `/news`
- `/photos`

Auth:
- `/signup`
- `/login`
- `/account` (в залогиненном состоянии)

Admin:
- `/admin`
- `/admin_users`
- `/admin_parcels`
- `/admin_create_parcel` (форма)

Примечание: фактические пути могут меняться, проверять по меню/ссылкам админки.

## Как выгрузить сайт в Figma (варианты)

### Вариант A (предпочтительный): импорт HTML в Figma через плагин
1. В Figma установить плагин класса “HTML to Figma / Web to Figma” (любой проверенный).
2. Импортировать страницы по URL `https://vr.metasiberia.com/...`.
3. Полученные слои разложить по страницам/фреймам (Public/Auth/Admin).
4. Завести базовые компоненты (buttons/links/forms) и стили.

Плюсы: получаем редактируемые слои, а не просто картинки.
Минусы: качество импорта зависит от плагина.

### Вариант A2 (бесплатный, полностью покрывает весь сайт): скриншоты + наш Figma-плагин
1. Снять скриншоты страниц Playwright-скриптом:
   - Папка: `substrata/tools/site_capture`
   - Установка: `npm i`
   - Запуск (public страницы): `node capture.mjs --base https://vr.metasiberia.com`
   - Или одной командой через wrapper:
     - `powershell -ExecutionPolicy Bypass -File C:\programming\substrata\scripts\capture_site_for_figma.ps1`
2. Для страниц, требующих логин (account/admin):
   - Вариант 1: задать env vars `METASIBERIA_ADMIN_USER`, `METASIBERIA_ADMIN_PASS` и повторить запуск.
   - Вариант 2: использовать `storageState.json` (Playwright) и передать `--storageState <path>`.
   - Вариант 3: передать cookie сессии:
     - `powershell -ExecutionPolicy Bypass -File C:\programming\substrata\scripts\capture_site_for_figma.ps1 -Cookie "<cookie_header_value>"`
3. Установить development plugin в Figma:
   - `substrata/tools/figma/metasiberia_site_importer/manifest.json`
4. В плагине выбрать `manifest.json` из папки скриншотов и все PNG, нажать `Import`.

Плюсы: бесплатно, выгружается весь сайт.
Минусы: сначала это референс-скриншоты (дальше поверх них собираем живые компоненты и дизайн-систему).

### Вариант A3 (через TalkToFigma MCP, без отдельного плагина-импортера)
Если настроен TalkToFigma (socket на `3055`) и есть актуальный channel из интерфейса плагина, можно импортировать PNG прямо через него. Channel меняется после переподключения; старые значения не переиспользовать.

1. Запустить локальный socket helper:
   - `powershell -ExecutionPolicy Bypass -File C:\programming\substrata\scripts\start_figma_mcp_socket.ps1`
2. В Figma открыть файл, куда импортируем (например Metasiberia-Lab) и подключить TalkToFigma плагин к каналу.
   - Канал берём из строки в плагине: `Connected to server in channel: XXXXXXXX`
3. Снять скриншоты сайта в папку `out_...`:
   - `powershell -ExecutionPolicy Bypass -File C:\programming\substrata\scripts\capture_site_for_figma.ps1 -Cookie "<cookie>"`
4. Импортировать в открытый Figma-файл:
   - `powershell -ExecutionPolicy Bypass -File C:\programming\substrata\scripts\import_site_capture_to_figma_via_talk_to_figma.ps1 -Channel <CHANNEL_FROM_PLUGIN> -InDir <out_dir>`

Примечание: исторически A3 использовал локально изменённый TalkToFigma dev-plugin с командой `create_image_rect`. Этот временный source tree не хранится в репозитории; перед повторением workflow нужно отдельно подтвердить установленную версию плагина.

### Вариант B (быстрый fallback): фреймы со скриншотами + поверх “обводка”
1. Сделать скриншоты ключевых страниц (желательно full-page).
2. Вставить скриншоты в Figma как изображения (Place image).
3. Поверх изображений собрать “живые” компоненты (текст/кнопки/формы) и постепенно убрать зависимость от растра.

Плюсы: быстро фиксирует текущий UI как референс.
Минусы: на старте это “картинки”, требуется ручная реконструкция UI.

### Вариант C (автоматизация через API): загрузка скриншотов/фреймов скриптом
Требует отдельного доступа:
- либо снятия лимита/доступа MCP-инструментов,
- либо Figma Personal Access Token (хранить только в `AGENTS_SECRETS.local.md`, не в репозитории).

Тогда можно:
- программно создать страницы/фреймы,
- залить PNG-скриншоты,
- разложить по страницам и подписать URL.

## Локальный экспорт HTML (для диффа и ревизии)
См. скрипт: `substrata/scripts/snapshot_site_pages.ps1`

Он сохраняет HTML/ассеты (для публичных страниц) в локальную папку и позволяет быстро сравнивать изменения сайта без ручного копирования.
