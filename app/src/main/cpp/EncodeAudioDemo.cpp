//
// Created by FC5981 on 2018/8/31.
//

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include "include/EncodeAudioDemo.h"
#include "include/androidlog.h"

jobject globalFFmpegRef;
jobject globalAudioEncodeListener;

JavaVM *global_VM;

const char *getEncodeFilePath(JNIEnv *env, const jobject outFile);

static void *
beginEncodeAudio(AVCodecContext *avCodecContext, AVFrame *frame, AVPacket *pkt, FILE *f);

void createNativeThread(void *);


typedef struct encodeAudio {
    AVCodecContext *avCodecContext;
    AVFrame *frame;
    AVPacket *pkt;
    FILE *f;
} encodeAudio;


static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sampleFormat) {
    const AVSampleFormat *sample_fmts = codec->sample_fmts; // 所有支持的SampleFormat
    while (*sample_fmts != AV_SAMPLE_FMT_NONE) {
        if (sampleFormat == *sample_fmts) {
            return 1;
        }
        sample_fmts++;
    }
    return 0;
}


/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec *codec) {
    const int *p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}


/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec *codec) {
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}


static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *output) {
    int ret;

    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }

        fwrite(pkt->data, 1, pkt->size, output);
        av_packet_unref(pkt);
    }
}

static void *encodeAudioMethod(void *argc) {
    ALOGM("Thread start");
    struct encodeAudio *audio = (struct encodeAudio *) argc;

    beginEncodeAudio(audio->avCodecContext,
                     audio->frame,
                     audio->pkt,
                     audio->f);


    fclose(audio->f);

    av_frame_free(&audio->frame);
    av_packet_free(&audio->pkt);
    avcodec_free_context(&audio->avCodecContext);

    pthread_exit(NULL);
}


