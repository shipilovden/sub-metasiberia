# Серверы Metasiberia и обмен между ними

Документ описывает текущую инфраструктуру после переезда на новый сервер: домены, Caddy reverse proxy/TLS, Metasiberia C++ backend, TheRift Hyperfy, отправку писем и минимальные улучшения аутентификации.
Секреты (пароли/токены/ключи) здесь не храним: см. `C:\programming\AGENTS_SECRETS.local.md`.

> **Статус проверки 2026-07-10:** архитектура и repo paths сверены с кодом и документацией. Состояния services, DNS, symlink targets и датированные incident snapshots не проверялись на production в рамках documentation-only аудита; перед любой операцией их нужно подтвердить заново. Никакая команда из этого файла сама по себе не является разрешением на deploy/restart/restore.

## 1) Узлы и роли

## 1.0 Публичные данные доступа (без секретов)
Секреты (пароли/токены) см. `C:\programming\AGENTS_SECRETS.local.md`.

Новый основной сервер:
- SSH login: `denshipilov`
- SSH alias (локально): `metasiberia-server` (в `C:\Users\densh\.ssh\config`)
- Windows SSH note: если `sudo -S` через OpenSSH pipe сообщает `Authentication failed`, это может быть ошибка TTY/кавычек. Сначала проверить доступ через PuTTY/plink с подтверждённым fingerprint сервера; в ходе production deploy такой способ подтвердил корректность сохранённого пароля. Пароли и токены в этот документ не копировать.
- LAN IP: `192.168.0.30`
- Public IP: `87.103.196.229`

Metasiberia v2 (старый fallback/архив):
- SSH login: `root`
- SSH alias (локально): `metasiberia-v2` (в `C:\Users\densh\.ssh\config`)
- IP: `185.182.110.184`

TheRift (старый источник/архив):
- SSH login: `root`
- IP: `130.49.151.103`

DNS-панель:
- URL: `https://dnsadmin.hosting.reg.ru/manager/ispmgr`
- Логин: `ce105715135`

REG.RU hosting metasiberia.com (ISPmanager):
- URL: `https://server263.hosting.reg.ru:1500/`
- Логин: `u2978374`

### 1.1 Новый основной Metasiberia server
- LAN IP: `192.168.0.30`
- Public IP: `87.103.196.229`
- Роль: основной Substrata game server + Caddy public TLS/reverse proxy + встроенный C++ webserver backend (сайт/админка/регистрация) + TheRift Hyperfy.
- Публичные production-домены:
  - `https://vr.metasiberia.com/` -> Caddy -> Metasiberia C++ webserver
  - `https://rift.metasiberia.com/` -> Caddy -> TheRift Hyperfy
- Текущие URL по IP:
  - `http://87.103.196.229/` -> 301 на `https://vr.metasiberia.com/`
  - `http://87.103.196.229:3002/` закрыт UFW; backend Hyperfy проверять с сервера через `http://127.0.0.1:3002/`
- Слушаемые порты (факт):
  - `caddy`: `80/tcp`, `443/tcp`
  - `metasiberia-server.service`: `8080/tcp`, `8443/tcp`, `7600/tcp`, `7601/udp`
  - `therift-hyperfy.service`: `3002/tcp` только локально/за UFW
