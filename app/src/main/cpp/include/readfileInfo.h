//
// Created by FC5981 on 2018/8/29.
//

#include <jni.h>

#ifndef TESTFFMPEGLIBS_READFILEINFO_H
#define TESTFFMPEGLIBS_READFILEINFO_H



#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jstring JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_readMediaInfo(JNIEnv *env, jobject instance,
                                                     jstring fileName_);


JNIEXPORT jstring JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_readMediaMetadata(JNIEnv *env, jobject instance,
                                                         jstring fileName_) ;


#ifdef __cplusplus
}
#endif


#endif //TESTFFMPEGLIBS_READFILEINFO_H