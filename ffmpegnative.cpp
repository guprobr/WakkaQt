#ifdef WAKKAQT_FFMPEG_NATIVE

#include "ffmpegnative.h"
#include "complexes.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <clocale>
#include <QDebug>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cmath>

namespace FFmpegNative {

// ─────────────────────────────────────────────────────────────────────────────
// getDuration
// ─────────────────────────────────────────────────────────────────────────────

double getDuration(const QString &filePath)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, filePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return 0.0;
    avformat_find_stream_info(fmt, nullptr);
    double dur = 0.0;
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        dur = double(fmt->duration) / double(AV_TIME_BASE);
    avformat_close_input(&fmt);
    return dur;
}

// ─────────────────────────────────────────────────────────────────────────────
// hasVideoStream
// ─────────────────────────────────────────────────────────────────────────────

bool hasVideoStream(const QString &filePath)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, filePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    avformat_find_stream_info(fmt, nullptr);
    bool found = false;
    for (unsigned i = 0; i < fmt->nb_streams && !found; ++i)
        found = (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO);
    avformat_close_input(&fmt);
    return found;
}

// ─────────────────────────────────────────────────────────────────────────────
// extractAudio — decode + resample to 44100 Hz / stereo or mono / Int16 WAV
// ─────────────────────────────────────────────────────────────────────────────

bool extractAudio(const QString &input, const QString &output,
                  qint64 offsetMs, const QString &filterStr)
{
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, input.toUtf8().constData(), nullptr, nullptr) < 0) {
        qWarning() << "FFmpegNative::extractAudio: cannot open" << input;
        return false;
    }
    avformat_find_stream_info(fmtCtx, nullptr);

    const int audioIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) {
        qWarning() << "FFmpegNative::extractAudio: no audio stream in" << input;
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVStream *audioStream = fmtCtx->streams[audioIdx];
    const AVCodec *dec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmtCtx); return false; }

    AVCodecContext *decCtx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(decCtx, audioStream->codecpar);
    if (avcodec_open2(decCtx, dec, nullptr) < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    const bool wantMono = filterStr.contains("mono", Qt::CaseInsensitive);
    const int   outRate  = 44100;
    const int   outCh    = wantMono ? 1 : 2;
    const uint64_t outMask = wantMono ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;

    SwrContext *swr = nullptr;
    AVChannelLayout inCL, outCL;
    av_channel_layout_copy(&inCL, &decCtx->ch_layout);
    if (inCL.nb_channels == 0)
        av_channel_layout_default(&inCL, std::max(1, (int)audioStream->codecpar->ch_layout.nb_channels));
    av_channel_layout_from_mask(&outCL, outMask);

    if (swr_alloc_set_opts2(&swr, &outCL, AV_SAMPLE_FMT_S16, outRate,
                             &inCL, decCtx->sample_fmt, decCtx->sample_rate,
                             0, nullptr) < 0 || swr_init(swr) < 0) {
        qWarning() << "FFmpegNative::extractAudio: swr_init failed";
        av_channel_layout_uninit(&inCL);
        av_channel_layout_uninit(&outCL);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }
    av_channel_layout_uninit(&inCL);
    av_channel_layout_uninit(&outCL);

    QByteArray pcmData;
    pcmData.reserve(outRate * outCh * sizeof(int16_t) * 60);

    const qint64 skipSamples = offsetMs > 0 ? offsetMs * outRate / 1000 : 0;
    qint64 totalSamples = 0;

    AVPacket *pkt   = av_packet_alloc();
    AVFrame  *frame = av_frame_alloc();

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }
        if (avcodec_send_packet(decCtx, pkt) < 0)  { av_packet_unref(pkt); continue; }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(decCtx, frame) == 0) {
            const int outN = (int)av_rescale_rnd(
                swr_get_delay(swr, decCtx->sample_rate) + frame->nb_samples,
                outRate, decCtx->sample_rate, AV_ROUND_UP);
            std::vector<uint8_t> tmp(outN * outCh * sizeof(int16_t));
            uint8_t *ptr = tmp.data();
            const int got = swr_convert(swr, &ptr, outN,
                                        (const uint8_t**)frame->data, frame->nb_samples);
            if (got > 0) {
                const int bytes = got * outCh * (int)sizeof(int16_t);
                if (totalSamples + got > skipSamples) {
                    const qint64 skipLocal = std::max<qint64>(0, skipSamples - totalSamples);
                    const int skipBytes = (int)skipLocal * outCh * (int)sizeof(int16_t);
                    pcmData.append(reinterpret_cast<const char*>(ptr + skipBytes),
                                   bytes - skipBytes);
                }
                totalSamples += got;
            }
            av_frame_unref(frame);
        }
    }
    // Flush resampler
    {
        const int outN = (int)swr_get_delay(swr, outRate) + 1024;
        std::vector<uint8_t> tmp(outN * outCh * sizeof(int16_t));
        uint8_t *ptr = tmp.data();
        const int got = swr_convert(swr, &ptr, outN, nullptr, 0);
        if (got > 0)
            pcmData.append(reinterpret_cast<const char*>(ptr),
                           got * outCh * (int)sizeof(int16_t));
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&decCtx);
    avformat_close_input(&fmtCtx);

    if (pcmData.isEmpty()) {
        qWarning() << "FFmpegNative::extractAudio: empty PCM for" << input;
        return false;
    }

    QAudioFormat afmt;
    afmt.setSampleRate(outRate);
    afmt.setChannelCount(outCh);
    afmt.setSampleFormat(QAudioFormat::Int16);

    QFile outFile(output);
    if (!outFile.open(QIODevice::WriteOnly)) {
        qWarning() << "FFmpegNative::extractAudio: cannot write" << output;
        return false;
    }
    writeWavHeader(outFile, afmt, pcmData.size(), pcmData);
    outFile.close();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// renderVideo helpers
