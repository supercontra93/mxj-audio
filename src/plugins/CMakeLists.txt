# mxj_plugins: VST3 and CLAP hosting, plugin scanner, Lua runner, plugin API. Filled in phase 6.
add_library(mxj_plugins INTERFACE)
target_link_libraries(mxj_plugins INTERFACE mxj_engine)
