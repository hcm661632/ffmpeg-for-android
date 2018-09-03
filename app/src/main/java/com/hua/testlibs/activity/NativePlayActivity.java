package com.hua.testlibs.activity;

import android.app.Activity;
import android.os.Bundle;
import android.view.SurfaceView;

import com.hua.testlibs.R;

public class NativePlayActivity extends Activity {
    SurfaceView surfaceView;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_video_encode);
        surfaceView = findViewById(R.id.surfaceView);
    }
}
