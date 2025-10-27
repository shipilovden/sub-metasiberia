# Руководство по изменению иконок в проекте Metasiberia

## Проблема
CMake свойство `VS_ICON` и ресурсные файлы (.rc) не всегда корректно работают с Visual Studio 2022 для встраивания иконок в exe файлы.

## Решение
Использование внешней утилиты `rcedit.exe` для прямого встраивания иконок в исполняемые файлы.

## Инструкция

### 1. Скачивание rcedit.exe
```bash
# Скачать rcedit.exe с GitHub
Invoke-WebRequest -Uri "https://github.com/electron/rcedit/releases/download/v1.1.1/rcedit-x64.exe" -OutFile "C:\programming\rcedit.exe"
```

### 2. Применение иконки к exe файлу
```bash
# Применить иконку к shki-nvkz.exe
C:\programming\rcedit.exe "C:\programming\substrata_output\vs2022\cyberspace_x64_sdl_custom\RelWithDebInfo\shki-nvkz.exe" --set-icon "C:\programming\substrata\icons\shki-nvkz.ico"
```

### 3. Очистка кэша иконок Windows
```bash
# Очистить кэш иконок
ie4uinit.exe -ClearIconCache

# Перезапустить проводник (опционально)
taskkill /f /im explorer.exe
start explorer.exe
```

## Автоматизация
Батник `01_MAIN_BUILD_TRIPLE_CLIENT_FINAL.bat` автоматически применяет иконку после сборки, если `rcedit.exe` найден.

## Файлы иконок
- **Иконка exe файла**: `C:\programming\substrata\icons\shki-nvkz.ico`
- **Иконка окна программы**: Устанавливается через Windows API в `SDLClient.cpp`
- **Кнопки интерфейса**: `C:\programming\substrata\resources\buttons\shki-nvkz_*.png`

## Примечания
- rcedit.exe должен быть в `C:\programming\rcedit.exe`
- Иконка должна быть в формате .ico
- После применения иконки может потребоваться очистка кэша Windows
