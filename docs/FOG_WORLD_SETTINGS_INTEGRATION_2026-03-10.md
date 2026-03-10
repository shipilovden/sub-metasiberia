# Интеграция настроек тумана мира

Дата: 2026-03-10

## Что добавлено

- В `WorldSettings` добавлены сериализуемые параметры двух слоёв тумана:
  - `layer_0_A`
  - `layer_0_scale_height`
  - `layer_1_A`
  - `layer_1_scale_height`
- Версия сериализации `WorldSettings` поднята до `6`.
- В `World Settings` UI добавлены элементы управления туманом.
- Изменения world settings теперь применяются локально сразу, а отправка на сервер идёт с небольшой задержкой, без обязательного round-trip перед локальным preview.
- Серверный `WorldSettingsUpdate` теперь включает `sender_avatar_UID`, чтобы клиент мог игнорировать эхо собственного обновления и не перезагружать terrain/UI повторно.

## Где это реализовано

- `shared/WorldSettings.h`
- `shared/WorldSettings.cpp`
- `gui_client/WorldSettingsWidget.h`
- `gui_client/WorldSettingsWidget.cpp`
- `gui_client/WorldSettingsWidget.ui`
- `gui_client/MainWindow.h`
- `gui_client/MainWindow.cpp`
- `gui_client/GUIClient.h`
- `gui_client/GUIClient.cpp`
- `gui_client/ClientThread.h`
- `gui_client/ClientThread.cpp`
- `server/WorkerThread.cpp`

## Зависимость от glare-core

Для визуального применения тумана добавлена поддержка fog uniforms и fog transmission в:

- `C:\programming\glare-core\opengl\OpenGLEngine.h`
- `C:\programming\glare-core\opengl\OpenGLEngine.cpp`
- `C:\programming\glare-core\opengl\shaders\common_frag_structures.glsl`
- `C:\programming\glare-core\opengl\shaders\phong_frag_shader.glsl`
- `C:\programming\glare-core\opengl\shaders\water_frag_shader.glsl`
- `C:\programming\glare-core\opengl\shaders\imposter_frag_shader.glsl`
- `C:\programming\glare-core\opengl\shaders\participating_media_frag_shader.glsl`
- `C:\programming\glare-core\opengl\shaders\env_frag_shader.glsl`

## Проверка

Сборка выполнена через `C:\programming\qt_build.ps1`.

Успешно собраны:

- `C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release\gui_client.exe`
- `C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe`
