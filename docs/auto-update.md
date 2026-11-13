# BreadBox automatic updates

Settings → Files → Updates offers **Automatic** (the default),
**Ask first**, **Off**, and **Check now**. A manual check always asks before
installation, including when Off is selected. A status line distinguishes
network, manifest, hash, SD, and install failures. No-Wi-Fi failures stay quiet.
The room checks on launch, after wake/restore, and after about 24 hours idle.
It never checks, downloads, or installs while a game owns the session.
Developer-remote/benchmark sessions reserve their own update/import/capture path;
player updates stay inactive when `dev_enabled()` is true.

## Trust and transport

Discovery uses the pinned latest asset URL
`https://github.com/BreadChaser/BreadBox/releases/latest/download/update.json`
to avoid the unauthenticated API budget. Updates trust GitHub over verified
HTTPS; there are no signing keys or detached signature assets. Discovery is fixed
to that URL; CIA URLs must be exactly that repository's `releases/download/<tag>/BreadBox.cia`
path, with one nonempty tag containing only ASCII letters, digits, `._+-`
(except `.` and `..`). Escapes, extra path segments and query strings are rejected.
The strict, bounded manifest has six unique string fields:

```json
{"title_id":"000400000B0B0B00","title_version":"25631","version":"0.9.1","size":"13600000","sha256":"<64 lowercase hex digits>","url":"https://github.com/BreadChaser/BreadBox/releases/download/v0.9.1/BreadBox.cia"}
```

Title ID, numeric AM title version, exact length, display version, immutable URL
and SHA-256 are validated together. Unknown fields, duplicates, escapes, foreign
repositories and oversized input fail closed. The staged CIA is SHA-256 checked
twice. AM's CIA metadata must match the manifest ID/version and exceed the
installed AM version. 3DSX sessions cannot install. SHA-256 detects payload
corruption; trust in the manifest comes from GitHub and verified HTTPS.
Anyone controlling the GitHub account can ship code to every updating console;
protect it with 2FA/passkeys and avoid automated publishing.

Curl/mbedTLS uses `romfs:/certs.pem`; peer and hostname verification are explicitly
**enabled**, including redirects. Production permits only HTTPS. Before accepting
a manifest response or caching a CIA URL, the final URL must use exactly
`github.com`, `objects.githubusercontent.com`, or `release-assets.githubusercontent.com`.
Cached CDN URLs are checked again before reuse. These are final-host checks; curl
can contact another HTTPS host while following a redirect, but rejects its result.
A persistent multi handle reuses connections across bounded 256 KiB queue turns. The first
CIA request resolves GitHub's redirect, subsequent turns use the final asset
URL, and expired CDN links (403/410) are resolved again once. There is no new TLS
chain for every chunk. The SD-space IPC is cached within each turn (first write,
then every MiB), rather than called for each received packet.

Desktop chain inspection found suitable bundled trust anchors for GitHub
and release-assets TLS 1.2. A desktop release-asset HEAD on 2026-11-04
confirmed the redirect to `release-assets.githubusercontent.com`;
`objects.githubusercontent.com` is retained for GitHub asset compatibility. This does **not** prove 3DS TLS reachability,
RTC validity, Old CPU cost, or CDN path building. Test these on both
Old and New hardware. Never disable certificate checks. If hardware rejects
the chain, fix the certificate bundle or TLS compatibility before release;
keep the fixed discovery URL, exact repository asset paths, final-host checks
and certificate verification enabled.

## Worker, writers and commit boundary

The low index lane owns discovery, resumable download and the first SHA check
(Old: core 0, priority 0x31; New: core 2). Import must use the low lane too;
visible checkpoints are for bounded UI reads. `bool bb_index_background_start(void)`
is main-thread-only, starts the shared worker without a scan, and does not need
a configured games folder. release integration must use this same API.

