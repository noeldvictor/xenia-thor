package jp.xenia.emulator;

import android.content.Context;
import android.os.Build;
import android.os.PowerManager;
import android.util.Log;
import android.widget.Toast;

import java.util.Locale;

/**
 * Keeps a game session from overheating the device (user, 2026-09-22: "we
 * cannot let the emulator fry the device"; a Gears of War session reached 52 C
 * on the case).
 *
 * <p>It listens to Android's thermal status (PowerManager, API 29+) while the
 * emulator is in the foreground and:
 * <ul>
 *   <li>passes the status to the native frame limiter (cvar
 *       {@code thor_thermal_status}): moderate caps the present rate at 30 fps,
 *       severe at 20, critical and above at 10;</li>
 *   <li>warns the player with a short message at each step up;</li>
 *   <li>pauses the game at critical, and continues it when the status falls
 *       to light or none;</li>
 *   <li>gives the FPS badge a heat readout: the thermal headroom forecast
 *       (API 30+, 100% = the throttling threshold) and the status word.</li>
 * </ul>
 * The native ADPF headroom throttle (gpu_adpf_thermal_throttle, on by
 * default on Android) caps earlier from the forecast; this class is the hard
 * floor and the player-facing part.
 */
final class ThermalGuard {
    private static final String TAG = "XeniaThermal";

    interface Host {
        boolean setConfigVar(String name, String value);

        void setPaused(boolean paused);
    }

    private final Context mContext;
    private final Host mHost;
    private PowerManager mPowerManager;
    private Object mListener;  // PowerManager.OnThermalStatusChangedListener (API 29+)
    private int mStatus;
    private boolean mPausedForHeat;
    private float mHeadroom = Float.NaN;
    private long mHeadroomReadMs;

    ThermalGuard(final Context context, final Host host) {
        mContext = context;
        mHost = host;
    }

    /** Start listening (call from onResume). */
    void start() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q || mListener != null) {
            return;
        }
        mPowerManager = mContext.getSystemService(PowerManager.class);
        if (mPowerManager == null) {
            return;
        }
        final PowerManager.OnThermalStatusChangedListener listener = this::onStatus;
        mListener = listener;
        mPowerManager.addThermalStatusListener(mContext.getMainExecutor(), listener);
        onStatus(mPowerManager.getCurrentThermalStatus());
    }

    /** Stop listening (call from onPause). A heat pause stays until resumed. */
    void stop() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q || mListener == null
                || mPowerManager == null) {
            return;
        }
        mPowerManager.removeThermalStatusListener(
                (PowerManager.OnThermalStatusChangedListener) mListener);
        mListener = null;
    }

    int status() {
        return mStatus;
    }

    private void onStatus(final int status) {
        final int previous = mStatus;
        mStatus = status;
        mHost.setConfigVar("thor_thermal_status", Integer.toString(status));
        Log.i(TAG, "thermal status " + previous + " -> " + status);
        // Status constants: 0 none, 1 light, 2 moderate, 3 severe, 4 critical,
        // 5 emergency, 6 shutdown.
        if (status >= 4) {
            if (!mPausedForHeat) {
                mPausedForHeat = true;
                mHost.setPaused(true);
                toast("The device is too hot. The game is paused until the device cools.");
            }
        } else if (status == 3 && previous < 3) {
            toast("The device is hot. The frame rate is lowered to 20 FPS to cool it.");
        } else if (status == 2 && previous < 2) {
            toast("The device is warm. The frame rate is limited to 30 FPS to cool it.");
        }
        if (status <= 1 && mPausedForHeat) {
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

    /**
     * A short heat readout for the FPS badge: "  heat 72%" (the headroom
     * forecast, 100% = the throttling threshold) and a word from moderate up.
     * The headroom is read at most once a second (Android returns NaN when it
     * is asked more often).
     */
    String badgeSuffix() {
        final StringBuilder sb = new StringBuilder();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && mPowerManager != null) {
            final long now = android.os.SystemClock.uptimeMillis();
            if (now - mHeadroomReadMs >= 1000) {
                mHeadroomReadMs = now;
                final float h = mPowerManager.getThermalHeadroom(10);
                if (!Float.isNaN(h)) {
                    // One log line per 5-point move, so a harness can see the
                    // forecast the native throttle acts on (tag XeniaThermal).
                    if (Float.isNaN(mHeadroom) || Math.abs(h - mHeadroom) >= 0.05f) {
                        Log.i(TAG, String.format(Locale.US, "headroom %.2f status %d", h, mStatus));
                    }
                    mHeadroom = h;
                }
            }
            if (!Float.isNaN(mHeadroom)) {
                sb.append(String.format(Locale.US, "   heat %d%%", Math.round(mHeadroom * 100f)));
            }
        }
        if (mStatus >= 4) {
            sb.append("  TOO HOT");
        } else if (mStatus == 3) {
            sb.append("  HOT");
        } else if (mStatus == 2) {
            sb.append("  WARM");
        }
        return sb.toString();
    }
}
