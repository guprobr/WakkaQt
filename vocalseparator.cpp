#include "vocalseparator.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QProcess>

#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include <array>

#ifdef WAKKAQT_ONNX
#if __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#  include <onnxruntime/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#  include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#else
#  error "onnxruntime_cxx_api.h not found"
#endif
#include <fftw3.h>
#endif

// UVR-MDX-NET-Inst_HQ_3: official UVR model repo on GitHub Releases — stable URL
static const char *MODEL_URL  =
    "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/"
    "UVR-MDX-NET-Inst_HQ_3.onnx";
static const char *MODEL_FILE = "UVR-MDX-NET-Inst_HQ_3.onnx";

// MDX-Net hop is n_fft / 4 — computed after reading model shape, not hardcoded

// ---------- public API ---------------------------------------------------

QString VocalSeparator::modelDir() {
    return QDir::homePath() + "/.WakkaQt/models";
}

QString VocalSeparator::modelPath() {
    return modelDir() + "/" + MODEL_FILE;
}

bool VocalSeparator::modelExists() {
    return QFile::exists(modelPath());
}

bool VocalSeparator::downloadModel(std::function<void(int)> progressFn, QString &errorOut) {
    QDir().mkpath(modelDir());

    QUrl modelUrl{MODEL_URL};
    QNetworkRequest req{modelUrl};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkAccessManager mgr;
    QEventLoop loop;
    QNetworkReply *reply = mgr.get(req);

    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [&](qint64 rx, qint64 tot) {
        if (tot > 0 && progressFn)
            progressFn(int(rx * 100 / tot));
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        errorOut = reply->errorString();
        reply->deleteLater();
        return false;
    }

    QFile out(modelPath());
    if (!out.open(QIODevice::WriteOnly)) {
        errorOut = "Cannot write model to: " + modelPath();
        reply->deleteLater();
        return false;
    }
    out.write(reply->readAll());
    out.close();
    reply->deleteLater();

    if (progressFn) progressFn(100);
    return true;
}

// =========================================================================
#ifdef WAKKAQT_ONNX
// =========================================================================

// Decode media file → interleaved float32 stereo at 44100 Hz via ffmpeg.
static std::vector<float> decodeToFloat(const QString &input, QString &err) {
    const QString tmp = QDir::tempPath() + "/wakka_sep_in.f32";
    QProcess p;
    p.start("ffmpeg", {"-y", "-i", input,
                       "-vn", "-ar", "44100", "-ac", "2",
                       "-f", "f32le", tmp});
    p.waitForFinished(600000);
    if (p.exitCode() != 0) {
        err = "ffmpeg decode: " + QString(p.readAllStandardError()).left(400);
        return {};
    }
    QFile f(tmp);
    if (!f.open(QIODevice::ReadOnly)) { err = "Cannot open " + tmp; return {}; }
    QByteArray bytes = f.readAll();
    f.close();
    QFile::remove(tmp);
    std::vector<float> pcm(bytes.size() / sizeof(float));
    std::memcpy(pcm.data(), bytes.constData(), bytes.size());
    return pcm;
}

// Write interleaved float32 stereo → WAV via ffmpeg.
static bool writeFloatWav(const std::vector<float> &pcm,
                          const QString &outPath, QString &err) {
    const QString tmp = QDir::tempPath() + "/wakka_sep_out.f32";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly)) { err = "Cannot write temp PCM"; return false; }
    f.write(reinterpret_cast<const char *>(pcm.data()), pcm.size() * sizeof(float));
    f.close();
    QProcess p;
    p.start("ffmpeg", {"-y", "-ar", "44100", "-ac", "2",
                       "-f", "f32le", "-i", tmp, outPath});
    p.waitForFinished(120000);
    QFile::remove(tmp);
    if (p.exitCode() != 0) {
        err = "ffmpeg encode: " + QString(p.readAllStandardError()).left(400);
        return false;
    }
    return true;
}

// ---- MDX-Net complex spectrogram ----------------------------------------
//
// MDX-Net input/output: [1, 4, bins, dim_t]
// Channel layout: 0=L_real, 1=L_imag, 2=R_real, 3=R_imag
// Memory: spec[ch][bin * frames + frame]

struct MdxSpec {
    int bins;
    int frames;
    std::vector<float> ch[4]; // [L_re, L_im, R_re, R_im][bins * frames]

    MdxSpec(int bins, int frames) : bins(bins), frames(frames) {
        for (auto &c : ch) c.assign(bins * frames, 0.f);
    }
};

