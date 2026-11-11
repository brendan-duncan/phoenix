#pragma once

#include <QString>

/// Telling the system which files Phoenix opens. On Windows the installer's
/// hooks call these: .phx opens in Phoenix by default, and .fla gets
/// Phoenix under "Open with" without taking it from its default app. Registered for
/// the current user, as the installer installs, so no administrator rights
/// are needed. On macOS the bundle's Info.plist declares the same.
namespace FileAssociation {

void registerTypes();
void unregisterTypes();

/// phoenix:// links (Open in Phoenix on Phoenix Cloud's pages) open in
/// this Phoenix. registerTypes() registers them too; this alone is for a
/// Phoenix the installer did not install. With \a always false, only when
/// nothing is registered for them, or what is has gone: a build run from
/// its folder does not take them from the installed Phoenix. On Windows
/// it is the current user's registry; on Linux, for an AppImage, a
/// .desktop file in the user's applications; on macOS the bundle's
/// Info.plist declares them.
void registerLinks(bool always);

/// Whether \a text is a phoenix:// link.
bool isLink(const QString& text);

} // namespace FileAssociation