- Вспомогательные сервисы:
  - `metasiberia-bot.service`: `/srv/metasiberia/data/services/metasiberia-bot`, symlink `/opt/metasiberia-bot`, подключается к `127.0.0.1:7600`
  - `metasiberia-screenshot-gui.service`: headless `gui_client --screenshotslave` под Xvfb, локально слушает `34534` для screenshot commands
  - `metasiberia-screenshot-bot.service`: подключается к `127.0.0.1:7600`, получает screenshot/map tile requests и отдаёт JPEG обратно в game server
  - перед стартом `metasiberia-screenshot-bot.service` выполняется `/usr/local/bin/metasiberia_restart_screenshot_gui_and_wait.sh`, который перезапускает GUI slave и ждёт локальный порт `34534`; это важно, потому что GUI slave принимает одно socket-соединение на процесс
  - runtime для screenshot GUI/bot: `/srv/metasiberia/output/test_builds`; обязательные runtime resources находятся рядом в `/srv/metasiberia/output/test_builds/data`
  - config screenshot bot: `/home/denshipilov/.glare_technologies/Cyberspace/screenshot_bot_config.xml`
  - screenshot GUI профиль: `/home/denshipilov/.config/Glare Technologies/Cyberspace.conf` должен содержать QSettings credentials для `127.0.0.1` / `screenshot_bot` и `LoginDialog\auto_login=true`, иначе GUI slave подключится анонимно и bot не будет вести себя как старый screenshot user
  - для Xvfb/llvmpipe держать в screenshot GUI профиле `setting/MSAA=false`, `setting/bloom=false`, `setting/shadows=false`, `setting/SSAO=false`; тяжёлая графика может подвешивать map tile rendering на software OpenGL
  - `metasiberia-map-progress.timer`: каждые 15 минут запускает `/usr/local/bin/metasiberia_map_maintenance.py sample`
  - `metasiberia-map-refresh.timer`: ежедневный low-cost refresh через `/usr/local/bin/metasiberia_map_maintenance.py regen`
  - map maintenance использует `METASIBERIA_BASE_URL=https://127.0.0.1:8443`, так как с самого сервера публичный `https://vr.metasiberia.com` может не открываться через router hairpin/NAT loopback
  - `metasiberia-map-progress.timer` и `metasiberia-map-refresh.timer` только проверяют/помечают tiles; фактический рендер тайлов делает `metasiberia-screenshot-bot.service`
  - real-map ground/minimap tiles мира `sub://vr.metasiberia.com/map` идут через `/osm_tile/<namespace>/<z>/<x>/<y>.png`; актуальный namespace клиента и webserver default — `metasiberia_raster_v4`, чтобы не переиспользовать старый кэш с `OpenStreetMap 403 Access blocked`
- Backup:
  - `metasiberia-backup.timer`: ежедневно запускает `/srv/metasiberia/bin/backup_server_state.sh`
  - source: `/home/denshipilov/cyberspace_server_state` -> `/srv/metasiberia/data/state/cyberspace_server_state.candidate-20260621`
  - destination: `/srv/metasiberia/data/backups`
  - retention: 7 дней
  - первый полный архив активного state создан 2026-06-21: `metasiberia-server_cyberspace_server_state_20260621_152746.tar.gz` (~20 GiB)
  - state backup включает весь активный каталог state, а не только `server_state.bin`; архивы около 20 GiB допустимы после миграции, но 35+ GiB считать аномалией и проверять `du -sh`/состав архива перед внешним pull
- Service backup / health:
  - `metasiberia-services-backup.timer`: отдельный ежедневный backup TheRift Hyperfy + Sniper в `/srv/metasiberia/data/backups/services`, retention 14 дней
  - `metasiberia-healthcheck.timer`: каждые 5 минут проверяет systemd-сервисы, локальные HTTP endpoints, backup age, map JSON, UFW, disk usage и SMART; статус: `/srv/metasiberia/data/health/health.json`
  - `metasiberia-restore-check.timer`: еженедельный smoke-check восстановления backup; полный smoke-check service+state archive 2026-06-22 прошёл успешно
  - Windows external pull-backup: `C:\programming\substrata\scripts\metasiberia_backup_pull.ps1`, destination `E:\MetasiberiaBackups`, scheduled task `MetasiberiaBackupPull` в 12:30 локального времени
  - Windows pull-скрипт имеет предохранитель `MaxStateArchiveGB` и по умолчанию не качает state-архивы больше 35 GiB без ручной проверки
- Laptop/server settings:
  - `enp2s0f0` статически настроен через netplan на `192.168.0.30/24`, gateway `192.168.0.1`
  - lid close игнорируется; `sleep.target`, `suspend.target`, `hibernate.target`, `hybrid-sleep.target` замаскированы
- GUI/admin:
  - Cockpit установлен и включён через `cockpit.socket`
  - LAN URL: `https://192.168.0.30:9090/`
  - UFW rule: `9090/tcp` разрешён только из `192.168.0.0/24`
