# BlackCamera OBS

**BlackCamera ASCII** — видеофильтр для OBS Studio, который превращает любой источник (устройство захвата видео, захват окна, медиа) в ASCII-графику в реальном времени. Это эффект [BlackCamera](https://github.com/Jop3r/BlackCamera) в виде плагина OBS: рендер на атласе глифов (DirectWrite + многопоточная сборка кадра на CPU), зум шрифта по лицу (YuNet) и кибер-HUD.

[🇬🇧 English version](README.md)

[![build](https://github.com/Jop3r/BlackCamera-OBS/actions/workflows/build.yml/badge.svg)](https://github.com/Jop3r/BlackCamera-OBS/actions/workflows/build.yml)

> Экспериментально. Только Windows x64, собирается и проверяется на OBS Studio 32.2.

---

## Установка

1. Скачайте `blackcamera-obs-<версия>-windows-x64.zip` со страницы [Releases](https://github.com/Jop3r/BlackCamera-OBS/releases).
2. Закройте OBS и распакуйте архив в `%ProgramData%\obs-studio\plugins` (путь можно вставить в адресную строку Проводника). Получится `%ProgramData%\obs-studio\plugins\blackcamera-obs`.
3. Запустите OBS.

Эта папка подходит для любой установки OBS (в том числе Steam) и не трогает папку программы. Удалить плагин — удалить `%ProgramData%\obs-studio\plugins\blackcamera-obs`.

Каждый push в `master` тоже собирает плагин: zip лежит в **Artifacts** [запуска сборки](https://github.com/Jop3r/BlackCamera-OBS/actions/workflows/build.yml).

## Сборка

Требования: Windows 10/11 x64, установленный OBS Studio, Visual Studio 2022 или новее с нагрузкой **Разработка классических приложений на C++**, [vcpkg](https://github.com/microsoft/vcpkg). OpenCV описан в `vcpkg.json`, ставится автоматически и линкуется статически, поэтому плагин — одна DLL.

1. Подготовьте SDK libobs под установленную версию OBS (один раз и заново после обновления OBS):

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\obs-sdk.ps1
   ```

   Скрипт берёт заголовки libobs из исходников obs-studio с тегом установленной версии (sparse checkout git) и генерирует `obs.lib` по экспортам установленного `obs.dll`, поэтому собирать сам OBS не нужно. Результат — в `.deps\obs-sdk`. Если OBS не нашёлся или нужна другая версия, передайте `-ObsDir` / `-Version`. Вместо него можно использовать настоящий CMake-пакет libobs (сборку obs-studio, `.deps` из obs-plugintemplate) через `-Dlibobs_DIR=...`.

2. Соберите:

   ```powershell
   $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
   cmake --workflow --preset release
   ```

   `cmake` должен быть в `PATH` (или используйте **Developer PowerShell for VS**). Первая сборка компилирует OpenCV через vcpkg и может идти долго; дальше используется бинарный кэш vcpkg.

Плагин раскладывается в `build\stage\Release\blackcamera-obs`:

```
blackcamera-obs\
  bin\64bit\blackcamera-obs.dll
  data\locale\en-US.ini, ru-RU.ini
  data\face_detection_yunet_2023mar.onnx
```

Установить сборку (OBS закрыт):

```powershell
cmake --install build --config Release --prefix "$env:ProgramData\obs-studio\plugins"
```

`tools\package.ps1` упаковывает её в `dist\blackcamera-obs-<версия>-windows-x64.zip` — тот же архив, что в релизе. Тег `v<версия>` (версия из `CMakeLists.txt`) заставляет CI опубликовать релиз с этим архивом.

## Использование

Правый клик по источнику → **Фильтры** → **+** → **BlackCamera ASCII**.

| Настройка | Что делает |
|-----------|------------|
| Яркость (-100..100), Контраст (0.1..3.0) | Применяются к источнику до преобразования |
| Размер ядра размытия (1..31, нечётный) | Сглаживает изображение перед подбором символов |
| Порог минимальной яркости (0..255) | Более тёмные ячейки остаются пустыми |
| Режим цвета | Свой / Цвета камеры / Зелёная матрица / Янтарный CRT / Киберпанк / Радуга |
| Сглаживание | По умолчанию / ClearType / Оттенки серого / Без сглаживания (ретро) |
| Набор символов | Разреженный / Стандартный / Классический Unicode / Двоичный / Матричный дождь / Свой |
| Размер шрифта (мин.) | Размер шрифта; минимальный, если включён зум по лицу |
| Масштабировать шрифт по высоте источника | Размеры шрифта заданы для картинки высотой 1080 px и пересчитываются под высоту источника (включено по умолчанию) |
| Зум шрифта по лицу | Макс. шрифт, Скорость зума, Кибер-HUD, Мёртвая зона, Макс. чувствительность, Мин. порог |

Сообщения плагина — в логе OBS (**Справка → Файлы журнала**) с префиксом `[blackcamera-obs]`.

## Как это работает

Фильтр рендерит свой источник в текстуру, читает её на CPU через две staging-поверхности (отображается та, что поставлена в очередь кадром раньше, поэтому GPU не простаивает) и отдаёт кадр рабочему потоку. Поток прогоняет трекер лица и ASCII-рендер на CPU; последний результат загружается в динамическую текстуру и рисуется вместо источника. Медленный кадр снижает частоту эффекта, но не частоту OBS. Размер выхода совпадает с размером источника.

Источник захватывается в SDR sRGB (HDR-источники OBS тонмапит сам), а результат рисуется как sRGB-источник, поэтому OBS правильно переводит его для SDR-, 16-битных и HDR-холстов.

## Структура исходников

| Путь | Отвечает за |
|------|-------------|
| `src/plugin-main.cpp` | Точка входа модуля, вывод логов в OBS |
| `src/ascii_filter.*` | Фильтр: свойства, чтение кадра с GPU, рабочий поток, отрисовка |
| `src/core/ascii_renderer.*` | Атлас глифов, сборка кадра, отрисовка HUD |
| `src/core/face_tracker.*` | Детекция YuNet, зум шрифта, сглаживание HUD |
| `src/core/settings.*` | Настройки эффекта, наборы символов |
| `src/core/log.*`, `src/core/platform.*` | Логирование, утилиты UTF-8 |
| `tools/obs-sdk.ps1` | Минимальный SDK libobs из установленного OBS |
| `tools/package.ps1` | Zip для релиза из собранного плагина |

`src/core` взят из [BlackCamera](https://github.com/Jop3r/BlackCamera) (`BlackCamera/`) без кода камеры, виртуальной камеры и файла настроек приложения.

## Лицензия

Исходный код — MIT, см. [LICENSE](LICENSE). Плагин линкуется с libobs (GPL-2.0-or-later), поэтому бинарник плагина распространяется на условиях GPL. Сторонние компоненты: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
