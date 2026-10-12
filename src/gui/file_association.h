#pragma once

/// Telling the system which files Phoenix opens. On Windows the installer's
/// hooks call these: .phx opens in Phoenix by default, and .fla gets
/// Phoenix under "Open with" without taking it from Animate. Registered for
/// the current user, as the installer installs, so no administrator rights
/// are needed. On macOS the bundle's Info.plist declares the same.
namespace FileAssociation {

void registerTypes();
void unregisterTypes();

} // namespace FileAssociation
