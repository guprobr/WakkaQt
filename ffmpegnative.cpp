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
#include <string>

namespace FFmpegNative {

// setlocale(LC_NUMERIC, "C") returns the *new* locale name ("C"), not the
// previous one — saving that return value and passing it back to setlocale()
// afterwards (as this code used to) just resets to "C" again, permanently
// leaving the process in the C locale after the first call anywhere in the
// app. Query the current locale explicitly before changing it, and copy it
// out immediately: the pointer setlocale() returns aliases internal storage
// that the very next setlocale() call is free to invalidate.
static std::string forceNumericLocaleC()
{
    const char *cur = setlocale(LC_NUMERIC, nullptr);
    std::string saved = cur ? cur : "C";
    setlocale(LC_NUMERIC, "C");
    return saved;
}

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
    // Preserve the source sample rate so the VocalEnhancer pipeline runs at native
    // quality. Callers that mix multiple streams handle resampling themselves.
    const int   outRate  = (decCtx->sample_rate > 0) ? decCtx->sample_rate : 44100;
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
        const std::string prevLocale = forceNumericLocaleC();

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
        setlocale(LC_NUMERIC, prevLocale.c_str());
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

// Applies a libavfilter chain to interleaved Int16 PCM at an arbitrary sample
// rate/channel count, returning filtered PCM in the same layout. Used to run
// audio masterization (deesser, speechnorm, etc.) on freshly extracted vocals
// before VocalEnhancer processes them, so the two stages don't compound.
QByteArray applyFilterChainS16(const QByteArray &pcmS16, int sampleRate, int channels,
                               const QString &filterChain)
{
    if (pcmS16.isEmpty() || filterChain.isEmpty() || sampleRate <= 0 ||
        (channels != 1 && channels != 2))
        return pcmS16;

    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFilterContext *srcCtx = nullptr, *sinkCtx = nullptr;

    const uint64_t layoutMask = (channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    const char *layoutName = (channels == 1) ? "mono" : "stereo";

    const QByteArray srcParams = QString("sample_rate=%1:sample_fmt=s16:channel_layout=%2:time_base=1/%1")
                                      .arg(sampleRate).arg(layoutName).toUtf8();

    bool ok = (avfilter_graph_create_filter(&srcCtx,
                   avfilter_get_by_name("abuffer"), "in",
                   srcParams.constData(), nullptr, graph) >= 0)
           && (avfilter_graph_create_filter(&sinkCtx,
                   avfilter_get_by_name("abuffersink"), "out",
                   nullptr, nullptr, graph) >= 0);

    QByteArray out;
    if (ok) {
        const QString fullChain = filterChain + QStringLiteral(",aformat=sample_fmts=s16");

        const std::string prevLocale = forceNumericLocaleC();

        AVFilterInOut *ins = nullptr, *outs = nullptr;
        ok = (avfilter_graph_parse2(graph, fullChain.toUtf8().constData(), &ins, &outs) >= 0);
        if (ok && ins)
            ok = (avfilter_link(srcCtx, 0, ins->filter_ctx, ins->pad_idx) >= 0);
        if (ok && outs)
            ok = (avfilter_link(outs->filter_ctx, outs->pad_idx, sinkCtx, 0) >= 0);
        avfilter_inout_free(&ins);
        avfilter_inout_free(&outs);
        ok = ok && (avfilter_graph_config(graph, nullptr) >= 0);

        setlocale(LC_NUMERIC, prevLocale.c_str());
    }

    if (ok) {
        const int bytesPerFrame = channels * (int)sizeof(int16_t);
        const int chunkSamples = 4096;
        AVFrame *inF  = av_frame_alloc();
        AVFrame *outF = av_frame_alloc();
        int64_t pts = 0;
        const int total = pcmS16.size() / bytesPerFrame;
        int offset = 0;

        while (offset <= total) {
            if (offset < total) {
                const int n = std::min(chunkSamples, total - offset);
                inF->sample_rate = sampleRate;
                inF->format      = AV_SAMPLE_FMT_S16;
                inF->nb_samples  = n;
                inF->pts         = pts;
                av_channel_layout_from_mask(&inF->ch_layout, layoutMask);
                av_frame_get_buffer(inF, 0);
                memcpy(inF->data[0], pcmS16.constData() + offset * bytesPerFrame,
                       n * bytesPerFrame);
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
                const int bytes = outF->nb_samples * bytesPerFrame;
                out.append(reinterpret_cast<const char*>(outF->data[0]), bytes);
                av_frame_unref(outF);
            }
        }

        av_frame_free(&inF);
        av_frame_free(&outF);
    } else {
        qWarning() << "FFmpegNative::applyFilterChainS16: filter graph failed, "
                       "returning input unchanged";
        out = pcmS16;
    }

    avfilter_graph_free(&graph);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pitch overlay helpers
// ─────────────────────────────────────────────────────────────────────────────

struct PitchPoint {
    int64_t ms;
    double  hz;
    double  cents;
    int     noteIdx;
    int     octave;
    bool    valid;
};

// 5×8 bitmap font. bit7 = leftmost column.
// Index map: 0=' ' 1='#' 2='+' 3='-' 4-13='0'-'9' 14-20='A'-'G' 21='c'
static const uint8_t kFont5x8[22][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x50,0x50,0xF8,0x50,0xF8,0x50,0x50,0x00}, // '#'
    {0x00,0x20,0x20,0xF8,0x20,0x20,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0xF8,0x00,0x00,0x00,0x00}, // '-'
    {0x70,0x88,0x88,0x88,0x88,0x88,0x70,0x00}, // '0'
    {0x20,0x60,0x20,0x20,0x20,0x20,0x70,0x00}, // '1'
    {0x70,0x88,0x08,0x10,0x20,0x40,0xF8,0x00}, // '2'
    {0x70,0x88,0x08,0x30,0x08,0x88,0x70,0x00}, // '3'
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x10,0x00}, // '4'
    {0xF8,0x80,0xF0,0x08,0x08,0x88,0x70,0x00}, // '5'
    {0x70,0x80,0xF0,0x88,0x88,0x88,0x70,0x00}, // '6'
    {0xF8,0x08,0x10,0x20,0x20,0x20,0x20,0x00}, // '7'
    {0x70,0x88,0x88,0x70,0x88,0x88,0x70,0x00}, // '8'
    {0x70,0x88,0x88,0x78,0x08,0x88,0x70,0x00}, // '9'
    {0x70,0x88,0x88,0xF8,0x88,0x88,0x88,0x00}, // 'A'
    {0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0,0x00}, // 'B'
    {0x70,0x88,0x80,0x80,0x80,0x88,0x70,0x00}, // 'C'
    {0xF0,0x88,0x88,0x88,0x88,0x88,0xF0,0x00}, // 'D'
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8,0x00}, // 'E'
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0x80,0x00}, // 'F'
    {0x70,0x88,0x80,0xB8,0x88,0x88,0x70,0x00}, // 'G'
    {0x00,0x00,0x00,0x70,0x80,0x80,0x70,0x00}, // 'c'
};

static const char * const kNoteNames[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static int fontIndex(char c)
{
    if (c == ' ') return 0;
    if (c == '#') return 1;
    if (c == '+') return 2;
    if (c == '-') return 3;
    if (c >= '0' && c <= '9') return 4 + (c - '0');
    if (c >= 'A' && c <= 'G') return 14 + (c - 'A');
    if (c == 'c') return 21;
    return 0;
}

static void drawGlyph(AVFrame *f, int x, int y, char c,
                      uint8_t Yv, uint8_t Uv, uint8_t Vv, int scale)
{
    const uint8_t *glyph = kFont5x8[fontIndex(c)];
    for (int row = 0; row < 8; ++row) {
        const uint8_t bits = glyph[row];
        for (int col = 0; col < 5; ++col) {
            if (!(bits & (0x80u >> col))) continue;
            for (int sy = 0; sy < scale; ++sy) {
                const int py = y + row * scale + sy;
                if (py < 0 || py >= f->height) continue;
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x + col * scale + sx;
                    if (px < 0 || px >= f->width) continue;
                    f->data[0][py * f->linesize[0] + px] = Yv;
                    const int ux = px >> 1, uy = py >> 1;
                    if (ux < f->width / 2 && uy < f->height / 2) {
                        f->data[1][uy * f->linesize[1] + ux] = Uv;
                        f->data[2][uy * f->linesize[2] + ux] = Vv;
                    }
                }
            }
        }
    }
}

