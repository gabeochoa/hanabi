# macOS app bundle

## Build and identity

`make app` builds `output/Hanabi.app` with bundle identifier
`io.github.gabeochoa.hanabi`. The bundle contains the executable, UI resources,
and the OpenSSL runtime libraries required by the TLS build. Packaging rewrites
those libraries to `@rpath`, seals the complete bundle with a local ad-hoc
signature, and runs `codesign --verify --deep --strict`.

The ad-hoc signature proves the local bundle is internally consistent. It is not
a Developer ID signature, is not notarized, and makes no Gatekeeper distribution
claim.

`make run` is unchanged: it builds and starts the developer executable. That
executable has no Hanabi bundle identity, does not request notification
permission, and does not write to CoreSpotlight.

## Install, update, and remove

```bash
make install-app
make uninstall-app
```

The default install path is `~/Applications/Hanabi.app`. Installation stages a
verified copy, refuses to replace an app with another bundle identifier, then
registers the result with LaunchServices. Uninstall unregisters and removes only
the same exact identifier. Override the path with
`APP_INSTALL_DIR=/path/to/Hanabi.app`.

For the build output alone:

```bash
make register-app
make unregister-app
make launch-app
```

## Notifications

The bundled app installs a `UNUserNotificationCenter` delegate and requests
alert/sound authorization on its first windowed run. The authorization request
never runs in `--screenshot`, scripted UI, unit, or bare-executable paths. A
notification carries its session id in `userInfo`; clicking the banner activates
Hanabi and routes the id through the same open-thread slot as a `hanabi://` URL.
The existing 30-second transition debounce, mute filtering, quiet hours, and
per-notification sound setting remain in the C++ caller.

Use a local test notification without contacting any backend:

```bash
HANABI_BACKEND=mock HANABI_NOTIFY_TEST=local-check \
  output/Hanabi.app/Contents/MacOS/hanabi
```

Delivery depends on the user's macOS notification permission. The native log
distinguishes authorized, denied, and error states.

## Spotlight

Once a non-mock catalog reaches `Loaded`, Hanabi builds at most 2,000 entries,
newest first. Each entry contains its title, preview, and an encoded
`hanabi://thread/<id>` URL. CoreSpotlight updates matching identifiers, removes
identifiers absent from the next complete catalog, and stores only the bounded
identifier manifest needed to compute deletion. A failed/offline refresh never
clears the last good index, and the zero-config mock is never donated.

The explicit local verification seam uses no backend:

```bash
HANABI_BACKEND=mock HANABI_SPOTLIGHT_TEST=local-check \
  output/Hanabi.app/Contents/MacOS/hanabi
HANABI_BACKEND=mock HANABI_SPOTLIGHT_TEST=clear \
  output/Hanabi.app/Contents/MacOS/hanabi
```

`mdfind`/`mdquery` may return no CoreSpotlight private-domain items even after the
index completion handler succeeds. In that case discovery remains Spotlight-UI
and permission/indexing-state gated; the completion log is the programmatic
proof that macOS accepted the donation.

## Diagnostics

```bash
output/Hanabi.app/Contents/MacOS/hanabi --native-diagnostics
output/hanabi.exe --native-diagnostics
make verify-app
```

The first command reports the real bundle identifier; the second reports
`bundle=none`. `make verify-app` also rejects machine-local dylib paths, missing
resources, a mismatched identifier, or an invalid bundle seal.