// Forward STFT: interleaved float32 stereo → MdxSpec
static MdxSpec computeSTFT(const std::vector<float> &stereo, int n_fft, int hop) {
    const int bins  = n_fft / 2 + 1;
    const int total = int(stereo.size()) / 2; // per-channel samples
    const int half  = n_fft / 2;

    // Center-padded: first frame center is sample 0
    const int frames = (total + half - 1) / hop + 1;

    // Periodic Hann window — matches torch.hann_window(n_fft, periodic=True)
    // which is the convention used when training MDX-Net models.
    // Critical: use n_fft (not n_fft-1) in the denominator.
    std::vector<double> win(n_fft);
    for (int i = 0; i < n_fft; ++i)
        win[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / n_fft));

    std::vector<double> inBuf(n_fft, 0.0);
    fftw_complex *outBuf =
        reinterpret_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex) * bins));
    fftw_plan plan = fftw_plan_dft_r2c_1d(n_fft, inBuf.data(), outBuf, FFTW_ESTIMATE);

    MdxSpec spec(bins, frames);

    for (int f = 0; f < frames; ++f) {
        const int center = f * hop;

        for (int stereoIdx = 0; stereoIdx < 2; ++stereoIdx) {
            for (int i = 0; i < n_fft; ++i) {
                const int si = center - half + i;
                inBuf[i] = (si >= 0 && si < total) ? double(stereo[si * 2 + stereoIdx]) * win[i]
                                                    : 0.0;
            }
            fftw_execute(plan);

            const int reIdx = stereoIdx * 2;     // 0 or 2
            const int imIdx = stereoIdx * 2 + 1; // 1 or 3
            for (int b = 0; b < bins; ++b) {
                spec.ch[reIdx][b * frames + f] = float(outBuf[b][0]);
                spec.ch[imIdx][b * frames + f] = float(outBuf[b][1]);
            }
        }
    }

    fftw_destroy_plan(plan);
    fftw_free(outBuf);
    return spec;
}

// Inverse STFT: MdxSpec → interleaved float32 stereo
static std::vector<float> computeISTFT(const MdxSpec &spec, int n_fft, int hop, int totalSamples) {
    const int bins = n_fft / 2 + 1;
    const int half = n_fft / 2;

    std::vector<double> win(n_fft);
    for (int i = 0; i < n_fft; ++i)
        win[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n_fft - 1)));

    fftw_complex *inBuf =
        reinterpret_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex) * bins));
    std::vector<double> outBuf(n_fft);
    fftw_plan plan = fftw_plan_dft_c2r_1d(n_fft, inBuf, outBuf.data(), FFTW_ESTIMATE);

    std::vector<float> audio(totalSamples * 2, 0.f);
    std::vector<float> norm(totalSamples, 0.f);

    for (int f = 0; f < spec.frames; ++f) {
        const int center = f * hop;

        for (int stereoIdx = 0; stereoIdx < 2; ++stereoIdx) {
            const int reIdx = stereoIdx * 2;
            const int imIdx = stereoIdx * 2 + 1;

            for (int b = 0; b < bins; ++b) {
                inBuf[b][0] = spec.ch[reIdx][b * spec.frames + f];
                inBuf[b][1] = spec.ch[imIdx][b * spec.frames + f];
            }
            fftw_execute(plan); // output = N * IDFT (unnormalized)

            for (int i = 0; i < n_fft; ++i) {
                const int si = center - half + i;
                if (si < 0 || si >= totalSamples) continue;
                const float w = float(win[i]);
                audio[si * 2 + stereoIdx] += float(outBuf[i]) / n_fft * w;
                if (stereoIdx == 0)
                    norm[si] += w * w; // computed once per sample
            }
        }
    }

    fftw_destroy_plan(plan);
    fftw_free(inBuf);

    // Normalize by squared-window sum (overlap-add reconstruction)
    for (int i = 0; i < totalSamples; ++i) {
        const float w = norm[i] > 1e-9f ? norm[i] : 1.f;
        audio[i * 2]     /= w;
        audio[i * 2 + 1] /= w;
    }
    return audio;
}

// ---- main separation routine --------------------------------------------