// ─────────────────────────────────────────────────────────────────────────────

// Decode entire audio track to float PCM (44100 Hz, stereo).
// Applies volume, and if offsetMs > 0, skips that many ms from the start.
// If offsetMs < 0, the caller should prepend silence after the fact.
static QVector<float> decodeAudioToFloat(const QString &path, qint64 offsetMs, double volume)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return {};
    avformat_find_stream_info(fmt, nullptr);

    const int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) { avformat_close_input(&fmt); return {}; }

    const AVCodec *dec = avcodec_find_decoder(fmt->streams[audioIdx]->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return {}; }
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, fmt->streams[audioIdx]->codecpar);
    avcodec_open2(ctx, dec, nullptr);

    SwrContext *swr = nullptr;
    AVChannelLayout srcCL, dstCL;
    av_channel_layout_copy(&srcCL, &ctx->ch_layout);
    if (srcCL.nb_channels == 0) av_channel_layout_default(&srcCL, 1);
    av_channel_layout_from_mask(&dstCL, AV_CH_LAYOUT_STEREO);
    swr_alloc_set_opts2(&swr, &dstCL, AV_SAMPLE_FMT_FLT, 44100,
                         &srcCL, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    swr_init(swr);
    av_channel_layout_uninit(&srcCL);
    av_channel_layout_uninit(&dstCL);

    const qint64 skipSamples = (offsetMs > 0) ? offsetMs * 44100 / 1000 : 0;
    qint64 totalSamples = 0;
    QVector<float> pcm;
    pcm.reserve(44100 * 2 * 60);

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frm = av_frame_alloc();

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }
        if (avcodec_send_packet(ctx, pkt) < 0) { av_packet_unref(pkt); continue; }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(ctx, frm) == 0) {
            const int outN = (int)av_rescale_rnd(
                swr_get_delay(swr, ctx->sample_rate) + frm->nb_samples,
                44100, ctx->sample_rate, AV_ROUND_UP);
            QVector<float> tmp(outN * 2);
            uint8_t *ptr = reinterpret_cast<uint8_t*>(tmp.data());
            const int got = swr_convert(swr, &ptr, outN,
                                        (const uint8_t**)frm->data, frm->nb_samples);
            if (got > 0) {
                const int startIdx = (totalSamples < skipSamples)
                    ? (int)std::min<qint64>(skipSamples - totalSamples, got) * 2 : 0;
                for (int i = startIdx; i < got * 2; ++i)
                    pcm.append(tmp[i] * float(volume));
                totalSamples += got;
            }
            av_frame_unref(frm);
        }
    }
    // Flush
    {
        const int outN = (int)swr_get_delay(swr, 44100) + 1024;
        QVector<float> tmp(outN * 2);
        uint8_t *ptr = reinterpret_cast<uint8_t*>(tmp.data());
        const int got = swr_convert(swr, &ptr, outN, nullptr, 0);
        if (got > 0)
            for (int i = 0; i < got * 2; ++i)
                pcm.append(tmp[i] * float(volume));
    }

    av_frame_free(&frm);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return pcm;
}

