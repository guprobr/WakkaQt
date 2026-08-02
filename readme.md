# WakkaQt 🎤

> *"Because Auto-Tune is expensive and your bathroom acoustics only get you so far."*

**WakkaQt** is a free, open-source karaoke recording and production studio built with Qt6. Load a karaoke video, grab a mic, sing your heart out, and walk away with a finished, mixed, pitch-corrected MP4 — complete with a webcam feed, vocal overlay, and a pitch indicator that will mercilessly show the world every flat note you tried to sneak past.

No subscriptions. No cloud. No judgment. (Well, maybe a little judgment from the pitch monitor.)

Current version: **2.8.4**

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

## Changelog

### v2.8.4 — Transactional restore, job/repository extraction, DSP/media library split, explicit lifecycle state machine

- **`restoreSession()` is now transactional and honest about failure** — session file restores are staged to `.restoring` siblings and only swapped onto the live `/tmp/` paths once every present file has copied successfully, mirroring the same stage-then-atomic-rename principle `saveSession()` already used; a failed restore now actually returns `false` (and the Library UI shows a real error) instead of silently reporting success with missing or half-restored files. Offsets/video metadata are reset to neutral defaults before an `offsets.json` read, instead of keeping whatever was left over from a previous session, and a live recording's extracted playback track failing to copy now aborts finalization with an error dialog instead of proceeding to render anyway
- **Extracted `RenderJob` and `PreviewJob`** — the FFmpeg render orchestration buried inside `MainWindow::mixAndRender()` (both the native `QtConcurrent` path and the `QProcess`/ffmpeg-CLI fallback) and the vocal extraction/enhancement pipeline buried inside `PreviewDialog` are now standalone `QObject`-derived job classes with `progress`/`finished` signals. `MainWindow`/`PreviewDialog` keep only UI concerns; `closeEvent()` cancellation now correctly covers the `QProcess` fallback render path too, which the old code never waited on
- **`SessionManager` → `SessionRepository`, with structured results** — `saveSession()`/`restoreSession()` no longer take 9 positional parameters / 10 output references; they now take/return `SessionSnapshot`, `SaveResult`, and `RestoreResult` structs carrying a real `ok`/`error` string. `deleteSession()`/`renameSession()` return an `OperationResult` too — previously their return value was discarded entirely by the Library dialog, so a failed delete/rename just silently did nothing
- **DSP and multimedia infrastructure split into separate static libraries** — `wakkaqt_dsp` (`VocalEnhancer`, `VocalSeparator`; links FFTW + optional ONNX Runtime) and `wakkaqt_media` (`FFmpegNative`, `AudioRecorder`, `AudioVizMediaPlayer`; links FFmpeg libav* + GLIB2) replace a single flat executable target where every dependency was linked unconditionally regardless of which files actually needed it
- **Explicit lifecycle state machine replaces the `isRecording`/`isAborting` bool soup** — a single `MainWindow::State` enum (`Idle/Recording/Aborting/Finalizing/Restoring/Rendering/Separating`) with a validated `trySetState()` transition now governs the recording/render/restore/separation lifecycle. This closed several real bugs found while mapping the old implicit state: vocal-separation ("Generate Backing Track") had zero guard against running mid-recording or mid-render; a restored session's stale `isPlayback` flag could leave transport controls operating on superseded media; `RenderJob::finished`'s cancelled/failed branches never reset recording state the way the success branch did; and the restore flow's output-file cancel branch left input controls disabled when the equivalent live-record flow didn't

### v2.7.7 — Session restore correctness, WAV parser hardening, background-thread lifetime fixes

