/*
 * FfmpegDemux.cpp
 *
 *  Created on: 2011-12-8
 *      Author: Liang Guangwei
 */

#include "BasicUsageEnvironment.hh"
#include "liveMedia.hh"
#include "ffmpeg_demuxed_elementary_stream.h"
#include "subsession/ffmpeg_server_media_subsession.h"
#include "ffmpeg_demux.h"
#include "ffmpeg_server_demux.h"
#include <iostream>
#include <cstdio>
#include <cstring>
extern "C" {
#include "libavutil/avutil.h"
#include "libavformat/avformat.h"
}

#include "NIOAndroidLog.h"

/*
 * just for compatible with the old ffmpeg version
 */
#define AVMEDIA_TYPE_VIDEO 0
#define AVMEDIA_TYPE_AUDIO 1

//#define EX_DEBUG

void av_dump_format_(AVFormatContext *ic, int index,
                    const char *url, int is_output)
{
    int i;
    uint8_t *printed = ic->nb_streams ? (uint8_t*)av_mallocz(ic->nb_streams) : NULL;
    if (ic->nb_streams && !printed)
        return;

    ALOGE("%s #%d, %s, %s '%s':\n",
           is_output ? "Output" : "Input",
           index,
           is_output ? ic->oformat->name : ic->iformat->name,
           is_output ? "to" : "from", url);

    if (!is_output) {
        av_log(NULL, AV_LOG_INFO, "  Duration: ");
        if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
            secs  = duration / AV_TIME_BASE;
            us    = duration % AV_TIME_BASE;
            mins  = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            ALOGE("%02d:%02d:%02d.%02d", hours, mins, secs,
                   (100 * us) / AV_TIME_BASE);
        } else {
            av_log(NULL, AV_LOG_INFO, "N/A");
        }
        if (ic->start_time != AV_NOPTS_VALUE) {
            int secs, us;
            ALOGE("start: ");
            secs = llabs(ic->start_time / AV_TIME_BASE);
            us   = llabs(ic->start_time % AV_TIME_BASE);
            ALOGE("%s%d.%06d",
                   ic->start_time >= 0 ? "" : "-",
                   secs,
                   (int) av_rescale(us, 1000000, AV_TIME_BASE));
        }
        ALOGE("bitrate: ");
        if (ic->bit_rate)
            ALOGE("%lld kb/s",(int64_t)ic->bit_rate / 1000);
        else
            ALOGE("N/A");
    }

    for (i = 0; i < ic->nb_chapters; i++) {
        AVChapter *ch = ic->chapters[i];
        ALOGE("Chapter #%d:%d: ", index, i);
        ALOGE("start %f, ", ch->start * av_q2d(ch->time_base));
        ALOGE("end %f\n", ch->end * av_q2d(ch->time_base));
    }

    if (ic->nb_programs) {
        int j, k, total = 0;
        for (j = 0; j < ic->nb_programs; j++) {
            AVDictionaryEntry *name = av_dict_get(ic->programs[j]->metadata,
                                                  "name", NULL, 0);
            ALOGE("Program %d %s\n", ic->programs[j]->id,
                   name ? name->value : "");
            for (k = 0; k < ic->programs[j]->nb_stream_indexes; k++) {
                printed[ic->programs[j]->stream_index[k]] = 1;
            }
            total += ic->programs[j]->nb_stream_indexes;
        }
        if (total < ic->nb_streams)
            ALOGE("  No Program\n");
    }

    for (i = 0; i < ic->nb_streams; i++)
        if (!printed[i])

    av_free(printed);
}

FfmpegServerDemux *FfmpegServerDemux::CreateNew(UsageEnvironment& env,
        char const* filename, Boolean reuse_source) {
    return new FfmpegServerDemux(env, filename, reuse_source);
}

