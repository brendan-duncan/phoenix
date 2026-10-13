#pragma once

/// Phoenix's name to Qt: where its settings are kept and what its data
/// folders are called.
namespace app_identity {

/// Names the organization and the application, which place the settings
/// (HKEY_CURRENT_USER\Software\Phoenix\Phoenix on Windows) and the data
/// folders (themes, templates, recovery copies).
void apply();

/// Phoenix kept its settings and data folders under "FLAViewer" before
/// 0.6: the first start without settings of its own copies the old settings
/// and data folders over. The old ones are left as they were, for an older
/// version still installed. Call apply() first.
void adoptOldSettings();

} // namespace app_identity
