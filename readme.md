# WakkaQt

**WakkaQt** is a karaoke recording and production studio built with Qt6. It plays back karaoke videos, records your voice (and optionally your webcam), applies real-time vocal enhancement, and renders everything into a finished video file — all from a single desktop window.

Current version: **2.0.2**

---

## Downloads

**Windows binaries** are available at: https://gu.pro.br/WakkaQt

For Linux, see the [build instructions](#building-on-linux) below.

---

## Features

### Playback & Recording
- Plays any video or audio file supported by Qt6 Multimedia (MP4, MKV, WebM, MP3, and more)
- Records microphone audio with selectable input device
- Optional webcam recording alongside the vocal track
- Transport controls: play/pause, stop/rewind, seek ±10 seconds
- Graphical progress bar with elapsed/total time display

### Real-Time Monitoring
- **Waveform visualizer** (`SndWidget`) — live oscilloscope display of the microphone input during recording
- **Pitch monitor** (`PitchMonitorWidget`) — always visible; shows the detected note name, octave, and cents deviation in real time using YIN pitch detection; includes a smoothed deviation bar so you can see how in-tune you are as you sing

### Vocal Enhancement Engine
The `VocalEnhancer` processes the recorded audio through a full DSP pipeline before rendering:

- **Pitch correction** — phase-vocoder-based pitch shifting with adjustable strength (0–100 %). Uses a hybrid YIN + HPS pitch detector with autocorrelation confidence gating for robust detection
- **Scale-aware correction** — snap corrected notes to a chosen musical scale (Chromatic, Major, Minor, Pentatonic, Blues, Dorian, Mixolydian, Lydian, Phrygian, Locrian, Harmonic Minor, Melodic Minor, Whole Tone, Diminished) in any of the 12 keys
- **Retune speed** — controls how quickly pitch snaps to target (0 ms = robotic auto-tune effect, up to 300 ms for a natural glide)
- **Formant preservation** — LPC-based spectral envelope extraction and re-synthesis keeps the vocal timbre natural after pitch shifting
- **Noise reduction** — spectral subtraction gate with adaptive noise floor estimation, adjustable strength
- **Reverb** — Freeverb-style Schroeder reverb with room size, decay, and wet/dry mix controls
- **Dynamics processing** — compressor, limiter, and harmonic exciter applied once after all pitch work; level matching via pre/post RMS with 4× cap to avoid over-boosting
- All FFTW plans (N = 2048 for phase vocoder, N = 1024 for noise gate, N = 4096 for pitch detection) are created once with `FFTW_MEASURE` and reused across the entire recording for maximum performance

### Preview & Mix Dialog
Before rendering, a preview dialog lets you:
- Listen to the processed vocal audio with all enhancements applied
- Adjust vocal volume via a dial
- Fine-tune the audio/video timing offset with a slider (accounts for system latency automatically)
- Tweak pitch correction amount, noise reduction amount, key, scale, retune speed, formant preservation, and all reverb parameters
- Watch a live audio visualizer of the enhanced vocal track
- Apply changes and preview again without leaving the dialog
- Seek forward/backward through the preview

### Rendering
- Mixes enhanced vocal audio, webcam video, and original karaoke playback into a single MP4
- Output resolution: 1920×1080 (karaoke stacked above webcam)
- **Native FFmpeg integration** — when the FFmpeg development libraries are present at build time, rendering is done entirely in-process via `libavformat`, `libavcodec`, `libavfilter`, `libswresample`, and `libswscale` with a real-time progress callback. Falls back gracefully to spawning `ffmpeg` via `QProcess` if the libraries are absent
- "Render Again" re-runs the mix from saved session files with updated settings
- **Pitch overlay** — a thin bar burned into the bottom of the webcam track shows the detected note name, cents deviation, and a color-coded tuning indicator (green < 10 ¢, yellow < 30 ¢, red ≥ 30 ¢). Pitch is analyzed at render time from the raw vocal recording using YIN detection with EMA smoothing; no data is logged during recording

### Rendering Safety
- All UI controls (transport, load, library, fetch, sing) are disabled for the entire duration of an FFmpeg render — whether triggered from the record flow or from the library's re-render path
- Progress-bar seek is blocked while rendering (`isPlayback = false`)
- Controls are reliably re-enabled on render success, failure, or missing output file

### Session Library
- Sessions are saved to `~/.WakkaQt/library/` as UUID-named folders containing all source files and a JSON metadata record
- The library dialog lists all saved sessions with timestamps and song names
- Sessions can be renamed, deleted, or re-rendered from the library
- Metadata stores webcam/audio/tuned paths, playback file, timing offsets, and system latency

### YouTube / URL Download
- Built-in `yt-dlp` integration downloads a video from any URL directly into the application
- A `DownloadDialog` shows download progress and handles cancellation
- The downloaded file is automatically loaded as the karaoke playback source

### Audio Visualizer
- `AudioVisualizerWidget` renders a live waveform of any audio stream (microphone monitor or enhanced vocal preview)
- Two corner visualizers are shown during webcam preview

---

## Changelog

### v2.0.2
- **Pitch overlay on rendered video** — a thin strip at the bottom of the webcam track shows the current note (e.g. "C#4"), a deviation bar, and a "+/−Nc" cents value. Color is green within 10 ¢, yellow within 30 ¢, and red beyond. Pitch is detected from the raw vocal track at render time (YIN + EMA) — no logging during recording. Scale adapts to resolution (3× for 1280+, 2× for smaller)
- **PitchMonitorWidget always visible** — the pitch display is now shown at all times, not just during recording
- **Library: multi-select deletion** — Ctrl+click or Shift+click selects multiple sessions; the Delete button is enabled for any selection, while Restore and Rename require exactly one session
- **Native sample rate preservation** — recorded audio is now extracted and processed at the microphone's native sample rate (e.g. 48 kHz); no forced downsampling to 44100 Hz in the vocal pipeline. The karaoke backing track is resampled to match if the rates differ, preventing chipmunk effects
- **Library: tuned.wav not saved** — the library no longer stores the intermediate tuned vocal; it is regenerated from the raw audio on every re-render, ensuring the preview dialog always reflects the current enhancement settings
- **A/V sync: fixed video desync when delay is applied** — when `manualOffset` was negative (vocal delayed relative to playback), the video delay was computed from a formula involving the system latency `offset` rather than `manualOffset` directly, causing the video to lag or lead audio by up to several hundred milliseconds. Both `effectiveAudioOffset` and `effectiveVideoOffset` are now set to `manualOffset` so the same shift is applied to both streams; positive → equal trim/seek, negative → equal silence prepend/video delay

### v2.0.1
- **VocalEnhancer: fixed progressive robotic distortion** — the compressor and harmonic exciter were running inside the pitch-correction loop, compounding on every re-enhance pass. They are now applied exactly once, after all pitch work is done
- **VocalEnhancer: fixed stale pitch state at section boundaries** — `smoothedPitch` was never reset after silence, causing wrong pitch-shift ratios at the start of each new phrase. After ~400 ms of unvoiced audio the pitch state is now cleared
- **VocalEnhancer: added octave detection guard** — when pitch confidence is low, a 2× or 0.5× detection error is corrected before it can influence the EMA
- **A/V sync: fixed negative manual-offset case** — when the user decremented the offset below zero, the webcam pre-roll footage was no longer seeked past, causing audio and video to drift. The `effectiveVideoOffset` formula now correctly accounts for the `offset` pre-roll in both directions
- **Rendering: UI fully locked during FFmpeg** — all controls (transport, load, library, fetch, input select) are disabled for the entire render regardless of which code path triggers it
- **FFmpeg native: keyframe alignment correction** — after `avformat_seek_file`, the first decoded frame may be earlier than requested due to keyframe rounding; the PTS of that first frame is used to compute a `seekCorrectionTb` that shifts all subsequent video frames back into exact alignment

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

If you want a debug build (with debug output enabled):

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

This installs the binary to `/usr/bin/WakkaQt`, the icon to `/usr/share/icons/hicolor/256x256/apps/WakkaQt.png`, and a `.desktop` launcher to `/usr/share/applications/`.

---

## Runtime Dependencies

| Tool | Purpose |
|------|---------|
| `ffmpeg` | Render fallback (used if FFmpeg dev libs were absent at build time) |
| `yt-dlp` | In-app video download from YouTube and other sites |

Both must be on `$PATH` at runtime. If the native FFmpeg integration was compiled in (default when dev libs are present), `ffmpeg` is only needed for the `QProcess` fallback path.

---

## License

See [LICENSE](LICENSE) for details.
