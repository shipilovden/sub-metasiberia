# Image Resource Basis Fallback

Дата: 2026-03-23

## Что произошло

На `vr.metasiberia.com` и связанных Metasiberia-host'ах клиент получал capability-флаг поддержки basis-текстур, но в runtime-логе фиксировались массовые ответы сервера вида:

- `Server couldn't send resource '... .basis' (resource not found)`

Из-за этого клиент запрашивал `.basis`-файлы вместо оригинальных `png/jpg/webp`, и изображения оставались белыми или пустыми.

## Что сделано

- В клиенте добавлен host-specific fallback: для Metasiberia-host'ов basis texture requests отключаются сразу после handshake с сервером.
- Клиент теперь использует оригинальные texture resources вместо отсутствующих `.basis`.

## Зачем это нужно

- Это совместимо с текущим серверным состоянием Metasiberia.
- Это не трогает XR-логику.
- Это даёт клиентский workaround, пока серверный набор basis-ресурсов не будет приведён в консистентное состояние.
