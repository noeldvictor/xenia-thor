package jp.xenia.emulator;

import android.content.Intent;
import android.os.Bundle;

public class GpuTraceViewerActivity extends WindowedAppActivity {
    // Debug launches from the PC (2026-09-21): an intent with string extras
    // instead of the EXTRA_CVARS bundle the launcher builds, because `am start`
    // cannot put a Bundle. target_trace_file names the trace (an absolute path
    // in the app's files dir, or a content URI); trace_viewer_dump_png with an
    // optional trace_viewer_dump_frame makes the viewer render that frame,
    // save the PNG, and quit. Together with the trace_frame tool this replays
    // a captured frame after each GPU change in seconds, with no game boot.
    public static final String EXTRA_TARGET_TRACE_FILE = "target_trace_file";
    public static final String EXTRA_DUMP_PNG = "trace_viewer_dump_png";
    public static final String EXTRA_DUMP_FRAME = "trace_viewer_dump_frame";

    @Override
    protected String getWindowedAppIdentifier() {
        return "xenia_gpu_vulkan_trace_viewer";
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        // Before super.onCreate: the native side reads the cvars bundle from
        // getIntent() while it initializes.
        final Intent intent = getIntent();
        if (intent != null && intent.getBundleExtra(EXTRA_CVARS) == null) {
            final Bundle cvars = new Bundle();
            String trace = intent.getStringExtra(EXTRA_TARGET_TRACE_FILE);
            if (trace == null && intent.getData() != null) {
                trace = "file".equals(intent.getData().getScheme())
                        ? intent.getData().getPath() : intent.getData().toString();
            }
            if (trace != null) {
                cvars.putString(EXTRA_TARGET_TRACE_FILE, trace);
            }
            if (intent.hasExtra(EXTRA_DUMP_PNG)) {
                cvars.putString(EXTRA_DUMP_PNG, intent.getStringExtra(EXTRA_DUMP_PNG));
                cvars.putInt(EXTRA_DUMP_FRAME, intent.getIntExtra(EXTRA_DUMP_FRAME, -1));
            }
            if (!cvars.isEmpty()) {
                intent.putExtra(EXTRA_CVARS, cvars);
                setIntent(intent);
            }
        }
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_gpu_trace_viewer);
        setWindowSurfaceView(findViewById(R.id.gpu_trace_viewer_surface_view));
    }
}