FfmpegServerDemux::~FfmpegServerDemux() {
    Medium::close(session0_demux_);
    delete[] filename_;

    for (int i = 0; i < MAX_STREAM_NUM; ++i) {
        delete[] stream_[i].extra_data;
    }
}

FfmpegServerDemux::FfmpegServerDemux(UsageEnvironment& env,
        char const* file_name, Boolean reuse_source) :
    Medium(env), reuse_source_(reuse_source) {
    filename_ = strDup(file_name);

    session0_demux_ = NULL;
    last_created_demux_ = NULL;
    last_client_session_id_ = ~0;

    video_stream_id_ = -1;
    audio_stream_id_ = -1;

    std::memset(&stream_[0], 0, sizeof(StreamInfo) * MAX_STREAM_NUM);
    for (int i = 0; i < MAX_STREAM_NUM; ++i) {

        stream_[i].codec_id = AV_CODEC_ID_NONE;
        stream_[i].channels = 1;
    }
}

FfmpegDemuxedElementaryStream *FfmpegServerDemux::NewElementaryStream(
        unsigned client_session_id, u_int8_t stream_id) {
    FfmpegDemux* demux_to_use = NULL;

    if (client_session_id == 0) {
        // 'Session 0' is treated especially, because its audio & video streams
        // are created and destroyed one-at-a-time, rather than both streams being
        // created, and then (later) both streams being destroyed (as is the case
        // for other ('real') session ids).  Because of this, a separate demux is
        // used for session 0, and its deletion is managed by us, rather than
        // happening automatically.
        if (session0_demux_ == NULL) {
            session0_demux_ = FfmpegDemux::CreateNew(envir(), filename_, False);
        }
        demux_to_use = session0_demux_;
    } else {
        // First, check whether this is a new client session.  If so, create a new
        // demux for it:
        if (client_session_id != last_client_session_id_) {
            last_created_demux_ = FfmpegDemux::CreateNew(envir(), filename_, True);
            // Note: We tell the demux to delete itself when its last
            // elementary stream is deleted.
            last_client_session_id_ = client_session_id;
            // Note: This code relies upon the fact that the creation of streams for
            // different client sessions do not overlap - so one "MPEG1or2Demux" is used
            // at a time.
        }
        demux_to_use = last_created_demux_;
    }

    if (demux_to_use == NULL)
        return NULL; // shouldn't happen

    return demux_to_use->NewElementaryStream(stream_id,
            stream_[stream_id].mine_type, stream_[stream_id].duration);
}

ServerMediaSubsession *FfmpegServerDemux::NewAudioServerMediaSubsession() {
    return NewServerMediaSubsession(AVMEDIA_TYPE_AUDIO);
}

ServerMediaSubsession *FfmpegServerDemux::NewVideoServerMediaSubsession() {
    return NewServerMediaSubsession(AVMEDIA_TYPE_VIDEO);
}

ServerMediaSubsession *FfmpegServerDemux::NewServerMediaSubsession(
        unsigned int type) {
    ServerMediaSubsession *sms = NULL;
    int stream_id = -1;

    //first time, we should found video and audio stream
    if (video_stream_id_ == -1) {
        if (!DetectedStream()) {
            return NULL;
        }
    }

    if (type == AVMEDIA_TYPE_VIDEO) {
        stream_id = video_stream_id_;
    } else {
        stream_id = audio_stream_id_;
    }

    //now, create subsessions
    switch (stream_[stream_id].codec_id) {
    case AV_CODEC_ID_H264:
        stream_[stream_id].mine_type = "video/MPEG";
        sms = FfmpegH264ServerMediaSubsession::CreateNew(*this, stream_id,
                False);
        break;

    case AV_CODEC_ID_MP3:
        stream_[stream_id].mine_type = "audio/MPEG";
        //every mp3 frame contains 1152 samales
        stream_[stream_id].duration = (1152 * 1000000)
                / stream_[stream_id].sample_rate;

        stream_[stream_id].duration = stream_[stream_id].duration;

        sms = FfmpegMp3ServerMediaSubsession::CreateNew(*this, stream_id, False);

        break;

    case AV_CODEC_ID_AAC:
        stream_[stream_id].mine_type = "audio/MPEG";
        //every aac frame contains 1024 sampales
        stream_[stream_id].duration = (1024 * 1000000)
                / stream_[stream_id].sample_rate;

        stream_[stream_id].duration = stream_[stream_id].duration;

        sms = FfmpegAACServerMediaSubession::CreateNew(*this, stream_id, False);
        break;

    case AV_CODEC_ID_MPEG4:
        stream_[stream_id].mine_type = "video/MPEG";
        sms = FfmpegMPEG4ServerMediaSubsession::CreateNew(*this, stream_id,
                False);
        break;
        //TODO: create other subsessions

    default:
        //can not find required stream
        envir() << "can not find video or audio stream\n";
    }
    return sms;
}

