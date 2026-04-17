# Кнопка Web-режима в тулбаре (2026-04-17)

## Что добавлено

- В `gui_client/URLWidget.ui` добавлена кнопка `browserPushButton` рядом с кнопкой "Назад" в адресной панели.
- В `gui_client/MainWindow.cpp` добавлен обработчик `openCurrentLocationInBrowserSlot()`.

## Поведение

- При нажатии кнопки открывается внешний браузер.
- Формируется ссылка web-клиента по текущему положению аватара:
  - путь берётся из `GUIClient::getCurrentWebClientURLPath()` (`/visit?...`).
  - учитываются `world`, `x`, `y`, `z`, `heading`.
- Работает из любого мира:
  - root-мир,
  - личный мир,
  - пользовательский/созданный мир (`username/world`).

## Хост и протокол

- Если хост пустой, используется `vr.metasiberia.com`.
- Для `89.104.70.23` применяется канонизация к `vr.metasiberia.com`.
- Для `localhost` и `127.0.0.1` используется `http://`, для остальных — `https://`.

## UI-иконка

- Для кнопки используется иконка-планета: `resources/buttons/browser_globe.png`.
- В рантайме загрузка идёт из `data/resources/buttons/browser_globe.png`.
- Если файл иконки недоступен, используется системная иконка сети (`QStyle::SP_DriveNetIcon`), а затем fallback-текст `Web`.