Installation has its own worker. Main blocks input and presents the shared plain
“Updating BreadBox…” progress card; this surface requests no scene assets or
background jobs. Main keeps processing APT/HOME. The worker settles queued save
and playlog work, atomically saves config, closes the log writer, and reserves
exclusive writer ownership. The **second full SHA**, AM metadata check and 16 KiB
AM write loop all run on that worker, below UI priority. A pending failed final
save or failed config replacement blocks AM. The installer is attempted at most
once per launch. During a game no phase can start.

`bb_writers.h` is the small neutral import/save contract:

```c
bool bb_writer_begin(void); // reserve before scheduling; false means defer
void bb_writer_end(void);   // after the last write/close/rename
bool bb_writers_busy(void);
```

Config, playlog and battery/RTC persistence reserve writers; save/later queues
and index/import work are drained before the installer's atomic exclusive CAS.
The import worker must reserve before enqueueing, release after its final
rename, and retain its request if reservation fails. This closes the race
between a busy check and AM installation. The log is stopped before exclusivity;
APT cannot reopen it while the installer owns the gate.

Before `AM_FinishCiaInstall`, the worker requests a **between-frame** acknowledgement.
Main finishes its current presentation, drains the GPU, acknowledges, and stops
all drawing.
Only then may AM commit. This prevents concurrent shared-font or lazy ROMFS reads
from the last progress frame. After success Home returns without `leave()`,
main bypasses its normal config/sound/UI teardown, joins the finished worker,
closes cached curl/SOC sessions without cover-state writes, exits dev networking,
unmounts ROMFS and sets `aptSetChainloaderToSelf()` **last**, after network exit.
No progress frame, scene leave, shared font, certificate or ROMFS read follows
commit. APT consumes the chainloader at process exit. HOME/Close before commit
cancels unfinished AM work; HOME while installing is still serviced. The APT
hook asks the worker to cancel, waits for its file/AM handles to close, and only
then hands the SD archive to the system. Streaming SHA checks also poll this
cancellation between bounded reads.

AM cancel runs on any unfinished read/write/finish failure. There is no installed-title or
ticket deletion, NAND install, reboot or privileged code; recovery deletes only
an uncommitted pending import context. Atomic AM commit and
APT/kernel behavior still need real-console measurement; software cannot promise
to undo a successful AM commit if the subsequent platform relaunch fails.

## Staging and retry policy

Only `sdmc:/3ds/BreadBox/updates/` holds staging, receipt and failed-hash metadata;
preferences use `updates.txt` in BreadBox's directory. A manifest SHA sidecar keys
the partial CIA. A new manifest SHA discards the old partial file. Wi-Fi loss/code 0,
HTTP 5xx and other transient errors retain the prefix and discard error bodies.
An ignored Range/HTTP 200 discards the prefix and starts a bounded full GET;
416 succeeds only if the local file already has the exact manifest length.
Definite SHA/size mismatch discards the part; a local read/close error retains it.
After two failures for one manifest SHA, persist a 24-hour backoff, including across
launches. Check now respects it. A changed manifest SHA can proceed immediately.

Space reservations are three CIA sizes + 4 MiB before staging, 2 MiB spare during
writes, and two sizes + 2 MiB before AM. All mutable state handoffs use atomics
or the worker completion release/acquire flag. A receipt is written before AM;
only a launch whose installed AM version matches it shows “Updated to 0.9.1”.
Successful staging cleanup is deferred until that next launch.

## Private testing and release procedure

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

For isolated emulator fixtures use `BB_UPDATE_DEV_URL` pointing at a local
mock server, then package only via `cia-development`. Generate the manifest with
`--development` to permit local HTTP and reserved emulator counters. Fixtures
retain full manifest/CIA/title/version/hash verification. No credentials are needed.

## Acceptance limits

Host tests execute production parsing, crypto, curl staging and AM orchestration
with fake console services, real pthread installation, explicit commit/draw
handshake, writer exclusion, failures, resume and settings. Host OpenSSL and
target mbedTLS exercise the same fixed SHA-256/parser known-answer test; the
target logs its result on startup.