- **Fixed audio-only sessions restoring with the wrong webcam state** — `MainWindow::hasCamera` reflected whatever camera is *currently* connected, so restoring an old audio-only session while a camera happens to be plugged in could pull in a stale/unrelated webcam file left over at a shared tmp path (or, the other way round, render a session that does have webcam footage as audio-only just because no camera is attached today). `SessionManager::restoreSession()` now reports a ground-truthed `hasWebcam` for the specific session being restored (checked against that session's own folder, not any live device state), a new `recordingHasWebcam` flag tracks it separately from `hasCamera`, and stale files at the shared tmp paths are actively cleaned up on restore instead of being left for the next session to trip over
- **WAV parser now validates format tags and header invariants instead of trusting bit depth alone** — a compressed codec (ADPCM, mu-law, etc.) that happened to report a bit depth matching a PCM width used to get silently read as if its packed bytes were raw samples; `parseWavPcm()` now accepts only uncompressed PCM/IEEE-float (unwrapping `WAVE_FORMAT_EXTENSIBLE` where needed), cross-checks `blockAlign`/`byteRate` against the declared channel count and bit depth, and trims a trailing partial frame instead of either rejecting the whole file or letting stray bytes shift every sample after them
- **Fixed two background-thread lifetime risks that could touch a destroyed window/dialog** — `PreviewDialog`'s vocal-audio extraction and `MainWindow`'s native render both used to run on a background thread via a discarded `QFuture`, with results marshalled back through `QMetaObject::invokeMethod` using a context (`this`, or in the render case `qApp`) that didn't actually guarantee safe delivery if the window/dialog closed first — the render path in particular used `qApp` as the context, which gave no protection at all since `qApp` outlives everything. Both now run through a proper `QFutureWatcher`, capture only local value copies on the background thread (never live members), and `closeEvent()` on both waits for the in-flight job to finish (cancelling the render first, via the same token the "Abort Render" button already used) before allowing the window to actually close
- **Bounded a remaining unbounded `ffprobe` wait** — `getMediaDuration()`'s non-native fallback path still called `QProcess::waitForFinished()` with no timeout; a hung probe could freeze the app indefinitely. Now bounded to 5 seconds, with the process killed and a `0` duration returned on timeout — the same failure value already used for a normal parse failure

### v2.7.1 — Audio-only recording, correctness/reliability fixes across recording, render, and separation

- **Recording, preview, and render now work with no camera** — a missing webcam used to hard-exit the app at startup (`chooseInputDevice()` called `exit(-1)` if the video-input list was empty, despite the README claiming webcam capture was optional). WakkaQt now falls back to audio-only end to end: device selection no longer requires a camera, `startRecording()`/`stopRecording()` skip every camera/`QMediaRecorder` call and drive the recording-started/auto-stop-at-end-of-song UI state directly instead of relying on signals a disconnected recorder never emits, and the render dialog restricts output to audio containers (MP3/FLAC/WAV/Opus) with both the native and `ffmpeg`-subprocess render paths fixed to not require a webcam file that was never recorded
- **Fixed a real STFT/iSTFT window mismatch in the vocal separator** — the forward transform used a periodic Hann window (correctly matching how the MDX-Net model was trained) but the inverse transform used a symmetric one, breaking the perfect-reconstruction assumption overlap-add relies on and introducing amplitude-modulation artifacts into separated vocals beyond what the existing normalization step could correct for. Both now use the same periodic window
- **SessionManager is now transactional** — sessions are built under a `.partial` directory and atomically renamed into place only on full success (via `QSaveFile` for the JSON writes), instead of a save that could fail halfway through and leave a broken, half-written session silently sitting in the library
- **WAV and PCM are now cleanly separated everywhere** — a shared `parseWavPcm()` chunk-walking parser replaced several fixed-44-byte-offset reads; the most serious instance was `AudioAmplifier`, where the WAV header was staying attached to the audio buffer fed straight to `QAudioSink`, and being audibly played as noise at the start of every backing track
- **VocalEnhancer processing can now be cancelled promptly** — closing the preview dialog mid-enhancement used to block on `QFutureWatcher::waitForFinished()` for however long the (uninterruptible) DSP pipeline had left to run; a cooperative `std::atomic<bool>` cancellation token is now polled inside the hot loops (phase vocoder, spectral noise gate, pitch-map builder), cutting cancel response time from the full processing duration down to under 200 ms regardless of recording length
- **Fixed the seek step in the preview dialog's `<<`/`>>` buttons** — hardcoded to a fixed byte count that only meant a consistent ~3 seconds at one specific audio format and silently skipped a different amount of time at any other sample rate/channel count; now derived from the live `QAudioFormat` via `bytesForDuration()`
- **`AudioRecorder` no longer reads an entire recording into memory to patch its WAV header** — it used to `readAll()` the whole file on stop, then fully reopen and rewrite it just to fix up two header fields; it now reserves the header up front and patches only those two size fields in place on stop, regardless of recording length
- **The video-finalization poll (`ffprobe`) is now fully async** — previously ran `QProcess::waitForFinished()` with no timeout at all on a repeating 222 ms GUI-thread timer, so a slow or stuck probe could freeze the UI outright; it's now driven by `QProcess::finished()` with a 5-second watchdog, and self-schedules its next attempt only after the current one actually completes instead of risking overlapping checks
- **ONNX model download now verifies integrity and commits atomically** — the MDX-Net model is checked against a pinned SHA-256 before ever touching disk, and written via a temp-file-then-atomic-rename (`QSaveFile`) instead of writing directly to its final path, so a crashed or interrupted download can no longer leave a corrupt file that a future launch mistakes for a complete, ready-to-load model
- **Fixed a `setlocale` bug that could permanently pin the whole process to the C locale** — three spots in `ffmpegnative.cpp` force `LC_NUMERIC` to `"C"` while building an `libavfilter` graph (so decimal points in filter strings parse correctly regardless of system locale), but were saving `setlocale()`'s return value — which is the locale it just switched *to*, not the one it replaced — as the value to restore afterward, so the very first filter-graph build in a session would silently and permanently leave the process in the C locale

### v2.6.9 — Webcam video preview, multi-effect video filters, UI cleanup

- **Live webcam video preview in PreviewDialog** — the recorded webcam feed now plays back alongside the vocals, kept in sync with the amplifier's playback clock (rewind/seek/offset changes all resync the video immediately, with a self-healing check every 250 ms in case of drift). The video preview and vocal visualizer are always visible above the tabs, regardless of which tab is open
- **12 combinable video effects** — new "✨ Effects" tab: 🌀 Vertigo, 🎞️ Technicolor, 🟤 Sepia, ⚡ High Contrast, ⚫ Black & White, 🔦 Vignette, 🌓 Negative, 🪞 Mirror, 🌊 Wave, 👻 Trails, 📼 VHS/Old Film, and ✏️ Edge Detect. Any number can be enabled at once (chained together in order), each with friendly sliders for its tunable parameters (Strength, Amount, Speed, etc.) instead of raw filter syntax. Same `libavfilter` chain drives both the live preview and the final render, so what you see is exactly what gets rendered. Rows are collapsed by default (click a name to expand its sliders) in a scrollable list, and any effect unavailable on the current machine (e.g. Vertigo without frei0r-plugins installed) is automatically hidden instead of erroring
- **frei0r plugin discovery** — `FREI0R_PATH` is now auto-detected across Linux, Windows, and macOS at startup (checked against common per-distro/per-OS install locations, plus a `frei0r-1` folder bundled next to the executable and a Shotcut install as fallbacks), since ffmpeg's frei0r loader doesn't discover most of these on its own
- **Effect preview performance** — frames are downscaled before being run through the effect filter graph (capped at 720px on the long side), cutting per-frame filtering/copy cost by roughly 7× for a typical 1080p webcam source with no visible quality loss at preview size; the final render is unaffected and stays full-resolution
- **Fixed video preview freezing on the first frame** — playback gating trusted `QAudioSink::state()==ActiveState` to decide when to start the video, which never reported active on at least one real audio backend despite audio playing normally; now derived from the amplifier's position actually advancing between checks, the same signal the elapsed-time display already relies on
- **PreviewDialog decluttered** — reorganized into three tabs (Preview & Sync / Vocal Tuning / Effects) instead of one long scroll of ~20 stacked controls
- **Fixed placeholder/video widget getting stuck** — the karaoke video area could get stuck showing the placeholder logo over an actively-playing video (root cause: `EndOfMedia` could fire as a transient status without a real `PlaybackStateChanged`, and around a dozen call sites were independently guessing visibility instead of reading it from the player). Replaced with a single `updateVideoVisibility()` driven purely by actual player state, plus a periodic self-healing check
- **Fixed save-dialog extensions** — typing a filename with no extension at all (leaving the default filter selected) used to fail with "Invalid File Extension" instead of falling back to the selected filter's extension; now uses `QFileDialog::setDefaultSuffix()`, which also stops the dialog from silently overwriting an extension you did type
- **Fixed MJPEG 4:4:4 webcam decode spam** — Qt Multimedia's FFmpeg backend tried (and failed) to hardware-decode the baseline MJPEG/4:4:4-chroma webcam recordings, spamming decode errors and leaving the preview blank; hardware decode is now disabled for the FFmpeg backend at startup

### v2.5 — Opus render fix, masterization pipeline rework, faster visualizer

- **Fixed Opus render failure** — `renderVideo` (and the `transcodeAudio`/`muxVideoWithAudio` helpers) hardcoded a 44100 Hz encoder sample rate, which `libopus` rejects outright (it only accepts 8/12/16/24/48 kHz), aborting every `.opus` render with "cannot open audio encoder". The encoder now queries its actually-supported sample rates and picks the closest match (48000 Hz for Opus), with the resampler, frame timing, and progress reporting all updated to follow the encoder's real rate instead of an assumed constant
- **Fixed a related heap overflow in the same resampling path** — packed (non-planar) sample formats like Opus's `FLT` interleave both channels into a single buffer, but the conversion buffer was sized for one channel only. This was invisible before because every previously-exercised codec (MP3, FLAC, AAC) uses a planar format; it only surfaced once Opus rendering was reachable, and manifested as an intermittent crash after render
- **Audio masterization moved earlier in the pipeline** — the deesser/speechnorm/compressor/highpass chain now runs on the raw vocal extract immediately after recording, before `VocalEnhancer` (pitch correction, formant preservation, reverb, etc.) processes it, instead of being applied to the final vocal+playback mix. The two stages no longer compound, and the preview dialog reflects the same signal path used at render time
- **Removed the treble boost from vocal masterization** — `treble=g=8` was pushing sibilance back in after the deesser; dropped from the default filter chain
- **Fixed vocals/playback filter separation** — mastering filters are scoped to the vocal track only; the backing track reaches the output unfiltered instead of being run through vocal-shaping filters during the mix stage
- **Removed the "🎬 Render Again" button** — its job (re-render without re-singing) is still available through the Session Library ("Restore" on any saved session), so the dedicated button and its scattered enable/disable bookkeeping across the render/library/playback managers were removed in favor of that single, more discoverable path
- **Faster audio visualizer** — the waveform visualizer's update interval and frame-position sampling were tightened from 100 ms to 50 ms, giving smoother, more responsive rendering during playback
- **YouTube browser pagination** — the "🔍 Browse YouTube" search dialog now supports a "Load More" control to fetch additional pages of results instead of being capped at the first batch
- **Session restore reliability fix** — restoring a session from the Library now always points at the session's own locally-saved `playback.wav` copy when present, instead of the original external file path, so restores keep working even if the original source file has since moved or been deleted
- **`compile_commands.json` export enabled** — `CMAKE_EXPORT_COMPILE_COMMANDS` turned on for better IDE/IntelliSense support during development

### v2.3 — YouTube karaoke browser & hardware-accelerated rendering

- **YouTube karaoke browser** — new "🔍 Browse YouTube" button opens a search dialog with two scrollable rows of cards (karaoke results and originals). Thumbnails load asynchronously via QNetworkAccessManager. Clicking a card shows a preview panel with a larger thumbnail, title, channel, and duration; clicking "Download this video" fills the URL field and launches the existing yt-dlp download flow. Powered by `yt-dlp --flat-playlist --dump-json`
- **Hardware-accelerated H.264 encoding (VAAPI / NVENC)** — `renderVideo` now probes available hardware encoders at render time in order: `h264_nvenc` → `h264_vaapi` → `h264_v4l2m2m`, falling back to software `libx264`. A dummy frame is encoded to confirm the driver actually works before committing (needed because `avcodec_open2` succeeds even when hardware encoding will silently fail). For Intel VAAPI, a fast YUV420P→NV12 conversion is applied per frame so the software pipeline stays in YUV420P while the VAAPI surface gets the NV12 format Intel iHD requires
- **Dialog close properly cancels background work** — closing the backing-track conversion dialog via the window X button now sets the cancellation flag, aborting the ONNX separation thread. Previously the thread ran to completion invisibly and could show a phantom save dialog. Same fix applied to `DownloadDialog` (yt-dlp processes are now killed on X via `closeEvent`) and `PreviewDialog` (in-flight enhancement futures and ffmpeg processes are cancelled before hiding)
- **Fixed `-Wstringop-overflow` false positives** in `vocalenhancer.cpp` — GCC's false positive on `QVector(N, 0.0)` silenced with early-exit guards in `applyLPCInverseFilter` and `applyLPCSynthesisFilter`

### v2.2.3 — Video recording/render improvements
- **A/V interleaving fixed** — audio and video packets are now written interleaved by DTS in both the karaoke render and the backing-track mux. Previously all audio was written before any video, causing players to show no video from the start and no audio after seeking
- **Raw camera input preferred** — the camera format selector now picks raw/uncompressed formats (NV12, YUYV, etc.) over JPEG when available, giving the recording encoder pristine sensor data instead of pre-compressed frames. JPEG remains the fallback
- **H264 preferred over MotionJPEG for recording** — H264 delivers far better quality at the same bitrate; MotionJPEG dropped to fallback in the codec preference order
- **H265 moved to last in recording codec preference** — H265 in Matroska is unreliable in Qt multimedia on Linux and caused a hard crash; it remains available as a last resort only
- **Fixed segfault on recording error** — destroying the `QMediaRecorder` object from inside its own `errorOccurred` signal handler was undefined behaviour. The media reset is now deferred via `QTimer::singleShot` so it runs after the signal unwinds
- **Higher render quality** — output bitrate raised from 3 Mbps to 5 Mbps, H264 encoder preset changed from `fast` to `medium`, and frame scaling upgraded from bilinear to bicubic

### v2.2.1
- **Abort render / abort separation** — both the video render and the vocal separation now have an Abort button that cancels the operation mid-flight without leaving corrupted output files
- **Video-preserving backing track output** — when the input to the vocal separator is a video file, the separated instrumental is muxed back onto the original video by default, preserving the visuals. Choosing an audio format (WAV, MP3) in the save dialog still saves audio only
- **Extension auto-follows filter in all save dialogs** — switching format in any save dialog (render, library export, backing track) now updates the output filename extension automatically. Achieved via `QFileDialog::DontUseNativeDialog` + `filterSelected` signal across all three dialogs
- **Native FFmpeg for all separator I/O** — `decodeToFloatStereo`, `writeFloatWav`, `transcodeAudio`, and `muxVideoWithAudio` are now implemented natively in-process via libavformat/libavcodec/libswresample. The old `QProcess ffmpeg` / `QProcess ffprobe` calls in the separator remain as `#else` fallbacks behind `#ifdef WAKKAQT_FFMPEG_NATIVE` guards
- **Pitch monitor height reduced** — `PitchMonitorWidget` maximum height lowered from 48 px to 24 px for a more compact layout

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