Boolean FfmpegServerDemux::DetectedStream() {
    struct AVFormatContext *format_ctx = NULL;

    av_register_all();

    //open file
    if (avformat_open_input (&format_ctx, filename_, NULL, NULL) != 0) {
        return False;
    }

    //find stream
    if (avformat_find_stream_info(format_ctx,NULL) < 0) {
        return False;
    }

    av_dump_format_(format_ctx, 0, filename_, 0);

    AVCodecContext *codec = NULL;

    //find first video stream
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        codec = format_ctx->streams[i]->codec;
        if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            //            video_tag_ = codec->codec_id; // codec->codec_tag;
            video_stream_id_ = i;

            //video sream information
            stream_[i].codec_id = codec->codec_id;
            stream_[i].extra_data_size = codec->extradata_size;
            stream_[i].extra_data
                    = new unsigned char[stream_[i].extra_data_size + 1];
            std::memcpy(stream_[i].extra_data, codec->extradata,
                    codec->extradata_size);
            stream_[i].extra_data[stream_[i].extra_data_size] = 0;

            int framerate = format_ctx->streams[i]->r_frame_rate.num
                    / format_ctx->streams[i]->r_frame_rate.den;
            stream_[i].duration = 1000000 / framerate;
            stream_[i].duration = format_ctx->duration / 1000000;

#ifdef EX_DEBUG
            envir() << "video stream information:\n";
            envir() << "stream :" << i << "\n";
            envir() << "frame rate:" << framerate << "\n";
            envir() << "duration : " << stream_[i].duration << "\n";
#endif

            break;
        }
    }

    //find first audio stream
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        codec = format_ctx->streams[i]->codec;
        if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            //            audio_tag_ = codec->codec_id; // codec->codec_tag;
            audio_stream_id_ = i;

            //audio stream information
            stream_[i].codec_id = codec->codec_id;
            stream_[i].channels = codec->channels;
            stream_[i].sample_rate = codec->sample_rate;
            stream_[i].extra_data_size = codec->extradata_size;
            stream_[i].extra_data
                    = new unsigned char[stream_[i].extra_data_size + 1];
            memcpy(stream_[i].extra_data, codec->extradata,
                    codec->extradata_size);
            stream_[i].extra_data[stream_[i].extra_data_size] = 0;
            //duration set later

            stream_[i].duration = format_ctx->duration / 1000000;

#ifdef EX_DEBUG
            envir() << "audio stream information:\n";
            envir() << "stream No.:" << i << "\n";
            envir() << "channels: " << stream_[i].channels << "\n";
            envir() << "sample rate:  " << stream_[i].sample_rate << "\n";
#endif

            break;
        }
    }

    //close file
    avformat_close_input(&format_ctx);

    return True;
}

char const* FfmpegServerDemux::MIMEtype(int stream_id) {
    return stream_[stream_id].mine_type;
}

const StreamInfo* FfmpegServerDemux::GetStreamInfo(int stream_id) {
    return &stream_[stream_id];
}

