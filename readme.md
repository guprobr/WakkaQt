# WakkaQt 🎤

> *"Because Auto-Tune is expensive and your bathroom acoustics only get you so far."*

**WakkaQt** is a free, open-source karaoke recording and production studio built with Qt6. Load a karaoke video, grab a mic, sing your heart out, and walk away with a finished, mixed, pitch-corrected MP4 — complete with a webcam feed, vocal overlay, and a pitch indicator that will mercilessly show the world every flat note you tried to sneak past.

No subscriptions. No cloud. No judgment. (Well, maybe a little judgment from the pitch monitor.)

Current version: **2.9.9**

---

## Downloads

**Windows binaries** — ready to run, no setup required:
👉 https://gu.pro.br/WakkaQt

**Linux users** — you get to build it yourself, which is a feature, not a bug. See [build instructions](#building-on-linux) below.

---

## What It Does

### 1. Plays Your Karaoke Track
Drop in any MP4, MKV, WebM, AVI, MOV, MP3, WAV, FLAC, or OPUS file. If Qt6 Multimedia can play it, WakkaQt will play it. You can also paste a YouTube URL and download the video directly from inside the app (powered by `yt-dlp`).

### 2. Records You Singing It
Select your microphone from a list of all detected devices. Hit **🎤 SING**. Optionally capture your webcam at the same time — for those who want to remember exactly what they looked like belting out Bohemian Rhapsody at 2 AM.

### 3. Makes You Sound Better Than You Are
Before rendering, the vocal track runs through a full DSP pipeline:

- **Pitch correction** — phase-vocoder pitch shifting with adjustable strength (0 = raw humanity, 100 = robot perfection)
- **Scale-aware snapping** — snap pitch to Major, Minor, Pentatonic, Blues, Dorian, Mixolydian, Lydian, Phrygian, Locrian, Harmonic Minor, Melodic Minor, Whole Tone, Diminished, or plain Chromatic — in any of the 12 keys
- **Retune speed** — 0 ms for that T-Pain effect, up to 300 ms for a natural glide
- **Formant preservation** — LPC-based envelope re-synthesis keeps your voice sounding human even after aggressive pitch shifting
- **Noise reduction** — spectral subtraction gate with adaptive noise floor estimation (goodbye, fan noise)
- **Reverb** — Freeverb-style Schroeder reverb with room size, decay, and wet/dry controls
- **Dynamics** — compressor, limiter, and harmonic exciter for a polished, loud-enough final mix

All FFTW plans are created once and reused for the entire recording — no plan allocation mid-session, no glitches.

### 4. Lets You Preview and Tweak
A full-featured preview dialog lets you hear the processed vocal, adjust every enhancement parameter in real time, nudge the audio/video sync offset, and preview again — as many times as you need before committing to a render.

### 5. Renders a Professional-Looking Video
Output: a 1920×1080 MP4 with the karaoke video on top and your webcam below. The vocal is mixed in with all enhancements applied. A pitch indicator strip is burned into the webcam frame — green when you're in tune, yellow when you're drifting, red when you're… having a moment.

Native FFmpeg integration renders entirely in-process with a real-time progress bar. Falls back gracefully to spawning `ffmpeg` via a subprocess if the dev libraries weren't present at build time.

### 6. Generates a Backing Track
Load any song and click **🎵 Backing Track**. WakkaQt downloads the **UVR-MDX-NET-Inst_HQ_3** ONNX vocal separation model (~80 MB, once) and runs it locally on your machine — no internet required after the first download, no cloud service, no privacy leak, no subscription.

The model separates vocals from the instrumental using MDX-Net deep learning, processed through a full STFT/iSTFT pipeline with FFTW3. If the input is a video file, the separated audio is muxed back onto the original video by default — so you keep the visuals. You can still save as audio-only (WAV or MP3) by choosing an audio format in the save dialog. Separation can be aborted mid-way with the Abort button.

### 7. Keeps a Session Library
Every recording is saved to `~/.WakkaQt/library/` with a UUID folder, all source files, and JSON metadata. The library dialog lists everything with timestamps. You can rename, delete, or re-render any session — with updated enhancement settings — at any time.

---

## Feature Overview

| Feature | Status |
|---|---|
| MP4/MKV/WebM/MP3/WAV/FLAC playback | ✅ |
| Microphone recording (selectable device) | ✅ |
| Webcam recording | ✅ |
| Audio-only recording (no webcam required) | ✅ |
| Real-time pitch monitor (YIN, always visible) | ✅ |
| Real-time waveform visualizer | ✅ |
| Pitch correction (phase vocoder) | ✅ |
| Scale/key-aware pitch snapping | ✅ |
| Formant preservation (LPC) | ✅ |
| Noise reduction (spectral subtraction) | ✅ |
| Reverb (Freeverb/Schroeder) | ✅ |
| Compressor + limiter + harmonic exciter | ✅ |
| Preview dialog with live tweak | ✅ |
| Native FFmpeg rendering (in-process) | ✅ |
| Pitch overlay on rendered video | ✅ |
| Live webcam video preview (synced) | ✅ |
| Video effects (Vertigo, Technicolor, ...) | ✅ |
| Session library (save/rename/delete/re-render) | ✅ |
| YouTube download (via yt-dlp) | ✅ |
| YouTube karaoke browser (search + preview) | ✅ |
| AI vocal separation → backing track (ONNX) | ✅ |
| Backing track: video-preserving output | ✅ |
| Hardware-accelerated H.264 (VAAPI / NVENC) | ✅ |
| Abort render / abort separation | ✅ |
| Cross-platform (Linux / Windows) | ✅ |
| Subscription required | ❌ |
| Phone home to a server | ❌ |
| Judgment about your singing | mostly ❌ |

---

## Building on Linux

### Prerequisites

Install the required development packages (Debian/Ubuntu):

```bash
sudo apt install \
    build-essential cmake ninja-build \
    qt6-base-dev qt6-multimedia-dev \
    libqt6multimedia6 libqt6multimediawidgets6 \
    libfftw3-dev \
    libavformat-dev libavcodec-dev libavfilter-dev \
    libavutil-dev libswresample-dev libswscale-dev \
    libglib2.0-dev \
    pkg-config
```

**H264 recording support (strongly recommended)**

The standard `libavcodec` package on Ubuntu/Debian is built without H264 encoding. Install the extra-codecs variant so WakkaQt can record webcam footage in H264 (the preferred codec):

```bash
sudo apt install libavcodec-extra
```

This replaces `libavcodec` with a version that has H264 (and other patent-encumbered codecs) compiled in. Without it, WakkaQt falls back to MotionJPEG for recording, which gives lower quality at the same bitrate.

On **Fedora / RHEL**, the `ffmpeg-free` package in the standard repos also lacks H264 encoding. Enable RPM Fusion and swap in the full FFmpeg build:

```bash
sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
sudo dnf swap ffmpeg-free ffmpeg --allowerasing
```

For the **AI backing-track feature**, also install the ONNX Runtime development package.

**Debian/Ubuntu:**
```bash
sudo apt install libonnxruntime-dev
```

**Fedora / RHEL** (not in standard repos — install from the official release):
```bash
ORT_VERSION=1.20.1
wget https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz
tar -xzf onnxruntime-linux-x64-${ORT_VERSION}.tgz
sudo cp -r onnxruntime-linux-x64-${ORT_VERSION}/include/onnxruntime /usr/local/include/
sudo cp onnxruntime-linux-x64-${ORT_VERSION}/lib/libonnxruntime.so* /usr/local/lib/
sudo ldconfig
```

**Windows (MinGW / MSVC):**

1. Download `onnxruntime-win-x64-*.zip` from the [ONNX Runtime releases page](https://github.com/microsoft/onnxruntime/releases).
2. Extract and copy the contents to `C:\Program Files (x86)\onnxruntime\`.
3. Inside that folder, create the subfolder `include\onnxruntime\` and move **all header files** from `include\` into it. The final structure must be:
   ```
   C:\Program Files (x86)\onnxruntime\
     include\
       onnxruntime\
         onnxruntime_cxx_api.h
         onnxruntime_c_api.h
         ... (all other .h files)
     lib\
       onnxruntime.lib
       onnxruntime.dll
   ```
4. Copy `onnxruntime.dll` next to `WakkaQt.exe` in your build/install folder — Windows needs the DLL at runtime.

CMake will find the library automatically in `Program Files (x86)\onnxruntime` during configure.

ONNX Runtime is entirely optional — if it is not found at configure time, WakkaQt builds and runs normally without it and the backing-track button simply won't appear.

On Fedora/RHEL-based systems:

```bash
sudo dnf install \
    cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtmultimedia-devel \
    fftw-devel \
    ffmpeg-free-devel \
    glib2-devel \
    pkgconf
```

You also need `ffmpeg` and `yt-dlp` installed as runtime tools:

```bash
sudo apt install ffmpeg yt-dlp   # Debian/Ubuntu
sudo dnf install ffmpeg yt-dlp   # Fedora
```

### Configure and Build

```bash
git clone https://github.com/guprobr/WakkaQt.git
cd WakkaQt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Debug build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

### Run

```bash
./build/WakkaQt
```

### Install System-Wide (optional)

```bash
sudo cmake --install build
```

Installs to `/usr/bin/WakkaQt`, with an icon at `/usr/share/icons/hicolor/256x256/apps/WakkaQt.png` and a `.desktop` launcher in `/usr/share/applications/`.

---

## Runtime Dependencies

| Tool | Purpose |
|---|---|
| `ffmpeg` | Render fallback when FFmpeg dev libs were absent at build time |
| `yt-dlp` | In-app video download from YouTube and other sites |

Both must be on `$PATH` at runtime. The ONNX model (~80 MB) is downloaded automatically on first use of the backing-track feature and cached in `~/.WakkaQt/models/`.

### Video Effects (optional — frei0r plugins)

Most effects on the PreviewDialog's "✨ Effects" tab are plain `libavfilter` chains and always work. A few (currently 🌀 Vertigo) are backed by [frei0r](https://frei0r.dev/) plugins instead, which ffmpeg loads dynamically at runtime — **not** a build-time dependency, and entirely optional: if a frei0r plugin isn't found, WakkaQt just hides that effect from the list instead of erroring.

WakkaQt looks for the plugins in a few common locations automatically (see `main.cpp`'s `FREI0R_PATH` setup), in this order:

**Linux:**
```bash
sudo apt install frei0r-plugins   # Debian/Ubuntu
sudo dnf install frei0r-plugins   # Fedora/RHEL
```
Auto-detected from the standard per-distro paths (`/usr/lib/x86_64-linux-gnu/frei0r-1`, `/usr/lib64/frei0r-1`, `/usr/lib/frei0r-1`, `/usr/local/lib/frei0r-1`) — no extra setup needed once the package is installed.

**Windows:**
There's no single official frei0r installer. Pick one:
1. **Bundle it with the app (recommended for distributing a prebuilt binary)** — drop a `frei0r-1` folder containing the plugin DLLs next to `WakkaQt.exe`. This is the first location WakkaQt checks.
2. **Install via [MSYS2](https://www.msys2.org/)**: `pacman -S mingw-w64-x86_64-frei0r-plugins`, then copy `mingw64/lib/frei0r-1` to `Program Files\frei0r-1` or `Program Files (x86)\frei0r-1`.
3. **Reuse an existing [Shotcut](https://shotcut.org/) install** — Shotcut bundles frei0r plugins, and WakkaQt will find them at `Program Files\Shotcut\lib\frei0r-1` if that's present.

**macOS:**
```bash
brew install frei0r
```
Auto-detected from Homebrew's install path (`/opt/homebrew/lib/frei0r-1` on Apple Silicon, `/usr/local/lib/frei0r-1` on Intel), a bundled `frei0r-1` folder next to the app binary, or an existing Shotcut.app install.

---

## License

See [LICENSE](LICENSE) for details.