Hardware remains required for GitHub GET + ranged asset TLS on Old/New, room frame
times while downloading, install durations, HOME/sleep, low-space behavior, and
repeatable relaunch safety. Test five cold/HOME cases per model and record
kernel PM faults. No hardware tests are authorized for reserved
16400. Automatic remains the default; do not publish an
updater-enabled player release until the maintainers accept the measured hardware
results.

## Runtime safety behavior

Automatic downloads and verifies silently. The room waits five seconds without
held input, then shows a shared three-second countdown notice. Any button or
touch during that countdown postpones installation for this launch; the staged
CIA remains available next launch. Wake resets the idle timer. Ask first and
Check now require explicit approval. Automatic remains the default.
The installer requires a connected adapter (PTMU), or MCU battery percentage at
least 50; unavailable telemetry defers. Power is checked again on the installer
worker immediately before starting AM. Cached room checks run at most every five
seconds. During the worker's index/cover drain, second SHA, and AM writes, the UI continues
presenting a shared progress card; SHA contributes the first 9%.

Startup and pre-install recovery enumerate SD pending titles and delete only
BreadBox's pending import with AM_DeletePendingTitle. Any enumeration/deletion
failure disables installation. No unrelated pending title is touched. Host tests
cover mixed title lists and failure. This is cleanup of an uncommitted import,
not rollback. Until hardware tests establish otherwise, a cut during
AM_FinishCiaInstall may leave the old title, the new title, or a damaged title
requiring manual recovery; the app cannot promise that it will still launch.
Power-cut behavior is not established by software tests. The
hardware acceptance plan must record title version, pending entries and
free space after cuts during streaming and near commit, using approved counters
rather than reserved 16400. Azahar support is covered by emulator validation.

Intervals use monotonic system ticks. Wake requests require at least one hour
since the last check, increasing to six then 24 hours after repeated discovery
failures. RTC rollback restarts and persists the manifest-SHA failure backoff;
it cannot underflow an interval or erase the backoff. Certificate verification
failures offer "Check the date and time" in Settings, without loosening TLS.
A failing SHA-256/parser known-answer test disables updates for the launch.

After failed/canceled installation, status.txt atomically retains the failure
phase (technical reason, also logged); Settings says "Last install didn't finish"
(or "Stopped. Will try again") in plain words that fit the card's foot line
(tests/ui_fit/update_lines.py). Good results are written in ink, problems in red.
The index lane and previous library root are restarted. Losing the exclusive
writer race defers instead of consuming the single AM attempt. Writer bookkeeping
faults increment a counter and permanently refuse exclusivity; they do not abort
or decrement an invalid count. Servers ignoring Range abort the ranged body
immediately, then use a bounded-size full GET with a low-speed stall timeout.
No-longer-newer manifests remove stale stage/sidecar files.

Production manifest generation refuses title counters 16399–16400, reserved for development.
Release counters come from `BB_VERSION` (`tools/build/title-version.py encode`):
the major field is 16 + 16*major + minor, the minor field the patch and the micro
field 15: 0.9.0 is 25615, 0.9.1 25631, 1.0.0 32783, 1.0.1 32799. 0.9.0 is above every
private test CIA installed before it (all 16xxx). Private test builds for a release use
the 15 counters just below it (0.9.1: 25616..25630), so the release installs over them. Test counters stay in isolated Azahar storage. A console already above
a release counter declines it. The host installer uses a canary-filled 96 KiB pthread stack; the ASan run
touched 27,920 bytes including pthread startup. This is not ARM headroom.
The installer has not received a reliable target high-water measurement: libctru exposes an opaque Thread, and
host pthread/ASan stack consumption does not establish target headroom. Target
stack measurement remains part of hardware acceptance; no stack margin is
claimed from the passing host tests.

