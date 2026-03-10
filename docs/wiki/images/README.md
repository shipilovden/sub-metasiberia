# Изображения Wiki Metasiberia

Этот каталог является центральным реестром изображений для wiki.

## Рекомендуемый порядок работы

1. Найдите нужную страницу wiki в этом файле.
2. Используйте точные `folder/filename`, указанные для нужного image-slot.
3. Положите PNG в эту папку, не переименовывая слот.
4. Если изображение уже существует, замените его на более удачную версию с тем же именем.
5. После обновления изображений снова запустите `scripts/publish_wiki_to_github.ps1`.

## Почему это лучше

- Markdown в страницах остаётся стабильным, потому что пути к изображениям не меняются.
- Можно добавлять изображения позже, не редактируя страницу заново.
- Сразу видно, какие image-slots уже заполнены, а какие ещё пустые.
- Та же структура масштабируется на всю wiki из 44 страниц.

## Важное правило

Большое количество изображений действительно улучшает wiki, но только если каждое изображение показывает шаг, результат или ошибку.
Не добавляйте почти одинаковые скриншоты только ради количества.

## Текущая карта изображений

### Home

- Файл: `images/wiki_main_image.png`
- Статус: готово

### 01 Install Windows

- Папка: `images/01-install-windows/`
- `hero.png` - не хватает
- `step-release-page.png` - готово
- `step-installer.png` - не хватает
- `result-installed.png` - не хватает
- `error-antivirus.png` - не хватает

### 02 Registration and Login

- Папка: `images/02-registration-and-login/`
- `hero.png` - не хватает
- `step-signup-form.png` - готово
- `step-terms-checkbox.png` - готово
- `result-logged-in.png` - не хватает
- `error-invalid-credentials.png` - не хватает

### 03 First Launch and Connection

- Папка: `images/03-first-launch-and-connection/`
- `hero.png` - готово
- `step-connect.png` - готово
- `step-world-loaded.png` - готово
- `result-ready.png` - готово
- `error-connection.png` - не хватает

## Правило именования для новых страниц

- Создавайте одну папку на один page slug:
  - `images/<page-slug>/`
- Используйте понятные имена слотов вместо безликой нумерации:
  - `hero.png`
  - `step-release-page.png`
  - `step-signup-form.png`
  - `result-ready.png`
  - `error-connection.png`

## Следующие страницы, для которых нужно подготовить слоты

- `04-client-ui-overview`
- `05-movement-and-camera-modes`
- `06-graphics-audio-mic-webcam-settings`
- `07-troubleshooting-startup-and-login`
- `08-faq-quick-answers`
