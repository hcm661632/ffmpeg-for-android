package com.hua.testlibs;

import android.Manifest;
import android.annotation.TargetApi;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import com.hua.nativeFFmpeg.NativeFFmpeg;

import java.io.File;
import java.io.IOException;

public class MainActivity extends Activity implements View.OnClickListener {

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib2");

//        System.loadLibrary("avcodec");
//        System.loadLibrary("avutil");  //这里自行体会, dlopen failed: "/data/app/com.hua.testlibs-7FzdC5gvJ7vjyX7e2rK2Ww==/lib/x86_64/libavutil.so" is 32-bit instead of 64-bit

//        System.loadLibrary("avutil-56");// dlopen failed: library "libm.so.6" not found
    }

    private NativeFFmpeg nativeFFmpeg;
    private Button btnDecodeAudio, btn_encodeAudio, btnReadMediaInfo, btnReadMediaMetadata,btn_encodeVideo;
    private ProgressBar audioEncodeProgressBar;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        TextView tv = findViewById(R.id.sample_text);
        tv.setText(stringFromJNI());

        nativeFFmpeg = new NativeFFmpeg();

        btnDecodeAudio = findViewById(R.id.btn_AudioDecode);
        btnDecodeAudio.setOnClickListener(this);
        btnReadMediaInfo = findViewById(R.id.btn_readMediaInfo);
        btnReadMediaInfo.setOnClickListener(this);
        btnReadMediaMetadata = findViewById(R.id.btn_readMediaMetadata);
        btnReadMediaMetadata.setOnClickListener(this);
        btn_encodeAudio = findViewById(R.id.btn_encodeAudio);
        btn_encodeAudio.setOnClickListener(this);
        audioEncodeProgressBar = findViewById(R.id.audioEncodeProgressBar);
        btn_encodeVideo = findViewById(R.id.btn_encodeVideo);
        btn_encodeVideo.setOnClickListener(this);


    }

    class HH extends AsyncTask<Void, Integer, Void> implements NativeFFmpeg.IAudioEncodeProgressListener {
        public HH(NativeFFmpeg.IAudioEncodeProgressListener listener) {
        }

        public HH() {
            nativeFFmpeg.registerAudioEncodeProcessListener(this);
        }

        @Override
        protected Void doInBackground(Void... voids) {
//            nativeFFmpeg.encodeAudioWithListener(new File("/sdcard/hh/xrdg.mp3"), this);
            nativeFFmpeg.encodeAudio(new File("/sdcard/hh/xrdg.mp3"),
                    64000, 44100, 3 /*At least 3*/,
                    2, this);

            return null;
        }

        @Override
        protected void onPreExecute() {
            super.onPreExecute();
        }

        @Override
        protected void onPostExecute(Void aVoid) {
            super.onPostExecute(aVoid);
        }

        @Override
        protected void onProgressUpdate(Integer... values) {
            audioEncodeProgressBar.setProgress(values[0]);

            super.onProgressUpdate(values);
        }



        @Override
        public void nowProgress(double progress) {
//            Log.d("HH","progress from Cpp " +(int) progress);
            publishProgress((int)progress);
        }

        @Override
        public void audioEncodeOver(boolean bAudioEncodeOver) {
            if(bAudioEncodeOver) {
                Log.d("HH","Down load over");

                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        Toast.makeText(MainActivity.this, "Audio Encode Over", Toast.LENGTH_SHORT).show();
                        Toast.makeText(MainActivity.this, "Audio Encode Over", Toast.LENGTH_SHORT).show();
                    }
                });
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        switch (requestCode) {
            case 0:
                if (grantResults[0] != PackageManager.PERMISSION_GRANTED) {
                    Toast.makeText(this, "You have no permission", Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(this, "You have granted the permission", Toast.LENGTH_SHORT).show();
                }
                break;
        }

        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();

    @TargetApi(Build.VERSION_CODES.M)
    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.btn_AudioDecode:
                if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED
                        || checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED
                        ) {
                    requestPermissions(new String[]{
                            Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE
                    }, 0);
                } else {
                    createAssignFile("/sdcard/hh/aout.mp3");
                    nativeFFmpeg.decodeAudio("/sdcard/hh/xrdg.mp3", "sdcard/hh/aout.mp3");
                }
                break;

            case R.id.btn_encodeAudio:
                createAssignFile("/sdcard/hh/encodeAudioDemo.mp3");

//                new Thread(new Runnable() {
//                    @Override
//                    public void run() {
//                        nativeFFmpeg.encodeAudioWhtiPthread("/sdcard/hh/encodeAudioDemo.mp3");
//                    }
//                }).start();

//                nativeFFmpeg.encodeAudioWhtiPthread("/sdcard/hh/encodeAudioDemo.mp3");

                new HH().execute();
                break;

            case R.id.btn_readMediaInfo:
                String s = nativeFFmpeg.readMediaInfo("/sdcard/hh/cjeg.mp3");
                btnReadMediaInfo.setText(s);

                break;


            case R.id.btn_readMediaMetadata:
                String mediaMetadata = nativeFFmpeg.readMediaMetadata("/sdcard/hh/xrdg.mp3");
                btnReadMediaMetadata.setText(mediaMetadata);

                break;

            case R.id.btn_encodeVideo:
                File videoFile = createAssignFile("/sdcard/hh/encodeVideoDemo.mp4");
                nativeFFmpeg.encodeVideo(videoFile,"mpeg4");
                break;
            default:
                break;
        }
    }

    private final File createAssignFile(String fileName) {
        final File outFile = new File(fileName);
        String absolutePath = outFile.getAbsolutePath();
        if (!outFile.exists()) try {
             outFile.createNewFile();
             return outFile;
        } catch (IOException e) {
            e.printStackTrace();
        }
        return outFile;
    }
}
