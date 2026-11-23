# Builds a portable Windows zip: markdown-notes.exe + Qt DLLs (windeployqt).
# Run from a Visual Studio developer shell with Qt's bin directory on PATH.
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot/../.."
Set-Location $root

cmake --preset release
cmake --build build/release --target markdown-notes

$out = "build/windows/MarkdownNotes"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "build/release/src/markdown-notes.exe" $out

windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "$out/markdown-notes.exe"

$doc = "$out/licences"
New-Item -ItemType Directory -Force $doc | Out-Null
Copy-Item LICENSE, THIRD_PARTY_NOTICES.md, README.md $out
Copy-Item third_party/md4c/LICENSE.md "$doc/md4c-LICENSE.md"
Copy-Item third_party/microtex/LICENSE "$doc/microtex-LICENSE"
Copy-Item -Recurse third_party/microtex/res/fonts/licences "$doc/microtex-font-licences"
$qtPrefix = (qmake -query QT_INSTALL_PREFIX)
foreach ($dir in @("$qtPrefix/LICENSES", "$qtPrefix/../../Licenses")) {
    if (Test-Path $dir) { Copy-Item -Recurse $dir "$doc/qt-licences" -ErrorAction SilentlyContinue }
}

$zip = "build/windows/MarkdownNotes-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$out/*" -DestinationPath $zip
Write-Host "Wrote $zip"
