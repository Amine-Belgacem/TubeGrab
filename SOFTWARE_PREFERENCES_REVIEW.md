# Software preferences recovered from earlier projects

Reviewed 2026-10-15. This is a portable record of the user's earlier instructions, not a claim of automatic memory in unrelated chats. SOFTWARE_MASTER_PROMPT.md contains the self-contained reusable prompt.

## Sources and coverage

- “Build EpisodePlayer Windows app” — chat `01a126e5-4bd0-7bd1-8d22-6a6132a0267d`: all 78 available turns, oldest through newest, including the original attached EpisodePlayer prompt and the later pasted refinement request. Workspace: `C:/Users/21651/Desktop/root/80693`. Cross-checked current README and TODO.
- “Build a lightweight C++ radio app” — chat `01a11526-e18c-7983-b34a-d7c3cb85baf5`: all 183 available turns. Workspace: `C:/Users/21651/Desktop/root/93840`. Cross-checked current README and TODO.
- “Build native Windows video-downloder” — chat `01a13c24-6be0-7003-8f99-a1fc7494d9e3`: all four available turns at retrieval; the latest turn was still active. Workspace: `C:/Users/21651/Desktop/root/45879`. Read README and VALIDATION as supporting implementation records. No changes were made to that app for this task.
- Related chats: “Create Video Downloader”, “Radio App Features”, “Design Minimal Win32 App”, “Menu Bar Styling Recommendation”, and “Redesign Layout Feedback”. Some older ChatGPT assistant messages are exposed only as unresolved content references. Those contents were not available; user text and complete build chats supply the main evidence.
- Attached screenshots were referenced in user corrections and the implementation records; this review did not visually inspect every historical attachment. Layout conclusions are grounded in available instructions and final recorded decisions.

## Stable preferences across projects

1. Real, complete Windows software: C++, standard Win32 controls, small and fast, usable executable, clean readable source, reproducible build, dependencies/licenses and current package.
2. Functionality, stability, safety, performance and simplicity come before decoration. Avoid unnecessary code, dependencies, custom drawing, speculative features and complicated settings.
3. Most-used work controls the layout. Radio listening from Favorites is primary; station discovery is secondary. Download selection and current transfer deserve useful space. A real media collection hierarchy can justify a TreeView; two filter tabs do not justify a sidebar.
4. Native Windows appearance is the default across the main window and all dialogs. Default menus, title bar, controls, selection and focus should stay consistent. Avoid theme-colored menu workarounds, replacement toolbars and visual modifications that create extra maintenance.
5. Compact does not mean cramped. Balance actual visible insets, icon/text groups, content-to-controls gaps and slider spacing. The user repeatedly requested exact equal gaps, not approximate shifts.
6. Lists need correct row height for their content, full-width selection, useful image sizes, readable titles and consistent padding. Do not introduce stripes, grids or card borders automatically.
7. Fix centering and interaction at the source. Native edit text should be vertically centered without merely making the field shorter. Trackbar hit mapping needs real channel/thumb geometry and DPI awareness. Search must retain focus and permit continuous typing.
8. Repainting quality matters: no stale control fragments, clipped frames, trembling sliders or flicker during resizing/minimize/restore. Test actual visible rendering as well as rectangles.
9. Save window placement and preferences; recover visibly from invalid/off-screen placement. Branding needs sharp tightly padded icons and a compact logo/version/credit About dialog. Error and information dialogs need recognizable severity icons.
10. Reliability includes cancellations, deadlines, clean resource ownership, bounded caches, safe persistence, outage handling, automatic recovery, and continued task switching after failure. User data must survive upgrades, application renames, corrupt saves and interrupted operations.
11. Show realistic images when discussing substantial layout changes. Implement the approved image accurately. Preserve a working design during refinements instead of redesigning unrelated areas.
12. Respect “do not implement yet” and “add to TODO”. Once implementation is requested, complete the authorized TODO items and deliver the verified build. Do not repeatedly ask about ordinary engineering choices.
13. Verify real functionality and package contents. Keep claims proportional to evidence; tests on one machine do not establish every-PC compatibility or absence of bugs.
14. Keep old builds and leftovers under control when cleanup is authorized, without deleting user data or files needed to rebuild or review the current version.