- Router/NAT: `192.168.0.1`, правило `SubstrataServer` -> `192.168.0.30` для `80/tcp`, `443/tcp`, `7600/tcp`, `7601/udp`. Если `3002/tcp` ещё есть в NAT роутера, серверный UFW его блокирует.
- Caddy config: `/etc/caddy/Caddyfile`
- Server state dir: `/home/denshipilov/cyberspace_server_state` -> `/srv/metasiberia/data/state/cyberspace_server_state.candidate-20260621`
  - Конфиг: `/home/denshipilov/cyberspace_server_state/substrata_server_config.xml`
  - Credentials: `/home/denshipilov/cyberspace_server_state/substrata_server_credentials.txt`
  - Web fragments: `/home/denshipilov/cyberspace_server_state/webserver_fragments`
  - Public web files: `/home/denshipilov/cyberspace_server_state/webserver_public_files`
  - В production-конфиге web ports: `web_http_port=8080`, `web_https_port=8443`
  - В production-конфиге должен быть `webserver_fragments_dir`, иначе главная админки теряет HTML-фрагмент с логотипом/названием
  - Публичный TLS выпускает и обновляет Caddy; backend TLS Metasiberia остаётся внутренним за Caddy.
  - Ожидаемое оформление главной: центральный логотип/название, фотоплёнка screenshots над footer, footer внизу страницы.
  - Telegram screenshot publishing: Telegram credentials присутствуют в server credentials; на 2026-06-22 DNS давал недоступный Telegram IP, поэтому в `/etc/hosts` добавлен override `api.telegram.org -> 149.154.167.220`. `getMe/getChat` проходят для `metasiberia_bot` и канала `metasiberia_channel`.

#### Быстрая сборка и выкладка C++ server на `metasiberia-server`

Это единственный production workflow для игрового сервера: production работает на Ubuntu/Linux. Сборка Windows `server.exe` не обновляет сервер и не заменяет Linux ELF. После любой правки `server/**` или серверной части `shared/**` нужно выполнить этот Linux build/release/restart workflow; если sudo-доступ не работает, остановиться и запросить актуальные полномочия, не переключая `current` вручную.

Обычный быстрый путь не требует пересборки всего проекта и раньше работал именно так:

```bash
ssh metasiberia-server
cd /srv/metasiberia/src/sub-metasiberia
cmake --build /srv/metasiberia/build/master --target server -j 4
ts=$(date +%Y%m%d_%H%M%S)
short=$(git rev-parse --short HEAD)
rel=/srv/metasiberia/releases/master-$short-$ts
mkdir -p "$rel"
cp -a /srv/metasiberia/output/test_builds/server "$rel/server"
chmod 755 "$rel/server"
ln -sfn "$rel" /srv/metasiberia/releases/current.next
ln -sfn "$rel" /srv/metasiberia/releases/current
sudo systemctl restart metasiberia-server.service
```

После рестарта проверять:

```bash
systemctl is-active metasiberia-server.service
ss -ltn | grep ':7600'
ss -ltn | grep ':8080'
ss -ltn | grep ':8443'
curl -k -sS --max-time 10 https://127.0.0.1:8443/world/map | head
```

Важно: `ExecStart` unit-файла смотрит на `/srv/metasiberia/releases/current/server`, а рабочая директория сервиса — `/srv/metasiberia/src/sub-metasiberia`. Не копировать Windows `server.exe`: production binary всегда Linux ELF `server`.

Рабочий режим проекта: изменения часто идут одновременно в Qt-клиент, C++ game server, webserver/resource pipeline, карту и чат. После правок протокола, вложений, личных сообщений, карты или ресурсообмена проверять оба конца: локальную Windows Qt-сборку клиента и Linux production/server build path. Не считать баг карты или чата “только UI”, пока не проверены protocol messages, resource upload/download, webserver endpoints и systemd-сервисы.

Если MiniMap показывает “Загрузка карты...”, проверять отдельно:

