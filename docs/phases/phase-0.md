# Phase 0 - Foundation

Goal (master plan, section 11): repository, CMake, JUCE, CI, empty window, CLAUDE.md,
command system skeleton, settings, logging, licence verification.

Acceptance: **CI is green, and the empty program can be installed (unzipped) and opened.**

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 0.1 | Verify every planned library licence from its official source | `docs/licenses/license-audit.md`, `THIRD_PARTY.md` | done |
| 0.2 | CMake project, JUCE 9.0.5 via FetchContent, one shared JUCE library | `CMakeLists.txt`, `cmake/` | done |
| 0.3 | Seven layer targets and the layer rule check | `src/*/CMakeLists.txt`, `tools/check_layers.py` | done |
| 0.4 | Command system skeleton (registry, string ids) and undo history | `src/core/commands`, `src/core/history` | done |
| 0.5 | Settings (JSON, schema version, damaged-file recovery) | `src/services/settings` | done |
| 0.6 | Log file per run, old files pruned | `src/services/logging` | done |
| 0.7 | Turkish and English interface, translation completeness test | `src/services/i18n`, `resources/i18n` | done |
| 0.8 | Grafit theme: palette, IBM Plex fonts, Phosphor icons, LookAndFeel | `src/app/theme` | done |
| 0.9 | Main window: menu bar, start page, status bar, About window | `src/app/ui` | done |
| 0.10 | Update switch, off in development builds | `src/services/updates`, CMake option `MXJ_ENABLE_UPDATES` | done |
| 0.11 | Unit tests (Catch2) | `tests/` | done |
| 0.12 | CI: Windows build, tests, smoke test, portable zip; Linux portability check | `.github/workflows/ci.yml` | done |
| 0.13 | CLAUDE.md, decision records, contribution files | repository root, `docs/` | done |

Not in phase 0 (by plan): opening audio, audio device settings, plugins, updates, installer.

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indirip şunları dene:

1. Zip'i bir klasöre aç, `MXJ AUDIO.exe` dosyasına çift tıkla. Program 2 saniyeden kısa sürede açılmalı.
   Windows "tanınmayan uygulama" uyarısı verirse "Ek bilgi" ve "Yine de çalıştır" de (program henüz imzasız).
2. Pencere koyu grafit renkte, üstte Dosya, Düzen, Görünüm... menüleri, ortada "Hoş geldin" yazısı olmalı.
3. Menülere tıkla. Henüz çalışmayan komutlar soluk görünür. Efektler gibi boş menüler "Sonraki bir sürümde gelecek" der.
4. Görünüm menüsünden "English" seç: tüm yazılar İngilizceye döner. "Turkish" ile geri dön.
5. Yardım > MXJ AUDIO Hakkında: sürüm ve lisans penceresi açılır.
6. Pencereyi büyüt, taşı, kapat ve programı yeniden aç: pencere aynı yerde ve boyutta açılmalı.
7. Yardım > Günlük Klasörünü Aç: içinde `mxj_...log` dosyaları görünmeli.