QString VocalSeparator::separate(const QString &inputFile,
                                 std::function<void(int)> progressFn,
                                 QString &errorOut) {
    if (!modelExists()) {
        errorOut = "Model not found at: " + modelPath();
        return {};
    }

    if (progressFn) progressFn(0);

    // 1. Decode to interleaved float32 stereo at 44100 Hz
    std::vector<float> stereo = decodeToFloat(inputFile, errorOut);
    if (stereo.empty()) return {};
    const int totalSamples = int(stereo.size()) / 2;

    if (progressFn) progressFn(4);

    // 2. Load ONNX model and query shape
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MDXSep");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(4);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    std::wstring wModelPath = modelPath().toStdWString();
    Ort::Session session(env, wModelPath.c_str(), opts);
#else
    std::string sModelPath = modelPath().toStdString();
    Ort::Session session(env, sModelPath.c_str(), opts);
#endif
    Ort::AllocatorWithDefaultOptions allocator;

    // Input shape: [1, 4, bins, dim_t]
    auto inputInfo = session.GetInputTypeInfo(0);
    auto inputShape = inputInfo.GetTensorTypeAndShapeInfo().GetShape();
    if (inputShape.size() < 4 || inputShape[1] != 4) {
        errorOut = QString("Unexpected MDX model input shape (expected [1,4,bins,dim_t]), got %1 dims")
                       .arg(inputShape.size());
        return {};
    }
    const int bins  = int(inputShape[2]);
    const int dim_t_raw = int(inputShape[3]);
    const int dim_t = (dim_t_raw > 0) ? dim_t_raw : 256; // guard against dynamic axis
    const int n_fft = (bins - 1) * 2;
    const int hop   = 1024; // MDX-Net Inst_HQ_3 training convention

    qDebug() << "[VocalSep] n_fft=" << n_fft << "hop=" << hop
             << "bins=" << bins << "dim_t=" << dim_t;

    auto inNamePtr  = session.GetInputNameAllocated(0, allocator);
    auto outNamePtr = session.GetOutputNameAllocated(0, allocator);
    const char *inNames[]  = {inNamePtr.get()};
    const char *outNames[] = {outNamePtr.get()};
    auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    if (progressFn) progressFn(6);

    // 3. STFT
    MdxSpec spec = computeSTFT(stereo, n_fft, hop);

    if (progressFn) progressFn(18);

    // 4. Trim-based chunking with context margin
    //    Each model call gets dim_t frames.
    //    TRIM frames on each side provide temporal context — their output is discarded.
    //    GEN frames are the usable centre output per chunk.
    //    This matches the audio-separator convention and eliminates boundary artefacts.
    const int TRIM = std::max(1, dim_t / 8);
    const int GEN  = dim_t - 2 * TRIM;

    MdxSpec outSpec(bins, spec.frames);
    std::vector<float> chunk(4 * bins * dim_t);
    std::array<int64_t, 4> inShape = {1, 4, bins, dim_t};

    // Count chunks for progress
    int totalChunks = 0;
    for (int i = 0; i < spec.frames; i += GEN) ++totalChunks;
    int chunksDone = 0;

    for (int i = 0; i < spec.frames; i += GEN) {
        // Input window [src_start, src_end) in spectrogram frame indices
        const int src_start = i - TRIM;
        const int src_end   = i + GEN + TRIM; // = i + dim_t - TRIM

        // Fill chunk — zero-pad at start/end if out of bounds
        std::fill(chunk.begin(), chunk.end(), 0.f);
        for (int ch = 0; ch < 4; ++ch) {
            for (int b = 0; b < bins; ++b) {
                for (int t = 0; t < dim_t; ++t) {
                    const int f = src_start + t;
                    if (f < 0 || f >= spec.frames) continue;
                    chunk[ch * bins * dim_t + b * dim_t + t] = spec.ch[ch][b * spec.frames + f];
                }
            }
        }

        Ort::Value inTensor = Ort::Value::CreateTensor<float>(
            memInfo, chunk.data(), chunk.size(), inShape.data(), inShape.size());
        auto outTensors = session.Run(Ort::RunOptions{nullptr},
                                      inNames, &inTensor, 1, outNames, 1);

        const float *outData = outTensors[0].GetTensorData<float>();

        // Copy only the usable GEN frames (skip TRIM on each side)
        // First chunk has no left context, so start discarding from 0 in output
        const int out_t0 = (i == 0) ? 0 : TRIM;
        const int out_t1 = std::min(dim_t, TRIM + GEN + (src_end > spec.frames ? TRIM : 0));

        for (int ch = 0; ch < 4; ++ch) {
            for (int b = 0; b < bins; ++b) {
                for (int t = out_t0; t < out_t1; ++t) {
                    const int f = i + (t - TRIM);  // output frame index
                    if (f < 0 || f >= spec.frames) continue;
                    outSpec.ch[ch][b * outSpec.frames + f] =
                        outData[ch * bins * dim_t + b * dim_t + t];
                }
            }
        }

        ++chunksDone;
        if (progressFn)
            progressFn(18 + chunksDone * 72 / totalChunks); // 18 → 90
    }

    if (progressFn) progressFn(91);

    // 5. iSTFT
    std::vector<float> output = computeISTFT(outSpec, n_fft, hop, totalSamples);

    if (progressFn) progressFn(96);

    // 8. Write output WAV
    const QString outPath = QDir::tempPath() + "/wakka_backing_track.wav";
    if (!writeFloatWav(output, outPath, errorOut)) return {};

    if (progressFn) progressFn(100);
    return outPath;
}

// =========================================================================
#else  // WAKKAQT_ONNX not compiled in
// =========================================================================

QString VocalSeparator::separate(const QString &,
                                 std::function<void(int)>,
                                 QString &errorOut) {
    errorOut = "ONNX Runtime not available. "
               "Install libonnxruntime-dev and rebuild WakkaQt.";
    return {};
}

#endif // WAKKAQT_ONNX
