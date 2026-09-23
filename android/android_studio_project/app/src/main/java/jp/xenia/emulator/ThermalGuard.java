package jp.xenia.emulator;

import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import android.widget.Toast;

import java.util.Locale;

/**
 * Keeps a game session from overheating the device (user, 2026-09-22: "we
 * cannot let the emulator fry the device"; a Gears of War session reached 52 C
 * on the case, and a short Banjo launch put the hottest CPU/GPU sensor at 94 C
 * within 35 s while Android still reported thermal status "none").
 *
 * <p>Two signals, the higher one wins:
 * <ul>
 *   <li>Android's thermal status (PowerManager listener, API 29+). It follows
 *       the skin temperature and reacts late on the Thor.</li>
 *   <li>The thermal headroom forecast (API 30+), polled every 2 s: the
 *       predicted headroom 10 s ahead, where 1.0 is the point of severe
 *       throttling. 0.90 counts as warm (level 2), 1.00 as hot (3), 1.15 as
 *       too hot (4).</li>
 * </ul>
 * The level goes to the native frame limiter (cvar {@code thor_thermal_status}:
 * warm caps at 30 fps, hot at 20, too hot at 10), the player gets a short
 * message at each step up, the game pauses at too hot and continues at level
 * 1 or 0, and the FPS badge shows the forecast and the level word.
 */
final class ThermalGuard {
    private static final String TAG = "XeniaThermal";
    private static final long POLL_MS = 2000;

    interface Host {
        boolean setConfigVar(String name, String value);

        void setPaused(boolean paused);
    }

    private final Context mContext;
    private final Host mHost;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private PowerManager mPowerManager;
    private Object mListener;  // PowerManager.OnThermalStatusChangedListener (API 29+)
    private boolean mStarted;
    private int mOsStatus;
    private int mForecastLevel;
    private int mLevel;
    private boolean mPausedForHeat;
    private float mHeadroom = Float.NaN;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            if (!mStarted) {
                return;
            }
            pollHeadroom();
            mHandler.postDelayed(this, POLL_MS);
        }
    };

    ThermalGuard(final Context context, final Host host) {
        mContext = context;
        mHost = host;
    }

    /** Start listening (call from onResume). */
    void start() {
        if (mStarted) {
            return;
        }
        mStarted = true;
        mPowerManager = mContext.getSystemService(PowerManager.class);
        if (mPowerManager != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            final PowerManager.OnThermalStatusChangedListener listener = this::onOsStatus;
            mListener = listener;
            mPowerManager.addThermalStatusListener(mContext.getMainExecutor(), listener);
            onOsStatus(mPowerManager.getCurrentThermalStatus());
        }
        mHandler.post(mPoll);
    }

    /** Stop listening (call from onPause). A heat pause stays until resumed. */
    void stop() {
        mStarted = false;
        mHandler.removeCallbacks(mPoll);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && mListener != null
                && mPowerManager != null) {
            mPowerManager.removeThermalStatusListener(
                    (PowerManager.OnThermalStatusChangedListener) mListener);
        }
        mListener = null;
    }

    int level() {
        return mLevel;
    }

    private void onOsStatus(final int status) {
        mOsStatus = status;
        update("os status " + status);
    }

    private void pollHeadroom() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || mPowerManager == null) {
            return;
        }
        final float h = mPowerManager.getThermalHeadroom(10);
        if (Float.isNaN(h)) {
            return;  // asked too often, or no data yet
        }
        if (Float.isNaN(mHeadroom) || Math.abs(h - mHeadroom) >= 0.05f) {
            Log.i(TAG, String.format(Locale.US, "headroom %.2f os status %d level %d",
                    h, mOsStatus, mLevel));
        }
        mHeadroom = h;
        final int level = h >= 1.15f ? 4 : h >= 1.0f ? 3 : h >= 0.9f ? 2 : 0;
        if (level != mForecastLevel) {
            mForecastLevel = level;
            update(String.format(Locale.US, "forecast %.2f", h));
        }
    }

    private void update(final String cause) {
        final int level = Math.max(mOsStatus, mForecastLevel);
        final int previous = mLevel;
        if (level == previous) {
            return;
        }
        mLevel = level;
        mHost.setConfigVar("thor_thermal_status", Integer.toString(level));
        Log.i(TAG, "level " + previous + " -> " + level + " (" + cause + ")");
        if (level >= 4) {
            if (!mPausedForHeat) {
                mPausedForHeat = true;
                mHost.setPaused(true);
                toast("The device is too hot. The game is paused until the device cools.");
            }
        } else if (level == 3 && previous < 3) {
            toast("The device is hot. The frame rate is lowered to 20 FPS to cool it.");
        } else if (level == 2 && previous < 2) {
            toast("The device is warm. The frame rate is limited to 30 FPS to cool it.");
        }
        if (level <= 1 && mPausedForHeat) {
            mPausedForHeat = false;
            mHost.setPaused(false);
            toast("The device is cooler. The game continues.");
        }
    }

    private void toast(final String message) {
        try {
            Toast.makeText(mContext, message, Toast.LENGTH_LONG).show();
        } catch (RuntimeException e) {
            Log.w(TAG, "toast failed: " + e);
        }
    }

    /** "   heat 72%" (the forecast, 100% = severe throttling) and the level word. */
    String badgeSuffix() {
        final StringBuilder sb = new StringBuilder();
        if (!Float.isNaN(mHeadroom)) {
            sb.append(String.format(Locale.US, "   heat %d%%", Math.round(mHeadroom * 100f)));
        }
        if (mLevel >= 4) {
            sb.append("  TOO HOT");
        } else if (mLevel == 3) {
            sb.append("  HOT");
        } else if (mLevel == 2) {
            sb.append("  WARM");
        }
        return sb.toString();
    }
}
