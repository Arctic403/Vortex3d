package com.vortex3d.app

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.os.Looper
import android.util.Log
import android.view.Choreographer
import android.view.Gravity
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView

/**
 * Android host for the Vortex3D Vulkan viewport.
 *
 * Android owns the Activity, SurfaceView and vsync scheduling. The native layer owns Vulkan.
 * Every JNI call is intentionally serialized on the Activity/UI thread so surface lifecycle and
 * frame submission cannot race one another.
 */
class MainActivity : Activity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "Vortex3D"
        private const val NATIVE_LIBRARY = "vortex_android"

        private val nativeLibraryLoaded: Boolean = try {
            System.loadLibrary(NATIVE_LIBRARY)
            true
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "Unable to load lib$NATIVE_LIBRARY.so", error)
            false
        } catch (error: SecurityException) {
            Log.e(TAG, "Security policy blocked lib$NATIVE_LIBRARY.so", error)
            false
        }

        @JvmStatic
        private external fun engineVersion(): String

        @JvmStatic
        private external fun nativeCreateRenderer(): Long

        @JvmStatic
        private external fun nativeDestroyRenderer(handle: Long)

        @JvmStatic
        private external fun nativeSurfaceCreated(handle: Long, surface: Surface): Boolean

        @JvmStatic
        private external fun nativeSurfaceChanged(handle: Long): Boolean

        @JvmStatic
        private external fun nativeSurfaceDestroyed(handle: Long)

        @JvmStatic
        private external fun nativeRenderFrame(handle: Long): Boolean

        @JvmStatic
        private external fun nativeRendererInfo(handle: Long): String
    }

    private lateinit var surfaceView: SurfaceView
    private lateinit var statusView: TextView

    /** Opaque native VulkanViewport pointer encoded as a jlong. Zero means unavailable. */
    private var rendererHandle: Long = 0L

    /** True only after nativeSurfaceCreated() succeeds and until surfaceDestroyed()/onDestroy(). */
    private var surfaceReady = false

    /** Activity lifecycle gate. Frames are scheduled only while resumed. */
    private var activityResumed = false

    /** Prevents any new work once Activity teardown begins. */
    private var activityDestroyed = false

    /** True while one Choreographer callback is currently queued. */
    private var frameCallbackPosted = false

    private val frameCallback = Choreographer.FrameCallback { frameTimeNanos ->
        // The queued callback has now been consumed. A continuation must explicitly post again.
        frameCallbackPosted = false

        if (!canRender()) {
            return@FrameCallback
        }

        val handle = rendererHandle
        if (handle == 0L) {
            return@FrameCallback
        }

        val shouldContinue = try {
            nativeRenderFrame(handle)
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "nativeRenderFrame() linkage failure", error)
            false
        } catch (error: RuntimeException) {
            Log.e(TAG, "nativeRenderFrame() failed at $frameTimeNanos ns", error)
            false
        }

        if (!shouldContinue) {
            // False is the native contract for stopping the frame loop. Keep surfaceReady true so
            // surfaceDestroyed() still performs the required native detach later.
            Log.e(TAG, "Native renderer stopped the frame loop")
            updateStatus()
            return@FrameCallback
        }

        scheduleNextFrame()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (!isOnUiThread()) {
            // Activity lifecycle callbacks are expected on the UI thread. Do not enter JNI if that
            // platform invariant is violated.
            Log.e(TAG, "onCreate() was not called on the UI thread")
            finish()
            return
        }

        rendererHandle = createRendererSafely()

        val root = FrameLayout(this)

        surfaceView = SurfaceView(this).apply {
            keepScreenOn = true
            holder.addCallback(this@MainActivity)
        }
        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            ),
        )

        statusView = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 12.0f
            setPadding(18, 12, 18, 12)
            setBackgroundColor(0x66000000)
        }
        root.addView(
            statusView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply {
                gravity = Gravity.TOP or Gravity.START
            },
        )

        setContentView(root)
        updateStatus()
    }

    override fun onResume() {
        super.onResume()

        if (!isOnUiThread()) {
            Log.e(TAG, "onResume() was not called on the UI thread")
            return
        }

        activityResumed = true
        scheduleNextFrame()
    }

    override fun onPause() {
        if (isOnUiThread()) {
            // Stop vsync work before Android can begin tearing down the Surface.
            activityResumed = false
            stopFrameLoop()
        } else {
            Log.e(TAG, "onPause() was not called on the UI thread")
        }

        super.onPause()
    }

    override fun onDestroy() {
        if (isOnUiThread()) {
            activityDestroyed = true
            activityResumed = false
            stopFrameLoop()

            // In the normal lifecycle surfaceDestroyed() has already detached the native window.
            // If Android tears down the Activity in another order, VulkanViewport's destructor also
            // detaches, so clearing Java-side state before deletion prevents late callback use-after-free.
            surfaceReady = false
            if (::surfaceView.isInitialized) {
                surfaceView.holder.removeCallback(this)
            }

            val handle = rendererHandle
            rendererHandle = 0L
            if (handle != 0L && nativeLibraryLoaded) {
                try {
                    nativeDestroyRenderer(handle)
                } catch (error: UnsatisfiedLinkError) {
                    Log.e(TAG, "nativeDestroyRenderer() linkage failure", error)
                } catch (error: RuntimeException) {
                    Log.e(TAG, "nativeDestroyRenderer() failed", error)
                }
            }
        } else {
            Log.e(TAG, "onDestroy() was not called on the UI thread")
        }

        super.onDestroy()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (!isOnUiThread()) {
            Log.e(TAG, "surfaceCreated() was not called on the UI thread")
            return
        }
        if (activityDestroyed) {
            Log.w(TAG, "Ignoring surfaceCreated() after Activity destruction")
            return
        }

        val handle = rendererHandle
        if (handle == 0L || !nativeLibraryLoaded) {
            Log.e(TAG, "surfaceCreated(): renderer is unavailable")
            updateStatus()
            return
        }

        val surface = holder.surface
        if (!surface.isValid) {
            Log.e(TAG, "surfaceCreated(): Surface is invalid")
            updateStatus()
            return
        }

        // A duplicate callback must not silently leak/replace an already attached native window.
        if (surfaceReady) {
            Log.w(TAG, "surfaceCreated(): replacing an already attached surface")
            destroyNativeSurfaceSafely(handle)
            surfaceReady = false
        }

        surfaceReady = try {
            nativeSurfaceCreated(handle, surface)
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "nativeSurfaceCreated() linkage failure", error)
            false
        } catch (error: RuntimeException) {
            Log.e(TAG, "nativeSurfaceCreated() failed", error)
            false
        }

        if (!surfaceReady) {
            Log.e(TAG, "Native renderer rejected the Android Surface")
        } else {
            Log.i(TAG, "Vulkan surface attached")
        }

        updateStatus()
        scheduleNextFrame()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (!isOnUiThread()) {
            Log.e(TAG, "surfaceChanged() was not called on the UI thread")
            return
        }
        if (activityDestroyed || !surfaceReady) {
            return
        }

        val handle = rendererHandle
        if (handle == 0L || !nativeLibraryLoaded) {
            Log.e(TAG, "surfaceChanged(): renderer is unavailable")
            return
        }
        if (width <= 0 || height <= 0 || !holder.surface.isValid) {
            Log.w(TAG, "Ignoring invalid surface change: ${width}x$height")
            return
        }

        val changed = try {
            nativeSurfaceChanged(handle)
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "nativeSurfaceChanged() linkage failure", error)
            false
        } catch (error: RuntimeException) {
            Log.e(TAG, "nativeSurfaceChanged() failed for ${width}x$height", error)
            false
        }

        if (!changed) {
            // Keep the surface attached. Native swapchain recreation may recover on a later frame
            // or another surfaceChanged() callback, and surfaceDestroyed() must still detach it.
            Log.e(TAG, "Native surface resize failed for ${width}x$height")
            updateStatus()
            return
        }

        Log.d(TAG, "Surface changed: ${width}x$height format=$format")
        updateStatus()
        scheduleNextFrame()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (!isOnUiThread()) {
            Log.e(TAG, "surfaceDestroyed() was not called on the UI thread")
            return
        }

        // No render callback may remain queued when native relinquishes ANativeWindow.
        stopFrameLoop()

        val wasReady = surfaceReady
        surfaceReady = false
        if (!wasReady) {
            updateStatus()
            return
        }

        val handle = rendererHandle
        if (handle != 0L && nativeLibraryLoaded) {
            destroyNativeSurfaceSafely(handle)
        }
        Log.i(TAG, "Vulkan surface detached")
        updateStatus()
    }

    /** Queue exactly one callback for the next display vsync. */
    private fun scheduleNextFrame() {
        if (!isOnUiThread()) {
            // This is defensive for future callers. Current lifecycle + Choreographer callbacks all
            // execute on the window/UI thread.
            runOnUiThread { scheduleNextFrame() }
            return
        }
        if (frameCallbackPosted || !canRender()) {
            return
        }

        frameCallbackPosted = true
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    /** Remove any callback that has not executed yet. Not reposting is what stops the loop. */
    private fun stopFrameLoop() {
        if (!isOnUiThread()) {
            runOnUiThread { stopFrameLoop() }
            return
        }
        if (!frameCallbackPosted) {
            return
        }

        Choreographer.getInstance().removeFrameCallback(frameCallback)
        frameCallbackPosted = false
    }

    private fun canRender(): Boolean =
        nativeLibraryLoaded &&
            !activityDestroyed &&
            activityResumed &&
            surfaceReady &&
            rendererHandle != 0L

    private fun createRendererSafely(): Long {
        if (!nativeLibraryLoaded) {
            return 0L
        }
        return try {
            nativeCreateRenderer().also { handle ->
                if (handle == 0L) {
                    Log.e(TAG, "nativeCreateRenderer() returned a null handle")
                }
            }
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "nativeCreateRenderer() linkage failure", error)
            0L
        } catch (error: RuntimeException) {
            Log.e(TAG, "nativeCreateRenderer() failed", error)
            0L
        }
    }

    private fun destroyNativeSurfaceSafely(handle: Long) {
        if (handle == 0L || !nativeLibraryLoaded) {
            return
        }
        try {
            nativeSurfaceDestroyed(handle)
        } catch (error: UnsatisfiedLinkError) {
            Log.e(TAG, "nativeSurfaceDestroyed() linkage failure", error)
        } catch (error: RuntimeException) {
            Log.e(TAG, "nativeSurfaceDestroyed() failed", error)
        }
    }

    private fun updateStatus() {
        if (!::statusView.isInitialized || !isOnUiThread()) {
            return
        }

        val handle = rendererHandle
        val rendererInfo = when {
            !nativeLibraryLoaded -> "Native renderer library unavailable"
            handle == 0L -> "Renderer allocation failed"
            else -> try {
                nativeRendererInfo(handle)
            } catch (error: UnsatisfiedLinkError) {
                Log.e(TAG, "nativeRendererInfo() linkage failure", error)
                "Renderer information unavailable"
            } catch (error: RuntimeException) {
                Log.e(TAG, "nativeRendererInfo() failed", error)
                "Renderer information unavailable"
            }
        }

        val version = if (!nativeLibraryLoaded) {
            "unavailable"
        } else {
            try {
                engineVersion()
            } catch (error: UnsatisfiedLinkError) {
                Log.e(TAG, "engineVersion() linkage failure", error)
                "unavailable"
            } catch (error: RuntimeException) {
                Log.e(TAG, "engineVersion() failed", error)
                "unavailable"
            }
        }

        statusView.text = "Vortex3D DEV viewport v0.3\nEngine $version\n$rendererInfo"
    }

    private fun isOnUiThread(): Boolean = Looper.myLooper() === Looper.getMainLooper()
}
