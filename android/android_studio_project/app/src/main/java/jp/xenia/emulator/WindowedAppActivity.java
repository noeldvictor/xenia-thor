package jp.xenia.emulator;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.Looper;
import android.text.InputFilter;
import android.text.InputType;
import android.util.Log;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import org.jetbrains.annotations.Nullable;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import jp.xenia.XeniaRuntimeException;

public abstract class WindowedAppActivity extends Activity {
    private static final String XAM_KEYBOARD_TAG = "XeniaKeyboard";

    // The EXTRA_CVARS value literal is also used in the native code.

    /**
     * Name of the Bundle intent extra containing Xenia config variable launch arguments.
     */
    public static final String EXTRA_CVARS = "jp.xenia.emulator.WindowedAppActivity.EXTRA_CVARS";

    static {
        System.loadLibrary("xenia-app");
    }

    private final WindowSurfaceListener mWindowSurfaceListener = new WindowSurfaceListener();

    // May be 0 while destroying (mainly while the superclass is).
    private long mAppContext = 0;

    @Nullable
    private WindowSurfaceView mWindowSurfaceView = null;

    private native long initializeWindowedAppOnCreate(
            String windowedAppIdentifier, AssetManager assetManager);

    private native void onDestroyNative(long appContext);

    private native void onWindowSurfaceLayoutChange(
            long appContext, int left, int top, int right, int bottom);

    private native boolean onWindowSurfaceMotionEvent(long appContext, MotionEvent event);

    private native void onWindowSurfaceChanged(long appContext, Surface windowSurface);

    private native void paintWindow(long appContext, boolean forcePaint);

    protected abstract String getWindowedAppIdentifier();

    protected void setWindowSurfaceView(@Nullable final WindowSurfaceView windowSurfaceView) {
        if (mWindowSurfaceView == windowSurfaceView) {
            return;
        }

        // Detach from the old surface.
        if (mWindowSurfaceView != null) {
            mWindowSurfaceView.getHolder().removeCallback(mWindowSurfaceListener);
            mWindowSurfaceView.setOnTouchListener(null);
            mWindowSurfaceView.setOnGenericMotionListener(null);
            mWindowSurfaceView.removeOnLayoutChangeListener(mWindowSurfaceListener);
            mWindowSurfaceView = null;
            if (mAppContext != 0) {
                onWindowSurfaceChanged(mAppContext, null);
            }
        }

        if (windowSurfaceView == null) {
            return;
        }

        mWindowSurfaceView = windowSurfaceView;
        // FIXME(Triang3l): This doesn't work if the layout has already been performed.
        mWindowSurfaceView.addOnLayoutChangeListener(mWindowSurfaceListener);
        mWindowSurfaceView.setOnGenericMotionListener(mWindowSurfaceListener);
        mWindowSurfaceView.setOnTouchListener(mWindowSurfaceListener);
        final SurfaceHolder windowSurfaceHolder = mWindowSurfaceView.getHolder();
        windowSurfaceHolder.addCallback(mWindowSurfaceListener);
        // If setting after the creation of the surface.
        if (mAppContext != 0) {
            final Surface windowSurface = windowSurfaceHolder.getSurface();
            if (windowSurface != null) {
                onWindowSurfaceChanged(mAppContext, windowSurface);
            }
        }
    }

    public void onWindowSurfaceDraw(final boolean forcePaint) {
        if (mAppContext == 0) {
            return;
        }
        paintWindow(mAppContext, forcePaint);
    }

    // Used from the native WindowedAppContext. May be called from non-UI threads.
    @SuppressWarnings("UnusedDeclaration")
    protected void postInvalidateWindowSurface() {
        if (mWindowSurfaceView == null) {
            return;
        }
        onWindowSurfaceFrameRequested();
        mWindowSurfaceView.postInvalidate();
    }

    protected void onWindowSurfaceFrameRequested() {
    }

    // Used from the native WindowedAppContext when guest code faults.
    @SuppressWarnings("UnusedDeclaration")
    public void onNativeGuestCrash(final String classification, final String details) {
    }

