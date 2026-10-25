# Reusable software build prompt

Replace the bracketed brief, then paste everything below into the new software's chat. Add any reference images or existing project files you want used.

---

Build the following software for me:

**Application:** [name or type]
**Purpose and everyday workflow:** [what I mainly do, in order]
**Required features:** [must-have features]
**Explicit exclusions or constraints:** [optional]
**Existing code / visual references:** [optional]

Act as a senior C++ Windows desktop developer. Deliver a complete, usable application, including a compiled executable when the environment supports it. Apply the following preferences without making me rediscover and correct the same issues.

### Priorities and scope

Prioritize functionality, reliability, data safety, performance, simplicity, and native Windows consistency. Keep startup fast, idle CPU low, memory justified, and the distribution reasonably small. Prefer the simplest dependable implementation over elaborate architecture or decoration.

Design around my most frequent workflow; keep secondary setup, discovery, import, maintenance, and advanced actions secondary. Implement every requested feature, but do not add speculative ones. Do not add accounts, ads, telemetry, cloud services, tray support, background services, automatic scans, or automatic updates unless this application's requirements call for them. Use the network only where its function requires it. Apply these principles to the new software; do not copy unrelated media-player or downloader features.

### Technology and native behavior

Default to C++20, Windows 10/11 x64, MSVC/Visual Studio, and standard Win32 Common Controls. Use an existing compatible project's toolchain when appropriate. No Electron, Chromium/WebView, web UI, Qt, WPF, WinUI, or heavy UI framework unless I explicitly request a different stack.

Use standard Windows title bars, menus, buttons, edits, dropdowns, tabs, lists, trackbars, dialogs, system colors, selection, focus, and keyboard behavior. Enable normal Windows visual styles. Avoid custom themes, forced title-bar colors, replacement menu bars, and custom controls. Where an essential feature needs custom content, keep it narrowly inside a native control and preserve native interaction and accessibility. Do not build a UI framework to solve a small layout problem. Use established specialist engines where needed instead of writing codecs or similar infrastructure.

### Layout and polish

Create a compact, practical, well-studied desktop layout. Give the main content useful space. Use framed sections where they clarify groups, readable lists, and a compact current-item/status area where relevant. Do not dedicate a sidebar to two tabs; use a tree or side pane only when the content actually needs hierarchy. Keep primary actions visible and move occasional actions into appropriate menus or dialogs.

Use consistent margins, gaps, typography, control heights, and icon-to-label spacing throughout every window. Balance visible top/bottom and left/right padding. Measure the edges and text Windows actually renders, not only control rectangles. Center field/tab/button text properly; do not disguise a centering problem by arbitrarily shrinking the control. Keep related controls together without oversized gaps, cramped clusters, or giant empty panels.

Size rows for their content: compact station/text rows, larger thumbnail rows only when useful. Preserve image proportions, use readable titles and sensible wrapping/ellipsis, and avoid unnecessary horizontal scrolling. Selection must cover the intended full row, including artwork, with readable progress/status. Keep native focus and selection rendering. Use subtle separators sparingly; avoid decorative borders, forced stripes, redundant filters, duplicate controls, and permanent instructions better placed in Help, tooltips, or empty states.

Use familiar, consistent button icons where useful, with tooltips for icon-only actions. No decorative activity dots, blinking indicators, or spectrum displays unless requested. Provide a distinctive app-appropriate icon with sharp Windows sizes, transparency, and little wasted padding so it reads well on the taskbar. Include a compact About dialog with logo, version, and “Developed by Amine Belgacem.” Alerts should have appropriate native severity icons.

Remember window size, position, maximized state, and relevant user choices. Restore visibly and recover safely from disconnected monitors. Support sensible minimum sizes, resizable panes where useful, clear resize affordances, and common DPI scales. Resizing, minimize/restore, scrolling, and switching views must not cause flicker, stale drawing, jumping controls, or lost input focus.

### Reliability and data

Keep slow scanning, networking, decoding, file work, and thumbnail loading off the UI thread. Use clear ownership, RAII, bounded caches/work queues, cancellation, deadlines, and clean shutdown. Discard obsolete asynchronous results. A stalled operation must not trap the app indefinitely: cancellation and switching tasks must still work, with automatic recovery for recoverable engine/network failures and clear errors for permanent ones. Do not make routine recovery depend on a manual engine restart.

Use Unicode throughout, including Arabic/French names and paths. Persist locally with validated input, atomic saves/transactions, backup/recovery where appropriate, and compatible migrations. Preserve settings/history across upgrades and renames. Never silently overwrite media, lose a valid profile, or delete originals before verifying a transfer. Preview destructive organization/renaming. Keep external helpers safely invoked and dependencies reproducible, documented, and packaged with required licenses.

Controls must do exactly what they indicate. Search must accept normal continuous typing. Selection, sorting, filtering, and context actions must target the correct visible item. Sliders must map clicks/drags accurately at different DPI scales without background updates fighting user input. Distinguish Pause, Stop, Resume, Cancel, and Retry according to this software's real behavior. Fix root causes rather than adding arbitrary offsets or hiding failures. Measure memory and resource use; do not sacrifice essential quality to meet an invented RAM target.

### Working process and delivery

Inspect the existing project and references first. Preserve an approved design and working features when refining it. Later explicit corrections supersede older proposals. Keep a concise requirements/TODO record, including exclusions and acceptance checks. If I ask only for ideas, a preview, or TODO notes, respect that scope. If I authorize implementation, continue through build, verification, and packaging without repeatedly asking permission for routine decisions. Ask only when a missing answer materially changes the result or an action requires authorization.

For a new interface or substantial redesign, show a realistic image achievable with the chosen native controls before committing to that layout. If I request preview approval, wait for it; otherwise continue with reasonable assumptions. Implement the shown design faithfully rather than delivering a different arrangement.

Build incrementally and verify the actual application. Exercise its core workflow with real engines/files where available, plus meaningful regression and failure tests. Inspect the native window visually; passing geometry tests alone does not prove it looks correct. Check long text, Unicode, empty/error states, rapid task changes, restart/recovery, resize/minimize/restore, and 100%, 125%, 150%, and 200% DPI where possible. Report environmental limits and distinguish fixtures from live verification. Do not claim universal compatibility or zero bugs.

Deliver the current runnable Release package, complete source/build files, dependencies/licenses, concise build/run instructions, and an honest validation/limitations record. Verify that the package contains the tested executable and required runtime files. Keep the repository tidy; remove obsolete generated artifacts within authorized cleanup scope while preserving current builds, source, references, rebuild dependencies, and user data. End with direct links and a concise account of what works, what was tested, and what remains unverified. Do not stop at a mockup, plan, stubs, or uncompiled code when the environment allows completion.
