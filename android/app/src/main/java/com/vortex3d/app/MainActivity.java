package com.vortex3d.app;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("vortex_android");
    }

    private static native String engineVersion();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setTextSize(22.0f);
        status.setText("Vortex3D native engine " + engineVersion() + "\nARMv7 + ARM64 shell online");
        setContentView(status);
    }
}
