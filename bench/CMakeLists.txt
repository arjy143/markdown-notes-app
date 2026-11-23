# Benchmarks for the performance budgets in CLAUDE.md. Run them on a Release build:
#   ./build/release/bench/bench_index --vault <path>
#   ./build/release/bench/bench_ui --vault <path>
#   ./build/release/bench/bench_app --vault <path>
add_executable(bench_index bench_index.cpp BenchUtil.h)
target_link_libraries(bench_index PRIVATE mn_core mn_warnings)

if(TARGET mn_ui)
    add_executable(bench_ui bench_ui.cpp BenchUtil.h $<TARGET_OBJECTS:microtex_res>)
    target_link_libraries(bench_ui PRIVATE mn_ui Qt6::Test mn_warnings)

    add_executable(bench_app bench_app.cpp BenchUtil.h)
    target_link_libraries(bench_app PRIVATE Qt6::Core mn_warnings)
    target_compile_definitions(bench_app PRIVATE MN_APP_PATH="$<TARGET_FILE:markdown-notes>")
    add_dependencies(bench_app markdown-notes)
endif()
