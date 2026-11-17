# mxj_services: settings, logging, paths, presets, translations, updates (phase 8), plugin catalogue (phase 6).
add_library(mxj_services STATIC
    paths/AppPaths.cpp
    paths/AppPaths.h
    presets/PresetStore.cpp
    presets/PresetStore.h
    settings/Settings.cpp
    settings/Settings.h
    settings/SettingKeys.h
    logging/Log.cpp
    logging/Log.h
    i18n/Localisation.cpp
    i18n/Localisation.h
    updates/UpdatePolicy.h)

target_link_libraries(mxj_services PUBLIC mxj_plugins)

target_compile_definitions(mxj_services PUBLIC
    MXJ_ENABLE_UPDATES=$<BOOL:${MXJ_ENABLE_UPDATES}>)

mxj_configure_target(mxj_services)
