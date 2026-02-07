# Создание установщика с веб-интерфейсом (CEF)

## Вариант 1: Установщик на CEF (рекомендуется)

Создан установщик, который использует CEF для отображения веб-интерфейса установки.

### Файлы:
- `substrata/installer/InstallerApp.cpp` - основное приложение установщика
- `substrata/installer/InstallerApp.h` - заголовочный файл
- `substrata/installer/CMakeLists.txt` - конфигурация сборки
- `substrata/scripts/installer_web_ui.html` - веб-интерфейс установки

### Сборка установщика:

```bash
cd substrata_build_sdl
cmake --build . --config RelWithDebInfo --target installer_app
```

### Использование:

1. Запустите `installer_app.exe`
2. Откроется окно с веб-интерфейсом установки
3. Следуйте инструкциям на экране

---

## Вариант 2: Inno Setup (классический установщик)

### Требования:
- Inno Setup 6 (скачать: https://jrsoftware.org/isinfo.php)

### Создание установщика:

```bash
cd substrata/scripts
ruby create_installer.rb
```

Или вручную:
1. Откройте `substrata/scripts/create_installer.iss` в Inno Setup Compiler
2. Нажмите Build > Compile

### Результат:
- `C:\programming\substrata_output\installers\MetasiberiaBeta-Setup.exe`

---

## Вариант 3: Простой batch-установщик

### Создание:

```powershell
cd substrata/scripts
powershell -ExecutionPolicy Bypass -File create_simple_installer.ps1
```

### Использование:
1. Скопируйте все файлы из `RelWithDebInfo` в папку
2. Добавьте `install.bat` и `uninstall.bat`
3. Упакуйте в ZIP или создайте самораспаковывающийся архив

---

## Рекомендация

**Для быстрого распространения:** Используйте Вариант 2 (Inno Setup) - самый простой и надежный.

**Для инновационного подхода:** Используйте Вариант 1 (CEF установщик) - красивый веб-интерфейс.

**Для минималистичного подхода:** Используйте Вариант 3 (batch-скрипт) - не требует дополнительных инструментов.
