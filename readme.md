# WakkaQt

**WakkaQt** is a karaoke recording and production studio built with Qt6. It plays back karaoke videos, records your voice (and optionally your webcam), applies real-time vocal enhancement, and renders everything into a finished video file — all from a single desktop window.

Current version: **2.0.0**

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
- **Pitch monitor** (`PitchMonitorWidget`) — shows the detected note name, octave, and cents deviation in real time using YIN pitch detection; includes a smoothed deviation bar so you can see how in-tune you are as you sing

### Vocal Enhancement Engine
The `VocalEnhancer` processes the recorded audio through a full DSP pipeline before rendering:

- **Pitch correction** — phase-vocoder-based pitch shifting with adjustable strength (0–100 %). Uses a hybrid YIN + HPS pitch detector with autocorrelation confidence gating for robust detection
- **Scale-aware correction** — snap corrected notes to a chosen musical scale (Chromatic, Major, Minor, Pentatonic, Blues, Dorian, Mixolydian, Lydian, Phrygian, Locrian, Harmonic Minor, Melodic Minor, Whole Tone, Diminished) in any of the 12 keys
- **Retune speed** — controls how quickly pitch snaps to target (0 ms = robotic auto-tune effect, up to 300 ms for a natural glide)
- **Formant preservation** — LPC-based spectral envelope extraction and re-synthesis keeps the vocal timbre natural after pitch shifting
- **Noise reduction** — spectral subtraction gate with adaptive noise floor estimation, adjustable strength
- **Reverb** — Freeverb-style Schroeder reverb with room size, decay, and wet/dry mix controls
- **Dynamics processing** — compressor, limiter, and harmonic exciter applied after pitch correction; level matching via pre/post RMS with 8× cap
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
