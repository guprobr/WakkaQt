
![wakkalogo](https://github.com/user-attachments/assets/0dc389ce-0678-4eaf-a888-04ba306ff2b4)

- Help requested: for MacOS tests. 

**Windows x86_64 zip bundle available at the end of this readme**, please follow instructions (mostly unpack, then run WakkaQt.exe)
On Windows 11 you *must* [disable Audio Enhancements on your microphone](https://support.microsoft.com/en-us/topic/disable-audio-enhancements-0ec686c4-8d79-4588-b7e7-9287dd296f72#:~:text=To%20disable%20audio%20enhancements%3A,Device%20Properties%20%3EAdditional%20device%20properties), because it messes the Qt6 recording process.

# WakkaQt - Karaoke App

WakkaQt is a karaoke application built with C++ and Qt6, designed to record vocals over a video/audio track and mix them into a rendered file. This app features webcam recording, YouTube video downloading, real-time sound visualization, and post-recording video rendering with FFmpeg. It automatically does some masterization on the vocal tracks. It also has a custom automatic auto-tuner class called VocalEnhancer that provides slight pitch shift/correction and formant preservation.

## Features

- **Record karaoke sessions** with synchronized video and audio playback.
- **Mix webcam video and vocals** with the karaoke video and export the result.
- **Real-time sound visualization** (green waveform microphone meter).
- **Download YouTube videos** and use them as karaoke tracks.
- **Video and Audio device selection** for recording.
- **Rendering** with FFmpeg for high-quality output, automatic masterization and vocal enhancement;
- **Vocal enhancement** with some effects and a custom auto tuning class programmed with the intention to improve the vocals a little bit without distorting or corrupting vocal formants. All of this is automatic, but you can adjust volumes before each rendering operation.
- **Intended Cross-platform compatibility** (Windows, macOS, Linux).

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
    - yt-dlp: Install from [yt-dlp GitHub page](https://github.com/yt-dlp/yt-dlp). *for Ubuntu 24.04 and below you must get a latest version from Github*
    - libfftw3: for our own custom VocalEnhancer class

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
3. **Sing & Record:** Click the "♪ SING ♪" button to start recording. The webcam will be used to record a video, while the audio input will record your voice.
4. **Stop Recording:** Once finished, click the "Finish!" button to stop the recording.
5. **Adjust vocals volume** Once finished recording, a dialog appears with a knob for you to amplify or reduce volume of the vocals. It is a very low quality amplification, just to adjust volumes. After rendering it will sound much better.
6. **Render the Video:** You can render and preview the mix of vocals and the karaoke track before the final video or audio file.
7. **Download YouTube Video:** You can enter a YouTube URL to download and use as a karaoke track. Other streaming services URL might work as well.
8. **Render again:** This button appears after rendering, so you can save a new filename and adjust options again, then render, again, with different options :D

## Project Structure

- **`mainwindow.cpp` / `mainwindow.h`:** Core application logic, including UI setup and media control, audio and video recording orchestration, playback downloads from streaming services and rendering.
- **`sndwidget.cpp` / `sndwidget.h`:** Custom widget for displaying sound levels from the current audio input source, the green audio visualizer at the top of the UI.
- **`previewdialog.cpp` / `previewdialog.h`:** Preview dialog for reviewing and adjusting vocal levels before rendering. Here the masterization and vocal enhancement takes place, allowing the user to amplify or reduce volume while listening to the backing track at the same time. Note that the final rendering with FFmpeg will sound much better, this is a low quality preview.
- **`audioamplifier.cpp` / `audioamplifier.h`:** Class to manipulate samples for volume adjustment, and to act as a media player to mix backing track with the recorded vocals.
- **`audiorecorder.cpp` / `audiorecorder.h`:** Class to record audio. It enables the configuration of different sample formats, channels and rates while recording sound, since QAudioInput with MediaCaptureSession refuses to record in different formats.
- **`audiovizmediaplayer.cpp` / `audiovizmediaplayer.h`:** Class to mimic QMediaPlayer but extracting audio from playbacks and serve the AudioVisualizer widget with visualization data.
- **`audiovisualizerwidget.cpp` / `audiovisualizerwidget.h`:** Class that implements the Yelloopy© audio visualizer widget.
- **`vocalenhancer.cpp` / `vocalenhancer.h`:** Class that implements a custom pitch shifter VocalEnhancer automatically applied right after each recording. The VocalEnhancer class provides a multi-step process for enhancing vocals in audio data by combining pitch detection, harmonic scaling, gain normalization, and windowed overlap-add techniques. Its three-pass scaling approach for pitch correction achieves enhancement without distorting vocal characteristics.
- **`resources.qrc`:** Resource file for including images like the app logo.

## FFmpeg Integration

The application uses FFmpeg to mix the recorded webcam video and vocals with the karaoke video. It applies various audio filters like normalization, echo, and compression to enhance vocal quality. We benefit from working with several different media formats for input/output that way.

## About Windows bundle ZIP

  - **A proper FFMPEG binary** is already on the root of the application directory.
  - **yt-dlp** is already there too, for your convenience.
  - NOTE: **antivirus software degrade this software a lot**, and **VPNs might make streaming services to block** the fetching of the video file when running *yt-dlp*.
    
  - You can download the windows x64 ZIP [Here on my website](https://gu.pro.br/WakkaQt-mswinX64.zip)

## Contributing

Feel free to contribute by submitting pull requests or reporting issues in the [GitHub Issues page](https://github.com/guprobr/WakkaQt/issues).

## License

This project is licensed under the MIT License

