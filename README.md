# TubeGrab

Version 2.3 retains the approved native Win32 layout: a Source row, large side-by-side Playlist and Downloads panes, an adjustable divider, Queue/History tabs, and a compact current-transfer strip. Format, quality, destination, playlist folders, and duplicate protection are in **Download options…**. Controls share Segoe UI 10-point typography, consistent field heights, borders, padding and alignment. The interface has **Cancel all**, padded thumbnails and a slim progress track. The multi-resolution TubeGrab icon is embedded in the executable; the design reference is `design/tubegrab-layout-approved.png`.

This release verifies the output container and codecs with ffprobe before recording completion, bounds verification to 30 seconds, and closes leftover engine descendants when the root process exits. Read-only state writes fail before copying the backup, and a failed Pause action retains its recovery instructions. New tests cover abrupt owner termination, real write restrictions, a 5,000-entry playlist and a 100-job fixture queue. See `VALIDATION.md` for measured results and the remaining release limits.

A native Windows video downloader built with C++17 and Win32 common controls. There is no browser runtime, .NET, Qt, Electron, account system, or relationship to ShowDeck's database. yt-dlp downloads the media; FFmpeg merges video/audio and extracts MP3.

## Run the release

Extract `TubeGrab-2.3.0-win64.zip`, run `scripts\Setup-Tools.ps1` once to install the verified external engines, then open `TubeGrab.exe`. Visual Studio is not needed to run the release. Keep the extracted folder writable for engine updates.

In this workspace, the verified build is `bin\x64\Release-2.3\TubeGrab.exe`, with the installed engines beside it. Close an older running copy before opening this build. Existing settings, history, and interrupted jobs are retained.

## Build and run

Requirements: Windows 10 version 1703 or later / Windows 11, x64; Visual Studio 2022 with **Desktop development with C++**, MSVC v143 and a Windows 10/11 SDK.

1. Open `TubeGrab.sln`, set **Release / x64**, and build the solution. The application is `bin\x64\Release\TubeGrab.exe`. The JSON header is vendored; compilation does not need internet access or NuGet.
2. Install the external engines from PowerShell:

   ```powershell
   .\scripts\Setup-Tools.ps1
   ```

   This fetches the official current yt-dlp and Deno releases and yt-dlp's FFmpeg build, verifies their GitHub SHA-256 asset digests, and installs `yt-dlp.exe`, `ffmpeg.exe`, `ffprobe.exe`, and `deno.exe` in `tools`. It downloads executable dependencies, not media. If a release does not publish a digest, the script stops without installing it. Close TubeGrab before replacing existing tools.
3. Build again to copy the engines beside the application, or use **Tools → Tools folder…** to select the repository's `tools` directory. Keep this folder writable for updates. No administrator privileges are needed.

Command-line build and tests:

```powershell
.\scripts\Build.ps1 -Configuration Release -Test
```

The static C++ runtime is linked into the application. External tools have their own requirements. The application itself can launch without them and explains which dependencies are missing when you analyze/download.