```bash
curl -k -I https://127.0.0.1:8443/osm_tile/metasiberia_raster_v4/6/37/22.png
systemctl is-active metasiberia-screenshot-gui.service metasiberia-screenshot-bot.service
journalctl -u metasiberia-server.service --since '20 minutes ago' | grep -Ei 'QueryMapTiles|osm|tile|resource|error|exception'
```

`/osm_tile/...` должен отдавать PNG 200 через встроенный webserver. `metasiberia-screenshot-bot.service` нужен для render/map-tile screenshots и обновления server-world tiles; отключённый screenshot-bot не должен ломать прямой `/osm_tile`, но может оставлять карту/тайлы неактуальными.

Перед выкладкой server-бинаря проверять размер и читаемость state:

```bash
ls -lh /home/denshipilov/cyberspace_server_state/server_state.bin
systemctl show metasiberia-server.service -p ActiveState -p SubState -p NRestarts --no-pager
```

Если `server_state.bin` уже аномально большой или сервис в цикле `Read past end of file`, это не проблема rebuild/restart. Сначала надо остановить restart-loop и восстановить state из последнего рабочего snapshot/backup, сохранив битый файл под `server_state.bin.bad-*` для анализа.

Инцидент 2026-06-29/30:
- после попытки выкладки chat attachment server-патча обнаружен уже раздувшийся/битый state;
- сохранены проблемные файлы:
  - `/home/denshipilov/cyberspace_server_state/server_state.bin.bad-after-chatdeploy-20260629_134610` (~101 GiB);
  - `/home/denshipilov/cyberspace_server_state/server_state.bin.bad-after-new-binary-corrupt-20260629_135745` (~88 MiB);
  - `/home/denshipilov/cyberspace_server_state/server_state.bin.bad-after-save-20260627_034900` (~77 MiB);
- рабочий fallback snapshot:
  - `/home/denshipilov/cyberspace_server_state/server_state.bin.bak_20260627_065809_before_map_world_fix` (~84 MiB);
- на 2026-06-30 00:04 UTC production был в restart-loop: `Read past end of file`, `NRestarts=4224`, активный `server_state.bin` ~1.2 GiB;
- восстановлено 2026-06-30 00:08 UTC из `/home/denshipilov/cyberspace_server_state/server_state.bin.bak_20260627_065809_before_map_world_fix`; битый файл сохранён как `/home/denshipilov/cyberspace_server_state/server_state.bin.bad-restore-20260630_000456`; после восстановления `metasiberia-server.service`, `metasiberia-bot.service`, `metasiberia-screenshot-gui.service`, `metasiberia-screenshot-bot.service`, `caddy.service` были `active`, порты `7600/8080/8443` слушали, `server_state.bin` был ~89 MiB.
- на 2026-06-30 01:10 UTC после reboot production снова ушёл в restart-loop с `Read past end of file`; активный state (~133 MiB) сохранён как `/home/denshipilov/cyberspace_server_state/server_state.bin.bad-restore-noconnect-20260630_011123`;
- восстановлено 2026-06-30 01:13 UTC из того же fallback snapshot; перед стартом остановлены `metasiberia-map-progress.timer`, `metasiberia-map-refresh.timer`, `metasiberia-screenshot-bot.service`, чтобы screenshot/map writers не спровоцировали повторную порчу state до отдельной диагностики. После восстановления `metasiberia-server.service`, `metasiberia-bot.service`, `caddy.service` `active`, порты `7600/8080/8443` слушают, проверка с Windows до `vr.metasiberia.com:7600` проходит.
- 2026-06-30 01:57 UTC `metasiberia-screenshot-bot.service` включён обратно точечно для восстановления server-world MiniMap tiles; bot подключился к `127.0.0.1:7600`, получает `map tile screenshot request`, GUI slave делает screenshots, сервер сохраняет `map_tile_screenshot_*.jpg` как resources. `metasiberia-map-progress.timer` и `metasiberia-map-refresh.timer` пока оставлены выключенными до отдельной проверки admin-token/map-maintenance, чтобы не плодить лишние state changes.