static void *
beginEncodeAudio(AVCodecContext *avCodecContext, AVFrame *frame, AVPacket *pkt, FILE *f) {
    int t = 0, ret = 0;
    int tincr = 2 * M_PI * 440.0 / avCodecContext->sample_rate;

    //获取当前native线程是否有没有被附加到jvm环境中
    ALOGE("Begin Encode Audio");
    jint nativeThreadAttatch = JNI_FALSE;
    JNIEnv *globalEnv;
    jint getEnv = global_VM->GetEnv((void **) &globalEnv, JNI_VERSION_1_6);
    ALOGE("tid = %d,getEnv = %d\n", gettid(), getEnv);   // 这里确实是 -2 ，没有被加到当前线程中
    if (getEnv == JNI_EDETACHED) {
        ALOGE("Current native thread has not attach to jvm");
        if (global_VM->AttachCurrentThread(&globalEnv, NULL) == 0) {
            nativeThreadAttatch = JNI_TRUE;
        } else {
            exit(0);
        }
    }
    jclass ffmpegClass = (globalEnv)->GetObjectClass(globalFFmpegRef);
    jclass listenerClass = globalEnv->GetObjectClass(globalAudioEncodeListener);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = (globalEnv)->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    jmethodID callbackMethod = globalEnv->GetMethodID(listenerClass, "nowProgress", "(I)V");
    if (methodID == NULL || callbackMethod == NULL) {
        ALOGE("No such method");
        global_VM->DetachCurrentThread();
    }

    for (int i = 0; i < 200; ++i) {
        /* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            ALOGE("av_frame_make_writable");
            exit(1);
        }
        usleep(1000 * 100); // 100ms   , total = 100ms * 200 = 20s , ANR
        uint16_t *samples = (uint16_t *) frame->data[0];
        for (int j = 0; j < avCodecContext->frame_size; ++j) {
            samples[2 * j] = sin(t) * 10000;

            for (int k = 1; k < avCodecContext->channels; ++k) {
                samples[2 * j + k] = samples[2 * j];
            }
            t += tincr;
        }
        encode(avCodecContext, frame, pkt, f);
        // Callback the progress
        (globalEnv)->CallVoidMethod(globalFFmpegRef, methodID, i);
        globalEnv->CallVoidMethod(globalAudioEncodeListener, callbackMethod, i);
    }

    if (nativeThreadAttatch) {
        // 释放全局引用
        globalEnv->DeleteGlobalRef(globalFFmpegRef);
        globalEnv->DeleteGlobalRef(globalAudioEncodeListener);
        globalEnv = NULL;
        global_VM->DetachCurrentThread();

    }
    // flush the encoder
    encode(avCodecContext, NULL, pkt, f);
    ALOGM("######################  END #########################");
    pthread_exit(NULL);
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudio222(JNIEnv *env, jobject instance,
                                                      jobject outFile,
                                                      jint bitRate, jint sampleRate,
                                                      jint best_ch_layout, jint channels) {

    jclass ffmpegClass = env->GetObjectClass(instance);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = env->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    if (methodID == NULL) {
        ALOGE("No such method");
    }
    env->CallVoidMethod(instance, methodID, 123);


    jclass listener = env->FindClass(
            "com/hua/nativeFFmpeg/NativeFFmpeg$IAudioEncodeProgressListener");
    if (listener == NULL) {
        ALOGE("No such class");
        return;
    }
    jmethodID callbackMethod = env->GetMethodID(listener, "nowProgress", "(I)V");
    if (callbackMethod == NULL) {
        ALOGE("callbackMethod NULL");
    }
    //JNI DETECTED ERROR IN APPLICATION: can't call void com.hua.nativeFFmpeg.NativeFFmpeg$IAudioEncodeProgressListener.nowProgress(int) on instance of com.hua.nativeFFmpeg.NativeFFmpeg
}

void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudioWithListener(JNIEnv *env, jobject instance,
                                                               jobject outFile,
                                                               jobject listenerobj) {
    jclass ffmpegClass = env->GetObjectClass(instance);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = env->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    if (methodID == NULL) {
        ALOGE("No such method");
    }
    env->CallVoidMethod(instance, methodID, 123);


//    jclass listener = env->FindClass("com/hua/nativeFFmpeg/NativeFFmpeg$IAudioEncodeProgressListener");
    jclass listener = env->GetObjectClass(listenerobj);
    if (listener == NULL) {
        ALOGE("No such class");
        return;
    }
    jmethodID callbackMethod = env->GetMethodID(listener, "nowProgress", "(I)V");
    if (callbackMethod == NULL) {
        ALOGE("No such Method");
        return;
    }
    for (int i = 0; i < 100; i++) {
        env->CallVoidMethod(listenerobj, callbackMethod, i);
        usleep(1000 * 50);
    }
}

void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudio(JNIEnv *env, jobject instance, jobject outFile,
                                                   jint bitRate, jint sampleRate,
                                                   jint best_ch_layout, jint channels,
                                                   jobject listenerobj) {
    env->GetJavaVM(&global_VM);
    globalFFmpegRef = env->NewGlobalRef(instance);
    globalAudioEncodeListener = env->NewGlobalRef(listenerobj);


    ALOGM("%d", __DATE__);
    const char *filename = getEncodeFilePath(env, outFile);
    const AVCodec *avCodec;
    AVCodecContext *avCodecContext = NULL;
    AVFrame *frame;
    AVPacket *pkt;
    int ret;
    FILE *f;
    uint16_t *samples;
    float t, tincr;

//     Find the MP2/MP3 encoder
    avCodec = avcodec_find_encoder(AV_CODEC_ID_MP2);
    if (!avCodec) {
        ALOGE("%s", "Codec not found");
        exit(1);
    }
    avCodecContext = avcodec_alloc_context3(avCodec);
    if (!avCodecContext) {
        ALOGE("%s", "Could not allocate audio codec context");
        exit(1);
    }

    // 这个有什么用呢， SAMPLE_FMT_S16
    avCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
    if (!check_sample_fmt(avCodec, avCodecContext->sample_fmt)) {
        ALOGE("Encoder does not support sample format %s",
              av_get_sample_fmt_name(avCodecContext->sample_fmt));
        exit(1);
    }
//     select other audio parameters supported by the encoder

    avCodecContext->sample_rate = sampleRate; // select_sample_rate(avCodec);//sampleRate;
    avCodecContext->bit_rate = bitRate;
    avCodecContext->channel_layout = best_ch_layout;//select_channel_layout(avCodec); //best_ch_layout;
    avCodecContext->channels = channels;//av_get_channel_layout_nb_channels(avCodecContext->channel_layout);

//    Open it
    if (avcodec_open2(avCodecContext, avCodec, NULL) < 0) {
        ALOGE("Could not open codec");
        exit(1);
    }

    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        ALOGE("Could not open %s", filename);
        exit(1);
    }
    //" wb "  write binary
    f = fdopen(fd, "wb");

    /* packet for holding encoded output */
    pkt = av_packet_alloc();
    if (!pkt) {
        ALOGE("Could not av_packet_alloc");
        exit(1);
    }
    frame = av_frame_alloc();
    if (!frame) {
        ALOGE("Could not av_frame_alloc");
        exit(1);
    }
//    select other audio parameters supported by the encoder
    frame->nb_samples = avCodecContext->frame_size;
    frame->channel_layout = avCodecContext->channel_layout;
    frame->format = avCodecContext->sample_fmt;

    // allocate the data buffers
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        ALOGE("Could not allocate audio data buffers %s", av_err2str(ret));
        exit(1);
    }

    struct encodeAudio *encodeAudio1 = new encodeAudio;
    encodeAudio1->avCodecContext = avCodecContext;
    encodeAudio1->frame = frame;
    encodeAudio1->pkt = pkt;
    encodeAudio1->f = f;


    //TODO  创建线程去完成耗时的编码
    createNativeThread(encodeAudio1);
#if 0
    beginEncodeAudio(avCodecContext, frame, pkt, f);

    fclose(f);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&avCodecContext);
#endif
}

void createNativeThread(void *encodeAudio) {
    pthread_t encodeAudioThread;
    pthread_create(&encodeAudioThread, NULL, encodeAudioMethod, encodeAudio);
}

const char *getEncodeFilePath(JNIEnv *env, const jobject outFile) {
    jclass jFileClass = env->GetObjectClass(outFile);

    jmethodID jgetAbsolutePathMethodID = env->GetMethodID(jFileClass, "getAbsolutePath",
                                                          "()Ljava/lang/String;");
    if (jgetAbsolutePathMethodID == NULL) {
        ALOGE("No such method");
        return "No such method";
    }
    jstring jstringPath = (jstring) env->CallObjectMethod(outFile, jgetAbsolutePathMethodID);
    ALOGI("%s\t, Line = %d\t,outFilePath = %s", __FUNCTION__, __LINE__,
          env->GetStringUTFChars(jstringPath, 0));
    return env->GetStringUTFChars(jstringPath, 0);
}