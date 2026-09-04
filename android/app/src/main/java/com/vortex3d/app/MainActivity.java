package com.vortex3d.app;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("vortex_android");
    }

    private static native String engineVersion();
    private static native long nativeCreateRenderer();
    private static native void nativeDestroyRenderer(long handle);
    private static native boolean nativeSurfaceCreated(long handle, Surface surface);
    private static native boolean nativeSurfaceChanged(long handle);
    private static native void nativeSurfaceDestroyed(long handle);
    private static native boolean nativeRenderFrame(long handle);
    private static native String nativeRendererInfo(long handle);

    private long rendererHandle;
    private boolean surfaceReady;
    private boolean frameLoopRunning;
    private TextView statusView;

    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            if (!frameLoopRunning) {
                return;
            }
            if (surfaceReady && rendererHandle != 0L && !nativeRenderFrame(rendererHandle)) {
                surfaceReady = false;
                frameLoopRunning = false;
                updateStatus();
                return;
            }
            Choreographer.getInstance().postFrameCallback(this);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        rendererHandle = nativeCreateRenderer();

        FrameLayout root = new FrameLayout(this);
        SurfaceView viewport = new SurfaceView(this);
        viewport.setKeepScreenOn(true);
        root.addView(
            viewport,
            new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        statusView = new TextView(this);
        statusView.setTextColor(Color.WHITE);
        statusView.setTextSize(12.0f);
        statusView.setPadding(18, 12, 18, 12);
        statusView.setBackgroundColor(0x66000000);
        FrameLayout.LayoutParams statusLayout = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT);
        statusLayout.gravity = Gravity.TOP | Gravity.START;
        root.addView(statusView, statusLayout);
        setContentView(root);

        viewport.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                surfaceReady = rendererHandle != 0L && nativeSurfaceCreated(rendererHandle, holder.getSurface());
                updateStatus();
                if (surfaceReady) {
                    startFrameLoop();
                }
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                if (surfaceReady && rendererHandle != 0L) {
                    surfaceReady = nativeSurfaceChanged(rendererHandle);
                    updateStatus();
                    if (!surfaceReady) {
                        stopFrameLoop();
                    }
                }
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                stopFrameLoop();
                if (rendererHandle != 0L) {
                    nativeSurfaceDestroyed(rendererHandle);
                }
                surfaceReady = false;
                updateStatus();
            }
        });

        updateStatus();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (surfaceReady) {
            startFrameLoop();
        }
    }

    @Override
    protected void onPause() {
        stopFrameLoop();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        stopFrameLoop();
        if (rendererHandle != 0L) {
            nativeDestroyRenderer(rendererHandle);
            rendererHandle = 0L;
        }
        super.onDestroy();
    }

    private void startFrameLoop() {
        if (frameLoopRunning) {
            return;
        }
        frameLoopRunning = true;
        Choreographer.getInstance().postFrameCallback(frameCallback);
    }

    private void stopFrameLoop() {
        if (!frameLoopRunning) {
            return;
        }
        frameLoopRunning = false;
        Choreographer.getInstance().removeFrameCallback(frameCallback);
    }

    private void updateStatus() {
        if (statusView == null) {
            return;
        }
        String rendererInfo = rendererHandle == 0L
            ? "Renderer allocation failed"
            : nativeRendererInfo(rendererHandle);
        statusView.setText(
            "Vortex3D DEV viewport v0.3\nEngine " + engineVersion() + "\n" + rendererInfo);
    }
}