### 1.2 TheRift / Hyperfy / Sniper
- Старый IP TheRift: `130.49.151.103`.
- Hyperfy перенесен на новый основной сервер:
  - путь: `/srv/metasiberia/data/services/therift/hyperfy`
  - symlink: `/var/www/hyperfy`
  - сервис: `therift-hyperfy.service`
  - production URL: `https://rift.metasiberia.com/`
  - backend/diagnostic URL: `http://127.0.0.1:3002/` с самого сервера; внешний `http://87.103.196.229:3002/` закрыт UFW
  - public env: `PUBLIC_WS_URL=wss://rift.metasiberia.com/ws`, `PUBLIC_API_URL=https://rift.metasiberia.com/api`, `PUBLIC_ASSETS_URL=https://rift.metasiberia.com/assets`
- Sniper перенесен на новый основной сервер:
  - путь: `/srv/metasiberia/data/services/therift/sniper-bot`
  - symlink: `/opt/sniper-bot`
  - сервисы: `sniper-bot.service`, `sniper-dryrun.service`
- `metasiberia-walk-bot` и `openclaw` не нужны, не переносить и не запускать.
- Старый TheRift `130.49.151.103` после переноса погашен как runtime: Hyperfy удален из PM2, `pm2-root.service` выключен, `caddy.service` выключен. Старый сервер оставлен только как SSH-архив.
- Снимок старой Hyperfy DB сохранен на новом сервере как архив миграции: `/srv/metasiberia/data/migration-archive/therift-old-20260621/db.sqlite`. Рабочую новую DB им не перетирать без отдельного решения.
- `riftworld.duckdns.org` устарел и не используется; попытка обновления старым найденным token дала `KO`, нужен актуальный DuckDNS token только если решим возвращать DuckDNS.
- Доступ: SSH (пароль в `C:\programming\AGENTS_SECRETS.local.md`).
- Примечание: если SSH ругается на host key mismatch, сначала проверить отпечаток ключа и обновить `known_hosts` осознанно.

### 1.3 Hosting/DNS metasiberia.com (REG.RU)
- Управление DNS/хостингом ведется через REG.RU (см. URL/логины выше).
- Важно: `metasiberia.com` сейчас может быть занят внешним лендингом/прокси; нельзя “просто” перевести A-record корня на `87.103.196.229` без риска сломать лендинг.

## 2) Доменная схема

Требования после cutover:
- Основные пользовательские адреса должны быть доменными.
- TLS на публичных доменах обслуживает Caddy.
- IP-адреса допускаются только для диагностики/legacy.
- Письма (reset password и др.) должны содержать доменную ссылку, а не IP.

### 2.1 Реализованная стратегия (без ломания корня домена)
1. Корень `metasiberia.com` и `www.metasiberia.com` оставлены на REG.RU shared hosting.
2. `vr.metasiberia.com` указывает на новый основной сервер и обслуживает Metasiberia web/admin/game.
3. `rift.metasiberia.com` указывает на новый основной сервер и обслуживает TheRift Hyperfy.
4. Caddy занимает публичные `80/443`, выпускает Let's Encrypt сертификаты и проксирует backend-сервисы.
5. Metasiberia C++ webserver слушает внутренние `8080/8443`, чтобы не конфликтовать с Caddy.

Если корневой домен `metasiberia.com` нужно использовать именно как основной webserver Substrata, это отдельное решение (и, вероятно, потребует переноса/изменения текущего лендинга).

### 2.2 Факт по DNS (обновлено 2026-06-21)
- `metasiberia.com` -> `37.140.192.242` (REG.RU хостинг, лендинг)
- `www.metasiberia.com` -> `37.140.192.242` (REG.RU хостинг)
- `vr.metasiberia.com` -> `87.103.196.229` (новый основной игровой сервер)
- `www.vr.metasiberia.com` -> `87.103.196.229`
- `rift.metasiberia.com` -> `87.103.196.229` (TheRift Hyperfy на новом сервере)
- `riftworld.duckdns.org` -> `95.163.227.206` (устаревшая запись, не использовать как текущую)

