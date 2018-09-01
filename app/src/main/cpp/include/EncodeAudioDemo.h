//
// Created by FC5981 on 2018/8/31.
//

#ifndef TESTFFMPEGLIBS_ENCODEAUDIODEMO_H
#define TESTFFMPEGLIBS_ENCODEAUDIODEMO_H


#include <jni.h>


#ifdef __cplusplus
extern "C" {
#endif

void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudio(JNIEnv *env, jobject instance, jobject outFile,
                                                   jint bitRate, jint sampleRate,
                                                   jint best_ch_layout, jint channels,jobject listenerobj);



void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudioWithListener(JNIEnv *env, jobject instance,
                                                               jobject outFile, jobject listener);

#ifdef __cplusplus
}
#endif

#endif //TESTFFMPEGLIBS_ENCODEAUDIODEMO_H
