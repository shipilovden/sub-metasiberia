# Audio Player: Музыкальные Настройки (2026-04-18)

Дата: 2026-04-18
Область: `WorldObject`, `ObjectEditor`, `WebViewData`, `MainWindow`, `WorkerThread`

## Что добавлено

1. Поддержка radio/stream URL в плейлисте аудиоплеера:
- В `ObjectEditor` уточнен UX текста для `Add URL`:
  - теперь явно указано: `Audio/Radio stream URL or local path`.
- Расширены фильтры локальных аудиофайлов в `Add Tracks`/`Add Audio Player`.
- Расширено распознавание playlist-контента аудиоплеера (`looksLikeAudioPlayerPlaylistContent`):
  - поддержаны расширения `mp3/wav/aac/m4a/ogg/oga/flac/opus/weba/m3u/m3u8/pls`;
  - добавлено распознавание extension-less radio stream URL (например URL с `stream/radio/icecast/shoutcast/listen`).

2. Настройка дистанции автоактивации аудиоплеера:
- Добавлено поле `audio_player_activation_distance` в `WorldObject`.
- Добавлены константы:
  - `DEFAULT_AUDIO_PLAYER_ACTIVATION_DISTANCE = 20.0 m`
  - `MIN_AUDIO_PLAYER_ACTIVATION_DISTANCE = 0.5 m`
  - `MAX_AUDIO_PLAYER_ACTIVATION_DISTANCE = 500.0 m`
- В `ObjectEditor` добавлен контрол `Activation Distance` (метры) в секции `Audio Player`.
- В `WebViewData::process` для `Audio Player` дистанция загрузки/выгрузки браузера теперь берется из `audio_player_activation_distance` (с clamp по диапазону).

## Сериализация и сеть

- Дисковая сериализация `WorldObject` повышена до версии `24`.
- В XML добавлено поле `audio_player_activation_distance`.
- В network-state добавлен optional tail `audio_player_activation_distance`.
- На сервере (`WorkerThread`) добавлен clamp этого поля при `ObjectFullUpdate`.

## Что важно по совместимости

- Для старых сетевых пакетов без нового поля используется безопасный fallback по умолчанию.
- Новое поле добавлено как optional tail в network-state, чтобы старые peer-ы могли игнорировать его.
