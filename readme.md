This software is currently in **Alpha stage** for developers to test. 
Help requested: for Windows and MacOS tests. **Windows x86_64 zip bundle with binaries will be available soon.**

NOTE: **gStreamer is mandatory on all platforms**

*WARNING*:  Latest ubuntu "realtime" kernel 6.8 seems unstable; "low-latency" and the usual "generic" seems fine.

# WakkaQt - Karaoke App

WakkaQt is a karaoke application built with C++ and Qt6, designed to record vocals over a video/audio track and mix them into a rendered file. This app features webcam recording, YouTube video downloading, real-time sound visualization, and post-recording video rendering with FFmpeg. It automatically does some masterization on the vocal tracks. It also uses an AutoTuner LV2 plugin for smoothing the results, X42 by Robin Gareus, with slight pitch shift/correction and formant preservation.

## Features

- **Record karaoke sessions** with synchronized video and audio playback.
- **Mix webcam video and vocals** with the karaoke video and export the result.
- **Real-time sound visualization** (green waveform volume meter).
- **Download YouTube videos** and use them as karaoke tracks.
- **Audio device selection** for recording.
- **Rendering** with FFmpeg for high-quality output, automatic masterization and vocal enhancement;
- **Intended Cross-platform compatibility** (Windows, macOS, Linux).

## Requirements

To build and run this application, ensure you have the following:

- **C++17** or later
- **Qt6** (Qt Multimedia module)
- **FFmpeg** (for video/audio mixing and rendering)
- **yt-dlp** (for downloading YouTube videos)
- **Gareus X42 AutoTune** (LV2 pitch correction plugin)
- **gStreamer** (plugins good, bad, ugly support)

## Installation

1. Clone the repository:

    ```bash
    git clone https://github.com/guprobr/WakkaQt.git
    cd WakkaQt
    ```

2. Install dependencies:
   
    - gStreamer: Probably already installed, but must have good, bad, ugly plugin set; 
      NOTE: if you are compiling your own gStreamer, pass -Dgpl=enabled to Meson to enable x264enc, etc.
      NOTE COMPILING ON WINDOWS: Install GSTREAMER "MINGW" *64 bit RUNTIME & DEVEL*: [This is the website](https://gstreamer.freedesktop.org/download/#windows)

    - Qt6: Install via your system package manager or the official [Qt website](https://www.qt.io/).
    - FFmpeg: Install from [FFmpeg website](https://ffmpeg.org/) or via your system package manager.
    - yt-dlp: Install from [yt-dlp GitHub page](https://github.com/yt-dlp/yt-dlp).

    Pitch correction LV2 plugin:
    - Gareus Autotuner X42 [Visit website](https://x42-plugins.com/x42/x42-autotune) or install via package managers.

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

1. **Load Karaoke Track:** Use the "Load playback from disk" button to load a video or audio file for the karaoke session. It will start a preview of the playback, and enables the SING button so you can start recording.
2. **Select Input Device:** Choose the microphone or audio input device for recording.
3. **Sing & Record:** Click the "♪ SING ♪" button to start recording. The webcam will be used to record a video, while the audio input will record your voice.
4. **Stop Recording:** Once finished, click the "Finish!" button to stop the recording.
5. **Adjust vocals volume** Once finished recording, a dialog appears with a slider for you to amplify or reduce volume of the vocals.
6. **Render the Video:** You can render and preview the mix of vocals and the karaoke track before the final video or audio file.
7. **Download YouTube Video:** You can enter a YouTube URL to download and use as a karaoke track. Other streaming services URL might work as well.
8. **Render again:** This button appears after rendering, so you can save a new filename and adjust options again, then render, again :D

## Project Structure

- **`mainwindow.cpp` / `mainwindow.h`:** Core application logic, including UI setup and media control.
- **`sndwidget.cpp` / `sndwidget.h`:** Custom widget for displaying sound levels.
- **`previewdialog.cpp` / `previewdialog.h`:** Preview dialog for reviewing and adjusting vocal levels before rendering.
- **`resources.qrc`:** Resource file for including images like the app logo.

## FFmpeg Integration

The application uses FFmpeg to mix the recorded webcam video and vocals with the karaoke video. It applies various audio filters like normalization, echo, and compression to enhance vocal quality.

Ensure that FFmpeg is correctly installed and added to your system's `PATH` for the app to work as expected. Except on windows, because:

## About Windows bundle ZIP

  - **A proper FFMPEG binary version with LV2 support** is already on the root of the application directory.
  - **yt-dlp** is already there too, for your convenience.
  - First download and **INSTALL the GStreamer mingw 64 bit _runtime_ .MSI** [official link](https://gstreamer.freedesktop.org/download/#windows)
  - You will need DirectX runtime [download and install from Microsoft](https://www.microsoft.com/en-us/download/details.aspx?id=35)
  - You will need VC REDIST x64 [download and install from Microsoft](https://aka.ms/vs/17/release/vc_redist.x64.exe) 
  - After extracting the ZIP, please double-click to **run as _normal_ user the CONFIG.BAT script**. It will set up environment variables and copy the LV2 plugin to the correct directory. It will ask admin permissions for that. PS: run as _normal user_, admin permissions will be _asked later_;
  - NOTE: **antivirus software degrade this software a lot**, and **VPNs might make streaming services to block** the fetching of the video file when running *yt-dlp*.

## Contributing

Feel free to contribute by submitting pull requests or reporting issues in the [GitHub Issues page](https://github.com/guprobr/WakkaQt/issues).

## License

This project is licensed under the MIT License

