# YoutubeDownloader

YoutubeDownloader - портативное Win32-приложение для скачивания видео, плейлистов и аудио с YouTube через `yt-dlp`. В приложении есть нативный Windows-интерфейс, очередь загрузок, предпросмотр названия и обложки, а также поддержка FFmpeg для объединения и обработки медиа.

## Screenshots

![Downloading video](screenshots/downloading.png)

![Settings window](screenshots/settings.png)

## Стек

- C++20
- CMake 3.20+
- Нативный Win32 UI: GDI+, DWM, Common Controls
- WinHTTP для HTTP-запросов и скачивания файлов
- `yt-dlp` для получения метаданных и скачивания медиа
- FFmpeg/FFprobe для объединения и обработки медиа
- `nlohmann/json` single-header library в `third_party/`
- MSVC/Visual Studio toolchain под Windows

## Сборка

Требования:

- Windows
- Visual Studio 2022 с workload `Desktop development with C++`
- CMake 3.20 или новее

Сконфигурировать и собрать release-версию:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

При сборке через Visual Studio generator исполняемый файл будет лежать в `build/bin/Release/`.

Запуск тестов:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Runtime

При запуске приложение проверяет наличие `yt-dlp` и может установить или обновить его из официальных GitHub Releases. FFmpeg можно указать вручную, найти через `PATH` или установить в локальную папку инструментов приложения.