### 2.3 Текущее состояние (2026-06-21)
- `https://vr.metasiberia.com/` — основной публичный адрес сервера/сайта.
- `https://rift.metasiberia.com/` — основной публичный адрес TheRift Hyperfy.
- `http://87.103.196.229/` редиректит на `https://vr.metasiberia.com/`.
- Старый Metasiberia v2 `185.182.110.184` остановлен как production (`substrata.service inactive`) и оставлен как fallback/архив.
- TheRift Hyperfy снаружи доступен только через `https://rift.metasiberia.com/`; прямой `http://87.103.196.229:3002/` закрыт UFW.

## 3) TLS (Let's Encrypt) и Caddy

Текущий production TLS обслуживает Caddy. Сертификаты на `vr.metasiberia.com`, `www.vr.metasiberia.com` и `rift.metasiberia.com` автоматически выпущены через Let's Encrypt и хранятся в storage Caddy (`/var/lib/caddy/...`).

В коде Metasiberia также есть поддержка http-01 challenge (файлы вида `/.well-known/acme-challenge/<token>`), но после перехода на Caddy это fallback/legacy-возможность, а не основной production-путь.

### 3.1 Legacy fallback: webroot для ACME

Основной production TLS обслуживает Caddy, поэтому этот раздел нужен только при явном возврате к встроенному http-01 fallback. В active `/home/denshipilov/cyberspace_server_state/substrata_server_config.xml` тогда задаётся:
- `letsencrypt_webroot_dir` (директория webroot)

Ожидаемый путь файла challenge:
- `<letsencrypt_webroot_dir>/.well-known/acme-challenge/<token>`

Пример (концептуально):
```xml
<config>
  <letsencrypt_webroot_dir>/home/denshipilov/cyberspace_server_state/letsencrypt_webroot</letsencrypt_webroot_dir>
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
В state dir обычно лежат `MyCertificate.crt` и `MyKey.key`. После перехода на Caddy публичный сертификат обслуживает Caddy, а TLS Metasiberia на `8443` считается внутренним backend TLS за reverse proxy.

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

## 7) Обмен между серверами (документированный снимок и намерение)

Снимок, записанный 2026-06-21/22 и не проверенный live 2026-07-10; перед использованием требуется read-only readback:
- Основная логика мира/аккаунтов/веба живет на новом основном сервере `metasiberia-server` (`192.168.0.30` LAN / `87.103.196.229` public).
- TheRift Hyperfy и Sniper физически перенесены на тот же новый сервер, но остаются отдельными сервисами и каталогами данных.
- Схема обмена/репликации данных между Substrata и Hyperfy/Sniper не зафиксирована в коде и должна быть явно описана перед внедрением (чтобы не сломать совместимость и не получить рассинхрон).

Рекомендуемое правило:
- Пока нет формализованного протокола/репликации, считать сервера независимыми.
- Если понадобится обмен (например, общие аккаунты/SSO/общий каталог worlds), нужно проектировать как отдельный слой с явными контрактами и документацией (и с учетом `SERVER_PROTOCOL.md`).

## 8) Что было зафиксировано как сделанное

Следующий список — operational snapshot на 2026-06-21/22, дополненный датированными incident notes ниже/выше. Он не является live health report на 2026-07-10.

1. DNS для `vr.metasiberia.com`, `www.vr.metasiberia.com` и `rift.metasiberia.com` указывает на `87.103.196.229`.
2. Caddy запущен и включен в автозагрузку; `metasiberia-server.service` больше не занимает публичные `80/443`.
3. В production-конфиг Metasiberia добавлены `web_http_port=8080` и `web_https_port=8443`.
4. TheRift Hyperfy `.env` переведен на `https://rift.metasiberia.com/` и `wss://rift.metasiberia.com/ws`.
5. Прямой внешний доступ `:3002` закрыт через UFW; TheRift public flow должен идти через Caddy/443.
6. `webserver_fragments` восстановлены с Metasiberia v2 на новом сервере.
7. `metasiberia-bot.service` перенесен на новый сервер и подключается локально к `127.0.0.1:7600`.
8. `metasiberia-screenshot-gui.service` и `metasiberia-screenshot-bot.service` подняты на новом сервере; бот успешно рендерит map tiles и обычные screenshots через headless GUI.
9. `metasiberia-map-progress.timer` и `metasiberia-map-refresh.timer` перенесены на новый сервер и используют локальный backend `https://127.0.0.1:8443`; публичные JSON доступны через встроенный webserver как `/files/map_progress.json` и `/files/map_refresh_status.json`.
10. Главная `https://vr.metasiberia.com/` проверена: центральный логотип/название, фотоплёнка над footer, footer внизу; `/map` отдаёт готовые JPEG tiles через `/tile`.
11. Telegram credentials найдены и сохранены на новом сервере; `api.telegram.org` закреплён в `/etc/hosts` на рабочий IP `149.154.167.220`, `getMe/getChat` проверены без отправки тестового сообщения.
12. Старые runtime-сервисы Metasiberia v2 и TheRift выключены; старые серверы оставлены как архивы/источники.