The installer polls `bool bb_index_shutdown_done(void)` after the bounded index
shutdown request; an outstanding index syscall cannot survive into AM commit.
Only the installer owns index lifecycle during that drain. Home tests busy before
committed as one presentation guard, and tick rechecks committed after observing
busy clear; a worker finishing between those reads cannot resume room drawing or
reopen the log. Host lifecycle checks pin these handoff guards.

Library publication pauses the shared network path. If it cancels discovery or
staging, the updater retains staging, waits five seconds in the room, and retries
the interrupted phase. Discovery defers only while the index is busy without a usable snapshot. Such
cancellations do not count as network failures or consume the AM attempt.

Progress/countdown copy uses plain periods because the progress font does not
contain the ellipsis glyph. The final copy correction preserves every executable
byte and every symbol address; `ascii-layout-proof.json` records this against
the artifact that passed the layout-growth stress test. Baseline and exact
acceptance entries were checked again for the final artifact.

For an authorized hardware fixture, `BB_UPDATE_TEST_REMOTE` permits
remote diagnostics while the player updater runs. It requires `BB_UPDATE_DEV_URL`
at compile time; benchmark sessions and active developer transfers still block
updates. Normal/default-URL builds retain the remote-session exclusion. Never
distribute this explicit development package.

## Crash journal, bounded waits and the relaunch decision

The log is closed before AM runs, so the last steps of an install used to leave
no trace. `updates/phase.txt` (`app/services/update_phase.c`) records each phase as
one fixed 39-byte line, `BBUP1 seq phase result stamp checksum` (FNV-1a over the
first 30 bytes; `stamp` is the monotonic millisecond clock): flush, log-close,
second-SHA, receipt, AM-start, writes-done, GPU-ack, finish, handles-closed,
installer-join, covers-exit, devnet-exit, audio-exit, romfs-exit, then
chainload or HOME-return, then GPU-exit (`result` is 1 ok / 0 failed, or the
service result code for AM start and finish, or the byte count for writes-done).
A GPU-exit that fails after a chainload is followed by HOME-return 0. A failed
install ends with `failure`. Phase numbers are only ever appended (audio-exit
and GPU-exit come after next-boot), so a journal reads the same in every build. Each record is one open, one write, one flush and one close of a
separate small file (never the AM destination, no handle kept), at most 32
records. A torn or damaged line is skipped on read; every complete line before
it survives, and the next append first ends a torn last line with a newline, so
the new record is never glued to the fragment. Records are written by one
writer at a time: a writer that finds the journal busy (only possible after a
timeout) drops its record instead of waiting. A new install keeps the previous
journal as `phase.prev.txt`. The next start logs the whole journal once
(`update: previous install journal ...`), says where it ended, and appends
`next-boot`, so "relaunch requested but the new process never started" differs
from "relaunch worked"; a journal that already ends in `next-boot` is not logged
again. Readable from the developer remote with the generic
`get sdmc:/3ds/BreadBox/updates/phase.txt` (and `phase.prev.txt`).

Waits on the install path are bounded, and none of them kills a thread: the
background save writer (15 s), the log writer (5 s), the main thread's poll of
the GPU queue after the last progress frame (4 s, no empty frames submitted)
and the worker's wait for that acknowledgment (10 s), the installer join after
the commit, the cover workers, the developer worker, the UI sound and the GPU
(5 s each). A timeout before
`AM_StartCiaInstall` abandons the install (the log and writers stay as they
were) and one before `AM_FinishCiaInstall` cancels the AM import, so no AM/FS
handle is abandoned before the commit. After the commit AM is done, but a
timeout turns the relaunch into a return to HOME while the process still exits
with work in flight: after an installer-join timeout the installer's
`FSFILE_Close` / `amExit` tail may still be running, and after a developer-join
timeout the developer worker still holds a live SOC session. Exiting with a live
SOC session is the `covers.c` pm-crash precedent, so on that path the HOME
return is not a safe fallback. Only developer builds have the developer worker;
public builds cannot take that path. The APT exit hook likewise returns at once
once the install has committed, so a stuck installer tail cannot hold the exit.
`AM_FinishCiaInstall` itself cannot be bounded or cancelled.

