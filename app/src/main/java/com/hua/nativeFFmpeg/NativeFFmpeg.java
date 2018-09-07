package com.hua.nativeFFmpeg;

import android.util.Log;
import android.view.Surface;

import java.io.File;

/**
 * Created by FC5981 on 2018/8/28.
 */

public class NativeFFmpeg {

    public native void decodeAudio(String fileName,String outFileName);
    public native String readMediaInfo(String fileName);
    public native String readMediaMetadata(String fileName);

    /**
     *
     * @param outFile                 编码后输出到指定文件 /sdcard/hh/encodeAudioDemo.mp3
     * @param bitRate                 编码音频文件的比特率 64
     * @param sampleRate              编码音频文件的采样率 44100
     * @param best_ch_layout          编码音频文件的音轨数  2
     * @param channels                编码音频文件的通道数，1 2
     */
    public native void encodeAudio(File outFile,int bitRate,int sampleRate,int best_ch_layout,int channels,IAudioEncodeProgressListener listener);
    public native void encodeAudioWithListener(File outFile,IAudioEncodeProgressListener listener);
    public native void encodeAudioWhtiPthread(String filePath);
    public native void nativePlay(String fileName,Surface surface);
    public native void nativePlayStop(boolean stop);

    public native void encodeVideo(File outFile,final String codecName);
    public native void decodeVideo(File srcFile,File outFile);
    public native void filteringVideo(String fileName);


    /**
     *  用于演示普通的回调，也就是找方法而已
     * @param processPercent
     */
    public void getEncodeProcess(int processPercent){

    }


    public IAudioEncodeProgressListener maudioEncodeProgressListener;

    public void registerAudioEncodeProcessListener(IAudioEncodeProgressListener audioEncodeProgressListener){
        Log.d("HH","registerAudioEncodeProcessListener ");
        maudioEncodeProgressListener = audioEncodeProgressListener;
    }

    public interface IAudioEncodeProgressListener{
        void nowProgress(double progress);
        void audioEncodeOver(boolean bAudioEncodeOver);
    }





















    public void testListener(){
        for (int i = 0; i< 200; i++) {
            try {
                Thread.sleep(50);
                maudioEncodeProgressListener.nowProgress(i);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }
}
