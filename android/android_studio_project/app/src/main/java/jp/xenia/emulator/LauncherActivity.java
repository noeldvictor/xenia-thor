package jp.xenia.emulator;

import android.app.Activity;
import android.content.SharedPreferences;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.view.KeyEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;
import java.util.Locale;

public class LauncherActivity extends Activity {
    private static final String EXTERNAL_STORAGE_PROVIDER =
            "com.android.externalstorage.documents";
    private static final String THOR_XBOX360_DOCUMENT_ID =
            "2664-21DE:Roms/xbox360";
    private static final Uri THOR_XBOX360_INITIAL_URI =
            DocumentsContract.buildDocumentUri(
                    EXTERNAL_STORAGE_PROVIDER, THOR_XBOX360_DOCUMENT_ID);

    private static final int REQUEST_OPEN_GAME = 1;
    private static final int REQUEST_OPEN_GAME_FOLDER = 2;
    private static final int REQUEST_OPEN_GPU_TRACE_VIEWER = 0;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_launcher);
        XeniaAndroidSettings.ensureInitialized(this);
        refreshLastGameCard();
        refreshLastRunStatus();
        refreshGameLibrary();
        refreshRecentGames();
        final View scanGamesCard = findViewById(R.id.launcher_scan_games_card);
        if (scanGamesCard != null) {
            scanGamesCard.requestFocus();
        }
    }

    @Override
    public boolean dispatchKeyEvent(final KeyEvent event) {
        if (event != null && event.getAction() == KeyEvent.ACTION_UP
                && isControllerActivateKey(event.getKeyCode())) {
            final View focused = getCurrentFocus();
            if (focused != null && focused.isEnabled()) {
                focused.performClick();
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshLastGameCard();
        refreshLastRunStatus();
        refreshGameLibrary();
        refreshRecentGames();
    }

    @Override
    protected void onActivityResult(
            final int requestCode, final int resultCode, final Intent data) {
        if (resultCode == RESULT_OK) {
            final Uri uri = data != null ? data.getData() : null;
            if (uri != null && requestCode == REQUEST_OPEN_GAME) {
                persistReadPermission(data, uri);
                rememberLastGame(uri);
                launchGame(uri);
            } else if (uri != null && requestCode == REQUEST_OPEN_GAME_FOLDER) {
                persistReadPermission(data, uri);
                XeniaAndroidSettings.addGameLibraryFolder(this, uri);
                scanAndRefreshGameLibrary();
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

    public void onScanGamesClick(final View view) {
        scanAndRefreshGameLibrary();
    }

    public void onAddGameFolderClick(final View view) {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, THOR_XBOX360_INITIAL_URI);
        startActivityForResult(intent, REQUEST_OPEN_GAME_FOLDER);
    }

    public void onLaunchLastGameClick(final View view) {
        final String lastGameUri = XeniaAndroidSettings.getPreferences(this)
                .getString(XeniaAndroidSettings.KEY_LAST_GAME_URI, "");
        if (lastGameUri == null || lastGameUri.isEmpty()) {
            return;
        }
        launchGame(Uri.parse(lastGameUri));
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

    private static boolean isControllerActivateKey(final int keyCode) {
        return keyCode == KeyEvent.KEYCODE_BUTTON_A
                || keyCode == KeyEvent.KEYCODE_BUTTON_START
                || keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                || keyCode == KeyEvent.KEYCODE_ENTER
                || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER;
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

    private void launchGame(final Uri uri) {
        launchGame(uri, getDisplayName(uri));
    }

    private void launchGame(final Uri uri, final String displayTitle) {
        final Intent emulatorIntent = new Intent(this, EmulatorActivity.class);
        final Bundle emulatorLaunchArguments =
                XeniaAndroidSettings.createLaunchArguments(this, uri);
        rememberLastGame(uri, displayTitle);
        XeniaAndroidSettings.recordLaunchStarted(
                this,
                displayTitle,
                emulatorLaunchArguments.getString("target", uri.toString()),
                uri.toString());
        emulatorIntent.putExtra(
                WindowedAppActivity.EXTRA_CVARS, emulatorLaunchArguments);
        startActivity(emulatorIntent);
    }

    private void rememberLastGame(final Uri uri) {
        rememberLastGame(uri, getDisplayName(uri));
    }

    private void rememberLastGame(final Uri uri, final String displayTitle) {
        XeniaAndroidSettings.getPreferences(this).edit()
                .putString(XeniaAndroidSettings.KEY_LAST_GAME_URI, uri.toString())
                .putString(XeniaAndroidSettings.KEY_LAST_GAME_TITLE, displayTitle)
                .apply();
        refreshLastGameCard();
    }

    private void refreshLastGameCard() {
        final View card = findViewById(R.id.launcher_last_game_card);
        final TextView detail = findViewById(R.id.launcher_last_game_detail);
        if (card == null || detail == null) {
            return;
        }
        final SharedPreferences preferences = XeniaAndroidSettings.getPreferences(this);
        final String lastGameUri = preferences.getString(
                XeniaAndroidSettings.KEY_LAST_GAME_URI, "");
        final boolean hasLastGame = lastGameUri != null && !lastGameUri.isEmpty();
        card.setEnabled(hasLastGame);
        card.setAlpha(hasLastGame ? 1.0f : 0.55f);
        if (hasLastGame) {
            detail.setText(preferences.getString(
                    XeniaAndroidSettings.KEY_LAST_GAME_TITLE,
                    getString(R.string.launcher_last_game_unknown)));
        } else {
            detail.setText(R.string.launcher_last_game_empty);
        }
    }

    private void refreshLastRunStatus() {
        final TextView status = findViewById(R.id.launcher_last_run_status);
        if (status == null) {
            return;
        }
        final SharedPreferences preferences = XeniaAndroidSettings.getPreferences(this);
        final String state = preferences.getString(
                XeniaAndroidSettings.KEY_LAST_RUN_STATE, "");
        if (state == null || state.isEmpty()) {
            status.setVisibility(View.GONE);
            return;
        }
        final String title = preferences.getString(
                XeniaAndroidSettings.KEY_LAST_RUN_TITLE,
                getString(R.string.launcher_last_game_unknown));
        if (XeniaAndroidSettings.LAST_RUN_STATE_EXITED_TO_MENU.equals(state)) {
            status.setText(getString(R.string.launcher_last_run_exited, title));
        } else if (XeniaAndroidSettings.LAST_RUN_STATE_GUEST_CRASH.equals(state)) {
            final String diagnostic = preferences.getString(
                    XeniaAndroidSettings.KEY_LAST_RUN_DIAGNOSTIC, "");
            if (isGuestHeapDiagnostic(diagnostic)) {
                status.setText(getString(R.string.launcher_last_run_guest_heap, title));
            } else {
                status.setText(getString(R.string.launcher_last_run_guest_crash, title));
            }
        } else if (XeniaAndroidSettings.LAST_RUN_STATE_RUNNING.equals(state)) {
            status.setText(getString(R.string.launcher_last_run_maybe_crashed, title));
        } else {
            status.setText(getString(R.string.launcher_last_run_unknown, title));
        }
        status.setVisibility(View.VISIBLE);
    }

    private void refreshRecentGames() {
        final LinearLayout section = findViewById(R.id.launcher_recent_games_section);
        final LinearLayout list = findViewById(R.id.launcher_recent_games_list);
        if (section == null || list == null) {
            return;
        }
        list.removeAllViews();
        final java.util.List<XeniaAndroidSettings.RecentGame> games =
                XeniaAndroidSettings.getRecentGames(this);
        if (games.isEmpty()) {
            section.setVisibility(View.GONE);
            return;
        }
        section.setVisibility(View.VISIBLE);
        for (final XeniaAndroidSettings.RecentGame game : games) {
            if (game.launchUri == null || game.launchUri.isEmpty()) {
                continue;
            }
            final TextView row = new TextView(this);
            row.setText(buildRecentGameText(game));
            row.setTextColor(getResources().getColor(R.color.xenia_text_secondary));
            row.setTextSize(12);
            row.setLetterSpacing(0);
            row.setFocusable(true);
            row.setClickable(true);
            row.setMinHeight(dp(44));
            row.setGravity(android.view.Gravity.CENTER_VERTICAL);
            row.setPadding(dp(12), dp(6), dp(12), dp(6));
            row.setBackgroundResource(R.drawable.launcher_chip);
            row.setOnClickListener(view -> launchGame(Uri.parse(game.launchUri)));
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            params.topMargin = dp(6);
            list.addView(row, params);
        }
        if (list.getChildCount() == 0) {
            section.setVisibility(View.GONE);
        }
    }

    private void scanAndRefreshGameLibrary() {
        final List<XeniaAndroidSettings.GameLibraryEntry> entries =
                XeniaAndroidSettings.scanGameLibrary(this);
        refreshGameLibrary();
        Toast.makeText(
                this,
                getResources().getQuantityString(
                        R.plurals.launcher_game_library_scan_result,
                        entries.size(),
                        entries.size()),
                Toast.LENGTH_SHORT).show();
    }

    private void refreshGameLibrary() {
        final LinearLayout section = findViewById(R.id.launcher_game_library_section);
        final TextView status = findViewById(R.id.launcher_game_library_status);
        final LinearLayout list = findViewById(R.id.launcher_game_library_list);
        if (section == null || status == null || list == null) {
            return;
        }
        list.removeAllViews();
        section.setVisibility(View.VISIBLE);
        final List<XeniaAndroidSettings.GameLibraryEntry> entries =
                XeniaAndroidSettings.getGameLibrary(this);
        if (entries.isEmpty()) {
            status.setText(R.string.launcher_game_library_empty);
            return;
        }
        status.setText(getResources().getQuantityString(
                R.plurals.launcher_game_library_count,
                entries.size(),
                entries.size()));
        for (final XeniaAndroidSettings.GameLibraryEntry entry : entries) {
            if (entry.launchUri == null || entry.launchUri.isEmpty()) {
                continue;
            }
            final TextView row = new TextView(this);
            row.setText(buildGameLibraryText(entry));
            row.setTextColor(getResources().getColor(R.color.xenia_text_secondary));
            row.setTextSize(12);
            row.setLetterSpacing(0);
            row.setFocusable(true);
            row.setClickable(true);
            row.setMinHeight(dp(46));
            row.setGravity(android.view.Gravity.CENTER_VERTICAL);
            row.setPadding(dp(12), dp(6), dp(12), dp(6));
            row.setBackgroundResource(R.drawable.launcher_chip);
            row.setOnClickListener(view -> launchGame(Uri.parse(entry.launchUri), entry.title));
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            params.topMargin = dp(6);
            list.addView(row, params);
        }
    }

    private String buildGameLibraryText(final XeniaAndroidSettings.GameLibraryEntry entry) {
        final String title = entry.title != null && !entry.title.isEmpty()
                ? entry.title
                : getString(R.string.launcher_last_game_unknown);
        final String kind = entry.kind != null && !entry.kind.isEmpty()
                ? entry.kind
                : getString(R.string.launcher_game_library_unknown_kind);
        final String path = entry.path != null && !entry.path.isEmpty()
                ? entry.path
                : entry.launchUri;
        return getString(R.string.launcher_game_library_row, title, kind, path);
    }

    private String buildRecentGameText(final XeniaAndroidSettings.RecentGame game) {
        final String title = game.title != null && !game.title.isEmpty()
                ? game.title
                : getString(R.string.launcher_last_game_unknown);
        return getString(R.string.launcher_recent_game_row, title, labelForGame(game));
    }

    private String labelForGame(final XeniaAndroidSettings.RecentGame game) {
        if (XeniaAndroidSettings.LAST_RUN_STATE_GUEST_CRASH.equals(game.state)) {
            return isGuestHeapDiagnostic(game.diagnostic)
                    ? getString(R.string.launcher_recent_game_guest_heap)
                    : getString(R.string.launcher_recent_game_guest_crash);
        }
        return labelForState(game.state);
    }

    private String labelForState(final String state) {
        if (XeniaAndroidSettings.LAST_RUN_STATE_EXITED_TO_MENU.equals(state)) {
            return getString(R.string.launcher_recent_game_exited);
        }
        if (XeniaAndroidSettings.LAST_RUN_STATE_RUNNING.equals(state)) {
            return getString(R.string.launcher_recent_game_maybe_crashed);
        }
        return getString(R.string.launcher_recent_game_unknown);
    }

    private boolean isGuestHeapDiagnostic(final String diagnostic) {
        if (diagnostic == null) {
            return false;
        }
        final String lower = diagnostic.toLowerCase(Locale.US);
        return lower.contains("heap") || lower.contains("rtlraiseexception");
    }

    private int dp(final int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private String getDisplayName(final Uri uri) {
        if ("content".equalsIgnoreCase(uri.getScheme())) {
            try (Cursor cursor = getContentResolver().query(
                    uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    final int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) {
                        final String displayName = cursor.getString(index);
                        if (displayName != null && !displayName.isEmpty()) {
                            return displayName;
                        }
                    }
                }
            } catch (final RuntimeException ignored) {
                // Fall back to URI parsing if the provider refuses metadata.
            }
        }
        final String segment = uri.getLastPathSegment();
        if (segment == null || segment.isEmpty()) {
            return uri.toString();
        }
        final int slash = Math.max(segment.lastIndexOf('/'), segment.lastIndexOf(':'));
        if (slash >= 0 && slash + 1 < segment.length()) {
            return segment.substring(slash + 1);
        }
        return segment;
    }
}
