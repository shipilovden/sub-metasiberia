# Сайты Metasiberia: границы, маршруты и деплой

> Статус: актуальная карта трёх production-сайтов. Проверено 2026-08-28 по исходникам и процедуре публикации. Секреты, пароли и токены в документ не включаются.

## 1. Три разные production-поверхности

| Surface | Production URL | Source of truth | Hosting/deployment | Назначение |
| --- | --- | --- | --- | --- |
| Официальный публичный сайт | `https://metasiberia.com/` | `C:\programming\Metasiberia official\Metasiberia one` | Shared hosting REG.RU, ручной FTP-деплой статического Tilda-export | Публичная презентация Metasiberia, участки, магазин, Lua и ссылки на server site |
| TerrainGen | `https://terragen.metasiberia.com/` | `C:\programming\Metasiberia official\TerrainGen` | Тот же shared hosting REG.RU и аккаунт ISPmanager, ручной FTP-деплой статических файлов | Браузерный генератор и редактор рельефа Metasiberia |
| Server site и web/admin | `https://vr.metasiberia.com/` | `C:\programming\substrata\webserver\`, `webserver_public_files\`, `webserver_fragments\` и связанные обработчики | Основной сервер: Caddy → `metasiberia-server.service`, workflow через `releases/current` | FAQ, Terms, Privacy, регистрация, аккаунт, админка и WebClient-вход |

Эти поверхности нельзя смешивать. Изменения в официальном сайте не требуют пересборки WebClient/Emscripten и не требуют переключения `releases/current`. Изменения server site выполняются по отдельному workflow, описанному в [`SERVERS_AND_EXCHANGE.md`](SERVERS_AND_EXCHANGE.md).

## 2. Правило ссылок официального сайта

Popup-меню официального сайта использует внешние ссылки на документы server site:

- `F.A.Q.` → `https://vr.metasiberia.com/faq`
- `Term of Use` → `https://vr.metasiberia.com/terms`

Ссылка на корень server site `https://vr.metasiberia.com/` называется `Metasiberia Control Hub` на английском и `Центр управления Метасибири` на русском. В popup-панели она не входит в основной список навигации: локализатор размещает её в нижнем блоке `t450__right_descr`, непосредственно над ASCII-подписью и перед social links. В русском режиме название выводится двумя строками: `Центр управления` / `Метасибири`. Это общий веб-портал, а не отдельная страница админки.

Эти ссылки должны быть одинаковыми во всех экспортированных `page*.html` и `files/page*body.html`. Не возвращать их к относительным `/faq` и `/terms`: такие маршруты принадлежат другому production-сайту.

Локальные footer-ссылки и остальные пункты меню изменяются отдельно и только по явному scope задачи.

Для текущего footer официального сайта действуют отдельные правила:

- `Denis Shipilov` → `mailto:denshipilov@gmail.com`;
- `F.A.Q.` → `https://vr.metasiberia.com/faq`;
- `Правила` / `Terms` → `https://vr.metasiberia.com/terms`.

Эти ссылки задаются локализатором `js/metasiberia-language-switcher.js`, потому что экспортированный footer заменяется при загрузке страницы. Footer server site восстанавливается в `webserver/WebServerResponseUtils.cpp` и должен содержать `F.A.Q.`, `Terms of use`, `Privacy Policy` и `Map`.

## 3. Источники и файловая схема официального сайта

Корень исходников:

```text
C:\programming\Metasiberia official\Metasiberia one
```

В deploy scope входят только статические материалы этого каталога:

- `page*.html`;
- `files/` с HTML-фрагментами;
- `css/`, `js/`, `images/`;
- `robots.txt`, `sitemap.xml`;
- `htaccess` из репозитория, который на production размещается как `.htaccess`;
- `404.html` и прочие файлы, явно включённые существующим `scripts/deploy.sh`.

FTP-relative production root:

```text
/www/metasiberia.com/
```

Для TerrainGen используется отдельный FTP-relative root поддомена:

```text
/www/terragen.metasiberia.com/
```

Это отдельный статический сайт и отдельный deploy scope. Доступы к REG.RU не дублируются в документации: их нужно брать из защищённого локального контекста основного сайта. Фактический document root поддомена перед загрузкой подтверждается в ISPmanager.

Корень исходников TerrainGen:

```text
C:\programming\Metasiberia official\TerrainGen
```

Для небольшой правки favicon в FTP-загрузку входят только актуальные `index.html` и `favicon.svg`; Vercel, WebClient, Emscripten и server-site workflow к этому сайту не относятся.

OS document root на REG.RU:

```text
/var/www/u2978374/data/www/metasiberia.com
```

Локальные archives/backups для этого workflow хранятся в `.deploy/` рядом с исходниками сайта. Секреты доступа берутся только из локального защищённого secret-файла согласно `AGENTS.md` и никогда не копируются в docs, archive manifest, commit или вывод команд.

## 4. Канонический workflow публикации официального сайта