    @Nullable
    @SuppressWarnings("UnusedDeclaration")
    public String showXamKeyboardInputDialogBlocking(
            @Nullable final String title,
            @Nullable final String description,
            @Nullable final String defaultText,
            final int maxLength) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            Log.w(XAM_KEYBOARD_TAG, "Rejecting synchronous XAM keyboard request on UI thread");
            return null;
        }

        final CountDownLatch done = new CountDownLatch(1);
        final AtomicBoolean completed = new AtomicBoolean(false);
        final AtomicReference<String> result = new AtomicReference<>(null);

        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) {
                Log.w(XAM_KEYBOARD_TAG, "Dropping XAM keyboard request while activity is closing");
                done.countDown();
                return;
            }

            if (showXamKeyboardOverlay(
                    title, description, defaultText, maxLength,
                    completed, result, done)) {
                return;
            }

            final EditText input = new EditText(this);
            configureXamKeyboardInput(input, defaultText, maxLength);

            final AlertDialog.Builder builder = new AlertDialog.Builder(this)
                    .setTitle(title != null && !title.isEmpty() ? title : "Keyboard Input")
                    .setView(input)
                    .setPositiveButton(android.R.string.ok, null)
                    .setNegativeButton(android.R.string.cancel, null);
            if (description != null && !description.isEmpty()) {
                builder.setMessage(description);
            }

            final AlertDialog dialog = builder.create();
            final Runnable cancel = () -> {
                if (completed.compareAndSet(false, true)) {
                    Log.i(XAM_KEYBOARD_TAG, "AlertDialog cancelled");
                    result.set(null);
                    done.countDown();
                }
            };
            final Runnable accept = () -> {
                if (completed.compareAndSet(false, true)) {
                    Log.i(XAM_KEYBOARD_TAG, "AlertDialog accepted");
                    result.set(input.getText().toString());
                    done.countDown();
                    dialog.dismiss();
                }
            };
            input.setOnEditorActionListener((view, actionId, event) -> {
                if (actionId == EditorInfo.IME_ACTION_DONE) {
                    accept.run();
                    return true;
                }
                return false;
            });
            dialog.setOnCancelListener(dialogInterface -> cancel.run());
            dialog.setOnDismissListener(dialogInterface -> cancel.run());
            dialog.setOnShowListener(dialogInterface -> {
                dialog.getButton(AlertDialog.BUTTON_POSITIVE)
                        .setOnClickListener(view -> accept.run());
                dialog.getButton(AlertDialog.BUTTON_NEGATIVE)
                        .setOnClickListener(view -> {
                            cancel.run();
                            dialog.dismiss();
                        });
                input.requestFocus();
                final Window window = dialog.getWindow();
                if (window != null) {
                    window.setSoftInputMode(
                            WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE
                                    | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
                }
                input.post(() -> showXamKeyboardInputMethod(input));
                input.postDelayed(() -> showXamKeyboardInputMethod(input), 150);
            });
            dialog.show();
            Log.i(XAM_KEYBOARD_TAG, "Showing fallback AlertDialog keyboard");
        });

        try {
            done.await();
        } catch (final InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        }
        return result.get();
    }

    private boolean showXamKeyboardOverlay(
            @Nullable final String title,
            @Nullable final String description,
            @Nullable final String defaultText,
            final int maxLength,
            final AtomicBoolean completed,
            final AtomicReference<String> result,
            final CountDownLatch done) {
        final View overlay = findViewById(R.id.emulator_xam_keyboard_overlay);
        final TextView titleView = findViewById(R.id.emulator_xam_keyboard_title);
        final TextView descriptionView =
                findViewById(R.id.emulator_xam_keyboard_description);
        final EditText input = findViewById(R.id.emulator_xam_keyboard_input);
        final Button okButton = findViewById(R.id.emulator_xam_keyboard_ok);
        final Button cancelButton = findViewById(R.id.emulator_xam_keyboard_cancel);
        if (overlay == null || titleView == null || descriptionView == null
                || input == null || okButton == null || cancelButton == null) {
            return false;
        }

        titleView.setText(title != null && !title.isEmpty() ? title : "Keyboard Input");
        if (description != null && !description.isEmpty()) {
            descriptionView.setText(description);
            descriptionView.setVisibility(View.VISIBLE);
        } else {
            descriptionView.setText("");
            descriptionView.setVisibility(View.GONE);
        }
        configureXamKeyboardInput(input, defaultText, maxLength);

        final Runnable cancel = () -> {
            if (completed.compareAndSet(false, true)) {
                Log.i(XAM_KEYBOARD_TAG, "Overlay cancelled");
                hideXamKeyboardOverlay(overlay, input);
                result.set(null);
                done.countDown();
            }
        };
        final Runnable accept = () -> {
            if (completed.compareAndSet(false, true)) {
                Log.i(XAM_KEYBOARD_TAG, "Overlay accepted");
                hideXamKeyboardOverlay(overlay, input);
                result.set(input.getText().toString());
                done.countDown();
            }
        };

        input.setOnEditorActionListener((view, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                accept.run();
                return true;
            }
            return false;
        });
        okButton.setOnClickListener(view -> accept.run());
        cancelButton.setOnClickListener(view -> cancel.run());

        overlay.setVisibility(View.VISIBLE);
        overlay.bringToFront();
        overlay.requestFocus();
        input.requestFocus();
        input.post(() -> showXamKeyboardInputMethod(input));
        input.postDelayed(() -> showXamKeyboardInputMethod(input), 150);
        Log.i(XAM_KEYBOARD_TAG, "Showing overlay keyboard title=\""
                + (title != null ? title : "") + "\" maxLength=" + maxLength);
        return true;
    }

    private void configureXamKeyboardInput(
            final EditText input,
            @Nullable final String defaultText,
            final int maxLength) {
        input.setSingleLine(true);
        input.setSelectAllOnFocus(true);
        input.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        input.setImeOptions(EditorInfo.IME_ACTION_DONE);
        input.setText(defaultText != null ? defaultText : "");
        final int maxChars = Math.max(0, maxLength - 1);
        if (maxChars > 0) {
            input.setFilters(new InputFilter[] { new InputFilter.LengthFilter(maxChars) });
        } else {
            input.setFilters(new InputFilter[0]);
        }
    }

    private void hideXamKeyboardOverlay(final View overlay, final EditText input) {
        hideXamKeyboardInputMethod(input);
        input.setOnEditorActionListener(null);
        overlay.setVisibility(View.GONE);
    }

    private void showXamKeyboardInputMethod(final EditText input) {
        final InputMethodManager inputMethodManager =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (inputMethodManager != null) {
            inputMethodManager.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
        }
    }

    private void hideXamKeyboardInputMethod(final EditText input) {
        final InputMethodManager inputMethodManager =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (inputMethodManager != null) {
            inputMethodManager.hideSoftInputFromWindow(input.getWindowToken(), 0);
        }
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enterImmersiveMode();

        final String windowedAppIdentifier = getWindowedAppIdentifier();
        mAppContext = initializeWindowedAppOnCreate(windowedAppIdentifier, getAssets());
        if (mAppContext == 0) {
            finish();
            throw new XeniaRuntimeException(
                    "Error initializing the windowed app " + windowedAppIdentifier);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        enterImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(final boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enterImmersiveMode();
        }
    }

    @Override
    protected void onDestroy() {
        setWindowSurfaceView(null);
        if (mAppContext != 0) {
            onDestroyNative(mAppContext);
        }
        mAppContext = 0;
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    protected final void enterImmersiveMode() {
        final View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    private class WindowSurfaceListener implements
            View.OnGenericMotionListener,
            View.OnLayoutChangeListener,
            View.OnTouchListener,
            SurfaceHolder.Callback2 {
        @Override
        public void onLayoutChange(
                final View v, final int left, final int top, final int right, final int bottom,
                final int oldLeft, final int oldTop, final int oldRight, final int oldBottom) {
            if (mAppContext != 0) {
                onWindowSurfaceLayoutChange(mAppContext, left, top, right, bottom);
            }
        }

        @Override
        public boolean onGenericMotion(final View view, final MotionEvent event) {
            if (mAppContext == 0) {
                return false;
            }
            return onWindowSurfaceMotionEvent(mAppContext, event);
        }

        @SuppressLint("ClickableViewAccessibility")
        @Override
        public boolean onTouch(final View view, final MotionEvent event) {
            if (mAppContext == 0) {
                return false;
            }
            return onWindowSurfaceMotionEvent(mAppContext, event);
        }

        @Override
        public void surfaceCreated(final SurfaceHolder holder) {
            if (mAppContext == 0) {
                return;
            }
            onWindowSurfaceChanged(mAppContext, holder.getSurface());
        }

        @Override
        public void surfaceChanged(
                final SurfaceHolder holder, final int format, final int width, final int height) {
            if (mAppContext == 0) {
                return;
            }
            onWindowSurfaceChanged(mAppContext, holder.getSurface());
        }

        @Override
        public void surfaceDestroyed(final SurfaceHolder holder) {
            if (mAppContext == 0) {
                return;
            }
            onWindowSurfaceChanged(mAppContext, null);
        }

        @Override
        public void surfaceRedrawNeeded(final SurfaceHolder holder) {
            onWindowSurfaceDraw(true);
        }
    }
}
