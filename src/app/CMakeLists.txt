# mxj_app: windows, panels, drawing, theme. Top layer.

juce_add_binary_data(mxj_resources
    HEADER_NAME MxjResources.h
    NAMESPACE   mxjres
    SOURCES
        ${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexSans-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/IBMPlexMono-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/icons/plus.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/folder-open.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/rows.svg
        ${PROJECT_SOURCE_DIR}/resources/logo.svg
        ${PROJECT_SOURCE_DIR}/resources/i18n/tr.txt)
set_target_properties(mxj_resources PROPERTIES FOLDER "MXJ")

add_library(mxj_app STATIC
    AppContext.h
    AppCommands.cpp
    AppCommands.h
    theme/Palette.h
    theme/Fonts.cpp
    theme/Fonts.h
    theme/Icons.cpp
    theme/Icons.h
    theme/GrafitLookAndFeel.cpp
    theme/GrafitLookAndFeel.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/MainComponent.cpp
    ui/MainComponent.h
    ui/StartPage.cpp
    ui/StartPage.h
    ui/StatusBar.cpp
    ui/StatusBar.h
    ui/AboutPanel.cpp
    ui/AboutPanel.h)

target_link_libraries(mxj_app PUBLIC mxj_services PRIVATE mxj_resources)
mxj_configure_target(mxj_app)

# The program itself.
juce_add_gui_app(MXJAudio
    PRODUCT_NAME   "MXJ AUDIO"
    COMPANY_NAME   "MXJ AUDIO"
    COMPANY_WEBSITE "https://github.com/supercontra93/mxj-audio"
    BUNDLE_ID      "io.github.supercontra93.mxjaudio"
    ICON_BIG       "${PROJECT_SOURCE_DIR}/resources/logo-512.png"
    DOCUMENT_EXTENSIONS mxj)

target_sources(MXJAudio PRIVATE Main.cpp)
target_link_libraries(MXJAudio PRIVATE mxj_app mxj_resources)
mxj_configure_target(MXJAudio)
set_target_properties(MXJAudio PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "")

install(TARGETS MXJAudio RUNTIME DESTINATION .)
install(FILES
    ${PROJECT_SOURCE_DIR}/LICENSE
    ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md
    DESTINATION .)
install(FILES
    ${PROJECT_SOURCE_DIR}/resources/fonts/OFL-IBM-Plex.txt
    ${PROJECT_SOURCE_DIR}/resources/icons/LICENSE-Phosphor.txt
    DESTINATION licenses)