static double yinDetect(const float *samples, int n, int sampleRate)
{
    const int W = std::min(n / 2, 512);
    if (W < 32) return -1.0;

    std::vector<double> d(W, 0.0);
    for (int tau = 1; tau < W; ++tau)
        for (int j = 0; j < W; ++j) {
            const double diff = samples[j] - samples[j + tau];
            d[tau] += diff * diff;
        }

    std::vector<double> cmnd(W);
    cmnd[0] = 1.0;
    double runSum = 0.0;
    for (int tau = 1; tau < W; ++tau) {
        runSum += d[tau];
        cmnd[tau] = (runSum > 0.0) ? d[tau] * tau / runSum : 1.0;
    }

    const double thresh = 0.10;
    for (int tau = 2; tau < W - 1; ++tau) {
        if (cmnd[tau] < thresh && cmnd[tau] <= cmnd[tau-1] && cmnd[tau] <= cmnd[tau+1]) {
            const double s0 = cmnd[tau-1], s1 = cmnd[tau], s2 = cmnd[tau+1];
            const double denom = 2.0 * s1 - s0 - s2;
            const double adj = (denom != 0.0) ? 0.5 * (s2 - s0) / denom : 0.0;
            const double tauF = tau + adj;
            if (tauF > 0.5) return double(sampleRate) / tauF;
            break;
        }
    }
    return -1.0;
}

static QVector<PitchPoint> analyzePitch(const QString &audioPath)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, audioPath.toUtf8().constData(), nullptr, nullptr) < 0)
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
    av_channel_layout_default(&dstCL, 1); // mono output
    swr_alloc_set_opts2(&swr, &dstCL, AV_SAMPLE_FMT_FLT, 44100,
                         &srcCL, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    swr_init(swr);
    av_channel_layout_uninit(&srcCL);
    av_channel_layout_uninit(&dstCL);

    QVector<float> mono;
    mono.reserve(44100 * 60);

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
            QVector<float> tmp(outN);
            uint8_t *ptr = reinterpret_cast<uint8_t*>(tmp.data());
            const int got = swr_convert(swr, &ptr, outN,
                                        (const uint8_t**)frm->data, frm->nb_samples);
            if (got > 0) {
                const int oldSz = mono.size();
                mono.resize(oldSz + got);
                memcpy(mono.data() + oldSz, tmp.constData(), got * sizeof(float));
            }
            av_frame_unref(frm);
        }
    }
    {
        const int outN = (int)swr_get_delay(swr, 44100) + 1024;
        QVector<float> tmp(outN);
        uint8_t *ptr = reinterpret_cast<uint8_t*>(tmp.data());
        const int got = swr_convert(swr, &ptr, outN, nullptr, 0);
        if (got > 0) {
            const int oldSz = mono.size();
            mono.resize(oldSz + got);
            memcpy(mono.data() + oldSz, tmp.constData(), got * sizeof(float));
        }
    }
    av_frame_free(&frm);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    if (mono.isEmpty()) return {};

    constexpr int kSR  = 44100;
    constexpr int kHop = kSR * 80 / 1000; // 80 ms hop
    constexpr int kWin = 2048;

    QVector<PitchPoint> result;
    double smoothHz = 0.0, smoothCents = 0.0;
    bool hadVoice = false;

    for (int pos = 0; pos + kWin <= mono.size(); pos += kHop) {
        const int64_t ms = int64_t(pos) * 1000 / kSR;
        const double hz  = yinDetect(mono.constData() + pos, kWin, kSR);

        PitchPoint pp;
        pp.ms = ms;
        if (hz > 60.0 && hz < 1600.0) {
            if (!hadVoice) { smoothHz = hz; hadVoice = true; }
            else           smoothHz = 0.3 * hz + 0.7 * smoothHz;

            const double midiF = 69.0 + 12.0 * std::log2(smoothHz / 440.0);
            const int midi = (int)std::round(midiF);
            pp.noteIdx = ((midi % 12) + 12) % 12;
            pp.octave  = midi / 12 - 1;

            const double rawCents = (midiF - midi) * 100.0;
            smoothCents = 0.3 * rawCents + 0.7 * smoothCents;
            pp.cents = smoothCents;
            pp.hz    = smoothHz;
            pp.valid = true;
        } else {
            smoothCents *= 0.85;
            pp.hz      = 0.0;
            pp.cents   = smoothCents;
            pp.noteIdx = 0;
            pp.octave  = 0;
            pp.valid   = false;
            if (std::abs(smoothCents) < 1.0) hadVoice = false;
        }
        result.append(pp);
    }
    return result;
}