Every thread must be gone before `main` returns: libctru's exit unmaps the heap,
where thread stacks live, before it ends the process, and a thread that runs after
that faults (a Luma exception). The committed path therefore ends the paused cover
workers (they still wake every 100 ms), drops a pending music read and joins the
background reader, stops the UI sound (`ndspExit` ends libctru's NDSP thread, or
CSND is released) and, after the relaunch decision, calls `gfxExit` (the GSP event
thread), as a normal quit does. `ndspExit` and `gfxExit` join their threads without
a bound, so they run on a short helper thread (its step passed as its own read-only
argument) that main waits for at most 5 s.

At the first teardown step that does not finish in time, no further step runs:
nothing the leftover thread can reach is freed, unmounted or closed (a live
background reader keeps ROMFS mounted, a live cover worker keeps curl, its pixels
and its sockets). The journal records the step's 0 and HOME-return, and main ends
the process itself (`exit_terminal`) instead of returning: libctru's exit would
still free each service's buffers, close its handles and unmap both heaps before
ending the process. `exit_terminal` does what libctru's own CIA exit does last
(`APT_PrepareToCloseApplication`, then `APT_CloseApplication`, which has NS end the
process, then `svcExitProcess`) with nothing in between, so the kernel stops every
thread before their memory goes. It closes the socket service first (a process that
ended with it open once crashed the process manager); that frees none of the app's
memory. It always returns to HOME: the chainload is only applied by `aptExit`, which
does not run. A normal quit or a HOME Close takes the same path when the index
worker (3 s) or a cover worker (3 s) does not stop, and when a PlayStation game was
closed from the HOME Menu: without the GPU right its core cannot be torn down, so
its CD, dynarec, GPU, SPU and rewind threads still run; from then on the session
keeps the system's memory loan and the exit only writes the saves and the settings.
The normal quit retires the importer and the covers (which may restart the index
worker for a stranded cleanup) before the index shutdown, and then closes the
index so that no worker starts after it. A 3dsx cannot end its process this way
(the Homebrew Launcher goes on in it), so a loader build returns with the leftover
thread still running; only the CIA ships.

After a commit, `bb_update_handoff` waits until two seconds have passed since AM
finished, then requests the relaunch only when every teardown step finished in
time, APT is not closing the app and the app still owns the GPU. Otherwise it
clears the chainloader and APT returns to HOME. The ending is a build-time choice
for hardware testing (`-DBB_UPDATE_HANDOFF=0` always HOME, `1` relaunch self,
the default, `2` launch the installed title by ID with `aptSetChainloader`);
there is no player setting. The two-second settle time and the choice of
soft-reset versus launch-by-ID are unproven on hardware: they are the levers to
try if a journal on the New ends at `chainload` without a `next-boot`.

Precommit preparation polls cover/import and index quiescence with a five-second
monotonic deadline. HOME/sleep cancellation keeps polling retirement until workers
stop or that deadline expires; it never enters AM. Unresolved final saves block
preparation before consumers are paused. Save and log joins use short slices
within their existing 15/5-second bounds. A consumer that cannot stop retains all
of its memory and handles; the shell selects its process-only terminal exit,
without entering AM or returning to gameplay. That terminal path still resolves
saves and writes configuration while skipping joins/teardown of abandoned workers.
APT cancellation waits at most 30
seconds for the installer before selecting that same terminal exit. The bounded
postcommit GPU acknowledgment, AM completion and handoff phases are unchanged.

If a final save failed and the player chose Later, a ready update's card says
"Waiting: save not written". Its download and approval are retained. Retrying the
save successfully or discarding the retained data clears this blocker; the next
eligible tick in the room (after leaving Settings, with power and writers ready)
starts the approved installation without asking again. Settings never installs.
