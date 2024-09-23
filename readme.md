This software is currently in Alpha stage for developers to test. Help requested: for Windows and MacOS tests. 
NOTE: *gStreamer is mandatory on all platforms*.

*WARNING*:  Latest ubuntu "realtime" kernel 6.8 seems unstable; "low-latency" and the usual "generic" seems fine.

# WakkaQt - Karaoke App

WakkaQt is a karaoke application built with C++ and Qt6, designed to record vocals over a video/audio track and mix them into a rendered file. This app features webcam recording, YouTube video downloading, real-time sound visualization, and post-recording video rendering with FFmpeg. It automatically does some masterization on the vocal tracks. It uses Gareus AutoTune X42 LV2 plugin for smoothing the results.

## Features

- **Record karaoke sessions** with synchronized video and audio playback.
- **Mix webcam video and vocals** with the karaoke video and export the result.
- **Real-time sound visualization** (green waveform volume meter).
- **Download YouTube videos** and use them as karaoke tracks.
- **Audio device selection** for recording.
- **Rendering** with FFmpeg for high-quality output.
- **Cross-platform compatibility** (Windows, macOS, Linux).

![Screenshot from 2024-09-16 06-48-18](https://github.com/user-attachments/assets/fe3c86a1-1ee9-4529-90b4-d97e50ca5bf7)

## Requirements

To build and run this application, ensure you have the following:

- **C++17** or later
- **Qt6** (Qt Multimedia module)
- **FFmpeg** (for video/audio mixing and rendering)
- **yt-dlp** (for downloading YouTube videos)
- **AutoTalent** (LADSPA pitch correction plugin)
- **gStreamer** (plugins good, bad, ugly support)

## Installation

1. Clone the repository:

    ```bash
    git clone https://github.com/guprobr/WakkaQt.git
    cd WakkaQt
    ```

2. Install dependencies:
   
    - Qt6: Install via your system package manager or the official [Qt website](https://www.qt.io/).
    - FFmpeg: Install from [FFmpeg website](https://ffmpeg.org/) or via your system package manager.
    - yt-dlp: Install from [yt-dlp GitHub page](https://github.com/yt-dlp/yt-dlp).
    - LADSPA Tom Baran AutoTalent: Install from [His website](http://tombaran.info/autotalent.html) or via pkg manager

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

1. **Load Karaoke Track:** Use the "Load playback from disk" button to load a video or audio file for the karaoke session.
2. **Select Input Device:** Choose the microphone or audio input device for recording.
3. **Sing & Record:** Click the "♪ SING ♪" button to start recording. The webcam will be used to record a video, while the audio input will record your voice.
4. **Stop Recording:** Once finished, click the "Finish!" button to stop the recording.
5. **Render the Video:** You can preview the mix of vocals and the karaoke track before saving the final video or audio file.
6. **Download YouTube Video:** You can enter a YouTube URL to download and use as a karaoke track.

## Project Structure

- **`mainwindow.cpp` / `mainwindow.h`:** Core application logic, including UI setup and media control.
- **`sndwidget.cpp` / `sndwidget.h`:** Custom widget for displaying sound levels.
- **`previewdialog.cpp` / `previewdialog.h`:** Preview dialog for reviewing and adjusting vocal levels before rendering.
- **`resources.qrc`:** Resource file for including images like the app logo.

## FFmpeg Integration

The application uses FFmpeg to mix the recorded webcam video and vocals with the karaoke video. It applies various audio filters like normalization, echo, and compression to enhance vocal quality.

Ensure that FFmpeg is correctly installed and added to your system's `PATH` for the app to work as expected.

## Contributing

Feel free to contribute by submitting pull requests or reporting issues in the [GitHub Issues page](https://github.com/guprobr/WakkaQt/issues).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

