# Серверы Metasiberia и обмен между ними

Документ описывает текущую инфраструктуру (факт), рекомендуемый план перехода с IP-адреса на доменное имя, TLS (Let's Encrypt) без остановки сервера, отправку писем и минимальные улучшения аутентификации.  
Секреты (пароли/токены/ключи) здесь не храним: см. `C:\programming\AGENTS_SECRETS.local.md`.

## 1) Узлы и роли

## 1.0 Публичные данные доступа (без секретов)
Секреты (пароли/токены) см. `C:\programming\AGENTS_SECRETS.local.md`.

Metasiberia v2:
- SSH login: `root`
- SSH alias (локально): `metasiberia-v2` (в `C:\Users\densh\.ssh\config`)

TheRift:
- SSH login: `root`

DNS-панель:
- URL: `https://dnsadmin.hosting.reg.ru/manager/ispmgr`
- Логин: `ce105715135`

REG.RU hosting metasiberia.com (ISPmanager):
- URL: `https://server263.hosting.reg.ru:1500/`
- Логин: `u2978374`

### 1.1 Metasiberia v2 (основной)
- IP: `89.104.70.23`
- Роль: основной Substrata server + встроенный C++ webserver (сайт/админка/регистрация).
- Публичный домен (основной): `https://vr.metasiberia.com/`
- Текущие URL (по IP):
  - `https://89.104.70.23/`
  - `https://89.104.70.23/signup`
- Слушаемые порты (факт):
  - `server`: `80`, `443`, `7600`
  - `nginx`: `3443` (не является обязательной частью web/админки, но присутствует)
- Server state dir: `/root/cyberspace_server_state`
  - Конфиг: `/root/cyberspace_server_state/substrata_server_config.xml`
  - Credentials: `/root/cyberspace_server_state/substrata_server_credentials.txt`
  - TLS по умолчанию: `MyCertificate.crt`, `MyKey.key` (обычно в state dir, возможно symlink на `/etc/letsencrypt/...`)

### 1.2 TheRift (второй сервер)
- IP: `95.163.227.206`
- Роль: отдельный сервер/площадка (назначение уточняется).
- Рекомендуемое использование (чтобы не рисковать продом):
  - staging/тестовый сервер для проверки новых сборок `server` и web-изменений перед выкатыванием на `Metasiberia v2`;
  - площадка для нагрузочных/интеграционных тестов, чтобы не влиять на пользователей.
- Доступ: SSH (пароль в `C:\programming\AGENTS_SECRETS.local.md`).
- Примечание: если SSH ругается на host key mismatch, сначала проверить отпечаток ключа и обновить `known_hosts` осознанно.

### 1.3 Hosting/DNS metasiberia.com (REG.RU)
- Управление DNS/хостингом ведется через REG.RU (см. URL/логины выше).
- Важно: `metasiberia.com` сейчас может быть занят внешним лендингом/прокси; нельзя “просто” перевести A-record корня на `89.104.70.23` без риска сломать лендинг.

## 2) Цель: уйти с IP на домен, не ломая текущее

Требования:
- Текущее по IP должно продолжать работать на время миграции.
- Новый “правильный” адрес должен быть доменным (`https://<host>/`).
- TLS: валидный сертификат на домен.
- Письма (reset password и др.) должны содержать доменную ссылку, а не IP.

### 2.1 Рекомендуемая стратегия (без ломания корня домена)
1. Выбрать поддомен под основной сервер (пример: `vr.metasiberia.com` или `app.metasiberia.com`).
2. В DNS создать `A` запись поддомена -> `89.104.70.23`.
3. Выпустить Let's Encrypt сертификат на этот поддомен.
4. Включить “канонический хост” в конфиге сервера, чтобы:
   - запросы на `https://89.104.70.23/...` 301 редиректились на `https://<поддомен>/...`
   - запросы на `http://<поддомен>/...` редиректились на `https://<поддомен>/...`

Если корневой домен `metasiberia.com` нужно использовать именно как основной webserver Substrata, это отдельное решение (и, вероятно, потребует переноса/изменения текущего лендинга).

### 2.2 Факт по DNS (снимок на 2026-02-15)
- `metasiberia.com` -> `176.57.65.17`
- `www.metasiberia.com` -> `176.57.65.17`
- `vr.metasiberia.com` -> `89.104.70.23`
- `89.104.70.23` reverse: `89-104-70-23.cloudvps.regruhosting.ru`

### 2.3 Текущее состояние миграции (2026-02-15)
- `https://vr.metasiberia.com/` считается основным публичным адресом сервера/сайта.
- Прямой доступ по IP (`https://89.104.70.23/`) сохраняем как fallback/диагностику.

## 3) TLS (Let's Encrypt) без остановки сервера

В код добавлена поддержка http-01 challenge (файлы вида `/.well-known/acme-challenge/<token>`), чтобы можно было выпускать сертификат через `certbot --webroot` без необходимости временно останавливать сервер.

### 3.1 Настройка webroot для ACME
В `/root/cyberspace_server_state/substrata_server_config.xml` нужно задать:
- `letsencrypt_webroot_dir` (директория webroot)

Ожидаемый путь файла challenge:
- `<letsencrypt_webroot_dir>/.well-known/acme-challenge/<token>`

Пример (концептуально):
```xml
<config>
  <letsencrypt_webroot_dir>/root/cyberspace_server_state/letsencrypt_webroot</letsencrypt_webroot_dir>
</config>
```

После этого можно использовать `certbot` в режиме webroot (конкретные команды зависят от окружения на сервере).

## 4) Канонический доменный адрес (редирект с IP на домен)

В конфиг добавлен параметр:
- `canonical_web_hostname` (например `vr.metasiberia.com`)

Поведение:
- Если `canonical_web_hostname` задан, сервер будет делать 301 редирект на канонический хост, сохраняя `path` и query-параметры.
- Если параметр пустой, поведение не меняется.

Пример (концептуально):
```xml
<config>
  <canonical_web_hostname>vr.metasiberia.com</canonical_web_hostname>
</config>
```

### 4.1 Факт по текущему TLS на Metasiberia v2 (снимок на 2026-02-15)
В state dir обычно лежат `MyCertificate.crt` и `MyKey.key`. На основном сервере они сейчас указывают на Let's Encrypt cert для `vr.metasiberia.com` (через symlink в `/etc/letsencrypt/live/...`).

## 5) Почта: отправка писем и “почтовая аутентификация” домена

### 5.1 Отправка писем из сервера (уже поддержано)
Сервер читает SMTP-настройки из credentials-файла:
- `email_sending_smtp_servername`
- `email_sending_smtp_username`
- `email_sending_smtp_password`
- `email_sending_from_name`
- `email_sending_from_email_addr`
- `email_sending_reset_webserver_hostname` (хост, который вставляется в ссылку reset password)

Важно:
- Значение `email_sending_reset_webserver_hostname` должно соответствовать выбранному доменному адресу (поддомену), иначе ссылки в письмах будут вести на старый хост/IP.
- Секреты (username/password) остаются только в credentials и/или `AGENTS_SECRETS.local.md`, не в репозитории.

### 5.2 DNS-аутентификация почты (SPF/DKIM/DMARC)
Чтобы письма не попадали в спам, для домена нужно настроить:
- SPF (TXT)
- DKIM (обычно CNAME/TXT, зависит от SMTP-провайдера)
- DMARC (TXT)

Конкретные значения берутся у SMTP-провайдера (например Mailgun/SendGrid/другой).  
Этот шаг делается в DNS-панели домена и не требует изменений в коде.

## 6) Аутентификация web: минимальные улучшения безопасности (без ломания)

Серверные сессии используют cookie `site-b`. В коде добавлено:
- `SameSite=Lax`
- `Secure` только при TLS-соединении
- `HttpOnly` сохранено

Это снижает риск CSRF и утечек cookie, при этом не ломает dev HTTP-сценарии.

## 7) Обмен между серверами (текущее состояние и намерение)

Текущее (факт):
- Основная логика мира/аккаунтов/веба живет на `89.104.70.23`.
- TheRift существует как отдельный сервер, но схема обмена/репликации данных не зафиксирована в коде и должна быть явно описана перед внедрением (чтобы не сломать совместимость и не получить рассинхрон).

Рекомендуемое правило:
- Пока нет формализованного протокола/репликации, считать сервера независимыми.
- Если понадобится обмен (например, общие аккаунты/SSO/общий каталог worlds), нужно проектировать как отдельный слой с явными контрактами и документацией (и с учетом `SERVER_PROTOCOL.md`).

## 8) Что менять в первую очередь (миграция “без боли”)
1. DNS: завести поддомен под основной сервер -> `89.104.70.23`.
2. Выпустить TLS сертификат на поддомен через ACME http-01 webroot.
3. Обновить credentials: `email_sending_reset_webserver_hostname=<поддомен>`.
4. Включить `canonical_web_hostname=<поддомен>` (после проверки, что DNS+TLS готовы).

## 9) Данные сайта, пользователи и “база”

### 9.0 Что такое webserver_fragments
`webserver_fragments` — это набор HTML-фрагментов (`*.htmlfrag`), которые встроенный C++ webserver подгружает с диска для некоторых страниц (about/docs/help и т.п.).  
Это не “отдельный сайт”, а часть сервера: страницы собираются из C++ обработчиков + этих шаблонов/фрагментов.

### 9.1 Где лежат данные сайта на основном сервере
Факт по `89.104.70.23`:
- Public files (CSS/JS/PNG) используются из: `/root/cyberspace_server_state/webserver_public_files`
- Webclient (wasm/html) используется из: `/root/cyberspace_server_state/webclient`
- HTML fragments: сейчас `webserver_fragments_dir` в конфиге закомментирован; фактически фрагменты берутся из дефолтного пути (см. `AGENTS.md`), либо из дистрибутива сервера. Если нужно удобно редактировать фрагменты, рекомендуется явно задать `webserver_fragments_dir` в `/root/cyberspace_server_state/substrata_server_config.xml` и держать их рядом со state dir.

### 9.2 Быстрое редактирование сайта (рекомендуемый workflow)
Рекомендация: исходники web-части держим в git (в этом репозитории), а на сервер выкатываем синхронизацией.
Добавлен скрипт деплоя статики/фрагментов на основной сервер:
- `scripts/deploy_web_to_metasiberia_v2.ps1`

Перед первой выкладкой (чтобы ничего не потерять), можно снять snapshot текущих серверных директорий:
- `scripts/snapshot_web_from_metasiberia_v2.ps1`

Важно:
- На сервере сейчас `webserver_public_files_dir` и `webclient_dir` уже переопределены в `/root/cyberspace_server_state/...` (через `substrata_server_config.xml`).
- `webserver_fragments_dir` пока НЕ переопределен, значит на Linux по умолчанию используется `/var/www/cyberspace/webserver_fragments`.
- На Linux сервер использует inotify-watcher. Поэтому выкладка должна обновлять содержимое директорий *in-place* (без `mv`/rename самой директории), иначе watcher “теряет” обновления. Скрипт деплоя учитывает это (rsync).

### 9.3 Пользователи и “БД” (как сейчас устроено)
Сервер хранит состояние (включая пользователей, парсели, сессии и т.п.) в файле базы:
- `/root/cyberspace_server_state/server_state.bin`

Суперадмин (god user):
- `UserID == 0` (`shared/UserID.h`).
  - Практически: самый первый зарегистрированный пользователь на новом пустом сервере получает id `0` и становится суперадмином.

Web-админка (основные страницы):
- `/admin` (главная)
- `/admin_users` (список пользователей)
- `/admin_user/<id>` (карточка пользователя)

## 10) Figma MCP (для быстрой работы с дизайном)
Figma MCP (Talk To Figma MCP) — это локальный dev-инструмент. Он не “часть прод-сайта”, а связка:
- локальный сокет-сервер на твоем ПК (`ws://localhost:3055`);
- плагин в Figma;
- инструменты в IDE (Cursor/Codex), которые могут читать контекст дизайна.

Скрипт запуска локального сокет-сервера:
- `scripts/start_figma_mcp_socket.ps1`

Данные подключения (file key / channel / порт) см. `C:\programming\AGENTS.md`.

Полезные операции (на сервере, под root):
- Сделать быстрый backup базы перед выкатыванием изменений:
  - `cp -a /root/cyberspace_server_state/server_state.bin /root/cyberspace_server_state/server_state.bin.bak_$(date +%Y%m%d_%H%M%S)`
- Проверить, что сервер жив и слушает порты:
  - `ss -lntp | egrep ':(80|443|7600)\\s'`