## 9) Данные сайта, пользователи и “база”

### 9.0 Что такое webserver_fragments
`webserver_fragments` — это набор HTML-фрагментов (`*.htmlfrag`), которые встроенный C++ webserver подгружает с диска для некоторых страниц (about/docs/help и т.п.).  
Это не “отдельный сайт”, а часть сервера: страницы собираются из C++ обработчиков + этих шаблонов/фрагментов.

### 9.1 Где лежат данные сайта на основном сервере
По operational snapshot 2026-06-21/22, не перепроверенному live 2026-07-10:
- Public files (CSS/JS/PNG) были настроены из: `/home/denshipilov/cyberspace_server_state/webserver_public_files`
- Webclient (wasm/html) был настроен из: `/home/denshipilov/cyberspace_server_state/webclient`
- HTML fragments были настроены из: `/home/denshipilov/cyberspace_server_state/webserver_fragments`.
- `/home/denshipilov/cyberspace_server_state` был записан как active state symlink; фактический target и config readback проверять непосредственно перед state/backup/web operation.
- Production config должен явно задавать `webserver_fragments_dir`, `webserver_public_files_dir` и `webclient_dir`. Linux defaults в коде указывают на `/var/www/cyberspace/...` и не доказывают фактический production path.

### 9.2 Редактирование сайта и historical scripts
Исходники web-части хранятся в git в этом репозитории. Способ синхронизации с новым production server должен быть подтверждён отдельно перед каждой выкладкой.

Скрипты `scripts/deploy_web_to_metasiberia_v2.ps1` и `scripts/snapshot_web_from_metasiberia_v2.ps1` содержат paths/assumptions старого Metasiberia v2 и сохраняются только как historical helpers. Они не являются текущим deployment workflow нового сервера.

Важно:
- Текущие production overrides должны указывать в active `/home/denshipilov/cyberspace_server_state/...`, а не в старый `/root/cyberspace_server_state/...`.
- На Linux сервер использует inotify-watcher. Поэтому будущая подтверждённая синхронизация должна обновлять содержимое директорий *in-place* (без `mv`/rename самой директории), иначе watcher “теряет” обновления. Historical v2 script использовал для этого `rsync`, но от этого не становится current production workflow.

### 9.2.1 Канонический workflow для web/admin Metasiberia

Этот раздел относится к сайту `https://vr.metasiberia.com/` и встроенной web-админке. Он не относится к WebClient/Emscripten и не требует пересборки клиентских артефактов.

Исходники в репозитории:
- серверные HTML-страницы, маршруты FAQ/Terms/Privacy и админки: `C:\programming\substrata\webserver\MainPageHandlers.cpp` и связанные обработчики в `webserver/`;
- общая локализация и browser-side поведение страниц: `C:\programming\substrata\webserver_public_files\site.js`;
- публичный адрес сайта: `https://vr.metasiberia.com/`;
- основные страницы: `/faq`, `/terms`, `/privacy`;
- web-админка: `/admin`, `/admin_users`, `/admin_user/<id>`.

