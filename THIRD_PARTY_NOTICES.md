# Third-party notices

Markdown Notes is MIT-licensed. It uses the following software. Release packages must ship this file and the licence texts it names.

| Component | Version | Licence | Where |
|---|---|---|---|
| Qt (Core, Gui, Widgets) | 6.5+ (linked dynamically) | LGPL-3.0 (or GPL / commercial) | <https://www.qt.io/>. Source: <https://download.qt.io/>. Users may replace the Qt libraries shipped with a release. |
| md4c | 0.6.0, with one local patch (`third_party/md4c/wikilink-limit.patch`) | MIT | `third_party/md4c/LICENSE.md` |
| SQLite | 3.54.1 amalgamation | Public domain | <https://sqlite.org/copyright.html> |
| MicroTeX | commit 0e3707f (2024-08-06) | MIT | `third_party/microtex/LICENSE` |
| MicroTeX fonts (Computer Modern, AMS, Euler, rsfs, stmary, dsrom, Cyrillic, Greek) | bundled with MicroTeX | Knuth licence / SIL OFL 1.1 / see files | `third_party/microtex/res/fonts/licences/`, `third_party/microtex/res/cyrillic/LICENSE`, `third_party/microtex/res/greek/LICENSE` |

The Qt LGPL licence text is at <https://www.gnu.org/licenses/lgpl-3.0.html>. Packaging scripts copy the Qt licence files from the Qt installation into the package.
