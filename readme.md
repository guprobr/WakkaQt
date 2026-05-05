# WakkaQt 🎤

> *"Because Auto-Tune is expensive and your bathroom acoustics only get you so far."*

**WakkaQt** is a free, open-source karaoke recording and production studio built with Qt6. Load a karaoke video, grab a mic, sing your heart out, and walk away with a finished, mixed, pitch-corrected MP4 — complete with a webcam feed, vocal overlay, and a pitch indicator that will mercilessly show the world every flat note you tried to sneak past.

No subscriptions. No cloud. No judgment. (Well, maybe a little judgment from the pitch monitor.)

Current version: **2.1.3**

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

### 6. Generates a Backing Track (NEW in v2.1.3)
Load any song and click **🎵 Backing Track**. WakkaQt downloads the **UVR-MDX-NET-Inst_HQ_3** ONNX vocal separation model (~80 MB, once) and runs it locally on your machine — no internet required after the first download, no cloud service, no privacy leak, no subscription.

The model separates vocals from the instrumental using MDX-Net deep learning, processed through a full STFT/iSTFT pipeline with FFTW3. The result is exported as WAV or MP3. Perfect for turning any song into a backing track for your next performance.

### 7. Keeps a Session Library
Every recording is saved to `~/.WakkaQt/library/` with a UUID folder, all source files, and JSON metadata. The library dialog lists everything with timestamps. You can rename, delete, or re-render any session — with updated enhancement settings — at any time.

---

## Feature Overview

| Feature | Status |
|---|---|
| MP4/MKV/WebM/MP3/WAV/FLAC playback | ✅ |
| Microphone recording (selectable device) | ✅ |
| Webcam recording | ✅ |
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
| Session library (save/rename/delete/re-render) | ✅ |
| YouTube download (via yt-dlp) | ✅ |
| AI vocal separation → backing track (ONNX) | ✅ |
| Cross-platform (Linux / Windows) | ✅ |
| Subscription required | ❌ |
| Phone home to a server | ❌ |
| Judgment about your singing | mostly ❌ |

---

## Changelog

### v2.1.3
- **Generate Backing Track** — new feature powered by the UVR-MDX-NET-Inst_HQ_3 ONNX model. Downloads the model on first use (~80 MB), then separates vocals from any loaded track entirely offline. Output can be saved as WAV or MP3. The button appears automatically when a file is loaded and disappears when it isn't needed
- **Save to existing file fixed** — overwriting an already-existing WAV with the backing track output no longer fails silently and loses the processed audio. The destination is removed first; on copy failure the temp file is preserved and its path is shown in the error dialog
- **Playback stopped before separation** — WakkaQt now stops playback before starting the ONNX separation process to free audio and CPU resources for the model run
- **CMake: ONNX Runtime optional dependency** — ONNX Runtime is detected at configure time; if absent, the backing-track feature is disabled gracefully with a build warning and everything else continues to work

### v2.0.2
- **Pitch overlay on rendered video** — note name (e.g. "C#4"), cents deviation bar, and "+/−Nc" value burned into the bottom of the webcam track. Color-coded: green < 10 ¢, yellow < 30 ¢, red ≥ 30 ¢
- **PitchMonitorWidget always visible** — pitch display is shown at all times, not just during recording
- **Library: multi-select deletion** — Ctrl+click or Shift+click to select multiple sessions for batch delete
- **Native sample rate preservation** — recorded audio is processed at the microphone's native sample rate (e.g. 48 kHz); no forced downsampling to 44100 Hz
- **Library: tuned.wav not saved** — the intermediate tuned vocal is regenerated from the raw audio on every re-render, so the preview dialog always reflects current settings
- **A/V sync: fixed video desync with manual offset** — both audio and video offsets are now set to `manualOffset` directly; previously a formula involving system latency caused the video to lag or lead by hundreds of milliseconds

### v2.0.1
- **VocalEnhancer: fixed progressive robotic distortion** — compressor and harmonic exciter were running inside the pitch-correction loop; now applied exactly once after all pitch work
- **VocalEnhancer: fixed stale pitch state at section boundaries** — pitch state is cleared after ~400 ms of silence, eliminating wrong shift ratios at phrase starts
- **VocalEnhancer: octave detection guard** — 2× / 0.5× detection errors corrected when pitch confidence is low
- **A/V sync: fixed negative manual-offset case** — webcam pre-roll footage is correctly seeked past in both directions
- **Rendering: UI fully locked during FFmpeg** — all controls disabled for the entire render regardless of code path
- **FFmpeg native: keyframe alignment correction** — PTS of the first decoded frame is used to shift all subsequent video frames into exact alignment after a seek

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
git clone https://github.com/YOUR_USERNAME/WakkaQt.git
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

---

## License

See [LICENSE](LICENSE) for details.