1. Определить репозиторий и прочитать ближайшие `AGENTS.md`; проверить `git status --short`.
2. Проверить целевой diff: `git diff --check`, а для изменённых JavaScript-файлов — `node --check`.
3. Создать staged archive существующим `scripts/deploy.sh` в `.deploy/`.
4. До FTP-загрузки скачать текущие production-файлы в timestamped backup и снять SHA256. Для удаляемых файлов backup обязателен: отсутствие файла в archive само по себе его не удаляет на FTP.
5. Проверить состав archive: текущие HTML/JS и фрагменты должны присутствовать; `WebClient`, Emscripten, CMake/Ninja, client, database, authentication, cookie/consent и другие несвязанные области должны отсутствовать.
6. Загрузить статические файлы в `/www/metasiberia.com/` ручным FTP workflow REG.RU. Для переименования `htaccess` → `.htaccess` использовать именно production-имя `.htaccess`.
7. Для удалённых страниц/ассетов выполнить отдельное удаление только после проверки точных remote targets.
8. Сверить production-файлы по SHA256 и выполнить HTTP smoke-check официальных URL. Для текущего удаления News ожидается `404` на `/news`; для опубликованных страниц — `200`.

Для статического официального сайта не выполняются `systemctl restart`, операции с `releases/current`, серверным binary или server state. Это отдельная граница от server site `vr.metasiberia.com`.

## 5. Запрещённые побочные изменения

При работе с `metasiberia.com` не изменять без отдельной задачи:

- `webclient`, Emscripten, `emscripten_build*`, CMake cache, Ninja и client build/output;
- `database`, server state, authentication и cookie/consent;
- backend/web-admin источники `C:\programming\substrata\webserver\`;
- DNS, reverse proxy, Caddy и systemd основного сервера;
- страницы и assets, не входящие в заявленный scope.

## 6. Related documentation

- [`SERVERS_AND_EXCHANGE.md`](SERVERS_AND_EXCHANGE.md) — основной сервер, server site/web-admin и отдельный deployment workflow.
- [`WEBCLIENT_METASIBERIA.md`](WEBCLIENT_METASIBERIA.md) — границы WebClient/Emscripten; не использовать для статического официального сайта.
- [`FIGMA_SITE_SYNC.md`](FIGMA_SITE_SYNC.md) — синхронизация макетов server site; официальный сайт остаётся отдельной surface.

## 7. SEO server site (`vr.metasiberia.com`)

SEO-файлы server site находятся в `C:\programming\substrata\webserver_public_files\`:

- `robots.txt` физически хранится здесь, но backend отдаёт его по корневому URL `https://vr.metasiberia.com/robots.txt`;
- `sitemap.xml` физически хранится здесь и отдаётся по `https://vr.metasiberia.com/sitemap.xml`;
- рекламное изображение для карточки главной страницы — `main.png`, исходник: `C:\programming\substrata\webserver\main.png`, публичная копия: `webserver_public_files\main.png`, URL изображения: `https://vr.metasiberia.com/files/main.png`.

Если понадобится заменить картинку для публикаций, класть её нужно в `webserver_public_files`. Для сохранения текущего workflow либо заменяется `main.png`, либо добавляется отдельный файл и обновляется `og:image` в `webserver/MainPageHandlers.cpp` и `webserver/WebServerResponseUtils.cpp`. Сейчас используется рекламное изображение `main.png` размером 1731×909.

В sitemap включаются стабильные публичные страницы и публичные списки. Формы входа/регистрации, аккаунт, админка, WebClient, API, ресурсы, операции изменения данных и прочие служебные маршруты блокируются в `robots.txt` или получают `noindex`. Динамические карточки пользовательских миров, участков, фото, новостей и событий не перечисляются статически, чтобы не создавать неконтролируемые URL-дубликаты.

SEO-разметка HTML централизованно формируется в `webserver/WebServerResponseUtils.cpp`. Для индексируемых страниц используются `title`, `description`, canonical, Open Graph, Twitter Card и Schema.org `WebPage`; главная страница дополнительно содержит Schema.org `WebSite` и использует тот же `main.png`. Root `robots.txt`/`sitemap.xml` требуют перезапуска server process после публикации нового binary, потому что маршруты находятся в server binary.

Verification-файлы Google Search Console и Яндекс Вебмастера не создаются с выдуманными токенами. После получения реального файла его нужно добавить отдельным согласованным изменением: стандартный путь verification-файла должен открываться в корне `https://vr.metasiberia.com/<имя-файла>`. Альтернатива — добавить выданный сервисом meta-тег в общий HTML-header или подтвердить домен через DNS.

Проверка карточки публикации: после restart открыть `https://vr.metasiberia.com/` в [OpenGraph.xyz](https://www.opengraph.xyz/) и проверить `og:title`, `og:description`, `og:image`, `og:url`; отдельно открыть URL изображения `https://vr.metasiberia.com/files/main.png` и убедиться, что он отвечает `200`.
