# Third-party components

- **nlohmann/json 3.11.3** — vendored as `third_party/json.hpp`, MIT license in `third_party/json.LICENSE.MIT`. https://github.com/nlohmann/json/tree/v3.11.3
- **yt-dlp** — external executable, downloaded separately. Source is Unlicense; standalone Windows bundles include GPLv3+ dependencies. Consult the exact release's notices before redistribution. https://github.com/yt-dlp/yt-dlp and https://github.com/yt-dlp/yt-dlp/blob/master/THIRD_PARTY_LICENSES.txt
- **FFmpeg / ffprobe** — external executables. The setup script selects a static GPL build from yt-dlp/FFmpeg-Builds. Build-dependent licensing and corresponding-source obligations apply. https://ffmpeg.org/legal.html and https://github.com/yt-dlp/FFmpeg-Builds
- **Deno** — external runtime, MIT licensed with third-party notices. https://github.com/denoland/deno

The setup script records the downloaded release versions and retains FFmpeg license files. It does not construct a legally complete third-party redistribution bundle. Review the applicable release licenses and supply required notices/source when packaging external tools for others.
