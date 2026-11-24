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
    core/index/Chunker.cpp core/index/Chunker.h
    core/index/Similarity.cpp core/index/Similarity.h
    core/index/TagTree.cpp core/index/TagTree.h
    core/index/VaultService.cpp core/index/VaultService.h
    core/templates/Templates.cpp core/templates/Templates.h
    core/capture/Capture.cpp core/capture/Capture.h
    core/tasks/TaskEdit.cpp core/tasks/TaskEdit.h
    core/settings/VaultSettings.cpp core/settings/VaultSettings.h
    core/ai/FrontmatterMerge.cpp core/ai/FrontmatterMerge.h
    core/ai/Prompts.cpp core/ai/Prompts.h
)
target_include_directories(mn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mn_core PUBLIC Qt6::Core PRIVATE md4c sqlite3 mn_warnings)
target_link_libraries(mn_core PUBLIC md4c)  # MarkdownDoc exposes md4c enums to the renderer

# --- ai: the vault assistant (Qt Network), optional ------------------------
if(MN_AI)
    add_library(mn_ai STATIC
        core/ai/AiClient.cpp core/ai/AiClient.h
        core/ai/Assistant.cpp core/ai/Assistant.h
    )
    target_include_directories(mn_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mn_ai PUBLIC mn_core Qt6::Network PRIVATE mn_warnings)
endif()

# --- ui: Qt Widgets ----------------------------------------------------------
file(GLOB MN_UI_SOURCES CONFIGURE_DEPENDS ui/*.cpp ui/*.h)
if(NOT MN_UI_SOURCES)
    return()
endif()
if(NOT MN_AI)
    # The assistant's widgets (ui/Ai*.cpp) need the assistant.
    list(FILTER MN_UI_SOURCES EXCLUDE REGEX "/Ai[A-Za-z]*\\.(cpp|h)$")
endif()
add_library(mn_ui STATIC ${MN_UI_SOURCES})
target_include_directories(mn_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mn_ui PUBLIC mn_core Qt6::Gui Qt6::Widgets PRIVATE microtex mn_warnings)
if(MN_AI)
    target_link_libraries(mn_ui PUBLIC mn_ai)
    target_compile_definitions(mn_ui PUBLIC MN_HAVE_AI=1)
endif()

# --- app ---------------------------------------------------------------------
qt_add_executable(markdown-notes WIN32 app/main.cpp $<TARGET_OBJECTS:microtex_res>)
if(WIN32)
    target_sources(markdown-notes PRIVATE app/app.rc)
endif()
# Window icon (Wayland/KDE use the installed desktop file's icon instead).
qt_add_resources(markdown-notes "app_icons" PREFIX "/icons" BASE ../packaging/icons
    FILES ../packaging/icons/markdown-notes-32.png ../packaging/icons/markdown-notes-64.png
          ../packaging/icons/markdown-notes-128.png ../packaging/icons/markdown-notes-256.png)
target_link_libraries(markdown-notes PRIVATE mn_ui mn_warnings)
set_target_properties(markdown-notes PROPERTIES OUTPUT_NAME markdown-notes)
install(TARGETS markdown-notes RUNTIME DESTINATION bin)