static void paintPitchOverlay(AVFrame *frame, int64_t lookupMs,
                               const QVector<PitchPoint> &pitches)
{
    if (pitches.isEmpty()) return;

    const int W = frame->width;
    const int H = frame->height;
    const int scale  = (W >= 1280) ? 3 : 2;
    const int glyphW = 5 * scale + scale; // 5 cols + 1 gap
    const int stripH = 8 * scale + 4 * scale;
    const int stripY = H - stripH;

    // Dark background strip
    for (int y = stripY; y < H; ++y)
        memset(frame->data[0] + y * frame->linesize[0], 20, W);
    for (int y = stripY / 2; y < H / 2; ++y) {
        memset(frame->data[1] + y * frame->linesize[1], 128, W / 2);
        memset(frame->data[2] + y * frame->linesize[2], 128, W / 2);
    }

    // Find closest pitch point (last entry with ms <= lookupMs)
    int lo = 0, hi = pitches.size() - 1, best = 0;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (pitches[mid].ms <= lookupMs) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    const PitchPoint &pp = pitches[best];
    if (!pp.valid) return;

    // Color: green <10 ¢, yellow 10–30 ¢, red >30 ¢
    const int absCents = (int)std::abs(std::round(pp.cents));
    uint8_t Yv, Uv, Vv;
    if      (absCents < 10) { Yv = 148; Uv =  81; Vv =  69; } // green
    else if (absCents < 30) { Yv = 177; Uv =  55; Vv = 148; } // yellow
    else                    { Yv = 109; Uv = 104; Vv = 198; } // red

    const int textY = stripY + 2 * scale;

    // Left: note + octave  e.g. "C#4"
    const QString noteStr = QString::fromLatin1(kNoteNames[pp.noteIdx])
                            + QString::number(pp.octave);
    int x = 2 * scale;
    for (QChar ch : noteStr) {
        drawGlyph(frame, x, textY, ch.toLatin1(), Yv, Uv, Vv, scale);
        x += glyphW;
    }

    // Center: gray baseline + white center tick + colored marker
    const int barW  = W / 3;
    const int barX  = (W - barW) / 2;
    const int midY  = stripY + stripH / 2;
    const float frac = std::clamp(float(pp.cents + 50.0) / 100.0f, 0.0f, 1.0f);
    const int markerX = barX + (int)(frac * barW);

    for (int px = barX; px < barX + barW && px < W; ++px) {
        if (px < 0) continue;
        frame->data[0][midY * frame->linesize[0] + px] = 80;
        const int ux = px >> 1, uy = midY >> 1;
        if (ux < W/2 && uy < H/2) {
            frame->data[1][uy * frame->linesize[1] + ux] = 128;
            frame->data[2][uy * frame->linesize[2] + ux] = 128;
        }
    }
    for (int py = stripY + scale; py < H - scale; ++py) { // center tick
        const int cx = W / 2;
        if (cx >= 0 && cx < W) frame->data[0][py * frame->linesize[0] + cx] = 200;
    }
    for (int py = midY - 2*scale; py <= midY + 2*scale && py < H; ++py) {
        if (py < 0) continue;
        for (int px = markerX - scale; px <= markerX + scale && px < W; ++px) {
            if (px < 0) continue;
            frame->data[0][py * frame->linesize[0] + px] = Yv;
            const int ux = px >> 1, uy = py >> 1;
            if (ux < W/2 && uy < H/2) {
                frame->data[1][uy * frame->linesize[1] + ux] = Uv;
                frame->data[2][uy * frame->linesize[2] + ux] = Vv;
            }
        }
    }

    // Right: cents value e.g. "+12c"
    const int centsRounded = (int)std::round(pp.cents);
    const QString centsStr = (centsRounded >= 0 ? "+" : "")
                             + QString::number(centsRounded) + "c";
    x = W - (int)centsStr.length() * glyphW - 2 * scale;
    for (QChar ch : centsStr) {
        drawGlyph(frame, x, textY, ch.toLatin1(), Yv, Uv, Vv, scale);
        x += glyphW;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// decodeToFloatStereo
// ─────────────────────────────────────────────────────────────────────────────

std::vector<float> decodeToFloatStereo(const QString &filePath)
{
    const QVector<float> q = decodeAudioToFloat(filePath, 0, 1.0);
    return std::vector<float>(q.constBegin(), q.constEnd());
}

// ─────────────────────────────────────────────────────────────────────────────
// writeFloatWav
// ─────────────────────────────────────────────────────────────────────────────

bool writeFloatWav(const std::vector<float> &pcm, const QString &outPath)
{
    if (pcm.empty() || pcm.size() % 2 != 0) {
        qWarning() << "FFmpegNative::writeFloatWav: invalid PCM";
        return false;
    }

    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_PCM_F32LE);
    if (!enc) { qWarning() << "FFmpegNative::writeFloatWav: PCM_F32LE not found"; return false; }

    AVFormatContext *outFmt = nullptr;
    if (avformat_alloc_output_context2(&outFmt, nullptr, "wav",
                                        outPath.toUtf8().constData()) < 0)
        return false;

    AVStream *st = avformat_new_stream(outFmt, nullptr);
    if (!st) { avformat_free_context(outFmt); return false; }

    AVCodecContext *encCtx = avcodec_alloc_context3(enc);
    encCtx->sample_fmt  = AV_SAMPLE_FMT_FLT;
    encCtx->sample_rate = 44100;
    encCtx->bit_rate    = 0;
    encCtx->time_base   = {1, 44100};
    av_channel_layout_from_mask(&encCtx->ch_layout, AV_CH_LAYOUT_STEREO);

    if (avcodec_open2(encCtx, enc, nullptr) < 0) {
        avcodec_free_context(&encCtx); avformat_free_context(outFmt); return false;
    }
    avcodec_parameters_from_context(st->codecpar, encCtx);
    st->time_base = encCtx->time_base;

    if (avio_open(&outFmt->pb, outPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
        avcodec_free_context(&encCtx); avformat_free_context(outFmt); return false;
    }
    if (avformat_write_header(outFmt, nullptr) < 0) {
        avio_closep(&outFmt->pb); avcodec_free_context(&encCtx); avformat_free_context(outFmt); return false;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frm = av_frame_alloc();
    const int chunkSz    = 4096;
    const int totalSamps = (int)(pcm.size() / 2);
    int64_t   pts        = 0;

    auto flushEnc = [&](AVFrame *f) {
        if (avcodec_send_frame(encCtx, f) < 0) return;
        while (avcodec_receive_packet(encCtx, pkt) == 0) {
            av_packet_rescale_ts(pkt, encCtx->time_base, st->time_base);
            pkt->stream_index = st->index;
            av_interleaved_write_frame(outFmt, pkt);
            av_packet_unref(pkt);
        }
    };

    for (int off = 0; off < totalSamps; off += chunkSz) {
        const int n = std::min(chunkSz, totalSamps - off);
        frm->format      = AV_SAMPLE_FMT_FLT;
        frm->sample_rate = 44100;
        frm->nb_samples  = n;
        frm->pts         = pts;
        av_channel_layout_from_mask(&frm->ch_layout, AV_CH_LAYOUT_STEREO);
        av_frame_get_buffer(frm, 0);
        memcpy(frm->data[0], pcm.data() + off * 2, n * 2 * sizeof(float));
        flushEnc(frm);
        av_frame_unref(frm);
        pts += n;
    }
    flushEnc(nullptr);

    av_write_trailer(outFmt);
    avio_closep(&outFmt->pb);
    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&encCtx);
    avformat_free_context(outFmt);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// transcodeAudio — shared encode pipeline used by transcodeAudio & muxVideoWithAudio
// ─────────────────────────────────────────────────────────────────────────────

// Return `preferred` if the encoder accepts it, otherwise the closest rate it
// does support (e.g. libopus only supports 8/12/16/24/48 kHz, so 44100 gets
// mapped to 48000). Encoders with no restriction list report nullptr/0 and
// `preferred` is returned unchanged.
static int pickSupportedSampleRate(const AVCodec *enc, int preferred)
{
    const int *rates = nullptr;
    int numRates = 0;
    avcodec_get_supported_config(nullptr, enc, AV_CODEC_CONFIG_SAMPLE_RATE, 0,
                                  reinterpret_cast<const void **>(&rates), &numRates);
    if (!rates || numRates <= 0)
        return preferred;

    int best = rates[0];
    int bestDiff = std::abs(rates[0] - preferred);
    for (int i = 1; i < numRates; ++i) {
        const int diff = std::abs(rates[i] - preferred);
        if (diff < bestDiff) { bestDiff = diff; best = rates[i]; }
    }
    return best;
}

// Encode S16 stereo 44100 Hz PCM into an already-opened output context.
// outFmt must have its header already written; caller writes trailer and closes.
static bool encodeS16ToStream(const QVector<int16_t> &s16,
                               AVFormatContext *outFmt,
                               AVStream *outSt,
                               AVCodecContext *encCtx,
                               SwrContext *encSwr,
                               std::function<void(int)> progressCb = {},
                               std::vector<AVPacket*> *queueOut = nullptr)
{
    AVAudioFifo *fifo = av_audio_fifo_alloc(encCtx->sample_fmt, 2,
                                             std::max(1, encCtx->frame_size));

    auto flushEnc = [&](AVFrame *f) {
        if (avcodec_send_frame(encCtx, f) < 0) return;
        while (true) {
            AVPacket *pkt = av_packet_alloc();
            if (avcodec_receive_packet(encCtx, pkt) < 0) { av_packet_free(&pkt); break; }
            av_packet_rescale_ts(pkt, encCtx->time_base, outSt->time_base);
            pkt->stream_index = outSt->index;
            if (queueOut) {
                queueOut->push_back(pkt);
            } else {
                av_interleaved_write_frame(outFmt, pkt);
                av_packet_free(&pkt);
            }
        }
    };

    auto pushToFifo = [&](const int16_t *src, int n) {
        if (encSwr) {
            const int maxOut = (int)swr_get_out_samples(encSwr, n);
            const bool planar = av_sample_fmt_is_planar(encCtx->sample_fmt);
            const int planes = planar ? 2 : 1;
            // Packed (non-planar) formats interleave both channels into the
            // single plane, so that buffer needs 2x the per-channel size.
            const int samplesPerPlane = planar ? 1 : 2;
            std::vector<std::vector<uint8_t>> bufs;
            std::vector<uint8_t *> ptrs;
            for (int p = 0; p < planes; ++p) {
                bufs.emplace_back((size_t)maxOut * samplesPerPlane * av_get_bytes_per_sample(encCtx->sample_fmt) + 16);
                ptrs.push_back(bufs.back().data());
            }
            const uint8_t *srcPtr = reinterpret_cast<const uint8_t *>(src);
            const int got = swr_convert(encSwr, ptrs.data(), maxOut, &srcPtr, n);
            if (got > 0)
                av_audio_fifo_write(fifo, reinterpret_cast<void **>(ptrs.data()), got);
        } else {
            void *ptr = const_cast<int16_t *>(src);
            av_audio_fifo_write(fifo, &ptr, n);
        }
    };

    const int frameSize      = (encCtx->frame_size > 0) ? encCtx->frame_size : 1024;
    const int totalSamplesIn = s16.size() / 2;
    int       samplesWritten = 0;
    int64_t   audioPts       = 0;

    while (samplesWritten < totalSamplesIn || av_audio_fifo_size(fifo) > 0) {
        if (samplesWritten < totalSamplesIn) {
            const int batch = std::min(frameSize * 4, totalSamplesIn - samplesWritten);
            pushToFifo(s16.constData() + samplesWritten * 2, batch);
            samplesWritten += batch;
            if (progressCb && totalSamplesIn > 0)
                progressCb(samplesWritten * 100 / totalSamplesIn);
        }
        while (av_audio_fifo_size(fifo) >= frameSize ||
               (samplesWritten >= totalSamplesIn && av_audio_fifo_size(fifo) > 0)) {
            const int read = std::min(frameSize, av_audio_fifo_size(fifo));
            if (read <= 0) break;
            AVFrame *af = av_frame_alloc();
            af->format      = encCtx->sample_fmt;
            af->nb_samples  = read;
            af->sample_rate = encCtx->sample_rate;
            af->pts         = audioPts;
            av_channel_layout_copy(&af->ch_layout, &encCtx->ch_layout);
            av_frame_get_buffer(af, 0);
            av_audio_fifo_read(fifo, reinterpret_cast<void **>(af->data), read);
            flushEnc(af);
            av_frame_free(&af);
            audioPts += read;
        }
    }
    flushEnc(nullptr);

    av_audio_fifo_free(fifo);
    return true;
}

// Build audio encoder context for 44100 Hz stereo into outFmt.
// Caller owns encCtx and encSwr (may be null) and must free them.
static bool openAudioEncoder(AVCodecID codecId, AVFormatContext *outFmt,
                              AVStream **outSt, AVCodecContext **encCtx,
                              SwrContext **encSwr)
{
    *encSwr = nullptr;
    const AVCodec *enc = avcodec_find_encoder(codecId);
    if (!enc) { qWarning() << "FFmpegNative: audio encoder not found"; return false; }

    *outSt  = avformat_new_stream(outFmt, enc);
    *encCtx = avcodec_alloc_context3(enc);
    (*encCtx)->sample_rate = pickSupportedSampleRate(enc, 44100);
    av_channel_layout_from_mask(&(*encCtx)->ch_layout, AV_CH_LAYOUT_STEREO);
    {
        const AVSampleFormat *fmts = nullptr; int nFmts = 0;
        avcodec_get_supported_config(nullptr, enc, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                      reinterpret_cast<const void **>(&fmts), &nFmts);
        (*encCtx)->sample_fmt = (fmts && nFmts > 0) ? fmts[0] : AV_SAMPLE_FMT_S16;
    }
    (*encCtx)->bit_rate  = (codecId == AV_CODEC_ID_PCM_S16LE ||
                            codecId == AV_CODEC_ID_FLAC) ? 0 : 192000;
    (*encCtx)->time_base = {1, (*encCtx)->sample_rate};
    if (outFmt->oformat->flags & AVFMT_GLOBALHEADER)
        (*encCtx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(*encCtx, enc, nullptr) < 0) {
        avcodec_free_context(encCtx); return false;
    }
    avcodec_parameters_from_context((*outSt)->codecpar, *encCtx);
    (*outSt)->time_base = (*encCtx)->time_base;

    if ((*encCtx)->sample_fmt != AV_SAMPLE_FMT_S16 || (*encCtx)->sample_rate != 44100) {
        AVChannelLayout stereo;
        av_channel_layout_from_mask(&stereo, AV_CH_LAYOUT_STEREO);
        swr_alloc_set_opts2(encSwr, &stereo, (*encCtx)->sample_fmt, (*encCtx)->sample_rate,
                             &stereo, AV_SAMPLE_FMT_S16, 44100, 0, nullptr);
        swr_init(*encSwr);
        av_channel_layout_uninit(&stereo);
    }
    return true;
}

static AVCodecID audioCodecForExt(const QString &ext)
{
    if (ext == "mp3")  return AV_CODEC_ID_MP3;
    if (ext == "flac") return AV_CODEC_ID_FLAC;
    if (ext == "opus") return AV_CODEC_ID_OPUS;
    if (ext == "wav")  return AV_CODEC_ID_PCM_S16LE;
    return AV_CODEC_ID_AAC;
}

bool transcodeAudio(const QString &input, const QString &output,
                    std::function<void(int)> progressCb)
{
    if (progressCb) progressCb(0);
    const QVector<float>   floatPcm = decodeAudioToFloat(input, 0, 1.0);
    if (floatPcm.isEmpty()) {
        qWarning() << "FFmpegNative::transcodeAudio: decode failed for" << input;
        return false;
    }
    if (progressCb) progressCb(20);
    const QVector<int16_t> s16Pcm = applyAudioFilter(floatPcm, {});
    if (progressCb) progressCb(35);

    const QString ext = QFileInfo(output).suffix().toLower();
    AVFormatContext *outFmt = nullptr;
    avformat_alloc_output_context2(&outFmt, nullptr, nullptr, output.toUtf8().constData());
    if (!outFmt) return false;

    AVStream      *outSt  = nullptr;
    AVCodecContext *encCtx = nullptr;
    SwrContext     *encSwr = nullptr;
    if (!openAudioEncoder(audioCodecForExt(ext), outFmt, &outSt, &encCtx, &encSwr)) {
        avformat_free_context(outFmt); return false;
    }

    if (avio_open(&outFmt->pb, output.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
        if (encSwr) swr_free(&encSwr);
        avcodec_free_context(&encCtx);
        avformat_free_context(outFmt);
        return false;
    }
    if (avformat_write_header(outFmt, nullptr) < 0) {
        avio_closep(&outFmt->pb);
        if (encSwr) swr_free(&encSwr);
        avcodec_free_context(&encCtx);
        avformat_free_context(outFmt);
        return false;
    }

    encodeS16ToStream(s16Pcm, outFmt, outSt, encCtx, encSwr,
        progressCb ? [&progressCb](int pct) { progressCb(35 + pct * 65 / 100); }
                   : std::function<void(int)>{});

    av_write_trailer(outFmt);
    avio_closep(&outFmt->pb);
    if (encSwr) swr_free(&encSwr);
    avcodec_free_context(&encCtx);
    avformat_free_context(outFmt);
    if (progressCb) progressCb(100);
    qDebug() << "FFmpegNative::transcodeAudio: done →" << output;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// muxVideoWithAudio
// ─────────────────────────────────────────────────────────────────────────────

bool muxVideoWithAudio(const QString &videoSrc, const QString &audioSrc,
                       const QString &output,
                       std::function<void(int)> progressCb)
{
    if (progressCb) progressCb(0);
    // Open video source to copy its video stream
    AVFormatContext *vidFmt = nullptr;
    if (avformat_open_input(&vidFmt, videoSrc.toUtf8().constData(), nullptr, nullptr) < 0) {
        qWarning() << "FFmpegNative::muxVideoWithAudio: cannot open video source";
        return false;
    }
    avformat_find_stream_info(vidFmt, nullptr);
    double totalDurSec = 0.0;
    if (vidFmt->duration != AV_NOPTS_VALUE && vidFmt->duration > 0)
        totalDurSec = double(vidFmt->duration) / AV_TIME_BASE;
    const int vidIdx = av_find_best_stream(vidFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vidIdx < 0) {
        qWarning() << "FFmpegNative::muxVideoWithAudio: no video stream in" << videoSrc;
        avformat_close_input(&vidFmt); return false;
    }

    // Decode audio source
    const QVector<float> floatPcm = decodeAudioToFloat(audioSrc, 0, 1.0);
    if (floatPcm.isEmpty()) {
        qWarning() << "FFmpegNative::muxVideoWithAudio: cannot decode audio from" << audioSrc;
        avformat_close_input(&vidFmt); return false;
    }
    if (progressCb) progressCb(10);
    const QVector<int16_t> s16Pcm = applyAudioFilter(floatPcm, {});
    if (progressCb) progressCb(20);

    // Output format
    AVFormatContext *outFmt = nullptr;
    avformat_alloc_output_context2(&outFmt, nullptr, nullptr, output.toUtf8().constData());
    if (!outFmt) { avformat_close_input(&vidFmt); return false; }

    // Video stream — stream copy (no re-encode)
    AVStream *inVidSt  = vidFmt->streams[vidIdx];
    AVStream *outVidSt = avformat_new_stream(outFmt, nullptr);
    avcodec_parameters_copy(outVidSt->codecpar, inVidSt->codecpar);
    outVidSt->codecpar->codec_tag = 0;
    outVidSt->time_base = inVidSt->time_base;

    // Audio stream — encode new audio
    const QString ext = QFileInfo(output).suffix().toLower();
    AVStream      *outAudSt  = nullptr;
    AVCodecContext *audioEncCtx = nullptr;
    SwrContext     *audioEncSwr = nullptr;
    if (!openAudioEncoder(audioCodecForExt(ext), outFmt, &outAudSt, &audioEncCtx, &audioEncSwr)) {
        avformat_close_input(&vidFmt); avformat_free_context(outFmt); return false;
    }

    if (!(outFmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outFmt->pb, output.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            if (audioEncSwr) swr_free(&audioEncSwr);
            avcodec_free_context(&audioEncCtx);
            avformat_close_input(&vidFmt);
            avformat_free_context(outFmt);
            return false;
        }
    }
    if (avformat_write_header(outFmt, nullptr) < 0) {
        if (!(outFmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&outFmt->pb);
        if (audioEncSwr) swr_free(&audioEncSwr);
        avcodec_free_context(&audioEncCtx);
        avformat_close_input(&vidFmt);
        avformat_free_context(outFmt);
        return false;
    }

    // Collect audio into a queue so it can be written interleaved with video.
    // Writing all audio before any video produces files where players show no
    // video from the start, or no audio after a seek.
    std::vector<AVPacket*> audioQueue;
    size_t audioQueueIdx = 0;
    encodeS16ToStream(s16Pcm, outFmt, outAudSt, audioEncCtx, audioEncSwr,
        progressCb ? [&progressCb](int pct) { progressCb(20 + pct * 20 / 100); }
                   : std::function<void(int)>{},
        &audioQueue);
    if (progressCb) progressCb(40);

    // Copy video packets — seek to start in case avformat_find_stream_info advanced it
    av_seek_frame(vidFmt, vidIdx, 0, AVSEEK_FLAG_BACKWARD);

    AVPacket *pkt = av_packet_alloc();
    int64_t firstPts = AV_NOPTS_VALUE;

    while (av_read_frame(vidFmt, pkt) >= 0) {
        if (pkt->stream_index != vidIdx) { av_packet_unref(pkt); continue; }

        // Normalize PTS/DTS to start from 0
        if (firstPts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
            firstPts = pkt->pts;
        if (firstPts != AV_NOPTS_VALUE) {
            if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= firstPts;
            if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= firstPts;
        }

        av_packet_rescale_ts(pkt, inVidSt->time_base, outVidSt->time_base);
        pkt->stream_index = outVidSt->index;

        // Drain audio packets whose DTS is at or before this video packet's DTS
        const int64_t videoDtsAV = av_rescale_q(
            pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts,
            outVidSt->time_base, AV_TIME_BASE_Q);
        while (audioQueueIdx < audioQueue.size()) {
            AVPacket *ap = audioQueue[audioQueueIdx];
            const int64_t audioDtsAV = av_rescale_q(
                ap->dts != AV_NOPTS_VALUE ? ap->dts : ap->pts,
                outAudSt->time_base, AV_TIME_BASE_Q);
            if (audioDtsAV > videoDtsAV) break;
            av_write_frame(outFmt, ap);
            av_packet_free(&audioQueue[audioQueueIdx++]);
        }

        if (progressCb && totalDurSec > 0 && pkt->pts != AV_NOPTS_VALUE) {
            const double ptsSec = double(pkt->pts) * av_q2d(outVidSt->time_base);
            progressCb(40 + int(60.0 * std::min(1.0, ptsSec / totalDurSec)));
        }
        av_write_frame(outFmt, pkt);
        av_packet_unref(pkt);
    }

    // Drain any audio that extends past the end of the video track
    for (; audioQueueIdx < audioQueue.size(); audioQueueIdx++) {
        av_write_frame(outFmt, audioQueue[audioQueueIdx]);
        av_packet_free(&audioQueue[audioQueueIdx]);
    }
    audioQueue.clear();

    av_write_trailer(outFmt);
    if (!(outFmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outFmt->pb);

    av_packet_free(&pkt);
    if (audioEncSwr) swr_free(&audioEncSwr);
    avcodec_free_context(&audioEncCtx);
    avformat_close_input(&vidFmt);
    avformat_free_context(outFmt);

    if (progressCb) progressCb(100);
    qDebug() << "FFmpegNative::muxVideoWithAudio: done →" << output;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// renderVideo helpers
// ─────────────────────────────────────────────────────────────────────────────

// Verify that a hardware encoder context can actually encode a frame.
// avcodec_open2 succeeds for some hw encoders even when the driver can't encode
// (e.g. VAAPI on a GPU that only supports hardware decode). Those encoders
// silently queue failing frames until an internal FFmpeg assertion fires at 16
// queued frames. This probe catches the failure early by encoding one dummy
// frame, then resets the context via avcodec_flush_buffers for real use.
// Returns true if the encoder is usable.
static bool probeHWEncoderCtx(AVCodecContext *ctx)
{
    // Determine the upload format the hw encoder expects (e.g. NV12 for Intel VAAPI)
    AVPixelFormat uploadFmt = AV_PIX_FMT_YUV420P;
    if (ctx->hw_frames_ctx)
        uploadFmt = reinterpret_cast<AVHWFramesContext*>(ctx->hw_frames_ctx->data)->sw_format;

    AVFrame *swf = av_frame_alloc();
    swf->format  = uploadFmt;
    swf->width   = ctx->width;
    swf->height  = ctx->height;
    swf->pts     = 0;
    if (av_frame_get_buffer(swf, 0) < 0) { av_frame_free(&swf); return false; }
    av_frame_make_writable(swf);
    // Fill all planes with 128 (valid gray for any planar or semi-planar format)
    for (int p = 0; p < AV_NUM_DATA_POINTERS && swf->data[p]; ++p)
        memset(swf->data[p], 128, swf->linesize[p] * (p == 0 ? ctx->height : ctx->height / 2));

    AVFrame *encf = swf;
    AVFrame *hwf  = nullptr;
    if (ctx->hw_frames_ctx) {
        hwf = av_frame_alloc();
        hwf->pts = 0;
        if (av_hwframe_get_buffer(ctx->hw_frames_ctx, hwf, 0) < 0
            || av_hwframe_transfer_data(hwf, swf, 0) < 0) {
            av_frame_free(&hwf);
            av_frame_free(&swf);
            return false;
        }
        encf = hwf;
    }

    bool ok = false;
    if (avcodec_send_frame(ctx, encf) >= 0) {
        AVPacket *pkt = av_packet_alloc();
        const int rc  = avcodec_receive_packet(ctx, pkt);
        av_packet_free(&pkt);
        // 0 = packet ready; EAGAIN = encoder buffering (normal for B-frame lookahead)
        ok = (rc == 0 || rc == AVERROR(EAGAIN));
    }

    if (hwf) av_frame_free(&hwf);
    av_frame_free(&swf);
    if (ok) avcodec_flush_buffers(ctx);
    return ok;
}

// Soft-clip a mixed sample to [-1, 1].
// Linear below ±0.8 (no level change in the normal range), then a tanh-shaped
// taper above that so peaks compress smoothly instead of chopping off flat.
static inline float softClip(float x)
{
    constexpr float knee  = 0.8f;
    constexpr float range = 1.0f - knee; // 0.2
    const float ax = std::abs(x);
    if (ax <= knee) return x;
    const float sign = (x > 0.f) ? 1.f : -1.f;
    return sign * (knee + range * tanhf((ax - knee) / range));
}

// ─────────────────────────────────────────────────────────────────────────────
// Video effects (VideoEffectProcessor) — shared by live preview and render
// ─────────────────────────────────────────────────────────────────────────────

// Builds a "buffer → <filterChain> → format=<pixFmt> → buffersink" graph.
// Mirrors applyAudioFilter()'s pattern (see above) for video frames instead
// of PCM. Returns false (with *graph left null) if the chain can't be built —
// e.g. a frei0r plugin the chain references isn't installed on this machine.
static bool buildVideoFilterGraph(AVFilterGraph **graph, AVFilterContext **srcCtx,
                                   AVFilterContext **sinkCtx, const QString &filterChain,
                                   int width, int height, AVPixelFormat pixFmt)
{
    *graph = avfilter_graph_alloc();
    if (!*graph)
        return false;

    const QByteArray srcParams = QStringLiteral(
        "video_size=%1x%2:pix_fmt=%3:time_base=1/25:pixel_aspect=1/1")
        .arg(width).arg(height).arg(int(pixFmt)).toUtf8();

    bool ok = (avfilter_graph_create_filter(srcCtx, avfilter_get_by_name("buffer"),
                   "in", srcParams.constData(), nullptr, *graph) >= 0)
           && (avfilter_graph_create_filter(sinkCtx, avfilter_get_by_name("buffersink"),
                   "out", nullptr, nullptr, *graph) >= 0);

    if (ok) {
        const QString fullChain = filterChain.isEmpty()
            ? QStringLiteral("format=pix_fmts=%1").arg(int(pixFmt))
            : filterChain + QStringLiteral(",format=pix_fmts=%1").arg(int(pixFmt));

        // Force "C" locale so avfilter parses decimal points correctly regardless
        // of the system locale (e.g. "0.5" would fail on German/French locales).
        const std::string prevLocale = forceNumericLocaleC();

        AVFilterInOut *ins = nullptr, *outs = nullptr;
        ok = (avfilter_graph_parse2(*graph, fullChain.toUtf8().constData(), &ins, &outs) >= 0);
        if (ok && ins)
            ok = (avfilter_link(*srcCtx, 0, ins->filter_ctx, ins->pad_idx) >= 0);
        if (ok && outs)
            ok = (avfilter_link(outs->filter_ctx, outs->pad_idx, *sinkCtx, 0) >= 0);
        avfilter_inout_free(&ins);
        avfilter_inout_free(&outs);
        ok = ok && (avfilter_graph_config(*graph, nullptr) >= 0);

        setlocale(LC_NUMERIC, prevLocale.c_str());
    }

    if (!ok) {
        avfilter_graph_free(graph);
        *srcCtx = *sinkCtx = nullptr;
    }
    return ok;
}

bool VideoEffectProcessor::isChainAvailable(const QString &filterChain)
{
    if (filterChain.isEmpty())
        return true;
    AVFilterGraph *graph = nullptr;
    AVFilterContext *srcCtx = nullptr, *sinkCtx = nullptr;
    const bool ok = buildVideoFilterGraph(&graph, &srcCtx, &sinkCtx, filterChain, 16, 16, AV_PIX_FMT_BGRA);
    if (graph) avfilter_graph_free(&graph);
    return ok;
}

struct VideoEffectProcessor::Impl {
    AVFilterGraph *graph = nullptr;
    AVFilterContext *srcCtx = nullptr;
    AVFilterContext *sinkCtx = nullptr;
    AVFrame *inFrame = nullptr;
    AVFrame *outFrame = nullptr;
    QString chain;
    int width = 0;
    int height = 0;
    int64_t pts = 0;
    bool chainBroken = false; // avoid retrying/re-warning every single frame

    ~Impl() { teardown(); }

    void teardown() {
        if (inFrame)  av_frame_free(&inFrame);
        if (outFrame) av_frame_free(&outFrame);
        if (graph)    avfilter_graph_free(&graph);
        srcCtx = sinkCtx = nullptr;
    }
};

VideoEffectProcessor::VideoEffectProcessor() : d(new Impl) {}
VideoEffectProcessor::~VideoEffectProcessor() = default;

QImage VideoEffectProcessor::process(const QImage &frame, const QString &filterChain)
{
    if (filterChain.isEmpty())
        return frame;

    const QImage src = frame.convertToFormat(QImage::Format_ARGB32);
    if (src.isNull())
        return frame;

    const bool needsRebuild = (filterChain != d->chain)
                            || (src.width() != d->width) || (src.height() != d->height);
    if (needsRebuild) {
        d->teardown();
        d->chain  = filterChain;
        d->width  = src.width();
        d->height = src.height();
        d->pts    = 0;
        d->chainBroken = !buildVideoFilterGraph(&d->graph, &d->srcCtx, &d->sinkCtx,
                                                 filterChain, d->width, d->height, AV_PIX_FMT_BGRA);
        if (d->chainBroken)
            qWarning() << "FFmpegNative: video effect chain failed to build, passing through:" << filterChain;
        else {
            d->inFrame  = av_frame_alloc();
            d->outFrame = av_frame_alloc();
        }
    }

    if (d->chainBroken)
        return frame;

    d->inFrame->format = AV_PIX_FMT_BGRA;
    d->inFrame->width  = d->width;
    d->inFrame->height = d->height;
    d->inFrame->pts    = d->pts++;
    if (av_frame_get_buffer(d->inFrame, 0) < 0) {
        av_frame_unref(d->inFrame);
        return frame;
    }
    av_image_copy_plane(d->inFrame->data[0], d->inFrame->linesize[0],
                         src.bits(), int(src.bytesPerLine()), d->width * 4, d->height);

    if (av_buffersrc_add_frame(d->srcCtx, d->inFrame) < 0) {
        av_frame_unref(d->inFrame);
        return frame;
    }
    av_frame_unref(d->inFrame);

    QImage result = frame;
    if (av_buffersink_get_frame(d->sinkCtx, d->outFrame) >= 0) {
        result = QImage(d->outFrame->data[0], d->width, d->height,
                         d->outFrame->linesize[0], QImage::Format_ARGB32).copy();
        av_frame_unref(d->outFrame);
    }
    return result;
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
                 const QString &rawVocalPath,
                 const std::atomic<bool> *cancelled,
                 std::function<void(double)> progressCb,
                 const QString &videoEffectChain)
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

    // ── Step 3: Mix vocals with untouched playback ──────────────────────────────
    // audioPath (tunedRecorded) already went through the audio-masterization
    // filter chain upstream, before VocalEnhancer ran on it, so no filtering
    // happens here — just mixing with the original, unaltered playback.
    const int mixLen = std::max(vocalPCM.size(), playbackPCM.size());
    if (mixLen <= 0) return false;
    QVector<float> mixedPCM(mixLen, 0.0f);
    for (int i = 0; i < mixLen; ++i) {
        const float v = (i < vocalPCM.size())    ? vocalPCM[i]    : 0.0f;
        const float p = (i < playbackPCM.size()) ? playbackPCM[i] : 0.0f;
        mixedPCM[i] = softClip(v + p);
    }
    vocalPCM.clear(); vocalPCM.squeeze();
    playbackPCM.clear(); playbackPCM.squeeze();

    // ── Step 4: Convert final mix to S16 ────────────────────────────────────────
    QVector<int16_t> finalAudio(mixedPCM.size());
    for (int i = 0; i < mixedPCM.size(); ++i)
        finalAudio[i] = int16_t(std::clamp(mixedPCM[i] * 32767.f, -32768.f, 32767.f));
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
    // libopus (and some other encoders) only accept a fixed set of sample
    // rates — 44100 isn't one of them, so pick the closest one it supports
    // (e.g. 48000) rather than failing avcodec_open2 below.
    audioEncCtx->sample_rate = pickSupportedSampleRate(audioEnc, 44100);
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
    audioEncCtx->time_base   = {1, audioEncCtx->sample_rate};
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

    // Resampler: S16 44100 Hz stereo (source PCM) → encoder's required sample format/rate
    SwrContext *encSwr = nullptr;
    if (audioEncCtx->sample_fmt != AV_SAMPLE_FMT_S16 || audioEncCtx->sample_rate != 44100) {
        AVChannelLayout stereo;
        av_channel_layout_from_mask(&stereo, AV_CH_LAYOUT_STEREO);
        swr_alloc_set_opts2(&encSwr,
                             &stereo, audioEncCtx->sample_fmt, audioEncCtx->sample_rate,
                             &stereo, AV_SAMPLE_FMT_S16, 44100,
                             0, nullptr);
        swr_init(encSwr);
        av_channel_layout_uninit(&stereo);
    }

    // FIFO for frame-size alignment
    AVAudioFifo *fifo = av_audio_fifo_alloc(audioEncCtx->sample_fmt, 2,
                                             std::max(1, audioEncCtx->frame_size));

    // ── Video encoder (if needed) ─────────────────────────────────────────────
    AVStream       *videoOutSt    = nullptr;
    AVCodecContext *videoEncCtx   = nullptr;
    SwsContext     *swsCtx        = nullptr;
    AVFrame        *videoEncFrame = nullptr;
    AVBufferRef    *vaapiDevCtx   = nullptr; // non-null when VAAPI hw encoder is active
    bool            vaapiEnabled  = false;
    AVPixelFormat   videoSwPixFmt = AV_PIX_FMT_YUV420P; // sw frame format fed to sws/encoder
    SwsContext     *vaapiConvCtx  = nullptr; // YUV420P→NV12 converter for VAAPI upload
    AVFrame        *vaapiNV12Frame = nullptr; // NV12 intermediate frame for VAAPI upload

    if (!audioOnlyOut) {
        // Try hardware encoders first (NVENC → VAAPI → V4L2 M2M), then fall
        // back to software H264/VP9.
        // The sw frame pipeline (sws_scale, paintPitchOverlay, videoEncFrame)
        // ALWAYS uses YUV420P — paintPitchOverlay assumes 3 separate planes and
        // would segfault if given NV12 (data[2] is nullptr in 2-plane formats).
        // VAAPI uses sw_format=YUV420P so av_hwframe_transfer_data converts
        // YUV420P→VAAPI internally; NVENC and V4L2 M2M accept YUV420P directly.
        struct HWCandidate { const char *name; AVHWDeviceType hwType; };
        static const HWCandidate kHW[] = {
            {"h264_nvenc",   AV_HWDEVICE_TYPE_NONE },
            {"h264_vaapi",   AV_HWDEVICE_TYPE_VAAPI},
            {"h264_v4l2m2m", AV_HWDEVICE_TYPE_NONE },
        };

        const AVCodec *videoEnc = nullptr;

        if (ext != "webm") {
            for (const auto &cand : kHW) {
                const AVCodec *enc = avcodec_find_encoder_by_name(cand.name);
                if (!enc) continue;

                AVBufferRef *devCtx = nullptr;
                if (cand.hwType != AV_HWDEVICE_TYPE_NONE &&
                    av_hwdevice_ctx_create(&devCtx, cand.hwType, nullptr, nullptr, 0) < 0)
                    continue;

                AVCodecContext *ctx = avcodec_alloc_context3(enc);
                ctx->width     = mainW;
                ctx->height    = mainH;
                ctx->time_base = {1, 90000};
                ctx->framerate = {30, 1};
                ctx->gop_size  = 12;
                ctx->bit_rate  = 5000000;
                if (outFmt->oformat->flags & AVFMT_GLOBALHEADER)
                    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

                if (devCtx) {
                    // VAAPI: hw frames pool. Intel iHD (and most VAAPI drivers)
                    // require NV12 surfaces for H264 encoding; YUV420P (I420)
                    // surfaces cause "encode issue 24" at runtime.
                    // The main sw pipeline stays in YUV420P; a separate conversion
                    // step (vaapiConvCtx) converts to NV12 before each hw upload.
                    AVBufferRef *framesRef = av_hwframe_ctx_alloc(devCtx);
                    auto *frCtx = reinterpret_cast<AVHWFramesContext*>(framesRef->data);
                    frCtx->format            = AV_PIX_FMT_VAAPI;
                    frCtx->sw_format         = AV_PIX_FMT_NV12;
                    frCtx->width             = mainW;
                    frCtx->height            = mainH;
                    frCtx->initial_pool_size = 20;
                    if (av_hwframe_ctx_init(framesRef) < 0) {
                        av_buffer_unref(&framesRef);
                        av_buffer_unref(&devCtx);
                        avcodec_free_context(&ctx);
                        continue;
                    }
                    ctx->pix_fmt       = AV_PIX_FMT_VAAPI;
                    ctx->hw_device_ctx = av_buffer_ref(devCtx);
                    ctx->hw_frames_ctx = framesRef;
                } else {
                    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
                }

                if (avcodec_open2(ctx, enc, nullptr) >= 0 && probeHWEncoderCtx(ctx)) {
                    videoEnc    = enc;
                    videoEncCtx = ctx;
                    if (devCtx) {
                        vaapiDevCtx  = devCtx;
                        vaapiEnabled = true;
                        // Pre-create the YUV420P→NV12 converter used per frame
                        vaapiConvCtx = sws_getContext(mainW, mainH, AV_PIX_FMT_YUV420P,
                                                       mainW, mainH, AV_PIX_FMT_NV12,
                                                       SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                        vaapiNV12Frame = av_frame_alloc();
                        vaapiNV12Frame->format = AV_PIX_FMT_NV12;
                        vaapiNV12Frame->width  = mainW;
                        vaapiNV12Frame->height = mainH;
                        av_frame_get_buffer(vaapiNV12Frame, 0);
                    }
                    qDebug() << "renderVideo: HW encoder selected:" << cand.name;
                    break;
                }
                qDebug() << "renderVideo: HW encoder probe failed, skipping:" << cand.name;
                avcodec_free_context(&ctx);
                if (devCtx) av_buffer_unref(&devCtx);
            }
        }

        // Software fallback: libx264 for H264 containers, libvpx-vp9 for WebM
        if (!videoEnc) {
            const AVCodecID vidCodecId = (ext == "webm") ? AV_CODEC_ID_VP9 : AV_CODEC_ID_H264;
            const AVCodec *enc = avcodec_find_encoder(vidCodecId);
            if (!enc) {
                qWarning() << "FFmpegNative::renderVideo: no video encoder found";
            } else {
                AVCodecContext *ctx = avcodec_alloc_context3(enc);
                ctx->width     = mainW;
                ctx->height    = mainH;
                ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
                ctx->time_base = {1, 90000};
                ctx->framerate = {30, 1};
                ctx->gop_size  = 12;
                ctx->bit_rate  = 5000000;
                if (vidCodecId == AV_CODEC_ID_H264)
                    av_opt_set(ctx->priv_data, "preset", "medium", 0);
                if (outFmt->oformat->flags & AVFMT_GLOBALHEADER)
                    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                if (avcodec_open2(ctx, enc, nullptr) >= 0) {
                    videoEnc    = enc;
                    videoEncCtx = ctx;
                    qDebug() << "renderVideo: software encoder (libx264/libvpx)";
                } else {
                    qWarning() << "FFmpegNative::renderVideo: cannot open video encoder";
                    avcodec_free_context(&ctx);
                }
            }
        }

        if (videoEnc && videoEncCtx) {
            videoOutSt = avformat_new_stream(outFmt, videoEnc);
            avcodec_parameters_from_context(videoOutSt->codecpar, videoEncCtx);
            videoOutSt->time_base = videoEncCtx->time_base;

            videoEncFrame = av_frame_alloc();
            videoEncFrame->format = videoSwPixFmt;
            videoEncFrame->width  = mainW;
            videoEncFrame->height = mainH;
            av_frame_get_buffer(videoEncFrame, 0);
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
            if (videoEncCtx)    avcodec_free_context(&videoEncCtx);
            if (videoEncFrame)  av_frame_free(&videoEncFrame);
            if (vaapiNV12Frame) av_frame_free(&vaapiNV12Frame);
            if (vaapiConvCtx)   sws_freeContext(vaapiConvCtx);
            if (vaapiDevCtx)    av_buffer_unref(&vaapiDevCtx);
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
        if (videoEncCtx)    avcodec_free_context(&videoEncCtx);
        if (videoEncFrame)  av_frame_free(&videoEncFrame);
        if (vaapiNV12Frame) av_frame_free(&vaapiNV12Frame);
        if (vaapiConvCtx)   sws_freeContext(vaapiConvCtx);
        if (vaapiDevCtx)    av_buffer_unref(&vaapiDevCtx);
        avformat_free_context(outFmt);
        return false;
    }

    bool wasCancelled = false;

    // Audio packets are collected here and written interleaved with video so
    // that players can seek to any position and find both streams together.
    // Writing all audio before any video (the naive approach) produces files
    // where players show no video from the start, or no audio after a seek.
    std::vector<AVPacket*> audioPacketQueue;
    size_t audioQueueIdx = 0;

    // Collect one encoded audio frame into audioPacketQueue (no writes yet).
    auto collectAudioPacket = [&](AVFrame *af) {
        if (avcodec_send_frame(audioEncCtx, af) < 0) return;
        while (true) {
            AVPacket *pkt = av_packet_alloc();
            if (avcodec_receive_packet(audioEncCtx, pkt) < 0) { av_packet_free(&pkt); break; }
            av_packet_rescale_ts(pkt, audioEncCtx->time_base, audioOutSt->time_base);
            pkt->stream_index = audioOutSt->index;
            audioPacketQueue.push_back(pkt);
        }
    };

    // Write buffered audio packets whose DTS is <= untilAV (AV_TIME_BASE units).
    auto drainAudioUpTo = [&](int64_t untilAV) {
        while (audioQueueIdx < audioPacketQueue.size()) {
            AVPacket *ap = audioPacketQueue[audioQueueIdx];
            const int64_t dtsAV = av_rescale_q(
                ap->dts != AV_NOPTS_VALUE ? ap->dts : ap->pts,
                audioOutSt->time_base, AV_TIME_BASE_Q);
            if (dtsAV > untilAV) break;
            av_write_frame(outFmt, ap);
            av_packet_free(&audioPacketQueue[audioQueueIdx++]);
        }
    };

    // Encode one video frame, draining buffered audio up to its DTS first.
    // VAAPI path: convert the YUV420P sw frame to NV12 (what Intel VAAPI needs),
    // then upload to a VAAPI surface via av_hwframe_transfer_data.
    auto flushVideoWithInterleave = [&](AVFrame *vframe) {
        AVFrame *encFrame = vframe;
        AVFrame *hwFrame  = nullptr;
        if (vframe && vaapiEnabled && vaapiConvCtx && vaapiNV12Frame) {
            sws_scale(vaapiConvCtx,
                      (const uint8_t*const*)vframe->data, vframe->linesize,
                      0, vframe->height,
                      vaapiNV12Frame->data, vaapiNV12Frame->linesize);
            vaapiNV12Frame->pts = vframe->pts;
            hwFrame = av_frame_alloc();
            if (av_hwframe_get_buffer(videoEncCtx->hw_frames_ctx, hwFrame, 0) < 0
                || av_hwframe_transfer_data(hwFrame, vaapiNV12Frame, 0) < 0) {
                av_frame_free(&hwFrame);
            } else {
                hwFrame->pts = vframe->pts;
                encFrame = hwFrame;
            }
        }
        if (avcodec_send_frame(videoEncCtx, encFrame) >= 0) {
            AVPacket *pkt = av_packet_alloc();
            while (avcodec_receive_packet(videoEncCtx, pkt) >= 0) {
                av_packet_rescale_ts(pkt, videoEncCtx->time_base, videoOutSt->time_base);
                pkt->stream_index = videoOutSt->index;
                const int64_t videoDtsAV = av_rescale_q(
                    pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts,
                    videoOutSt->time_base, AV_TIME_BASE_Q);
                drainAudioUpTo(videoDtsAV);
                av_write_frame(outFmt, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
        if (hwFrame) av_frame_free(&hwFrame);
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
                // Convert S16 → encoder format/rate
                const int maxOut = (int)swr_get_out_samples(encSwr, n);
                std::vector<std::vector<uint8_t>> planeBufs;
                std::vector<uint8_t*> ptrs;
                const bool planar = av_sample_fmt_is_planar(audioEncCtx->sample_fmt);
                const int planes = planar ? 2 : 1;
                // Packed (non-planar) formats interleave both channels into the
                // single plane, so that buffer needs 2x the per-channel size.
                const int samplesPerPlane = planar ? 1 : 2;
                for (int p = 0; p < planes; ++p) {
                    planeBufs.emplace_back((size_t)maxOut * samplesPerPlane * av_get_bytes_per_sample(audioEncCtx->sample_fmt) + 16);
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
            if (cancelled && cancelled->load()) { wasCancelled = true; break; }

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
                af->sample_rate = audioEncCtx->sample_rate;
                af->pts         = audioPts;
                av_channel_layout_copy(&af->ch_layout, &audioEncCtx->ch_layout);
                av_frame_get_buffer(af, 0);
                av_audio_fifo_read(fifo, reinterpret_cast<void**>(af->data), read);
                collectAudioPacket(af);
                av_frame_free(&af);
                audioPts += read;

                // Audio phase: 0–10% if video follows, 0–100% for audio-only output
                if (progressCb && totalDurSec > 0) {
                    const double frac = std::min(1.0, double(audioPts) / audioEncCtx->sample_rate / totalDurSec);
                    progressCb(audioOnlyOut ? frac : 0.1 * frac);
                }
            }
        }
        // Flush audio encoder
        collectAudioPacket(nullptr);
    }
    finalAudio.clear(); finalAudio.squeeze();

    // For audio-only output there is no video to interleave with, so write
    // all buffered audio packets now.
    if (audioOnlyOut || !videoEncCtx) {
        drainAudioUpTo(INT64_MAX);
        audioPacketQueue.clear();
    }

    // ── Step 7: Encode video ──────────────────────────────────────────────────
    if (videoEncCtx && videoOutSt && videoEncFrame) {
        QVector<PitchPoint> pitchData;
        if (!rawVocalPath.isEmpty())
            pitchData = analyzePitch(rawVocalPath);

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
            // Create swscale: webcam native format → YUV420P at mainW×mainH
            swsCtx = sws_getContext(webcamDec->width, webcamDec->height,
                                     webcamDec->pix_fmt,
                                     mainW, mainH,
                                     AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC, nullptr, nullptr, nullptr);

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

            // Keyframe-alignment correction: avformat_seek_file snaps to the nearest
            // keyframe BEFORE the requested position.  After PTS normalization the
            // first video frame lands at output PTS=0 but its content is from
            // T_kf_ms, while the audio was trimmed at exactly videoOffsetMs.
            // The difference (videoOffsetMs - T_kf_ms) must be added to every
            // video PTS so the streams are sample-accurate.
            // This is computed once the first decoded frame reveals firstSrcPts.
            int64_t seekCorrectionTb = 0; // set after first frame when videoOffsetMs > 0

            // Optional video effect (Vertigo, Technicolor, ...) — the graph is
            // built once here and reused for every frame; see
            // buildVideoFilterGraph() near the top of this file. Falls back to
            // rendering without the effect if the chain can't be built (e.g. a
            // frei0r plugin referenced by it isn't installed on this machine).
            AVFilterGraph *effectGraph = nullptr;
            AVFilterContext *effectSrcCtx = nullptr, *effectSinkCtx = nullptr;
            AVFrame *effectSrcFrame = nullptr, *effectOutFrame = nullptr;
            if (!videoEffectChain.isEmpty()) {
                if (buildVideoFilterGraph(&effectGraph, &effectSrcCtx, &effectSinkCtx,
                                           videoEffectChain, mainW, mainH, AV_PIX_FMT_YUV420P)) {
                    effectSrcFrame = av_frame_alloc();
                    effectOutFrame = av_frame_alloc();
                } else {
                    qWarning() << "FFmpegNative: video effect chain failed to build, rendering without it:"
                               << videoEffectChain;
                }
            }

            while (av_read_frame(webcamFmt, pkt) >= 0) {
                if (cancelled && cancelled->load()) { wasCancelled = true; av_packet_unref(pkt); break; }
                if (pkt->stream_index != webcamVidIdx) { av_packet_unref(pkt); continue; }
                if (avcodec_send_packet(webcamDec, pkt) < 0) { av_packet_unref(pkt); continue; }
                av_packet_unref(pkt);

                while (avcodec_receive_frame(webcamDec, frm) == 0) {
                    const int64_t srcPts = frm->pts;

                    // Record first valid PTS so we can zero-base all subsequent frames.
                    // After avformat_seek_file the first frame's PTS is non-zero; writing
                    // it directly while audio starts at 0 causes A/V desync.
                    if (firstSrcPts == AV_NOPTS_VALUE && srcPts != AV_NOPTS_VALUE) {
                        firstSrcPts = srcPts;
                        if (videoOffsetMs > 0) {
                            // T_kf_ms: where the keyframe actually landed (ms)
                            const int64_t T_kf_ms = static_cast<int64_t>(
                                firstSrcPts * av_q2d(inputTB) * 1000.0);
                            seekCorrectionTb = av_rescale_q(
                                videoOffsetMs - T_kf_ms,
                                AVRational{1, 1000},
                                videoEncCtx->time_base);
                            qDebug() << "FFmpegNative: seekTarget=" << videoOffsetMs
                                     << "ms  keyframeLanded=" << T_kf_ms
                                     << "ms  correction=" << (videoOffsetMs - T_kf_ms) << "ms";
                        }
                    }
                    const int64_t relPts = (srcPts != AV_NOPTS_VALUE && firstSrcPts != AV_NOPTS_VALUE)
                                          ? srcPts - firstSrcPts
                                          : AV_NOPTS_VALUE;

                    sws_scale(swsCtx,
                              (const uint8_t * const*)frm->data, frm->linesize,
                              0, webcamDec->height,
                              videoEncFrame->data, videoEncFrame->linesize);
                    av_frame_unref(frm);

                    if (effectGraph) {
                        av_frame_ref(effectSrcFrame, videoEncFrame);
                        effectSrcFrame->pts = fallbackPts; // graph only needs a monotonic pts
                        if (av_buffersrc_add_frame(effectSrcCtx, effectSrcFrame) >= 0
                            && av_buffersink_get_frame(effectSinkCtx, effectOutFrame) >= 0) {
                            av_frame_copy(videoEncFrame, effectOutFrame);
                            av_frame_unref(effectOutFrame);
                        }
                        av_frame_unref(effectSrcFrame);
                    }

                    if (!pitchData.isEmpty() && relPts != AV_NOPTS_VALUE) {
                        const int64_t frameMs = (int64_t)(av_q2d(inputTB) * double(relPts) * 1000.0);
                        const int64_t lookupMs = std::max<int64_t>(0, frameMs + audioOffsetMs);
                        paintPitchOverlay(videoEncFrame, lookupMs, pitchData);
                    }

                    videoEncFrame->pts = (relPts != AV_NOPTS_VALUE)
                        ? av_rescale_q(relPts, inputTB, videoEncCtx->time_base) + videoDelayTb + seekCorrectionTb
                        : fallbackPts + videoDelayTb + seekCorrectionTb;
                    fallbackPts = videoEncFrame->pts - videoDelayTb - seekCorrectionTb
                                  + av_rescale_q(1, inputTB, videoEncCtx->time_base);

                    flushVideoWithInterleave(videoEncFrame);

                    // Video = 10–100%: progress from normalized frame time / total audio duration
                    if (progressCb && totalDurSec > 0 && relPts != AV_NOPTS_VALUE) {
                        const double frameSec = av_q2d(inputTB) * double(relPts);
                        progressCb(0.1 + 0.9 * std::min(1.0, frameSec / totalDurSec));
                    }
                }
            }
            flushVideoWithInterleave(nullptr);

            if (effectSrcFrame) av_frame_free(&effectSrcFrame);
            if (effectOutFrame) av_frame_free(&effectOutFrame);
            if (effectGraph)    avfilter_graph_free(&effectGraph);

            av_frame_free(&frm);
            av_packet_free(&pkt);
            sws_freeContext(swsCtx);
        }

        if (webcamDec) avcodec_free_context(&webcamDec);
        if (webcamFmt) avformat_close_input(&webcamFmt);
    }

    // Write any audio that extends past the end of the video track.
    drainAudioUpTo(INT64_MAX);
    audioPacketQueue.clear();

    // ── Finalize ──────────────────────────────────────────────────────────────
    if (!wasCancelled)
        av_write_trailer(outFmt);

    if (!(outFmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outFmt->pb);

    av_audio_fifo_free(fifo);
    if (encSwr)         swr_free(&encSwr);
    avcodec_free_context(&audioEncCtx);
    if (videoEncCtx)    avcodec_free_context(&videoEncCtx);
    if (videoEncFrame)  av_frame_free(&videoEncFrame);
    if (vaapiNV12Frame) av_frame_free(&vaapiNV12Frame);
    if (vaapiConvCtx)   sws_freeContext(vaapiConvCtx);
    if (vaapiDevCtx)    av_buffer_unref(&vaapiDevCtx);
    avformat_free_context(outFmt);

    if (wasCancelled) {
        qDebug() << "FFmpegNative::renderVideo: aborted";
        return false;
    }
    if (progressCb) progressCb(1.0);
    qDebug() << "FFmpegNative::renderVideo: done →" << outputPath;
    return true;
}

} // namespace FFmpegNative

#endif // WAKKAQT_FFMPEG_NATIVE
