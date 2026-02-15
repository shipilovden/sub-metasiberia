# Metasiberia Site Importer (Figma plugin)

Импортирует PNG-скриншоты страниц `https://vr.metasiberia.com/` в Figma-файл, раскладывая по страницам/фреймам.

Зачем: бесплатная альтернатива “Web to Figma” для выгрузки **всего** сайта (Public/Auth/Admin).

## Как установить (Development plugin)
1. Figma Desktop: `Menu -> Plugins -> Development -> Import plugin from manifest...`
2. Выбрать `manifest.json` из этой папки.

## Как использовать
1. Сначала снять скриншоты скриптом:
   - `tools/site_capture` (Playwright) или вручную.
2. В Figma открыть этот плагин.
3. Выбрать `manifest.json` и все PNG файлы, которые он перечисляет.
4. Нажать `Import`.

## Формат manifest.json
Ожидается:
```json
{
  "items": [
    {
      "category": "Public|Auth|Admin",
      "name": "Root",
      "path": "/",
      "url": "https://vr.metasiberia.com/",
      "file": "root.png",
      "width": 1440,
      "height": 900
    }
  ]
}
```

