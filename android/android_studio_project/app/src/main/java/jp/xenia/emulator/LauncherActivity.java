package jp.xenia.emulator;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.View;

public class LauncherActivity extends Activity {
    private static final String EXTERNAL_STORAGE_PROVIDER =
            "com.android.externalstorage.documents";
    private static final String THOR_XBOX360_DOCUMENT_ID =
            "2664-21DE:Roms/xbox360";
    private static final Uri THOR_XBOX360_INITIAL_URI =
            DocumentsContract.buildDocumentUri(
                    EXTERNAL_STORAGE_PROVIDER, THOR_XBOX360_DOCUMENT_ID);

    private static final int REQUEST_OPEN_GAME = 1;
    private static final int REQUEST_OPEN_GPU_TRACE_VIEWER = 0;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_launcher);
    }

    @Override
    protected void onActivityResult(
            final int requestCode, final int resultCode, final Intent data) {
        if (resultCode == RESULT_OK) {
            final Uri uri = data != null ? data.getData() : null;
            if (uri != null && requestCode == REQUEST_OPEN_GAME) {
                persistReadPermission(data, uri);
                final Intent emulatorIntent = new Intent(this, EmulatorActivity.class);
                final Bundle emulatorLaunchArguments =
                        XeniaAndroidSettings.createLaunchArguments(this, uri);
                emulatorIntent.putExtra(
                        WindowedAppActivity.EXTRA_CVARS, emulatorLaunchArguments);
                startActivity(emulatorIntent);
            } else if (uri != null && requestCode == REQUEST_OPEN_GPU_TRACE_VIEWER) {
                final Intent gpuTraceViewerIntent = new Intent(this, GpuTraceViewerActivity.class);
                final Bundle gpuTraceViewerLaunchArguments = new Bundle();
                gpuTraceViewerLaunchArguments.putString("target_trace_file", uri.toString());
                gpuTraceViewerIntent.putExtra(
                        WindowedAppActivity.EXTRA_CVARS, gpuTraceViewerLaunchArguments);
                startActivity(gpuTraceViewerIntent);
            }
        }
    }

    public void onLaunchGameClick(final View view) {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, THOR_XBOX360_INITIAL_URI);
        startActivityForResult(intent, REQUEST_OPEN_GAME);
    }

    public void onLaunchGpuTraceViewerClick(final View view) {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        startActivityForResult(intent, REQUEST_OPEN_GPU_TRACE_VIEWER);
    }

    public void onOpenSettingsClick(final View view) {
        startActivity(new Intent(this, SettingsActivity.class));
    }

    public void onLaunchWindowDemoClick(final View view) {
        startActivity(new Intent(this, WindowDemoActivity.class));
    }

    private void persistReadPermission(final Intent data, final Uri uri) {
        if (data == null) {
            return;
        }
        final int takeFlags = data.getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if ((takeFlags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) {
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (final SecurityException ignored) {
            // Some providers return one-shot grants only; launching still works.
        }
    }
}
