# Phoenix

Phoenix is 2D vector animation software.

![Phoenix](docs/phoenix.png)

## [Building](docs/BUILDING.md)

## Tests

Building the top-level project builds the tests too, as the `phoenix_tests`
target:

```
ctest --test-dir build -C Debug --output-on-failure
```

Drop `-C Debug` with a single-config generator such as Ninja or Unix Makefiles.

That reports two tests, because libas3 registers its own suite alongside
Phoenix's. Add `-R phoenix_tests` to run only Phoenix's.

The editing model, the geometry and the path parser and writer need neither Qt
nor the file readers, so their tests also build on their own — useful when Qt is
not installed:

```
cmake -S tests -B build-tests
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

That leaves out the tests covering the XML and zip readers, which the top-level
build supplies: 177 tests standalone against 200 from the full build.

Some tests exercise real documents rather than hand-built ones — checking that
files round-trip through the writer unchanged, and that the shape arrangement
agrees with the topology the files encode. They are skipped unless pointed at a
folder of `.fla` files or extracted XFL directories:

```
set PHOENIX_FLA_CORPUS=path/to/fla/files
```

## Releases and updates

Phoenix is installed and kept up to date with [Velopack](https://velopack.io).
Each release on GitHub carries a Windows installer
(`Phoenix-win-x64-Setup.exe`), which installs for the current
user without asking for admin rights, a macOS installer for Apple silicon
(`Phoenix-osx-arm64-Setup.pkg`, signed and notarized), which installs
`Phoenix.app` into Applications, and a portable zip of each. An installed copy
looks for a newer release a few seconds after it starts (and from Help > Check
for Updates), downloads it in the background, and offers to restart into it;
an update put off is installed when Phoenix quits. A copy run from the build
folder does not update.

To release, push a version tag:

```
git tag v0.2.0
git push origin v0.2.0
```

The **Release** workflow (`.github/workflows/release.yml`) builds that version,
packs it with `tools/release/pack.py`, and publishes it as the tag's release,
with a delta package from the previous release. A tag with a suffix
(`v0.2.0-beta.1`) makes a pre-release, which installed copies do not update
to. Running the workflow by hand from the Actions tab, with a version and
**publish** left off, builds and packs without releasing anything; the packed
release is kept as an artifact of the run.

The macOS job signs with the Developer ID certificates and notarizes with an
app-specific password, all kept as repository secrets: `MACOS_APP_CERT_P12`
and `MACOS_INSTALLER_CERT_P12` (the Developer ID Application and Installer
certificates with their keys, as base64 `.p12`), `MACOS_CERT_PASSWORD`,
`APPLE_ID`, `APPLE_TEAM_ID` and `APPLE_APP_PASSWORD`. The certificates expire
after five years and are made again at developer.apple.com.

To pack a release locally, install the `vpk` tool at the Velopack version
pinned in `cmake/PhoenixVelopack.cmake`, configure with the version, and pack:

```
dotnet tool install -g vpk --version 1.2.161
cmake -S . -B build -DPHOENIX_VERSION=0.2.0
cmake --build build --config Release --target Phoenix
python tools/release/pack.py --version 0.2.0 --out releases
```

To try an update before publishing it, pack two versions into the same
`releases` folder, install the older one with its Setup.exe, and start it with
`PHOENIX_UPDATE_SOURCE` set to that folder; it updates from there rather than
from GitHub. `-DPHOENIX_UPDATER=OFF` builds without the updater.

## [FLA Format Documentation](docs/FLA_FORMAT.md)
