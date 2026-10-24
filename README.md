# Tectonic Synth v5 — Quantum Observatory (C++20 CLAP Plugin)

## Инструкция для GitHub Copilot / Автоматического деплоя и сборки установщиков

Этот архив полностью готов к загрузке в репозиторий GitHub:

1. **Структура проекта:**
   - `tectonic_synth.cpp` — полный монолитный исходный код синтезатора (C++20, CLAP 1.2, 5 физических движков, 60 FPS OpenGL/ImGui).
   - `CMakeLists.txt` — кроссплатформенная сборка CMake + `CPack` для генерации установщиков (`.exe` NSIS на Windows, `.pkg` на macOS, `.deb` на Linux).
   - `.github/workflows/build.yml` — пайплайн GitHub Actions, который:
     1) собирает `.clap` и инсталляторы под Windows, macOS (Universal) и Linux;
     2) автоматически публикует веб-сайт из папки `docs/index.html` на **GitHub Pages**;
     3) при создании тега `v5.0.0` публикует готовые установщики в **GitHub Releases**.
   - `installer/install_windows.ps1` и `installer/install_unix.sh` — скрипты быстрой установки в системную папку CLAP.
   - `docs/index.html` — готовый лендинг для выкладки на сайт (GitHub Pages).

2. **Задача для GitHub Copilot (скопируйте в чат Copilot):**
   > Распакуй все файлы из архива в корень репозитория (сохранив структуру `.github/workflows/build.yml`, `docs/index.html`, `installer/`, `CMakeLists.txt` и `tectonic_synth.cpp`), сделай коммит в ветку `main`, включи GitHub Pages из ветки `main` (через GitHub Actions) и запушь тег `v5.0.0`, чтобы автоматически собрались установщики под Windows/macOS/Linux и опубликовался сайт.
