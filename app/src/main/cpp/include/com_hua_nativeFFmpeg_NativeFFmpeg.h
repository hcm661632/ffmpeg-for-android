/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_hua_nativeFFmpeg_NativeFFmpeg */

/**
 * Create by
 * D:\MyDocument\ASProject\Testffmpeglibs\app\build\intermediates\classes\debug>
 * javah -classpath D:\MyDocument\AndroidEnvironMent\HHSDK\sdk\platforms\android-26\android.jar; -jni com.hua.nativeFFmpeg.NativeFFmpeg
 */

#ifndef _Included_com_hua_nativeFFmpeg_NativeFFmpeg
#define _Included_com_hua_nativeFFmpeg_NativeFFmpeg
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     com_hua_nativeFFmpeg_NativeFFmpeg
 * Method:    decodeAudio
 * Signature: (Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_hua_nativeFFmpeg_NativeFFmpeg_decodeAudio
  (JNIEnv *, jobject, jstring, jstring);

/*
 * Class:     com_hua_nativeFFmpeg_NativeFFmpeg
 * Method:    readMediaInfo
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_hua_nativeFFmpeg_NativeFFmpeg_readMediaInfo
  (JNIEnv *, jobject, jstring);

/*
 * Class:     com_hua_nativeFFmpeg_NativeFFmpeg
 * Method:    readMediaMetadata
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_hua_nativeFFmpeg_NativeFFmpeg_readMediaMetadata
  (JNIEnv *, jobject, jstring);

/*
 * Class:     com_hua_nativeFFmpeg_NativeFFmpeg
 * Method:    encodeAudio
 * Signature: (Ljava/io/File;IIII)V
 */
JNIEXPORT void JNICALL Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudio
  (JNIEnv *, jobject, jobject, jint, jint, jint, jint);

#ifdef __cplusplus
}
#endif
#endif
