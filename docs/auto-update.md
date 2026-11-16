# Updates

How BreadBox updates itself, for whoever publishes a release. What players see is
in the [README](getting-started.md#updates).

## What the app does

Settings → Updates offers **Ask first** (the default), **Automatic**, **Off** and
**Check now**. A manual check always asks before installing, even with Off. The
room checks at launch and after about 24 hours idle, and on wake-up when the
last check is old enough (one hour; six hours after two failed checks, 24 hours
after three or more). It never checks, downloads or installs while a game is
running. Updates require the official SD-installed title; `.3dsx`, NAND and
different-title copies cannot install updates. While the developer remote is on, player updates stay off.
In both cases the Updates screen still opens and says why ("Off while remote is on",
"Needs the installed app"), and Check now shows the same line.

## The manifest

The app reads one fixed URL,
`https://github.com/BreadChaser/BreadBox/releases/latest/download/update.json`.
It has exactly six string fields:

```json
{"title_id":"000400000B0B0B00","title_version":"25631","version":"0.9.1","size":"13600000","sha256":"<64 lowercase hex digits>","url":"https://github.com/BreadChaser/BreadBox/releases/download/v0.9.1/BreadBox.cia"}
```

The title ID, AM title version, size, display version, URL and SHA-256 are
checked together; unknown or duplicate fields and oversized input are rejected.
The CIA URL must be this repository's `releases/download/<tag>/BreadBox.cia`. The
title version must be higher than the installed one (versions only go up).

Trust comes from GitHub over verified HTTPS: there are no signing keys. Peer and
host name checks stay on (`romfs:/certs.pem`), redirects included, and the final
host must be `github.com`, `objects.githubusercontent.com` or
`release-assets.githubusercontent.com`. The downloaded CIA is checked against the
SHA-256 twice before it is installed. Anyone who controls the GitHub account can
ship code to every console that updates, so protect it with 2FA or a passkey.

## Download and retry

Downloads are staged in `sdmc:/3ds/BreadBox/updates/` and resume after a
dropped connection. Accepting Update persists the manifest and consent before
staging. Accepted downloads continue on every menu outside a game. Settings stops after verification and says "Ready. Reopen to install".
Returning to the room installs it; closing directly from Settings also works:
the next room startup rechecks the staged bytes and installs without a new
check or download, even with Off. Explicit acceptance clears an earlier
Automatic countdown postponement.

A temporary install failure keeps the accepted manifest and staged file. There
is at most one AM install attempt per launch; power and space checks before AM
remain eligible for automatic retry. The next room startup retries without
the check backoff. A fresh explicit acceptance permits another attempt. HOME,
sleep and a GPU barrier timeout cancel an uncommitted import. A damaged file is
discarded and fetched again; after two hash failures for the same manifest, the
app waits 24 hours, then retries from the room even if it stays open. Network failures retain the partial for the next launch or
Check now; a transport cancellation retries after five seconds. No retry runs
during a game or while the developer remote is enabled.

Initial discovery needs free space for three CIAs plus 4 MiB, less any matching
staged bytes. An accepted complete stage skips discovery and only needs the
install margin: two CIAs plus 2 MiB. Low space says how many decimal MB must be
free on SD. Write failures ask the player to check the SD card; network, date,
server and install failures have separate short instructions on Updates.

The power check permits installation whenever the charger is connected, or the
battery percentage is at least 10%. Below 10% while unplugged, both the room and
Updates say "Battery low. Plug in". If the percentage service cannot be read,
the app falls back to `ptm:u` battery levels (0–5), deferring only at level 0.
If either charging state or both battery readings are unavailable, installation
is allowed. No service access-list change is required. The room shows the same
instruction as Updates when an accepted update is blocked. It retries power
and space checks every five seconds, so plugging in or freeing space starts
installation without another button press. AM commits the
title only after the complete import and GPU barrier; an aborted import is
cancelled before the old executable exits. Keep power on during installation.

## Publishing a release

1. Run `make release` with the pinned SDK and makerom-fixed, using an increasing
   AM title version. Release and normal CIA packaging reject `BB_UPDATE_DEV_URL`;
   only explicit `cia-development` permits emulator fixtures.
2. Verify and freeze that exact CIA, its SHA-256 and matching ELF.
3. Run `tools/build/update-manifest.py BreadBox.cia --title-version N --version V
   --url https://github.com/BreadChaser/BreadBox/releases/download/TAG/BreadBox.cia
   --out OUT`. The tool validates CIA identity/version, URL and sizes, writes
   `update.json`, and prints the CIA SHA-256. No key is needed.
4. Run host/layout gates, exact-CIA emulator checks and hardware acceptance
   against the frozen artifacts.
5. Attach **BreadBox.cia** and **update.json** to the same GitHub release.
   Never rewrite a released CIA URL. The tools do not upload, tag or push.

Before a release, test the exact CIA on an Old and a New 3DS, including an update
from the previous release.

For emulator tests, `BB_UPDATE_DEV_URL` points at a local server; only the
`cia-development` target accepts it, and `update-manifest.py --development`
allows local HTTP.
