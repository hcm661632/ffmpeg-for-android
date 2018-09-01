#include <jni.h>

//#include "libavcodec/testhh.h"
#include "TestThirdLib.h"

#include <libavcodec/avcodec.h>

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

#define AVERROR_EOF                FFERRTAG( 'E','O','F',' ') ///< End of file
extern "C"
JNIEXPORT jstring
JNICALL
Java_com_hua_testlibs_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    const char *string = avcodec_configuration();

    char charArr[] = "lower characters to upper characters";
    TestThirdLib *testThirdLib = new TestThirdLib();
    testThirdLib->converToUpper(charArr);
    return env->NewStringUTF(string);
}


static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile) {
    int i, ch;
    int ret, data_size;
    /* send the packet with the compressed data to the decoder */
    ret = avcodec_send_packet(dec_ctx, pkt);  // 这里会出错，返回  AVERROR_INVALIDDATA
    if (ret < 0) {
        ALOGD("Error submitting the packet to the decoder  %s   [ LINE = ]%ld retVal = %d  ErrorInfo %s\n",__FUNCTION__,__LINE__,ret,strerror(errno));
        ALOGD("Error submitting the packet to the decoder  %s   [ LINE = ]%ld retVal = %d  ErrorInfo %s\n",__FUNCTION__,__LINE__,ret,av_err2str(ret));
        ALOGE("%d\n",AVERROR_INVALIDDATA);

        exit(1);
    }

    /* read all the output frames (in general there may be any number of them */
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            fprintf(stderr, "Failed to calculate data size\n");
            exit(1);
        }
        for (i = 0; i < frame->nb_samples; i++)
            for (ch = 0; ch < dec_ctx->channels; ch++)
                fwrite(frame->data[ch] + data_size * i, 1, data_size, outfile);
    }
}




// 尝试解码音频
extern "C"
JNIEXPORT void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_decodeAudio(JNIEnv *env, jobject instance, jstring fileName_,
                                                   jstring outFileName_) {
    const char *filename = env->GetStringUTFChars(fileName_, 0);
    const char *outfilename = env->GetStringUTFChars(outFileName_, 0);
    ALOGD("%s  Begin............",__FUNCTION__);
    const AVCodec *codec;
    AVCodecContext *c = NULL;
    AVCodecParserContext *parser = NULL;
    int len, ret;
    FILE *f, *outfile;
    uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    size_t data_size;
    AVPacket *pkt;
    AVFrame *decoded_frame = NULL;

    pkt = av_packet_alloc();

    /* find the MPEG audio decoder */
    codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
    if (!codec) {
        ALOGD("%s  Codec not found ............",__FUNCTION__);
        exit(1);
    }

    parser = av_parser_init(codec->id);
    if (!parser) {
        ALOGD("%s  Parser not found ............",__FUNCTION__);
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        ALOGD("%s Could not allocate audio codec context ............",__FUNCTION__);
        exit(1);
    }

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        ALOGD("%s Could not open codec  ............",__FUNCTION__);
        exit(1);
    }

    f = fopen(filename, "rb");
    if (!f) {
        ALOGD("Could not open .. %s",filename);
        exit(1);
    }
    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        ALOGD("fopen outputFile failed .. %s",__FUNCTION__);
        av_free(c);
        exit(1);
    }

    /* decode until eof */
    data = inbuf;
    data_size = fread(inbuf, 1, AUDIO_INBUF_SIZE, f);

    while (data_size > 0) {
        if (!decoded_frame) {
            if (!(decoded_frame = av_frame_alloc())) {
                ALOGD("Could not allocate audio frame  %s %d\n",__FUNCTION__,__LINE__);
                exit(1);
            }
        }

        ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                               data, data_size,
                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            ALOGD("Error while parsing  %s %d\n",__FUNCTION__,__LINE__);
            exit(1);
        }
        data += ret;
        data_size -= ret;

        if (pkt->size)
            decode(c, pkt, decoded_frame, outfile);

        if (data_size < AUDIO_REFILL_THRESH) {
            memmove(inbuf, data, data_size);
            data = inbuf;
            len = fread(data + data_size, 1,
                        AUDIO_INBUF_SIZE - data_size, f);
            if (len > 0)
                data_size += len;
        }
    }
    ALOGD("%s,%d",__FUNCTION__,__LINE__);
    /* flush the decoder */
    pkt->data = NULL;
    pkt->size = 0;
    decode(c, pkt, decoded_frame, outfile);

    fclose(outfile);
    fclose(f);

    avcodec_free_context(&c);
    av_parser_close(parser);
    av_frame_free(&decoded_frame);
    av_packet_free(&pkt);


    env->ReleaseStringUTFChars(fileName_, filename);
    env->ReleaseStringUTFChars(outFileName_, outfilename);

}