// Apply an avfilter chain (e.g. "deesser,speechnorm,...") to float stereo 44100 PCM.
// Returns S16 stereo 44100 Hz output. Falls back to plain float→S16 conversion on error.
static QVector<int16_t> applyAudioFilter(const QVector<float> &input,
                                          const QString &filterChain)
{
    QVector<int16_t> out;
    out.reserve(input.size());

    // ── Build graph: abuffersrc → <filterChain>,aformat=s16 → abuffersink ──
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFilterContext *srcCtx = nullptr, *sinkCtx = nullptr;

    const QByteArray srcParams =
        "sample_rate=44100:sample_fmt=flt:channel_layout=stereo:time_base=1/44100";

    bool ok = (avfilter_graph_create_filter(&srcCtx,
                   avfilter_get_by_name("abuffer"), "in",
                   srcParams.constData(), nullptr, graph) >= 0)
           && (avfilter_graph_create_filter(&sinkCtx,
                   avfilter_get_by_name("abuffersink"), "out",
                   nullptr, nullptr, graph) >= 0);

    if (ok) {
        const QString fullChain = filterChain.isEmpty()
            ? QStringLiteral("aformat=sample_fmts=s16:channel_layouts=stereo")
            : filterChain + QStringLiteral(",aformat=sample_fmts=s16:channel_layouts=stereo");

        // Force "C" locale so avfilter parses decimal points correctly regardless of
        // the system locale (e.g. "0.5" would fail on German/French locales otherwise).
        const char *prevLocale = setlocale(LC_NUMERIC, "C");

        AVFilterInOut *ins = nullptr, *outs = nullptr;
        ok = (avfilter_graph_parse2(graph, fullChain.toUtf8().constData(), &ins, &outs) >= 0);
        if (ok && ins)
            ok = (avfilter_link(srcCtx, 0, ins->filter_ctx, ins->pad_idx) >= 0);
        if (ok && outs)
            ok = (avfilter_link(outs->filter_ctx, outs->pad_idx, sinkCtx, 0) >= 0);
        avfilter_inout_free(&ins);
        avfilter_inout_free(&outs);
        ok = ok && (avfilter_graph_config(graph, nullptr) >= 0);

        // Restore previous locale
        if (prevLocale) setlocale(LC_NUMERIC, prevLocale);
    }

    if (ok) {
        const int chunkSamples = 4096;
        AVFrame *inF  = av_frame_alloc();
        AVFrame *outF = av_frame_alloc();
        int64_t pts = 0;
        const int total = input.size() / 2;
        int offset = 0;

        while (offset <= total) {
            if (offset < total) {
                const int n = std::min(chunkSamples, total - offset);
                inF->sample_rate = 44100;
                inF->format      = AV_SAMPLE_FMT_FLT;
                inF->nb_samples  = n;
                inF->pts         = pts;
                av_channel_layout_from_mask(&inF->ch_layout, AV_CH_LAYOUT_STEREO);
                av_frame_get_buffer(inF, 0);
                memcpy(inF->data[0], input.constData() + offset * 2,
                       n * 2 * sizeof(float));
                if (av_buffersrc_add_frame(srcCtx, inF) < 0) {
                    av_frame_unref(inF);
                    break;
                }
                av_frame_unref(inF);
                offset += n;
                pts    += n;
            } else {
                [[maybe_unused]] int flushRet = av_buffersrc_add_frame(srcCtx, nullptr);
                ++offset;
            }

            while (av_buffersink_get_frame(sinkCtx, outF) >= 0) {
                const int16_t *data = reinterpret_cast<const int16_t*>(outF->data[0]);
                const int n = outF->nb_samples * 2; // stereo
                for (int i = 0; i < n; ++i)
                    out.append(data[i]);
                av_frame_unref(outF);
            }
        }

        av_frame_free(&inF);
        av_frame_free(&outF);
    } else {
        qWarning() << "FFmpegNative: audio filter graph failed, applying plain conversion";
        out.resize(input.size());
        for (int i = 0; i < input.size(); ++i)
            out[i] = int16_t(std::clamp(input[i] * 32767.f, -32768.f, 32767.f));
    }

    avfilter_graph_free(&graph);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// renderVideo
// ─────────────────────────────────────────────────────────────────────────────

bool renderVideo(const QString &audioPath,
                 const QString &webcamPath,
                 const QString &playbackPath,
                 const QString &outputPath,
                 double vocalVolume,
                 qint64 audioOffsetMs,
                 qint64 videoOffsetMs,
                 const QString &resolution,
                 const QString &audioMasterization,
                 std::function<void(double)> progressCb)
{
    const QString ext = QFileInfo(outputPath).suffix().toLower();
    const bool audioOnlyOut = (ext == "mp3" || ext == "wav" ||
                               ext == "flac" || ext == "opus");

    // ── Parse resolution ──────────────────────────────────────────────────────
    const QStringList rp = resolution.split('x');
    const int mainW = rp.value(0, "1280").toInt();
    const int mainH = rp.value(1, "720").toInt();

    // ── Step 1: Load vocal audio with volume and offset applied ───────────────
    QVector<float> vocalPCM = decodeAudioToFloat(audioPath,
                                                  std::max<qint64>(0, audioOffsetMs),
                                                  vocalVolume);
    if (vocalPCM.isEmpty()) {
        qWarning() << "FFmpegNative::renderVideo: failed to decode vocal audio";
        return false;
    }
    // If offset is negative, prepend silence
    if (audioOffsetMs < 0) {
        const int silenceLen = (int)((-audioOffsetMs) * 44100LL / 1000) * 2;
        QVector<float> silence(silenceLen, 0.0f);
        silence += vocalPCM;
        vocalPCM = std::move(silence);
    }

    // ── Step 2: Load playback audio ───────────────────────────────────────────
    QVector<float> playbackPCM = decodeAudioToFloat(playbackPath, 0, 1.0);

    // ── Step 3: Mix ───────────────────────────────────────────────────────────
    const int mixLen = std::max(vocalPCM.size(), playbackPCM.size());
    QVector<float> mixedPCM(mixLen, 0.0f);
    for (int i = 0; i < mixLen; ++i) {
        const float v = (i < vocalPCM.size())    ? vocalPCM[i]    : 0.0f;
        const float p = (i < playbackPCM.size()) ? playbackPCM[i] : 0.0f;
        mixedPCM[i] = std::clamp(v + p, -1.0f, 1.0f);
    }
    vocalPCM.clear(); vocalPCM.squeeze();
    playbackPCM.clear(); playbackPCM.squeeze();

    // ── Step 4: Apply audio masterization ─────────────────────────────────────
    QVector<int16_t> finalAudio = applyAudioFilter(mixedPCM, audioMasterization);
    mixedPCM.clear(); mixedPCM.squeeze();

    if (finalAudio.isEmpty()) {
        qWarning() << "FFmpegNative::renderVideo: audio pipeline produced no output";
        return false;
    }

    // Total duration from audio for progress reporting
    const double totalDurSec = double(finalAudio.size() / 2) / 44100.0;

    // ── Step 5: Set up output muxer ───────────────────────────────────────────
    AVFormatContext *outFmt = nullptr;
    avformat_alloc_output_context2(&outFmt, nullptr, nullptr,
                                   outputPath.toUtf8().constData());
    if (!outFmt) {
        qWarning() << "FFmpegNative::renderVideo: cannot create output context";
        return false;
    }

    // Choose audio encoder
    const AVCodecID audioCodecId = [&]() -> AVCodecID {
        if (ext == "mp3")  return AV_CODEC_ID_MP3;
        if (ext == "flac") return AV_CODEC_ID_FLAC;
        if (ext == "opus") return AV_CODEC_ID_OPUS;
        if (ext == "wav")  return AV_CODEC_ID_PCM_S16LE;
        return AV_CODEC_ID_AAC;
    }();

    const AVCodec *audioEnc = avcodec_find_encoder(audioCodecId);
    if (!audioEnc) {
        qWarning() << "FFmpegNative::renderVideo: audio encoder not found";
        avformat_free_context(outFmt);
        return false;
    }

    AVStream *audioOutSt = avformat_new_stream(outFmt, audioEnc);
    AVCodecContext *audioEncCtx = avcodec_alloc_context3(audioEnc);
    audioEncCtx->sample_rate = 44100;
    av_channel_layout_from_mask(&audioEncCtx->ch_layout, AV_CH_LAYOUT_STEREO);
    {
        // avcodec_get_supported_config() replaces the deprecated sample_fmts field
        const AVSampleFormat *supportedFmts = nullptr;
        int numFmts = 0;
        avcodec_get_supported_config(nullptr, audioEnc,
                                      AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                      reinterpret_cast<const void**>(&supportedFmts),
                                      &numFmts);
        audioEncCtx->sample_fmt = (supportedFmts && numFmts > 0)
                                  ? supportedFmts[0] : AV_SAMPLE_FMT_S16;
    }
    audioEncCtx->bit_rate    = (audioCodecId == AV_CODEC_ID_PCM_S16LE ||
                                audioCodecId == AV_CODEC_ID_FLAC) ? 0 : 192000;
    audioEncCtx->time_base   = {1, 44100};
    if (outFmt->oformat->flags & AVFMT_GLOBALHEADER)
        audioEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(audioEncCtx, audioEnc, nullptr) < 0) {
        qWarning() << "FFmpegNative::renderVideo: cannot open audio encoder";
        avcodec_free_context(&audioEncCtx);
        avformat_free_context(outFmt);
        return false;
    }
    avcodec_parameters_from_context(audioOutSt->codecpar, audioEncCtx);
    audioOutSt->time_base = audioEncCtx->time_base;

    // Resampler: S16 stereo → encoder's required sample format
    SwrContext *encSwr = nullptr;
    if (audioEncCtx->sample_fmt != AV_SAMPLE_FMT_S16) {
        AVChannelLayout stereo;
        av_channel_layout_from_mask(&stereo, AV_CH_LAYOUT_STEREO);
        swr_alloc_set_opts2(&encSwr,
                             &stereo, audioEncCtx->sample_fmt, 44100,
                             &stereo, AV_SAMPLE_FMT_S16, 44100,
                             0, nullptr);
        swr_init(encSwr);
        av_channel_layout_uninit(&stereo);
    }

    // FIFO for frame-size alignment
    AVAudioFifo *fifo = av_audio_fifo_alloc(audioEncCtx->sample_fmt, 2,
                                             std::max(1, audioEncCtx->frame_size));

    // ── Video encoder (if needed) ─────────────────────────────────────────────
    AVStream *videoOutSt        = nullptr;
    AVCodecContext *videoEncCtx = nullptr;
    SwsContext *swsCtx          = nullptr;
    AVFrame *videoEncFrame      = nullptr;

    if (!audioOnlyOut) {
        const AVCodecID vidCodecId = (ext == "webm") ? AV_CODEC_ID_VP9 : AV_CODEC_ID_H264;
        const AVCodec *videoEnc = avcodec_find_encoder(vidCodecId);
        if (!videoEnc) {
            qWarning() << "FFmpegNative::renderVideo: h264 encoder not found";
            // fall through — produce audio-only output
        } else {
            videoOutSt = avformat_new_stream(outFmt, videoEnc);
            videoEncCtx = avcodec_alloc_context3(videoEnc);
            videoEncCtx->width      = mainW;
            videoEncCtx->height     = mainH;
            videoEncCtx->pix_fmt    = AV_PIX_FMT_YUV420P;
            videoEncCtx->time_base  = {1, 90000}; // fine timebase; actual fps from input PTS
            videoEncCtx->framerate  = {30, 1};     // declared fps for encoder metadata/level calc
            videoEncCtx->gop_size   = 12;
            videoEncCtx->bit_rate   = 3000000;
            if (vidCodecId == AV_CODEC_ID_H264)
                av_opt_set(videoEncCtx->priv_data, "preset", "fast", 0);
            if (outFmt->oformat->flags & AVFMT_GLOBALHEADER)
                videoEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (avcodec_open2(videoEncCtx, videoEnc, nullptr) < 0) {
                qWarning() << "FFmpegNative::renderVideo: cannot open video encoder";
                avcodec_free_context(&videoEncCtx);
                videoOutSt = nullptr;
            } else {
                avcodec_parameters_from_context(videoOutSt->codecpar, videoEncCtx);
                videoOutSt->time_base = videoEncCtx->time_base;

                videoEncFrame = av_frame_alloc();
                videoEncFrame->format = AV_PIX_FMT_YUV420P;
                videoEncFrame->width  = mainW;
                videoEncFrame->height = mainH;
                av_frame_get_buffer(videoEncFrame, 0);
            }
        }
    }

    // ── Open output file ──────────────────────────────────────────────────────
    if (!(outFmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outFmt->pb, outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            qWarning() << "FFmpegNative::renderVideo: cannot open output" << outputPath;
            // cleanup
            av_audio_fifo_free(fifo);
            if (encSwr) swr_free(&encSwr);
            avcodec_free_context(&audioEncCtx);
            if (videoEncCtx) avcodec_free_context(&videoEncCtx);
            if (videoEncFrame) av_frame_free(&videoEncFrame);
            avformat_free_context(outFmt);
            return false;
        }
    }
    if (avformat_write_header(outFmt, nullptr) < 0) {
        qWarning() << "FFmpegNative::renderVideo: avformat_write_header failed";
        if (!(outFmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&outFmt->pb);
        av_audio_fifo_free(fifo);
        if (encSwr) swr_free(&encSwr);
        avcodec_free_context(&audioEncCtx);
        if (videoEncCtx) avcodec_free_context(&videoEncCtx);
        if (videoEncFrame) av_frame_free(&videoEncFrame);
        avformat_free_context(outFmt);
        return false;
    }

    // ── Helper: send one AVFrame to encoder and write resulting packets ────────
    auto flushEncoder = [&](AVCodecContext *encCtx, AVStream *st, AVFrame *frame) {
        if (avcodec_send_frame(encCtx, frame) < 0) return;
        AVPacket *pkt = av_packet_alloc();
        while (avcodec_receive_packet(encCtx, pkt) >= 0) {
            av_packet_rescale_ts(pkt, encCtx->time_base, st->time_base);
            pkt->stream_index = st->index;
            av_interleaved_write_frame(outFmt, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    };

    // ── Step 6: Encode audio ──────────────────────────────────────────────────
    {
        const int frameSize = (audioEncCtx->frame_size > 0) ? audioEncCtx->frame_size : 1024;
        const int totalSamplesIn = finalAudio.size() / 2; // per-channel sample count
        int samplesWritten = 0;
        int64_t audioPts = 0;

        // Helper: push N samples of S16 into the fifo, converting format if needed
        auto pushToFifo = [&](const int16_t *src, int n) {
            if (encSwr) {
                // Convert S16 → encoder format
                const int maxOut = n + 64;
                int planeSamples = av_get_bytes_per_sample(audioEncCtx->sample_fmt) * n;
                std::vector<std::vector<uint8_t>> planeBufs;
                std::vector<uint8_t*> ptrs;
                const int planes = av_sample_fmt_is_planar(audioEncCtx->sample_fmt) ? 2 : 1;
                for (int p = 0; p < planes; ++p) {
                    planeBufs.emplace_back(maxOut * av_get_bytes_per_sample(audioEncCtx->sample_fmt) + 16);
                    ptrs.push_back(planeBufs.back().data());
                }
                const uint8_t *srcPtr = reinterpret_cast<const uint8_t*>(src);
                const int got = swr_convert(encSwr, ptrs.data(), maxOut, &srcPtr, n);
                if (got > 0)
                    av_audio_fifo_write(fifo, reinterpret_cast<void**>(ptrs.data()), got);
            } else {
                // S16 packed — write directly
                void *ptr = const_cast<int16_t*>(src);
                av_audio_fifo_write(fifo, &ptr, n);
            }
        };

        while (samplesWritten < totalSamplesIn || av_audio_fifo_size(fifo) > 0) {
            // Feed audio data into fifo
            if (samplesWritten < totalSamplesIn) {
                const int batch = std::min(frameSize * 4, totalSamplesIn - samplesWritten);
                pushToFifo(finalAudio.constData() + samplesWritten * 2, batch);
                samplesWritten += batch;
            }

            // Drain fifo in frameSize chunks
            while (av_audio_fifo_size(fifo) >= frameSize ||
                   (samplesWritten >= totalSamplesIn && av_audio_fifo_size(fifo) > 0))
            {
                const int read = std::min(frameSize, av_audio_fifo_size(fifo));
                if (read <= 0) break;

                AVFrame *af = av_frame_alloc();
                af->format      = audioEncCtx->sample_fmt;
                af->nb_samples  = read;
                af->sample_rate = 44100;
                af->pts         = audioPts;
                av_channel_layout_copy(&af->ch_layout, &audioEncCtx->ch_layout);
                av_frame_get_buffer(af, 0);
                av_audio_fifo_read(fifo, reinterpret_cast<void**>(af->data), read);
                flushEncoder(audioEncCtx, audioOutSt, af);
                av_frame_free(&af);
                audioPts += read;

                // Audio phase: 0–10% if video follows, 0–100% for audio-only output
                if (progressCb && totalDurSec > 0) {
                    const double frac = std::min(1.0, double(audioPts) / 44100.0 / totalDurSec);
                    progressCb(audioOnlyOut ? frac : 0.1 * frac);
                }
            }
        }
        // Flush audio encoder
        flushEncoder(audioEncCtx, audioOutSt, nullptr);
    }
    finalAudio.clear(); finalAudio.squeeze();

    // ── Step 7: Encode video ──────────────────────────────────────────────────
    if (videoEncCtx && videoOutSt && videoEncFrame) {
        AVFormatContext *webcamFmt = nullptr;
        AVCodecContext  *webcamDec = nullptr;
        int webcamVidIdx = -1;

        if (avformat_open_input(&webcamFmt, webcamPath.toUtf8().constData(),
                                nullptr, nullptr) >= 0) {
            avformat_find_stream_info(webcamFmt, nullptr);
            webcamVidIdx = av_find_best_stream(webcamFmt, AVMEDIA_TYPE_VIDEO,
                                               -1, -1, nullptr, 0);
            if (webcamVidIdx >= 0) {
                // Seek to videoOffset
                if (videoOffsetMs > 0) {
                    const int64_t ts = videoOffsetMs * AV_TIME_BASE / 1000;
                    avformat_seek_file(webcamFmt, -1, INT64_MIN, ts, ts + 2*AV_TIME_BASE, 0);
                }
                const AVCodec *vdec = avcodec_find_decoder(
                    webcamFmt->streams[webcamVidIdx]->codecpar->codec_id);
                if (vdec) {
                    webcamDec = avcodec_alloc_context3(vdec);
                    avcodec_parameters_to_context(webcamDec,
                        webcamFmt->streams[webcamVidIdx]->codecpar);
                    avcodec_open2(webcamDec, vdec, nullptr);
                }
            }
        }

        if (webcamDec) {
            // Create swscale: webcam native format → YUV420p mainW×mainH
            swsCtx = sws_getContext(webcamDec->width, webcamDec->height,
                                     webcamDec->pix_fmt,
                                     mainW, mainH,
                                     AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);

            // Input timebase — used to convert frame PTS to seconds for progress
            const AVRational inputTB = webcamFmt->streams[webcamVidIdx]->time_base;

            AVPacket *pkt        = av_packet_alloc();
            AVFrame  *frm        = av_frame_alloc();
            int64_t  firstSrcPts = AV_NOPTS_VALUE; // normalize video PTS to start at 0
            int64_t  fallbackPts = 0;

            // When videoOffsetMs < 0 the webcam was not seeked; instead delay the
            // video stream so it starts |videoOffsetMs| ms into the output — matching
            // how the audio gets an equivalent silence prepend.
            const int64_t videoDelayTb = (videoOffsetMs < 0)
                ? av_rescale_q(-videoOffsetMs, AVRational{1, 1000}, videoEncCtx->time_base)
                : 0;

            while (av_read_frame(webcamFmt, pkt) >= 0) {
                if (pkt->stream_index != webcamVidIdx) { av_packet_unref(pkt); continue; }
                if (avcodec_send_packet(webcamDec, pkt) < 0) { av_packet_unref(pkt); continue; }
                av_packet_unref(pkt);

                while (avcodec_receive_frame(webcamDec, frm) == 0) {
                    const int64_t srcPts = frm->pts;

                    // Record first valid PTS so we can zero-base all subsequent frames.
                    // After avformat_seek_file the first frame's PTS is non-zero; writing
                    // it directly while audio starts at 0 causes A/V desync.
                    if (firstSrcPts == AV_NOPTS_VALUE && srcPts != AV_NOPTS_VALUE)
                        firstSrcPts = srcPts;
                    const int64_t relPts = (srcPts != AV_NOPTS_VALUE && firstSrcPts != AV_NOPTS_VALUE)
                                          ? srcPts - firstSrcPts
                                          : AV_NOPTS_VALUE;

                    sws_scale(swsCtx,
                              (const uint8_t * const*)frm->data, frm->linesize,
                              0, webcamDec->height,
                              videoEncFrame->data, videoEncFrame->linesize);
                    av_frame_unref(frm);

                    videoEncFrame->pts = (relPts != AV_NOPTS_VALUE)
                        ? av_rescale_q(relPts, inputTB, videoEncCtx->time_base) + videoDelayTb
                        : fallbackPts + videoDelayTb;
                    fallbackPts = videoEncFrame->pts - videoDelayTb
                                  + av_rescale_q(1, inputTB, videoEncCtx->time_base);

                    flushEncoder(videoEncCtx, videoOutSt, videoEncFrame);

                    // Video = 10–100%: progress from normalized frame time / total audio duration
                    if (progressCb && totalDurSec > 0 && relPts != AV_NOPTS_VALUE) {
                        const double frameSec = av_q2d(inputTB) * double(relPts);
                        progressCb(0.1 + 0.9 * std::min(1.0, frameSec / totalDurSec));
                    }
                }
            }
            flushEncoder(videoEncCtx, videoOutSt, nullptr);

            av_frame_free(&frm);
            av_packet_free(&pkt);
            sws_freeContext(swsCtx);
        }

        if (webcamDec) avcodec_free_context(&webcamDec);
        if (webcamFmt) avformat_close_input(&webcamFmt);
    }

    // ── Finalize ──────────────────────────────────────────────────────────────
    av_write_trailer(outFmt);

    if (!(outFmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outFmt->pb);

    av_audio_fifo_free(fifo);
    if (encSwr)       swr_free(&encSwr);
    avcodec_free_context(&audioEncCtx);
    if (videoEncCtx)  avcodec_free_context(&videoEncCtx);
    if (videoEncFrame) av_frame_free(&videoEncFrame);
    avformat_free_context(outFmt);

    if (progressCb) progressCb(1.0);
    qDebug() << "FFmpegNative::renderVideo: done →" << outputPath;
    return true;
}

} // namespace FFmpegNative

#endif // WAKKAQT_FFMPEG_NATIVE
