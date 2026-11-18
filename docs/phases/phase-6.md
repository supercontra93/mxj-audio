# Phase 6 - Plugins (version 0.6)

Goal (master plan, section 11): VST3 and CLAP hosting, scanner in a separate process, Lua runner and API,
plugin page, local test catalogue.

Acceptance: **common free VST3 plugins work, and an example Lua plugin is installed from the page.**

## Tasks

| # | Task | Where | Status |
|---|---|---|---|
| 6.1 | Dynamic effect types: plugins add and remove types at run time; plugin state (`EffectState`) stored with the values; shared live plugin per rack slot (rack and mixer strip), separate ones for applying and mixdown; script errors stop a render | `src/dsp/effects`, `src/dsp/process/Render`, `SessionMixer` | done |
| 6.2 | VST3 hosting through JUCE, own CLAP host (headers 1.2.7): load, process (real-time safe, never waits), state, latency, the plugin's own window | `src/plugins/clap`, `src/plugins/host/HostedPlugin` | done |
| 6.3 | Plugin list (`known-plugins.xml`), standard and user folders, block list; scanner program `mxj-plugin-scanner` that scans one file per process (crash, 60 s hang or failure: blocked) | `src/plugins/host`, `src/plugins/scanner` | done |
| 6.4 | Lua 5.4.8 runner: sandbox (no files, no modules, memory and time limits), API version 1 (`mxj` table, blocks), effect, generator, analysis and tool plugins | `src/plugins/lua/LuaScript` | done |
| 6.5 | MXJ plugin manager: plugin.json checks, install and update from `.mxjplugin` (zip with path checks), remove, switch off, developer folders reloaded on save, console | `src/plugins/lua/LuaPlugins`, `LuaManifest` | done |
| 6.6 | Catalogue (`index.json` format 1, SHA-256 and size checked before installing) and the local test catalogue built from five example plugins | `src/plugins/catalogue`, `cmake/MxjCatalog.cmake`, `resources/lua-plugins` | done |
| 6.7 | Plugin page with four tabs: Installed, Discover, VST and CLAP, Developer | `src/app/plugins/PluginsPage` | done |
| 6.8 | Plugin window (VST3 editor, CLAP native window); "Plugin Window..." button in every place that shows effect controls; plugin effects in the Effects menu (VST3, CLAP, MXJ Plugins), effect rack and track effects; Plugins menu | `src/app/plugins`, `ParameterPanel`, `EffectDialog` | done |
| 6.9 | Tests (manifest, sandbox, Lua effect, generator, analysis and tool, examples, catalogue install and checksum, CLAP and VST3 test plugins, crash during scan blocked, dynamic catalogue) and smoke test (install from the catalogue, apply, undo); package with scanner, catalogue and licences | `tests/PluginTests.cpp`, `tests/plugins`, `Main.cpp`, CI | done |

Decision record: [0010](../adr/0010-plugins.md). Writing plugins: [Lua API](../plugins/lua-api.md).
Moved to later: see `docs/later.md`.

## Shortcuts

No new shortcuts. Everything is in the **Plugins** menu (Plugins..., Scan for New VST3 and CLAP Plugins,
Install Plugin from File..., then the analysis and tool plugins) and in **Effects > VST3 / CLAP / MXJ Plugins**.

## Manual test list for Baybars (Türkçe)

CI'nin ürettiği zip'i indir ve bir klasöre çıkar. Klasörde `MXJ AUDIO.exe`'nin yanında `mxj-plugin-scanner.exe`
ve `catalog` klasörü olmalı (programı bu klasörden çalıştır).

**Ücretsiz VST3 eklentileri (kabul testi):** Denemek için birkaç ücretsiz VST3 kur, örneğin Valhalla Supermassive
(reverb/delay), TDR Nova (EQ) ya da MeldaProduction MFreeFXBundle. Kurulumlar genelde
`C:\Program Files\Common Files\VST3` klasörüne koyar. CLAP denemek istersen Airwindows Consolidated ya da
Surge XT Effects'in CLAP sürümünü kurabilirsin (`C:\Program Files\Common Files\CLAP`).

1. Eklentiler > Eklentiler... sayfasını aç. Dört sekme olmalı: Yüklü, Keşfet, VST ve CLAP, Geliştirici.
2. **VST ve CLAP** sekmesi: "Yeni Eklentileri Tara"ya bas. İlerleme yazısı görünür; bitince eklentiler listede olur.
   Standart klasörler listede "(standart)" diye yazar. Başka bir klasörde eklentin varsa "Klasör Ekle..." ile ekleyip
   tekrar tara.
3. Bir ses dosyası aç, bir kısmını seç. Efektler > VST3 > (ör. TDR Nova...) seç: efekt penceresi ve eklentinin kendi
   penceresi açılır. Eklentinin penceresinde bir ayar değiştir, Önizleme ile dinle, Uygula de. Ctrl+Z ile geri al.
4. Efekt rafını aç (Ctrl+R), "+" ile bir VST3 eklentisi ekle, "Eklenti Penceresi..." ile ayarla, çalarken sesi değişsin.
   Programı kapatıp aç: raf ve eklentinin ayarı korunmalı.
5. Çok kanallı oturumda bir kanalın efektlerine (Pencere > Kanal Efektleri) aynı şekilde bir VST3 ekle; çalarken duyulsun,
   mixdown'da da olsun. Projeyi kaydedip açınca eklenti ayarı geri gelmeli.
6. **Keşfet** sekmesi: yerel deneme kataloğunda 5 örnek eklenti görünür. Aramaya "trem" yaz, Tremolo kalmalı. Kategori
   kutusunu dene. Tremolo'yu seçip "Kur"a bas (kabul testi: örnek Lua eklentisi sayfadan kuruluyor). Durum "Kurulu 1.0.0" olur.
7. Efektler > MXJ Eklentileri > Tremolo... ile seçime uygula, dinle. Sine Sweep'i (üreteç) de kurup imlece ekle.
8. Silence Finder'ı kur. Eklentiler > Analiz > Silence Finder... ile çalıştır: sessiz bölümlerin listesi açılır.
9. Reset View'u kur: "program komutlarını çalıştırma" izni sorulur, kabul et. Eklentiler > Araçlar > Reset View:
   seçim kalkar, tüm dosya görünür, imleç başa gider.
10. **Yüklü** sekmesi: Tremolo'yu "Kapat" ile kapat; Efektler menüsünden kalkmalı. "Aç" ile geri getir. "Kaldır" ile sil.
11. **Geliştirici** sekmesi: `resources\lua-plugins\tremolo` klasörünü (ya da kopyasını) "Klasörden Yükle..." ile yükle.
    `main.lua` içinde bir şeyi bozup kaydet (ör. bir satıra `x(` yaz): konsolda hata görünür. Düzeltip kaydet: eklenti
    kendiliğinden yeniden yüklenir.
12. Bozuk bir eklentinin programı çökertmediğini görmek istersen: rastgele bir dosyanın uzantısını `.vst3` yapıp
    VST3 klasörüne koy ve tara; "Engellenen dosyalar" listesine düşmeli, program açık kalmalı.
