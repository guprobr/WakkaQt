
![wakkalogo](https://github.com/user-attachments/assets/0dc389ce-0678-4eaf-a888-04ba306ff2b4)

- Help requested: for MacOS tests. 

**Windows x86_64 zip bundle available at the end of this readme**, please follow instructions (mostly unpack, then run WakkaQt.exe)
On Windows 11 you *must* [disable Audio Enhancements on your microphone](https://support.microsoft.com/en-us/topic/disable-audio-enhancements-0ec686c4-8d79-4588-b7e7-9287dd296f72#:~:text=To%20disable%20audio%20enhancements%3A,Device%20Properties%20%3EAdditional%20device%20properties), because it messes the Qt6 recording process.

# WakkaQt v1.9 — Karaoke App

WakkaQt is a karaoke application built with C++ and Qt6, designed to record vocals over a video/audio track and mix them into a rendered file. This app features webcam recording, YouTube video downloading, real-time sound visualization, and post-recording video rendering with FFmpeg. It automatically does some masterization on the vocal tracks. It also has a custom automatic auto-tuner class called VocalEnhancer that provides slight pitch shift/correction and formant preservation.

---

## Features

- **Record karaoke sessions** with synchronized video and audio playback.
- **Mix webcam video and vocals** with the karaoke video and export the result.
- **Session Library** — save and restore previous sessions to re-adjust and re-render.
- **Real-time sound visualization** (green waveform microphone meter).
- **Download YouTube videos** and use them as karaoke tracks.
- **Video and Audio device selection** for recording.
- **Rendering** with FFmpeg for high-quality output, automatic masterization and vocal enhancement.
- **Vocal enhancement** with a custom auto-tuning class that improves vocals without distorting formants. Adjustable per render.
- **Intended cross-platform compatibility** (Windows, macOS, Linux).

## Requirements

To build and run this application, ensure you have the following:

- **C++17** or later
- **Qt6** (Qt Multimedia module)
- **FFmpeg** (for video/audio mixing and rendering)
- **yt-dlp** (for downloading YouTube videos)
- **fftw3** (for the custom VocalEnhancer class)

## Installation

1. Clone the repository:

    ```bash
    git clone https://github.com/guprobr/WakkaQt.git
    cd WakkaQt
    ```

2. Install dependencies:
   
    - Qt6: Install via your system package manager or the official [Qt website](https://www.qt.io/).
    - FFmpeg: Install from [FFmpeg website](https://ffmpeg.org/) or via your system package manager.
    - yt-dlp: Install from [yt-dlp GitHub page](https://github.com/yt-dlp/yt-dlp). *For Ubuntu 24.04 and below you must get the latest version from GitHub.*
    - libfftw3: for the custom VocalEnhancer class.

# Ubuntu/Debian
```
sudo apt update
sudo apt install qt6-base-dev qt6-multimedia-dev ffmpeg yt-dlp libfftw3-dev
```
# Fedora
```
sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg yt-dlp fftw-devel
```
# Arch Linux
```
sudo pacman -S qt6-base qt6-multimedia ffmpeg yt-dlp fftw
```
# openSUSE
```
sudo zypper install qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg yt-dlp fftw3-devel
```

3. Build the project:

    ```bash
    mkdir build
    cd build
    cmake ..
    make
    ```

4. Run the application:

    ```bash
    ./WakkaQt
    ```

## Usage

1. **Load Karaoke Track:** Use the "Load playback" option on the "File" menu to load a video or audio file for the karaoke session. It will start a preview of the playback, and enables the SING button so you can start recording.
2. **Select Input Device:** Choose the microphone or audio input device for recording.
3. **Sing & Record:** Click the "♪ SING ♪" button to start recording. The webcam will record video while the audio input records your voice.
4. **Stop Recording:** Once finished, click the "Finish!" button to stop the recording.
5. **Adjust vocals volume:** A dialog appears with a knob for you to amplify or reduce volume of the vocals. After rendering it will sound much better than the preview.
6. **Render the Video:** Renders and previews the mix of vocals and the karaoke track.
7. **Session Library:** Click the 📚 Library button or use `File → Session Library` to restore any previous session for re-adjustment and re-render without singing again.
8. **Download YouTube Video:** Enter a YouTube URL to download and use as a karaoke track.
9. **Render again:** This button appears after rendering, so you can save a new filename and adjust options again.

## Project Structure

- **`mainwindow.cpp` / `mainwindow.h`:** Core application logic, UI setup, media control, recording orchestration, playback, downloads and rendering.
- **`mainwindowLibraryMgr.cpp`:** Session Library feature — save, restore and manage past recording sessions.
- **`sessionmanager.cpp` / `sessionmanager.h`:** Disk I/O for the library — saves/loads session artefacts and JSON metadata to `~/.WakkaQt/library/`.
- **`librarydialog.cpp` / `librarydialog.h`:** Library browser dialog — list, rename, delete and restore sessions.
- **`sndwidget.cpp` / `sndwidget.h`:** Custom widget for displaying sound levels from the current audio input source.
- **`previewdialog.cpp` / `previewdialog.h`:** Preview dialog for reviewing and adjusting vocal levels before rendering. Masterization and vocal enhancement takes place here.
- **`audioamplifier.cpp` / `audioamplifier.h`:** Sample manipulation for volume adjustment; acts as a media player mixing the backing track with recorded vocals.
- **`audiorecorder.cpp` / `audiorecorder.h`:** Records audio with configurable sample format, channel count and rate.
- **`audiovizmediaplayer.cpp` / `audiovizmediaplayer.h`:** Extracts audio from playbacks and feeds the AudioVisualizer widget.
- **`audiovisualizerwidget.cpp` / `audiovisualizerwidget.h`:** The Yelloopy© audio visualizer widget.
- **`vocalenhancer.cpp` / `vocalenhancer.h`:** Custom pitch shifter — multi-step process combining pitch detection, harmonic scaling, gain normalisation and windowed overlap-add.
- **`complexes.cpp` / `complexes.h`:** Global constants (FFmpeg filter strings), shared utility functions (`isAudioOnlyFile`, `writeWavHeader`), and global temp-file paths.
- **`resources.qrc`:** Resource file for including images like the app logo.

## FFmpeg Integration

The application uses FFmpeg to mix the recorded webcam video and vocals with the karaoke video. It applies various audio filters like normalisation, echo, and compression to enhance vocal quality.

## About Windows bundle ZIP

  - **A proper FFMPEG binary** is already in the root of the application directory.
  - **yt-dlp** is already there too, for your convenience.
  - NOTE: **antivirus software degrades this software a lot**, and **VPNs might make streaming services block** the fetching of the video file when running *yt-dlp*.
    
  - You can download the windows x64 ZIP [Here on my website](https://gu.pro.br/WakkaQt-mswinX64.zip)

---


## What's new in v1.9

### New features
- **Session Library** (`File → Session Library` / 📚 Library button): every recording session is automatically saved to `~/.WakkaQt/library/` before the render step. You can restore any previous session directly to the volume/offset adjustment and re-render stage — without having to sing again. Sessions can be renamed and deleted from the library dialog.

## Contributing

Feel free to contribute by submitting pull requests or reporting issues in the [GitHub Issues page](https://github.com/guprobr/WakkaQt/issues).

## License

This project is licensed under the MIT License
