#pragma once

#include "core/settings/VaultSettings.h"

#include <QString>

namespace mn {

// The link written into note `fromNote` for a file just added at `path` (a pasted
// image, a drawing), in the vault's link settings: wikilink or Markdown link, and
// the "new link format" style. `nameInUse`: another file already has this file
// name, so the shortest form would be ambiguous and the full path is written.
QString newFileLink(const VaultSettings& s, const QString& path, const QString& fromNote, bool nameInUse,
                    bool embed);

} // namespace mn
