# macOS app bundle

## Build and identity

`resources/macos/branding.json` is the versioned source of truth for the native
bundle identity:

- `app_name`: display name, `.app` filename, menu-bar labels, window title, and
  default install destination
- `bundle_id`: bundle identity and the base for notification, Spotlight, and URL
  type identifiers
- `executable_name`: executable filename under `Contents/MacOS`
- `url_scheme`: deep-link scheme and the per-brand local data directory name

The checked-in `resources/macos/Info.plist` is a template, not a second set of
defaults. `scripts/branding.py` validates the JSON and generates a C++ header and
final plist under an identity-keyed `output/branding/` directory. Make uses the
same resolved values for the `.app` path, generated files, compilation, package
scripts, verification, and installation. Identity-keyed object directories
prevent a branded build from reusing objects compiled for another identity.

The defaults remain `Hanabi`, `io.github.gabeochoa.hanabi`, executable `hanabi`,
and scheme `hanabi`:

```bash
make app
make verify-app
```

One-off builds can override all four values without editing the repository:

```bash
make verify-app \
  APP_NAME=Ember \
  BUNDLE_ID=io.github.gabeochoa.ember \
  EXECUTABLE_NAME=ember \
  URL_SCHEME=ember
```

Display and executable names accept spaces when quoted. Names are restricted to
safe ASCII path characters, bundle identifiers must use at least three
reverse-DNS components, and schemes must follow RFC 3986 scheme grammar. Invalid
values stop generation before packaging.

Derived values are not independently configurable: the URL type name,
notification request prefix, Spotlight domain, Spotlight item prefix, Spotlight
manifest key, WebSocket diagnostic label, and local data namespace all derive
from the four validated fields. `make source-checks` verifies generated outputs
and rejects duplicated default identity literals in identity-sensitive source.

The bundle contains the executable, UI resources, and the OpenSSL runtime
libraries required by the TLS build. Packaging rewrites those libraries to
`@rpath`, seals the complete bundle with a local ad-hoc signature, and runs
`codesign --verify --deep --strict`.

The ad-hoc signature proves the local bundle is internally consistent. It is not
a Developer ID signature, is not notarized, and makes no Gatekeeper distribution
claim.

`make run` is unchanged: it builds and starts the developer executable. That
executable has no bundle identity, does not request notification permission, and
does not write to CoreSpotlight.

## Install, update, and remove

```bash
make install-app
make uninstall-app
```

The default install path is `~/Applications/<app_name>.app`. Installation stages
a verified copy, refuses to replace an app with another bundle identifier or
display name, then registers the result with LaunchServices. Uninstall
unregisters and removes only the same exact identity. Override the complete path
with `APP_INSTALL_DIR=/path/to/Name.app`; quote paths containing spaces.

For the build output alone:

```bash
make register-app
make unregister-app
make launch-app
```

The same identity overrides used by `make app` must be supplied to later
register, install, launch, or uninstall commands for that branded build.

## Notifications

The bundled app installs a `UNUserNotificationCenter` delegate and requests
alert/sound authorization on its first windowed run. The authorization request
never runs in `--screenshot`, scripted UI, unit, or bare-executable paths. A
notification carries its session id in `userInfo`; clicking the banner activates
the configured app and routes the id through the same open-thread slot as its
configured URL scheme. Notification request identifiers use a prefix derived
from the bundle identifier.

The existing 30-second transition debounce, mute filtering, quiet hours, and
per-notification sound setting remain in the C++ caller.

Use a local test notification without contacting any backend:

```bash
HANABI_BACKEND=mock HANABI_NOTIFY_TEST=local-check \
  "output/Hanabi.app/Contents/MacOS/hanabi"
```

Delivery depends on the user's macOS notification permission. The native log
distinguishes authorized, denied, and error states.

## Spotlight

Once a non-mock catalog reaches `Loaded`, the app builds at most 2,000 entries,
newest first. Each entry contains its title, preview, and an encoded URL using
the configured scheme. CoreSpotlight updates matching identifiers, removes
identifiers absent from the next complete catalog, and stores only the bounded
identifier manifest needed to compute deletion. The domain, item prefix, and
manifest key derive from the bundle identifier, so differently branded builds
do not share an index. A failed/offline refresh never clears the last good
index, and the zero-config mock is never donated.

The explicit local verification seam uses no backend:

```bash
HANABI_BACKEND=mock HANABI_SPOTLIGHT_TEST=local-check \
  "output/Hanabi.app/Contents/MacOS/hanabi"
HANABI_BACKEND=mock HANABI_SPOTLIGHT_TEST=clear \
  "output/Hanabi.app/Contents/MacOS/hanabi"
```

`mdfind`/`mdquery` may return no CoreSpotlight private-domain items even after the
index completion handler succeeds. In that case discovery remains Spotlight-UI
and permission/indexing-state gated; the completion log is the programmatic
proof that macOS accepted the donation.

## Diagnostics

```bash
"output/Hanabi.app/Contents/MacOS/hanabi" --native-diagnostics
"output/hanabi.exe" --native-diagnostics
"output/Hanabi.app/Contents/MacOS/hanabi" \
  --parse-thread-url 'hanabi://thread/local%20check'
make verify-app
```

The first command reports the real bundle identifier plus the compiled scheme,
notification prefix, and Spotlight domain. The bare executable reports
`bundle=none`. The URL parser command prints the decoded thread id and performs
no AppKit, filesystem, or network work. `make verify-app` also rejects
machine-local dylib paths, missing resources, plist/runtime identity mismatches,
invalid URL parsing, or an invalid bundle seal.
