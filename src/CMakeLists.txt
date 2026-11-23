# --- core: QtCore only, all vault logic ------------------------------------
add_library(mn_core STATIC
    core/vault/FileTypes.cpp core/vault/FileTypes.h
    core/vault/Paths.cpp core/vault/Paths.h
    core/vault/TextFile.cpp core/vault/TextFile.h
    core/vault/VaultScanner.cpp core/vault/VaultScanner.h
    core/parse/Frontmatter.cpp core/parse/Frontmatter.h
    core/parse/MarkdownDoc.cpp core/parse/MarkdownDoc.h
    core/parse/NoteParser.cpp core/parse/NoteMetadata.h
    core/links/FileIndex.cpp core/links/FileIndex.h
    core/links/RenamePlanner.cpp core/links/RenamePlanner.h
    core/links/LinkSuggest.cpp core/links/LinkSuggest.h
    core/index/Sqlite.cpp core/index/Sqlite.h
    core/index/IndexTypes.h
    core/index/IndexStore.cpp core/index/IndexStore.h
    core/index/IndexWorker.cpp core/index/IndexWorker.h
    core/index/Similarity.cpp core/index/Similarity.h
    core/index/TagTree.cpp core/index/TagTree.h
    core/index/VaultService.cpp core/index/VaultService.h
    core/templates/Templates.cpp core/templates/Templates.h
    core/capture/Capture.cpp core/capture/Capture.h
    core/tasks/TaskEdit.cpp core/tasks/TaskEdit.h
    core/settings/VaultSettings.cpp core/settings/VaultSettings.h
)
target_include_directories(mn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mn_core PUBLIC Qt6::Core PRIVATE md4c sqlite3 mn_warnings)
target_link_libraries(mn_core PUBLIC md4c)  # MarkdownDoc exposes md4c enums to the renderer

# --- ui: Qt Widgets ----------------------------------------------------------
file(GLOB MN_UI_SOURCES CONFIGURE_DEPENDS ui/*.cpp ui/*.h)
if(NOT MN_UI_SOURCES)
    return()
endif()
add_library(mn_ui STATIC ${MN_UI_SOURCES})
target_include_directories(mn_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mn_ui PUBLIC mn_core Qt6::Gui Qt6::Widgets PRIVATE microtex mn_warnings)

# --- app ---------------------------------------------------------------------
qt_add_executable(markdown-notes WIN32 app/main.cpp $<TARGET_OBJECTS:microtex_res>)
target_link_libraries(markdown-notes PRIVATE mn_ui mn_warnings)
set_target_properties(markdown-notes PROPERTIES OUTPUT_NAME markdown-notes)
install(TARGETS markdown-notes RUNTIME DESTINATION bin)