Production paths на `metasiberia-server`:
- source checkout: `/srv/metasiberia/src/sub-metasiberia`;
- Linux build tree: `/srv/metasiberia/build/master`;
- Linux server output: `/srv/metasiberia/output/test_builds/server`;
- staged releases: `/srv/metasiberia/releases/master-<git-short>-<UTC-timestamp>/`;
- active release: `/srv/metasiberia/releases/current`;
- active public files: `/home/denshipilov/cyberspace_server_state/webserver_public_files/`;
- active HTML fragments: `/home/denshipilov/cyberspace_server_state/webserver_fragments/`;
- WebClient files: `/home/denshipilov/cyberspace_server_state/webclient/` (для web/admin deployment не изменять);
- systemd unit: `metasiberia-server.service`; его `ExecStart` должен использовать `/srv/metasiberia/releases/current/server`.

Правила выкладки:
1. Перед началом зафиксировать текущие `readlink -f /srv/metasiberia/releases/current`, SHA256 active binary и production `webserver_public_files/site.js`.
2. Сохранить backup текущего binary и `site.js` в `/srv/metasiberia/data/backups/<UTC-timestamp>/`; после копирования сверить SHA256 backup с исходными файлами.
3. Выполнить `git diff --check`. Для изменённого `site.js` дополнительно выполнить `node --check webserver_public_files/site.js`.
4. Если менялся `webserver/MainPageHandlers.cpp`, собирать только Linux target `server` в `/srv/metasiberia/build/master`; не собирать `client`, `gui_client`, WebClient, Emscripten и не запускать CMake/Ninja workflow для клиентских деревьев.
5. Создать новый staged release в `/srv/metasiberia/releases/`, скопировать туда именно проверенный Linux ELF `server`, проверить его SHA256 и наличие ожидаемых текстовых маркеров страницы. Не использовать Windows `server.exe`.
6. После проверки staged release обновить production `webserver_public_files/site.js` in-place; не переименовывать и не заменять каталоги `cyberspace_server_state` или `webserver_public_files`, поскольку watcher использует inotify.
7. Переключить сначала `releases/current.next`, затем `releases/current` на новый release. До переключения убедиться, что target release содержит текущие изменения FAQ/Terms/Privacy, если они входят в scope.
8. После переключения выполнить разрешённый оператором `sudo systemctl restart metasiberia-server.service`. Если restart выполняется вручную оператором, deployment считать ожидающим runtime-проверки до его завершения.
9. После restart проверить `ActiveState`, `MainPID`, `NRestarts`, фактический target `readlink -f /srv/metasiberia/releases/current`, SHA256 текущего binary и production `site.js`.
10. Выполнить HTTP smoke-check для `/faq`, `/terms` и `/privacy`. HTTP 200 сам по себе не подтверждает новую редакцию: дополнительно проверять ожидаемые текстовые маркеры и отдельно фиксировать runtime-проверку переключения RU/EN в браузере (выполнена или невозможна в текущей среде).

Rollback: вернуть `/srv/metasiberia/releases/current` на предварительно зафиксированный рабочий release, восстановить production `site.js` из backup in-place и перезапустить `metasiberia-server.service`; затем повторить проверки из пункта 9. Не трогать при web/admin deployment `database`, `server_state.bin`, authentication, cookie/consent, WebClient и другие страницы вне scope.

### 9.3 Пользователи и “БД” (как сейчас устроено)
Сервер хранит состояние (включая пользователей, парсели, сессии и т.п.) в файле базы:
- `/home/denshipilov/cyberspace_server_state/server_state.bin`

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

File key хранится в `docs/FIGMA_SITE_SYNC.md`; актуальный channel всегда брать из UI подключённого Figma plugin. Channel не фиксируется в `AGENTS.md` и не переиспользуется после reconnect.

Полезные операции (на сервере):
- Сделать быстрый backup базы перед выкатыванием изменений:
  - `sudo cp -a /home/denshipilov/cyberspace_server_state/server_state.bin /home/denshipilov/cyberspace_server_state/server_state.bin.bak_$(date +%Y%m%d_%H%M%S)`
- Проверить, что сервер жив и слушает порты:
  - `sudo ss -lntup | egrep ':(80|443|7600|8080|8443|3002)\\s'`
  - `sudo ss -lnup | egrep ':7601\\s'`
