package com.vortex3d.app;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static {
        System.loadLibrary("vortex_android");
    }

    private static final int TWO_FINGER_UNDECIDED = 0;
    private static final int TWO_FINGER_PAN = 1;
    private static final int TWO_FINGER_ZOOM = 2;

    private static native String engineVersion();
    private static native long nativeCreateRenderer();
    private static native void nativeDestroyRenderer(long handle);
    private static native boolean nativeSurfaceCreated(long handle, Surface surface);
    private static native boolean nativeSurfaceChanged(long handle);
    private static native void nativeSurfaceDestroyed(long handle);
    private static native boolean nativeRenderFrame(long handle);
    private static native boolean nativeOrbitCamera(long handle, float deltaX, float deltaY);
    private static native boolean nativePanCamera(long handle, float deltaX, float deltaY);
    private static native boolean nativeZoomCamera(long handle, float scaleFactor);
    private static native boolean nativeTapViewport(long handle, float x, float y);
    private static native String nativeRendererInfo(long handle);

    private long rendererHandle;
    private boolean surfaceReady;
    private boolean frameLoopRunning;
    private TextView statusView;

    private int gesturePointerCount;
    private int twoFingerMode = TWO_FINGER_UNDECIDED;
    private float touchSlop;
    private float oneFingerDownX;
    private float oneFingerDownY;
    private float lastTouchX;
    private float lastTouchY;
    private boolean oneFingerOrbiting;
    private boolean multiTouchOccurred;
    private float lastCentroidX;
    private float lastCentroidY;
    private float lastSpan;
    private float twoFingerStartCentroidX;
    private float twoFingerStartCentroidY;
    private float twoFingerStartSpan;
    private float twoFingerStartX0;
    private float twoFingerStartY0;
    private float twoFingerStartX1;
    private float twoFingerStartY1;

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
        touchSlop = ViewConfiguration.get(this).getScaledTouchSlop();

        FrameLayout root = new FrameLayout(this);
        SurfaceView viewport = new SurfaceView(this);
        viewport.setKeepScreenOn(true);
        viewport.setOnTouchListener((view, event) -> handleViewportTouch(event));
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
                resetGestureState();
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
        resetGestureState();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        stopFrameLoop();
        resetGestureState();
        if (rendererHandle != 0L) {
            nativeDestroyRenderer(rendererHandle);
            rendererHandle = 0L;
        }
        super.onDestroy();
    }

    private boolean handleViewportTouch(MotionEvent event) {
        if (rendererHandle == 0L || !surfaceReady) {
            resetGestureState();
            return true;
        }

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                gesturePointerCount = 1;
                twoFingerMode = TWO_FINGER_UNDECIDED;
                oneFingerDownX = event.getX(0);
                oneFingerDownY = event.getY(0);
                lastTouchX = oneFingerDownX;
                lastTouchY = oneFingerDownY;
                oneFingerOrbiting = false;
                multiTouchOccurred = false;
                return true;

            case MotionEvent.ACTION_POINTER_DOWN:
                multiTouchOccurred = true;
                oneFingerOrbiting = false;
                if (event.getPointerCount() >= 2) {
                    beginTwoFingerGesture(event);
                }
                return true;

            case MotionEvent.ACTION_MOVE:
                if (event.getPointerCount() >= 2) {
                    multiTouchOccurred = true;
                    updateTwoFingerGesture(event);
                } else if (event.getPointerCount() == 1) {
                    updateOneFingerGesture(event);
                }
                return true;

            case MotionEvent.ACTION_POINTER_UP:
                // The surviving finger re-anchors on the next MOVE. It cannot become a
                // selection tap after a multi-touch gesture.
                gesturePointerCount = 0;
                twoFingerMode = TWO_FINGER_UNDECIDED;
                lastSpan = 0.0f;
                twoFingerStartSpan = 0.0f;
                oneFingerOrbiting = false;
                multiTouchOccurred = true;
                return true;

            case MotionEvent.ACTION_UP:
                if (gesturePointerCount == 1 && !oneFingerOrbiting && !multiTouchOccurred &&
                    distance(oneFingerDownX, oneFingerDownY, event.getX(0), event.getY(0)) <= touchSlop) {
                    if (nativeTapViewport(rendererHandle, event.getX(0), event.getY(0))) {
                        updateStatus();
                    }
                }
                resetGestureState();
                return true;

            case MotionEvent.ACTION_CANCEL:
                resetGestureState();
                return true;

            default:
                return true;
        }
    }

    private void updateOneFingerGesture(MotionEvent event) {
        float x = event.getX(0);
        float y = event.getY(0);

        if (gesturePointerCount != 1) {
            oneFingerDownX = x;
            oneFingerDownY = y;
            lastTouchX = x;
            lastTouchY = y;
            oneFingerOrbiting = false;
            gesturePointerCount = 1;
            return;
        }

        if (!oneFingerOrbiting) {
            if (distance(oneFingerDownX, oneFingerDownY, x, y) >= touchSlop) {
                oneFingerOrbiting = true;
            }
            // Discard movement inside the tap slop instead of applying a jump when orbit
            // first locks. The next MOVE starts camera motion from this anchor.
            lastTouchX = x;
            lastTouchY = y;
            return;
        }

        nativeOrbitCamera(rendererHandle, x - lastTouchX, y - lastTouchY);
        lastTouchX = x;
        lastTouchY = y;
    }

    private void beginTwoFingerGesture(MotionEvent event) {
        float x0 = event.getX(0);
        float y0 = event.getY(0);
        float x1 = event.getX(1);
        float y1 = event.getY(1);
        float centroidX = (x0 + x1) * 0.5f;
        float centroidY = (y0 + y1) * 0.5f;
        float span = distance(x0, y0, x1, y1);

        lastCentroidX = centroidX;
        lastCentroidY = centroidY;
        lastSpan = span;
        twoFingerStartCentroidX = centroidX;
        twoFingerStartCentroidY = centroidY;
        twoFingerStartSpan = span;
        twoFingerStartX0 = x0;
        twoFingerStartY0 = y0;
        twoFingerStartX1 = x1;
        twoFingerStartY1 = y1;
        twoFingerMode = TWO_FINGER_UNDECIDED;
        gesturePointerCount = 2;
    }

    private void updateTwoFingerGesture(MotionEvent event) {
        float x0 = event.getX(0);
        float y0 = event.getY(0);
        float x1 = event.getX(1);
        float y1 = event.getY(1);
        float centroidX = (x0 + x1) * 0.5f;
        float centroidY = (y0 + y1) * 0.5f;
        float span = distance(x0, y0, x1, y1);

        if (gesturePointerCount != 2) {
            beginTwoFingerGesture(event);
            return;
        }

        if (twoFingerMode == TWO_FINGER_UNDECIDED) {
            float move0X = x0 - twoFingerStartX0;
            float move0Y = y0 - twoFingerStartY0;
            float move1X = x1 - twoFingerStartX1;
            float move1Y = y1 - twoFingerStartY1;
            float move0 = magnitude(move0X, move0Y);
            float move1 = magnitude(move1X, move1Y);
            float totalFingerTravel = move0 + move1;

            float centroidTravel = distance(
                twoFingerStartCentroidX,
                twoFingerStartCentroidY,
                centroidX,
                centroidY);
            float spanTravel = Math.abs(span - twoFingerStartSpan);

            float combinedX = move0X + move1X;
            float combinedY = move0Y + move1Y;
            float combinedTravel = magnitude(combinedX, combinedY);
            boolean symmetricPinch =
                spanTravel >= touchSlop * 1.15f &&
                centroidTravel <= Math.max(touchSlop, spanTravel * 0.40f) &&
                totalFingerTravel >= touchSlop * 2.0f &&
                combinedTravel <= totalFingerTravel * 0.40f;

            float differentialX = move0X - move1X;
            float differentialY = move0Y - move1Y;
            float differentialTravel = magnitude(differentialX, differentialY);
            boolean coherentPan =
                centroidTravel >= touchSlop &&
                spanTravel <= Math.max(touchSlop * 0.75f, centroidTravel * 0.30f) &&
                totalFingerTravel >= touchSlop * 2.0f &&
                differentialTravel <= totalFingerTravel * 0.45f;

            if (symmetricPinch) {
                twoFingerMode = TWO_FINGER_ZOOM;
            } else if (coherentPan) {
                twoFingerMode = TWO_FINGER_PAN;
            } else {
                return;
            }

            lastCentroidX = centroidX;
            lastCentroidY = centroidY;
            lastSpan = span;
            return;
        }

        if (twoFingerMode == TWO_FINGER_PAN) {
            float spanDelta = Math.abs(span - lastSpan);
            float centroidDelta = distance(lastCentroidX, lastCentroidY, centroidX, centroidY);
            if (spanDelta <= Math.max(touchSlop * 0.60f, centroidDelta * 0.45f)) {
                nativePanCamera(rendererHandle, centroidX - lastCentroidX, centroidY - lastCentroidY);
            }
        } else if (twoFingerMode == TWO_FINGER_ZOOM) {
            float centroidDelta = distance(lastCentroidX, lastCentroidY, centroidX, centroidY);
            float spanDelta = Math.abs(span - lastSpan);
            if (lastSpan > 1.0f && span > 1.0f &&
                spanDelta >= centroidDelta * 1.30f) {
                nativeZoomCamera(rendererHandle, span / lastSpan);
            }
        }

        lastCentroidX = centroidX;
        lastCentroidY = centroidY;
        lastSpan = span;
    }

    private static float distance(float x0, float y0, float x1, float y1) {
        return magnitude(x1 - x0, y1 - y0);
    }

    private static float magnitude(float x, float y) {
        return (float) Math.sqrt(x * x + y * y);
    }

    private void resetGestureState() {
        gesturePointerCount = 0;
        twoFingerMode = TWO_FINGER_UNDECIDED;
        lastSpan = 0.0f;
        twoFingerStartSpan = 0.0f;
        oneFingerOrbiting = false;
        multiTouchOccurred = false;
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
            "Vortex3D DEV viewport v0.4\nEngine " + engineVersion() + "\n" + rendererInfo);
    }
}