Manual installation, including offline installations: put the four executables together in a folder and select it in the app. Sources: [yt-dlp releases](https://github.com/yt-dlp/yt-dlp/releases), [FFmpeg builds for yt-dlp](https://github.com/yt-dlp/FFmpeg-Builds/releases), and [Deno releases](https://github.com/denoland/deno/releases). Use the standalone Windows x64 yt-dlp release; it bundles the YouTube challenge solver. A recent Deno runtime is also required by [yt-dlp's current YouTube support](https://github.com/yt-dlp/yt-dlp/wiki/EJS).

## Use

1. Paste an HTTPS YouTube video or playlist link and select **Analyze** (Enter in the URL field also works). A watch URL with a playlist parameter analyzes the playlist. Remove `list=` if you only want its single video.
2. Results arrive incrementally. Scroll for thumbnails; use checkboxes, **Select all**, or **Deselect all**. The download button shows the selected count. Private/deleted/known live entries cannot be selected. Flat playlist metadata may omit a duration or fail to identify unavailable entries; the engine reports those failures during downloading. The Analyze button becomes **Stop analysis** while it is working; stopping retains entries already received.
3. Open **Download options…**. Choose 480p, 720p, 1080p, or Best, and MP4 or MP3. Resolution is an upper limit; the best compatible stream below it is selected. **MP4 uses H.264 video with AAC audio** for broad Windows/local-player compatibility. Best means the best available H.264/AAC combination, which may have a lower maximum resolution than VP9/AV1. Sources without a compatible stream fail visibly instead of silently producing a different format. MP3 uses the best audio source and FFmpeg's quality level 0.
4. Choose a destination and optionally create a folder for the playlist. Folder names include the playlist title and ID to avoid collisions. Filenames include the video title and ID. Names and destination are captured when queued so recovery uses the same path.
5. Select **Download selected**. The queue runs sequentially to limit CPU, bandwidth and disk contention. You can analyze another URL while downloading. Per-row progress shows the current stream; it may restart when the engine switches from video to audio. Processing includes merging, conversion and verification. A row completes only after successful engine exit, a nonempty output file, and an ffprobe check for H.264/AAC MP4 or audio-only MP3. Verification has a 30-second deadline and supports cancellation; it inspects container/codec metadata, rather than decoding every frame. An invalid file is retained with an error and never added to history. Move an unreadable file aside before retrying, because existing output files are protected from overwriting.

The **Current transfer** frame shows the current title, progress, speed and remaining time. Drag the divider to balance the lists, or focus it and use Left/Right; Home restores the default balance. The balance is saved. On narrow displays, Format and Speed columns collapse to preserve useful video titles; the active strip and selected-row details retain that information.

**Pause** stops the engine process tree and keeps `.part` files, fragments, and intermediate media. The same button position switches to **Resume**, which reruns yt-dlp with `--continue`. Byte-range/fragment continuation depends on the source; conversion or merging may restart. **Cancel** cancels the chosen active/pending queue rows and keeps partial files. When no queue row is selected, **Cancel** targets the current transfer. **Cancel all** stops the active transfer and cancels every queued/paused download, without a row selection. Completed downloads, history, failure diagnostics and partial files are retained. Cancellation is committed before waiting for the engine to stop, so a restart cannot revive it as paused work. These controls remain available when History is open. **Clear** removes completed/cancelled queue rows while idle, retaining history and media files. **Retry** restarts failed/cancelled items; the engine already retries transient requests and fragments up to five times. Closing saves pending/active work; reopening requires **Resume** before network activity resumes. Partial files are never automatically deleted.

**Skip downloaded videos** is enabled by default and uses History to prevent the same YouTube ID/output format from being added again, even at a different quality or destination. MP4 and MP3 have separate history keys. Uncheck it to intentionally queue completed content again; active, pending and failed queue entries still cannot be added twice. For a different quality, choose a different destination or move the existing file: `--no-overwrites` protects existing media even when duplicate protection is disabled. **Forget selected** removes history records while keeping files. This preference is saved locally; older profiles keep duplicate protection enabled.

Select a failed row to see its error in the status bar; hover the status bar for the complete message or double-click the failed row for an error dialog. **Tools → Open log** exposes recent engine diagnostics. **Show file** (or double-click a completed row) reveals media in Explorer. Right-click Queue/History rows for their native action menu. Import the ordinary MP4/MP3 files into ShowDeck using its own import workflow. Compatibility has not been tested against a particular ShowDeck build.

**Tools → Update yt-dlp** invokes the official binary's self-updater, while downloads and analysis are idle. It retains the installed release channel. If YouTube changes break extraction, update first. FFmpeg and Deno can be refreshed with the setup script. Errors caused by private, removed, restricted, or unavailable content remain visible; the app does not add account, cookie, or authentication features.

## Local data and recovery

`%LOCALAPPDATA%\ClearFetch\state.json` stores settings, queue, and history. TubeGrab deliberately keeps the original ClearFetch profile and instance-lock location so upgrades preserve existing downloads and cannot race an older running copy. Saves write a temporary file, flush it, keep `state.json.bak`, then replace the primary file atomically. A corrupt or missing primary recovers from the backup; a corrupt original is preserved as `state.json.damaged`. If both are unreadable, startup fails with recovery instructions instead of overwriting them. State write failures stop the active engine with partial files retained and pause further queue dispatch. An actionable status message explains how to recover. Queue/history transitions are saved together; a crash before committing a completed result reruns the same deterministic output path without overwriting existing media.

`session.log` is written when opened or when the app exits normally. The in-memory log is bounded to approximately 512 Ki UTF-16 characters. It can contain URLs, titles, file paths and engine diagnostics. There is no telemetry. Thumbnail requests go directly to YouTube's image host over HTTPS, on a background worker, for rows near the viewport. Missing thumbnails use a neutral placeholder; the in-memory bitmap cache is capped at 256 images. A per-user instance lock prevents concurrent state writers.

## Architecture

| Component | Responsibility |
| --- | --- |
| `App` / `AppUI` | Main-thread Win32 controls and frames, native menus, DPI layout, selection, current-transfer strip, queue state machine, bounded worker event mailbox |
| `Options` | Native resource dialog, validated draft settings, Save/Cancel behavior |
| `Engine` | yt-dlp argument construction, streamed analysis, progress parsing, verified completion, updater |
| `Process` | Shell-free Unicode `CreateProcessW`, explicit inherited handles, cancellable pipe reader, optional deadline, kill-on-close Windows Job Object and descendant cleanup after root exit |
| `Model` | Typed settings/jobs/history, metadata decoding, schema/version validation, crash recovery |
| `Common` | UTF-8 conversion, Windows paths, command-line quoting, atomic writes, native folder picker |
| `Thumbnails` | Bounded HTTPS requests and WIC image decode on a background thread |

UI and queue data are owned by the UI thread. Workers receive immutable job snapshots and post events; they never access controls. Cancellation signals are atomic. The app does not invoke a command shell, reads no user yt-dlp configuration/plugins, validates input hosts and video IDs, and uses absolute tool paths. The app requires no third-party runtime UI libraries. `third_party/json.hpp` is nlohmann/json v3.11.3 (MIT); its license is included.

## Validation

`TubeGrab.Tests` covers Unicode/metacharacter argument round trips, URL restrictions, Windows filenames, nullable metadata, MP4/MP3 selectors, progress, state recovery, corrupt/missing-primary backup recovery, unreadable profiles, real read-only and locked-file write failures, missing/empty/incompatible output rejection, bounded subprocess execution, noisy orphan cleanup and process-tree cancellation. An independent test owner is forcibly terminated while its engine writes a partial file; the test checks job cleanup, retained bytes, paused recovery and preserved history. A fake engine exercises analysis, output verification and the updater. These fixture checks do not test YouTube's live service or media encoding.

The same executable tests the real native controls using an isolated profile and disabled network workers. It also loads the native options dialog, checks transactional draft editing, destination validation and UTF-8 resource captions. Actual font objects are compared across the main window and dialog at each scale. Additional checks enforce equal field heights, borders and margins, aligned labels/actions, caption fit, dropdown visibility, frame stacking and font cleanup after repeated openings. It checks containment, overlap, frame stacking, label fit, list proportions, scrollbar changes during streamed analysis, drop-down visibility, queue/history switching, current progress/ETA, unavailable-video selection, pause/resume controls, cancellation settings-save failure, coalesced progress storms, and backward-compatible settings at 100%, 125%, 150%, and 200% application scaling and four window sizes (900×560 through 1680×980 logical pixels). Native-control PNG renders are saved under `tests/results`; the tests do not take over the desktop or edit the user's profile. See `VALIDATION.md` for the current build and live visual checks.

The native-control suite also streams 5,000 entries through the real bounded mailbox, checks bulk and individual selection, and runs a 100-job subset through the actual application dispatcher using deterministic fake yt-dlp/ffprobe executables. It checks completion, persisted history and duplicate prevention. This is queue stress coverage, not 100 live downloads. Timings and mailbox measurements are saved to `tests/results/stress-result.json`.

With tools installed, `scripts\Verify-Media.ps1 -Verifier .\bin\x64\Release-2.3\TubeGrab.Tests.exe` creates a two-second local sample, transfers it through real yt-dlp, converts MP3 using FFmpeg, validates progress JSON, and inspects outputs with ffprobe. The optional Verifier runs the production output validator against both valid files and corrupt nonempty files, checking rejection without deletion. Artifacts stay in `build\media-validation`; no YouTube connection is used.

After media validation, `python scripts\Verify-Recovery.py` uses a throttled HTTP server bound to 127.0.0.1 to inject two temporary HTTP 503 errors, interrupt a real yt-dlp transfer, drop a connection mid-response, and ignore a resume Range request. It checks partial-file recovery or restart as appropriate and compares all three final files against the source SHA-256 digest. It requires Python 3 and installed yt-dlp. Evidence stays under `build\recovery-validation`.

Manual end-to-end checklist with installed engines:

- Download a permitted single video as 480p MP4 and as MP3; inspect streams with ffprobe and play both.
- Analyze a playlist, scroll thumbnails, deselect several entries, and verify only selected entries download.
- Pause during a large transfer; resume and verify partial-file reuse. Close during a transfer and resume after relaunch.
- Cancel active and queued selections, then retry. Disconnect the network and confirm retries, final error and a responsive UI.
- Re-add completed content and verify duplicate prevention; forget a record and confirm requeueing.
- Test Unicode paths, a read-only/full destination, unavailable/private entries, 100/150/200% DPI, keyboard navigation, and engine updates.

## Distribution

Distribute `TubeGrab.exe` with the `tools` directory if desired. The downloader is separate from its external engines. Third-party executables are not part of the source project; their licensing and source obligations apply if you redistribute them. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Use the downloader for content you are authorized to save.

`scripts\Package.ps1` refreshes `dist\TubeGrab-source.zip` with the solution, source, tests, build/setup scripts, vendored JSON header and approved design reference. Generated test profiles/media and engine binaries are excluded.

`scripts\Package-Release.ps1` creates `dist\TubeGrab-2.3.0-win64.zip`, containing the executable, dependency setup script, documentation, licenses for the vendored header, and SHA-256 manifest. External engines are installed separately with `scripts\Setup-Tools.ps1`; the local built application already has the installed engines beside it. The release is unsigned. No installer or system-wide changes are needed.
