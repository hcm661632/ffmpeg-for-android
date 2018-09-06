//
// Created by FC5981 on 2018/9/4.
//


#include <jni.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "include/EncodeAVDemo.h"
#include "include/androidlog.h"

#define LOG_TAG "DecodeVideo_JNI"
#define INBUF_SIZE 4096
void printCodecInfo(AVCodec *pCodec);

const char *getEncodeFilePath(JNIEnv *env,  jobject outFile) {
    jclass jFileClass = env->GetObjectClass(outFile);

    jmethodID jgetAbsolutePathMethodID = env->GetMethodID(jFileClass, "getAbsolutePath",
                                                          "()Ljava/lang/String;");
    if (jgetAbsolutePathMethodID == NULL) {
        ALOGE("No such method");
        return "No such method";
    }
    jstring jstringPath = (jstring) env->CallObjectMethod(outFile, jgetAbsolutePathMethodID);
    ALOGI("%s\t, Line = %d\t,outFilePath = %s", __FUNCTION__, __LINE__, env->GetStringUTFChars(jstringPath, 0));
    return env->GetStringUTFChars(jstringPath, 0);
}


static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}


static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                   const char *filename)
{
    char buf[1024];
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);

    if (ret < 0) {
        ALOGE("Error sending a packet for decoding %s",av_err2str(ret));
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            ALOGE("Error during decoding %s",av_err2str(ret));
            exit(1);
        }

        ALOGD("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);

        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "%s-%d", filename, dec_ctx->frame_number);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
}



void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_decodeVideo(JNIEnv *env, jobject instance, jobject srcFile_,
                                                    jobject outFile) {
    const char *srcFile = getEncodeFilePath(env,  srcFile_);
    const char* outFileName = getEncodeFilePath(env,outFile);
    AVFormatContext *avFormatContext = avformat_alloc_context();
    int ret = avformat_open_input(&avFormatContext, srcFile, NULL, NULL);
    if (ret < 0) {
        ALOGE("Can not open input");
        return;
    }
    ret = avformat_find_stream_info(avFormatContext, NULL);
    if (ret < 0) {
        ALOGE("Can not find Stream Info");
        return;
    }

    int videoStream = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(videoStream < 0) {
        ALOGE("Can't find video stream in input file");
    }
    AVCodecParameters * avCodecParameters = avFormatContext->streams[videoStream]->codecpar;

    AVCodec *pAVCodec = avcodec_find_decoder(avCodecParameters->codec_id);
    if(!pAVCodec) {
        ALOGE("Can't find decoder");
    }



    AVCodecContext *pAVCodecContext = avcodec_alloc_context3(pAVCodec);
    if(!pAVCodecContext){
        ALOGE("Can't allocate decoder context");
    }
    ret = avcodec_parameters_to_context(pAVCodecContext, avCodecParameters);  // 成功返回 0
    if(ret){
        ALOGE("Can't copy decoder context");
        return;
    }
    ret = avcodec_open2(pAVCodecContext, pAVCodec, NULL);
    if (ret < 0) {
        ALOGE("Can not find Open Codec");
        return;
    }

    AVCodecParserContext *pAVCodecParserContext = av_parser_init(pAVCodec->id);

    AVFrame *pAVFrame = av_frame_alloc();
    if(!pAVFrame) {
        ALOGE("Can not allocate frame");
        return;
    }
    AVPacket * pkt = av_packet_alloc();
    if(!pkt) {
        ALOGE("Can not allocate packet");
        return;
    }
    FILE * f = fopen(srcFile,"rb");
    size_t data_size = 0;
    uint8_t * data;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    AVPacket *outPkt = av_packet_alloc();

    while (!feof(f)) {
        /* read raw data from the input file */
        data_size = fread(inbuf,1,INBUF_SIZE,f);
        if(!data_size) break;
        /* use the parser to split the data into frames */
        data = inbuf;

        while (data_size > 0) {
            ret = av_parser_parse2(pAVCodecParserContext,pAVCodecContext,&outPkt->data,&outPkt->size,data,data_size,
                             AV_NOPTS_VALUE,AV_NOPTS_VALUE,0);
            if(ret < 0) {
                ALOGE( "Error while parsing %s",av_err2str(ret));
                exit(1);
            }
            data      += ret;
            data_size -= ret;

            if (outPkt->size)
                decode(pAVCodecContext, pAVFrame, outPkt, outFileName);
        }

    }
    /* flush the decoder */
    decode(pAVCodecContext, pAVFrame, NULL, outFileName);

    fclose(f);
//    printCodecInfo(pAVCodec);
    avformat_close_input(&avFormatContext);
    av_parser_close(pAVCodecParserContext);
    avcodec_free_context(&pAVCodecContext);
    av_frame_free(&pAVFrame);
    av_packet_free(&pkt);
    av_packet_free(&outPkt);




}

void printCodecInfo(AVCodec *pCodec) {
    ALOGD("Name of the codec implementation %s",pCodec->name);
    ALOGD("enum AVMediaType %d",pCodec->type);
    ALOGD("enum AVCodecID %d",pCodec->id);
    ALOGD("capabilities %d",pCodec->capabilities);
    ALOGD("enum AVPixelFormat %d",pCodec->pix_fmts);
    ALOGD("supported_samplerates %d",pCodec->supported_samplerates);
    ALOGD("AVSampleFormat %d",pCodec->sample_fmts);
    ALOGD("channel_layouts %d",pCodec->channel_layouts);
}
