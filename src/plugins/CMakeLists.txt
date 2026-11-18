# mxj_plugins: VST3 and CLAP hosting, plugin scanner, Lua runner, plugin API, plugin catalogue (phase 6).
# Uses JUCE's windowless plugin module only; plugin windows are made by the app layer.
add_library(mxj_plugins STATIC
    catalogue/PluginCatalogue.cpp
    catalogue/PluginCatalogue.h
    clap/ClapFormat.cpp
    clap/ClapFormat.h
    host/HostedPlugin.cpp
    host/HostedPlugin.h
    host/PluginHost.cpp
    host/PluginHost.h
    host/PluginScanner.cpp
    host/PluginScanner.h
    lua/LuaManifest.cpp
    lua/LuaManifest.h
    lua/LuaPlugins.cpp
    lua/LuaPlugins.h
    lua/LuaScript.cpp
    lua/LuaScript.h)

target_link_libraries(mxj_plugins PUBLIC mxj_engine PRIVATE mxj_lua mxj_clap)
mxj_configure_target(mxj_plugins)

# The scanner program, started by MXJ AUDIO for each plugin file (see ScannerMain.cpp).
add_executable(mxj_plugin_scanner scanner/ScannerMain.cpp)
target_link_libraries(mxj_plugin_scanner PRIVATE mxj_plugins)
set_target_properties(mxj_plugin_scanner PROPERTIES OUTPUT_NAME "mxj-plugin-scanner")
mxj_configure_target(mxj_plugin_scanner)