## Final decisions that supersede earlier ideas

### EpisodePlayer → QuickPlay → ShowDeck

- One configured media folder; scanning is manually initiated. No tray support, startup scans, watchers or background services.
- Embedded video, always present in the player layout. Separate standalone video and the Show Video toggle were rejected. Theater/fullscreen are supported; Alt+L toggles the library.
- One episode-row browser with useful thumbnails; the separate thumbnail/grid mode was removed. Keep search and one sorting dropdown; status filtering was removed.
- Default native selection. Borderless episode rows use subtle bottom separators. Colored progress must remain readable on selected rows.
- Shuffle remains available in the Playback menu but is removed from the bottom bar.
- Theater/fullscreen controls moved after volume with a short light separator. Fullscreen reuses the playback controls in an interaction-revealed overlay.
- Single-click video play/pause should feel immediate; double-click fullscreen must not leave an unintended playback change.
- Stop resets position and releases playback resources; Pause preserves position. Memory investigation must account for legitimate decoder/buffer costs rather than impose arbitrary limits.
- ShowDeck branding and current credit override older names and artwork. Preserve the existing profile/history through the rename.

### QuickStation

- Favorites opens by default; permanent station list, with Now playing below it. Discovery and adding stations are secondary tasks.
- Final menu decision is completely default Windows drawing. Earlier owner-drawn/menu-color exceptions were later revoked. No added menu/header separator, gridlines or striped rows.
- Favorites has no redundant favorite checkbox. All stations supports marking favorites. Normal full-row native selection includes the logo.
- One Play/Stop toggle and one Mute/Unmute toggle, with familiar symbols; live radio has no true buffered Pause in this implementation.
- Playback controls group from the left with equal gaps to the volume slider. Logo and Play align by their visible edges; logo and two text lines form a balanced unit.
- The activity meter/dot/spectrum was explored, implemented, revised, and ultimately removed, including unused audio-level sampling. Do not resurrect it as a supposed preference.
- Automatic engine/network recovery is expected. Stop and changing stations must escape stuck Connecting; manual engine restart is not the normal recovery workflow.
- Saved stations play directly even if discovery is down. A relevant valid recent cache can assist discovery; it cannot manufacture unavailable live results.
- Final logo is the recognizable deep-plum/mint/coral radio mark. Its colors belong to this app's identity, not a required palette for all future software.

### ClearFetch downloader

- Independent application using yt-dlp and FFmpeg; files should be easy to use in ShowDeck. Do not merge the apps or couple their databases.
- Analyze before downloading. Playlist rows include thumbnails, title, duration and checkboxes; select all, deselect all or a subset.
- Required qualities/formats, destination, optional playlist folder, queue/history, progress/speed/remaining time, pause/resume where supported, cancellation, retry, recovery and duplicate protection.
- The first layout was rejected. Approved direction: framed source row; large playlist beside framed options; Queue/History below; compact Now downloading strip with clear controls. This use of a functional options panel does not justify a generic navigation sidebar.
- Tool setup/updates/logs and secondary actions belong in menus. Updates here are a functional requirement for the external extraction engine, not a global preference for automatic application updating.
- Settings and history local; UI responsive during analysis/downloads; errors understandable. Engine fixture checks, local real-media checks and live YouTube checks must be distinguished.

## How to apply these lessons elsewhere

Use the master prompt with a new application brief. Carry over engineering and usability principles; choose layout, engines, storage and failure behavior to match the new task. Do not copy tray behavior, media modes, networking, a particular database, app colors, or exact pixel values unless the new application's brief justifies them. Later explicit instructions override these defaults.
