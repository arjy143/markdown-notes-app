# Local changes to MicroTeX

Upstream: https://github.com/NanoMichael/MicroTeX, commit `0e3707f6dafebb121d98b53c64364d16fefe481d` (2024-08-06).

Only the files needed for the Qt backend are vendored (`src/` without the other platforms and samples, plus `res/`).

1. `src/latex.cpp`: memory from `asprintf()` was released with `delete`, which is undefined behaviour (found by AddressSanitizer). Now released with `free()`. See `patches.diff`.
2. `shim/tinyxml2.h` and `shim/tinyxml2_shim.cpp` are not from upstream. They implement the small subset of the tinyxml2 API that MicroTeX uses, on top of `QXmlStreamReader`, so tinyxml2 isn't needed (see `docs/decisions.md`).
