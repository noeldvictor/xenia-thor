package jp.xenia.emulator;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

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
    private static final int GAME_TILE_COLUMNS = 4;
    private static final int LAUNCHER_TAB_GAMES = 0;
    private static final int LAUNCHER_TAB_RECENT = 1;
    private static final int LAUNCHER_TAB_BROWSE = 2;
    private static final int LAUNCHER_TAB_TOOLS = 3;
    private static final Pattern DISC_PATTERN = Pattern.compile(
            "(?i)(?:disc|disk|cd)\\s*([0-9]+)");
    private static final Pattern TITLE_ID_PATTERN = Pattern.compile("^[0-9A-Fa-f]{8}$");
    private int activeLauncherTab = LAUNCHER_TAB_GAMES;

    private static final class GameLibraryGroup {
        public final String title;
        public String kind;
        public String coverLookupPath;
        public final ArrayList<XeniaAndroidSettings.GameLibraryEntry> entries =
                new ArrayList<>();

        public GameLibraryGroup(final String title) {
            this.title = title;
        }
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_launcher);
        XeniaAndroidSettings.ensureInitialized(this);
        promoteLibrarySections();
        refreshLastRunStatus();
        refreshGameLibrary();
        refreshRecentGames();
        setLauncherTab(activeLauncherTab);
        focusPrimaryLauncherTarget();
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
        refreshLastRunStatus();
        refreshGameLibrary();
        refreshRecentGames();
        setLauncherTab(activeLauncherTab);
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

    public void onLauncherGamesTabClick(final View view) {
        setLauncherTab(LAUNCHER_TAB_GAMES);
        focusPrimaryLauncherTarget();
    }

    public void onLauncherRecentTabClick(final View view) {
        setLauncherTab(LAUNCHER_TAB_RECENT);
        focusPrimaryLauncherTarget();
    }

    public void onLauncherBrowseTabClick(final View view) {
        setLauncherTab(LAUNCHER_TAB_BROWSE);
        focusPrimaryLauncherTarget();
    }

    public void onLauncherToolsTabClick(final View view) {
        setLauncherTab(LAUNCHER_TAB_TOOLS);
        focusPrimaryLauncherTarget();
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
        emulatorIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        final Bundle emulatorLaunchArguments =
                XeniaAndroidSettings.createLaunchArguments(this, uri);
        emulatorLaunchArguments.putAll(getDebugLaunchOverrides());
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

    private Bundle getDebugLaunchOverrides() {
        final Bundle overrides = new Bundle();
        if (!BuildConfig.DEBUG) {
            return overrides;
        }
        final Intent intent = getIntent();
        if (intent == null) {
            return overrides;
        }
        copyIntExtra(intent, overrides, "arm64_speed_profile_interval_ms");
        copyIntExtra(intent, overrides, "arm64_speed_profile_top_functions");
        copyIntExtra(intent, overrides, "arm64_speed_profile_min_delta");
        copyIntExtra(intent, overrides, "log_level");
        copyBooleanExtra(intent, overrides, "disassemble_functions");
        copyStringExtra(intent, overrides, "disassemble_function_filter");
        copyBooleanExtra(intent, overrides, "arm64_speed_profile_thread_snapshot");
        copyBooleanExtra(intent, overrides, "arm64_speed_profile_thread_snapshot_on_idle");
        copyBooleanExtra(intent, overrides, "xboxkrnl_thread_wait_trace");
        copyIntExtra(intent, overrides, "xboxkrnl_thread_wait_trace_budget");
        copyIntExtra(intent, overrides, "xboxkrnl_thread_wait_trace_after_ms");
        copyStringExtra(intent, overrides, "xboxkrnl_thread_wait_trace_guest_tids");
        copyBooleanExtra(intent, overrides, "xboxkrnl_physical_memory_audit");
        copyIntExtra(intent, overrides, "xboxkrnl_physical_memory_audit_budget");
        copyBooleanExtra(intent, overrides, "xboxkrnl_physical_suballocation_audit");
        copyIntExtra(intent, overrides, "xboxkrnl_physical_suballocation_audit_budget");
        copyBooleanExtra(intent, overrides, "xboxkrnl_reenter_audit");
        copyIntExtra(intent, overrides, "xboxkrnl_reenter_audit_budget");
        copyStringExtra(intent, overrides, "xboxkrnl_reenter_audit_guest_tids");
        copyBooleanExtra(intent, overrides, "android_xam_keyboard_ime");
        return overrides;
    }

    private static void copyBooleanExtra(
            final Intent intent, final Bundle bundle, final String name) {
        if (intent.hasExtra(name)) {
            bundle.putBoolean(name, intent.getBooleanExtra(name, false));
        }
    }

    private static void copyIntExtra(
            final Intent intent, final Bundle bundle, final String name) {
        if (intent.hasExtra(name)) {
            bundle.putInt(name, intent.getIntExtra(name, 0));
        }
    }

    private static void copyStringExtra(
            final Intent intent, final Bundle bundle, final String name) {
        if (intent.hasExtra(name)) {
            final String value = intent.getStringExtra(name);
            if (value != null) {
                bundle.putString(name, value);
            }
        }
    }

    private void rememberLastGame(final Uri uri) {
        rememberLastGame(uri, getDisplayName(uri));
    }

    private void rememberLastGame(final Uri uri, final String displayTitle) {
        XeniaAndroidSettings.getPreferences(this).edit()
                .putString(XeniaAndroidSettings.KEY_LAST_GAME_URI, uri.toString())
                .putString(XeniaAndroidSettings.KEY_LAST_GAME_TITLE, displayTitle)
                .apply();
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
            final View row = buildRecentGameRow(game);
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

    private void setLauncherTab(final int tab) {
        activeLauncherTab = tab;
        final LinearLayout librarySection = findViewById(R.id.launcher_game_library_section);
        final LinearLayout recentSection = findViewById(R.id.launcher_recent_games_section);
        final LinearLayout recentList = findViewById(R.id.launcher_recent_games_list);
        if (librarySection != null) {
            librarySection.setVisibility(tab == LAUNCHER_TAB_RECENT ? View.GONE : View.VISIBLE);
        }
        if (recentSection != null) {
            final boolean hasRecent = recentList != null && recentList.getChildCount() > 0;
            recentSection.setVisibility(
                    tab == LAUNCHER_TAB_RECENT && hasRecent ? View.VISIBLE : View.GONE);
        }
        updateLauncherTab(R.id.launcher_games_tab, tab == LAUNCHER_TAB_GAMES);
        updateLauncherTab(R.id.launcher_recent_tab, tab == LAUNCHER_TAB_RECENT);
        updateLauncherTab(R.id.launcher_browse_tab, tab == LAUNCHER_TAB_BROWSE);
        updateLauncherTab(R.id.launcher_tools_tab, tab == LAUNCHER_TAB_TOOLS);
    }

    private void updateLauncherTab(final int viewId, final boolean active) {
        final TextView tab = findViewById(viewId);
        if (tab == null) {
            return;
        }
        tab.setTextColor(getResources().getColor(
                active ? R.color.xenia_green_soft : R.color.xenia_text_secondary));
        tab.setAlpha(active ? 1.0f : 0.78f);
    }

    private void scanAndRefreshGameLibrary() {
        final List<XeniaAndroidSettings.GameLibraryEntry> entries =
                XeniaAndroidSettings.scanGameLibrary(this);
        refreshGameLibrary();
        setLauncherTab(LAUNCHER_TAB_GAMES);
        focusPrimaryLauncherTarget();
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
        final List<GameLibraryGroup> groups = buildGameLibraryGroups(entries);
        if (groups.isEmpty()) {
            status.setText(R.string.launcher_game_library_empty);
            return;
        }
        if (groups.size() == entries.size()) {
            status.setText(getResources().getQuantityString(
                    R.plurals.launcher_game_library_count,
                    groups.size(),
                    groups.size()));
        } else {
            status.setText(getString(
                    R.string.launcher_game_library_group_count,
                    groups.size(),
                    entries.size()));
        }
        LinearLayout currentRow = null;
        int column = 0;
        for (final GameLibraryGroup group : groups) {
            if (currentRow == null || column >= GAME_TILE_COLUMNS) {
                currentRow = new LinearLayout(this);
                currentRow.setOrientation(LinearLayout.HORIZONTAL);
                final LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT);
                rowParams.topMargin = dp(8);
                list.addView(currentRow, rowParams);
                column = 0;
            }
            final View tile = buildGameLibraryTile(group);
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    0,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    1);
            if (column > 0) {
                params.leftMargin = dp(8);
            }
            currentRow.addView(tile, params);
            column++;
        }
        if (currentRow != null) {
            while (column > 0 && column < GAME_TILE_COLUMNS) {
                final View spacer = new View(this);
                final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                        0,
                        1,
                        1);
                params.leftMargin = dp(8);
                currentRow.addView(spacer, params);
                column++;
            }
        }
    }

    private void promoteLibrarySections() {
        final LinearLayout root = findViewById(R.id.launcher_root);
        if (root == null) {
            return;
        }
        moveChildToIndex(root, R.id.launcher_recent_games_section, 2);
        moveChildToIndex(root, R.id.launcher_game_library_section, 2);
    }

    private void moveChildToIndex(
            final LinearLayout root, final int childId, final int targetIndex) {
        final View child = findViewById(childId);
        if (child == null || child.getParent() != root) {
            return;
        }
        final int safeIndex = Math.max(0, Math.min(targetIndex, root.getChildCount() - 1));
        final int currentIndex = root.indexOfChild(child);
        if (currentIndex == safeIndex) {
            return;
        }
        root.removeView(child);
        root.addView(child, safeIndex);
    }

    private void focusPrimaryLauncherTarget() {
        if (activeLauncherTab == LAUNCHER_TAB_RECENT) {
            final LinearLayout recentList = findViewById(R.id.launcher_recent_games_list);
            if (recentList != null && recentList.getChildCount() > 0) {
                recentList.getChildAt(0).requestFocus();
                return;
            }
        } else if (activeLauncherTab == LAUNCHER_TAB_BROWSE) {
            final View scanGamesCard = findViewById(R.id.launcher_scan_games_card);
            if (scanGamesCard != null) {
                scanGamesCard.requestFocus();
                return;
            }
        } else if (activeLauncherTab == LAUNCHER_TAB_TOOLS) {
            final View settingsCard = findViewById(R.id.launcher_settings_card);
            if (settingsCard != null) {
                settingsCard.requestFocus();
                return;
            }
        }
        final LinearLayout libraryList = findViewById(R.id.launcher_game_library_list);
        if (libraryList != null && libraryList.getChildCount() > 0) {
            final View firstRow = libraryList.getChildAt(0);
            if (firstRow instanceof LinearLayout
                    && ((LinearLayout) firstRow).getChildCount() > 0) {
                ((LinearLayout) firstRow).getChildAt(0).requestFocus();
                return;
            }
            firstRow.requestFocus();
            return;
        }
        final View scanGamesCard = findViewById(R.id.launcher_scan_games_card);
        if (scanGamesCard != null) {
            scanGamesCard.requestFocus();
        }
    }

    private void showDiscPicker(final GameLibraryGroup group) {
        final String[] labels = new String[group.entries.size()];
        for (int i = 0; i < group.entries.size(); ++i) {
            final XeniaAndroidSettings.GameLibraryEntry entry = group.entries.get(i);
            String discLabel = discBadgeFor(entry.title + " " + entry.path + " " + entry.launchUri);
            if (discLabel.isEmpty()) {
                discLabel = "Disc " + (i + 1);
            }
            final String kind = entry.kind != null && !entry.kind.isEmpty()
                    ? entry.kind.toUpperCase(Locale.US)
                    : getString(R.string.launcher_game_library_unknown_kind);
            labels[i] = discLabel + " - " + kind;
        }
        new AlertDialog.Builder(this)
                .setTitle(group.title)
                .setItems(labels, (dialog, which) -> {
                    final XeniaAndroidSettings.GameLibraryEntry entry = group.entries.get(which);
                    final String discLabel = discBadgeFor(
                            entry.title + " " + entry.path + " " + entry.launchUri);
                    final String launchTitle = discLabel.isEmpty()
                            ? group.title
                            : group.title + " - " + discLabel;
                    showGameActions(entry, launchTitle);
                })
                .show();
    }

    private List<GameLibraryGroup> buildGameLibraryGroups(
            final List<XeniaAndroidSettings.GameLibraryEntry> entries) {
        final LinkedHashMap<String, GameLibraryGroup> groups = new LinkedHashMap<>();
        for (final XeniaAndroidSettings.GameLibraryEntry entry : entries) {
            if (entry.launchUri == null || entry.launchUri.isEmpty()) {
                continue;
            }
            final String rawTitle = entry.title != null && !entry.title.isEmpty()
                    ? entry.title
                    : getString(R.string.launcher_last_game_unknown);
            final String discBadge = discBadgeFor(
                    rawTitle + " " + entry.path + " " + entry.launchUri);
            final String displayTitle = displayTitleWithoutDisc(rawTitle);
            final String normalizedTitle = normalizedGameGroupKey(displayTitle);
            final String key = discBadge.isEmpty()
                    ? normalizedTitle + "|" + entry.launchUri
                    : normalizedTitle;
            GameLibraryGroup group = groups.get(key);
            if (group == null) {
                group = new GameLibraryGroup(displayTitle);
                groups.put(key, group);
            }
            if (group.kind == null || group.kind.isEmpty()) {
                group.kind = entry.kind != null && !entry.kind.isEmpty()
                        ? entry.kind.toUpperCase(Locale.US)
                        : getString(R.string.launcher_game_library_unknown_kind);
            }
            if (group.coverLookupPath == null || group.coverLookupPath.isEmpty()) {
                group.coverLookupPath = entry.path != null && !entry.path.isEmpty()
                        ? entry.path
                        : entry.launchUri;
            }
            group.entries.add(entry);
        }
        return new ArrayList<>(groups.values());
    }

    private View buildGameLibraryTile(final GameLibraryGroup group) {
        final XeniaAndroidSettings.GameLibraryEntry firstEntry = group.entries.get(0);
        final String title = group.title != null && !group.title.isEmpty()
                ? group.title
                : getString(R.string.launcher_last_game_unknown);
        final String kind = group.kind != null && !group.kind.isEmpty()
                ? group.kind
                : getString(R.string.launcher_game_library_unknown_kind);
        final String path = group.coverLookupPath != null && !group.coverLookupPath.isEmpty()
                ? group.coverLookupPath
                : firstEntry.launchUri;
        final String discBadge = discBadgeFor(
                firstEntry.title + " " + firstEntry.path + " " + firstEntry.launchUri);
        final boolean multiDisc = group.entries.size() > 1;
        return makeGameTile(
                kind,
                title,
                multiDisc
                        ? getResources().getQuantityString(
                        R.plurals.launcher_game_library_disc_count,
                        group.entries.size(),
                        group.entries.size())
                        : discBadge.isEmpty()
                                ? getString(R.string.launcher_game_library_ready)
                                : discBadge,
                multiDisc
                        ? kind + " / "
                                + getString(R.string.launcher_game_library_disc_picker_detail)
                        : discBadge.isEmpty() ? kind : kind + " / " + discBadge,
                path,
                view -> {
                    if (multiDisc) {
                        showDiscPicker(group);
                    } else {
                        showGameActions(firstEntry, title);
                    }
                });
    }

    private View buildRecentGameRow(final XeniaAndroidSettings.RecentGame game) {
        final String title = game.title != null && !game.title.isEmpty()
                ? game.title
                : getString(R.string.launcher_last_game_unknown);
        final String status = labelForGame(game);
        return makeGameRow(
                getString(R.string.launcher_recent_game_badge),
                title,
                status,
                status,
                game.target,
                view -> showGameActions(
                        Uri.parse(game.launchUri), title, game.target, game.launchUri));
    }

    private void showGameActions(
            final XeniaAndroidSettings.GameLibraryEntry entry,
            final String displayTitle) {
        if (entry == null || entry.launchUri == null || entry.launchUri.isEmpty()) {
            return;
        }
        showGameActions(
                Uri.parse(entry.launchUri),
                displayTitle,
                entry.path,
                entry.launchUri);
    }

    private void showGameActions(
            final Uri launchUri,
            final String displayTitle,
            final String path,
            final String launchUriText) {
        final String title = displayTitle != null && !displayTitle.isEmpty()
                ? displayTitle
                : getDisplayName(launchUri);
        final String titleId = resolveTitleIdForSaveTools(title, path, launchUriText);
        final ArrayList<String> labels = new ArrayList<>();
        labels.add(getString(R.string.launcher_game_action_start));
        if (!titleId.isEmpty()) {
            labels.add(getString(R.string.launcher_game_action_backup_save));
            labels.add(getString(R.string.launcher_game_action_reset_save));
        }
        final String[] items = labels.toArray(new String[0]);
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setItems(items, (dialog, which) -> {
                    if (which == 0) {
                        launchGame(launchUri, title);
                    } else if (which == 1) {
                        backupSaveData(titleId, title);
                    } else if (which == 2) {
                        confirmResetSaveData(titleId, title);
                    }
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private String resolveTitleIdForSaveTools(
            final String title,
            final String path,
            final String launchUri) {
        final String titleId = XeniaCoverArt.findCachedTitleId(
                this, title, path + " " + launchUri);
        if (!titleId.isEmpty() && TITLE_ID_PATTERN.matcher(titleId).matches()) {
            return titleId.toUpperCase(Locale.US);
        }
        final String lookup = (title + " " + path + " " + launchUri).toLowerCase(Locale.US);
        if (lookup.contains("burnout revenge")) {
            return "454107DC";
        }
        if (lookup.contains("project sylpheed")) {
            return "535107D4";
        }
        return "";
    }

    private void backupSaveData(final String titleId, final String title) {
        final File source = getSaveDataDirectory(titleId);
        if (!source.isDirectory()) {
            Toast.makeText(this, R.string.launcher_game_save_missing, Toast.LENGTH_SHORT).show();
            return;
        }
        final File destination = getSaveBackupDirectory(titleId, title);
        try {
            copyDirectory(source, destination);
            Toast.makeText(this, R.string.launcher_game_save_backed_up, Toast.LENGTH_SHORT)
                    .show();
        } catch (final IOException exception) {
            Toast.makeText(this, R.string.launcher_game_save_backup_failed, Toast.LENGTH_SHORT)
                    .show();
        }
    }

    private void confirmResetSaveData(final String titleId, final String title) {
        final File source = getSaveDataDirectory(titleId);
        if (!source.isDirectory()) {
            Toast.makeText(this, R.string.launcher_game_save_missing, Toast.LENGTH_SHORT).show();
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle(R.string.launcher_game_action_reset_save)
                .setMessage(getString(R.string.launcher_game_save_reset_message, title))
                .setPositiveButton(R.string.launcher_game_save_reset_confirm,
                        (dialog, which) -> resetSaveData(titleId, title))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void resetSaveData(final String titleId, final String title) {
        final File source = getSaveDataDirectory(titleId);
        if (!source.isDirectory()) {
            Toast.makeText(this, R.string.launcher_game_save_missing, Toast.LENGTH_SHORT).show();
            return;
        }
        final File backup = getSaveBackupDirectory(titleId, title);
        final File parent = backup.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            Toast.makeText(this, R.string.launcher_game_save_reset_failed, Toast.LENGTH_SHORT)
                    .show();
            return;
        }
        if (!source.renameTo(backup)) {
            try {
                copyDirectory(source, backup);
                deleteRecursively(source);
            } catch (final IOException exception) {
                Toast.makeText(this, R.string.launcher_game_save_reset_failed,
                        Toast.LENGTH_SHORT).show();
                return;
            }
        }
        Toast.makeText(this, R.string.launcher_game_save_reset_done, Toast.LENGTH_SHORT).show();
    }

    private File getSaveDataDirectory(final String titleId) {
        return new File(new File(getFilesDir(), "content"), titleId);
    }

    private File getSaveBackupDirectory(final String titleId, final String title) {
        final SimpleDateFormat format =
                new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US);
        final String safeTitle = sanitizeFileName(title);
        final String name = titleId + "-"
                + (safeTitle.isEmpty() ? "save" : safeTitle)
                + "-" + format.format(new Date());
        return new File(new File(getFilesDir(), "save-backups"), name);
    }

    private static void copyDirectory(final File source, final File destination)
            throws IOException {
        if (source.isDirectory()) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Unable to create " + destination);
            }
            final File[] children = source.listFiles();
            if (children == null) {
                return;
            }
            for (final File child : children) {
                copyDirectory(child, new File(destination, child.getName()));
            }
            return;
        }
        final File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Unable to create " + parent);
        }
        final FileInputStream input = new FileInputStream(source);
        try {
            final FileOutputStream output = new FileOutputStream(destination);
            try {
                final byte[] buffer = new byte[16 * 1024];
                while (true) {
                    final int read = input.read(buffer);
                    if (read < 0) {
                        break;
                    }
                    output.write(buffer, 0, read);
                }
            } finally {
                output.close();
            }
        } finally {
            input.close();
        }
    }

    private static void deleteRecursively(final File file) throws IOException {
        if (file == null || !file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            final File[] children = file.listFiles();
            if (children != null) {
                for (final File child : children) {
                    deleteRecursively(child);
                }
            }
        }
        if (!file.delete() && file.exists()) {
            throw new IOException("Unable to delete " + file);
        }
    }

    private static String sanitizeFileName(final String value) {
        if (value == null || value.isEmpty()) {
            return "";
        }
        return value.replaceAll("[^A-Za-z0-9._-]+", "_")
                .replaceAll("_+", "_")
                .replaceAll("^_+|_+$", "");
    }

    private View makeGameTile(
            final String coverText,
            final String title,
            final String status,
            final String detail,
            final String coverLookupPath,
            final View.OnClickListener clickListener) {
        final LinearLayout tile = new LinearLayout(this);
        tile.setOrientation(LinearLayout.VERTICAL);
        tile.setGravity(Gravity.CENTER_HORIZONTAL);
        tile.setMinimumHeight(dp(192));
        tile.setPadding(dp(8), dp(8), dp(8), dp(8));
        tile.setBackgroundResource(R.drawable.launcher_game_tile);
        tile.setFocusable(true);
        tile.setClickable(true);
        tile.setOnClickListener(clickListener);

        final FrameLayout coverFrame = new FrameLayout(this);
        coverFrame.setBackgroundResource(R.drawable.launcher_cover_placeholder);
        tile.addView(coverFrame, new LinearLayout.LayoutParams(dp(94), dp(124)));

        final ImageView coverImage = new ImageView(this);
        coverImage.setScaleType(ImageView.ScaleType.CENTER_CROP);
        coverImage.setVisibility(View.GONE);
        coverFrame.addView(coverImage, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        final TextView cover = new TextView(this);
        cover.setGravity(Gravity.CENTER);
        cover.setText(coverText);
        cover.setTextColor(getResources().getColor(R.color.xenia_blue));
        cover.setTextSize(11);
        cover.setTypeface(Typeface.DEFAULT_BOLD);
        cover.setLetterSpacing(0);
        coverFrame.addView(cover, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        XeniaCoverArt.loadInto(this, coverImage, cover, title, coverLookupPath);

        final TextView titleView = new TextView(this);
        titleView.setText(title);
        titleView.setTextColor(getResources().getColor(R.color.xenia_text));
        titleView.setTextSize(12);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        titleView.setLetterSpacing(0);
        titleView.setMaxLines(2);
        titleView.setGravity(Gravity.CENTER);
        titleView.setEllipsize(TextUtils.TruncateAt.END);
        final LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        titleParams.topMargin = dp(7);
        tile.addView(titleView, titleParams);

        final TextView statusView = new TextView(this);
        statusView.setBackgroundResource(R.drawable.launcher_status_badge);
        statusView.setGravity(Gravity.CENTER);
        statusView.setPadding(dp(8), 0, dp(8), 0);
        statusView.setText(status);
        statusView.setTextColor(getResources().getColor(R.color.xenia_green_soft));
        statusView.setTextSize(10);
        statusView.setTypeface(Typeface.DEFAULT_BOLD);
        statusView.setSingleLine(true);
        statusView.setEllipsize(TextUtils.TruncateAt.END);
        final LinearLayout.LayoutParams statusParams =
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT, dp(24));
        statusParams.topMargin = dp(6);
        tile.addView(statusView, statusParams);

        final TextView detailView = new TextView(this);
        detailView.setText(detail);
        detailView.setTextColor(getResources().getColor(R.color.xenia_text_secondary));
        detailView.setTextSize(10);
        detailView.setLetterSpacing(0);
        detailView.setSingleLine(true);
        detailView.setGravity(Gravity.CENTER);
        detailView.setEllipsize(TextUtils.TruncateAt.END);
        final LinearLayout.LayoutParams detailParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        detailParams.topMargin = dp(4);
        tile.addView(detailView, detailParams);

        return tile;
    }

    private View makeGameRow(
            final String coverText,
            final String title,
            final String detail,
            final String status,
            final String coverLookupPath,
            final View.OnClickListener clickListener) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(108));
        row.setPadding(dp(12), dp(8), dp(12), dp(8));
        row.setBackgroundResource(R.drawable.launcher_game_tile);
        row.setFocusable(true);
        row.setClickable(true);
        row.setOnClickListener(clickListener);

        final FrameLayout coverFrame = new FrameLayout(this);
        coverFrame.setBackgroundResource(R.drawable.launcher_cover_placeholder);
        final LinearLayout.LayoutParams coverParams =
                new LinearLayout.LayoutParams(dp(74), dp(94));
        row.addView(coverFrame, coverParams);

        final ImageView coverImage = new ImageView(this);
        coverImage.setScaleType(ImageView.ScaleType.CENTER_CROP);
        coverImage.setVisibility(View.GONE);
        coverFrame.addView(coverImage, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        final TextView cover = new TextView(this);
        cover.setGravity(Gravity.CENTER);
        cover.setText(coverText);
        cover.setTextColor(getResources().getColor(R.color.xenia_blue));
        cover.setTextSize(11);
        cover.setTypeface(Typeface.DEFAULT_BOLD);
        cover.setLetterSpacing(0);
        coverFrame.addView(cover, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        XeniaCoverArt.loadInto(this, coverImage, cover, title, coverLookupPath);

        final LinearLayout textColumn = new LinearLayout(this);
        textColumn.setOrientation(LinearLayout.VERTICAL);
        textColumn.setGravity(Gravity.CENTER_VERTICAL);
        final LinearLayout.LayoutParams textParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        textParams.leftMargin = dp(14);
        row.addView(textColumn, textParams);

        final TextView titleView = new TextView(this);
        titleView.setText(title);
        titleView.setTextColor(getResources().getColor(R.color.xenia_text));
        titleView.setTextSize(16);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        titleView.setLetterSpacing(0);
        titleView.setSingleLine(true);
        titleView.setEllipsize(TextUtils.TruncateAt.END);
        textColumn.addView(titleView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        final TextView detailView = new TextView(this);
        detailView.setText(detail);
        detailView.setTextColor(getResources().getColor(R.color.xenia_text_secondary));
        detailView.setTextSize(12);
        detailView.setLetterSpacing(0);
        detailView.setSingleLine(true);
        detailView.setEllipsize(TextUtils.TruncateAt.END);
        final LinearLayout.LayoutParams detailParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        detailParams.topMargin = dp(3);
        textColumn.addView(detailView, detailParams);

        final TextView statusView = new TextView(this);
        statusView.setBackgroundResource(R.drawable.launcher_status_badge);
        statusView.setGravity(Gravity.CENTER);
        statusView.setMinWidth(dp(64));
        statusView.setPadding(dp(9), 0, dp(9), 0);
        statusView.setText(status);
        statusView.setTextColor(getResources().getColor(R.color.xenia_green_soft));
        statusView.setTextSize(11);
        statusView.setTypeface(Typeface.DEFAULT_BOLD);
        statusView.setSingleLine(true);
        statusView.setEllipsize(TextUtils.TruncateAt.END);
        final LinearLayout.LayoutParams statusParams =
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT, dp(30));
        statusParams.leftMargin = dp(10);
        row.addView(statusView, statusParams);

        return row;
    }

    private String compactPath(final String path) {
        if (path == null || path.isEmpty()) {
            return "";
        }
        final String normalized = path.replace('\\', '/');
        final int lastSlash = normalized.lastIndexOf('/');
        if (lastSlash <= 0 || lastSlash >= normalized.length() - 1) {
            return normalized;
        }
        final int previousSlash = normalized.lastIndexOf('/', lastSlash - 1);
        if (previousSlash < 0 || previousSlash >= lastSlash - 1) {
            return normalized.substring(lastSlash + 1);
        }
        return normalized.substring(previousSlash + 1);
    }

    private String discBadgeFor(final String value) {
        if (value == null || value.isEmpty()) {
            return "";
        }
        final Matcher matcher = DISC_PATTERN.matcher(value);
        if (matcher.find()) {
            return "Disc " + matcher.group(1);
        }
        return "";
    }

    private String displayTitleWithoutDisc(final String value) {
        if (value == null || value.isEmpty()) {
            return "";
        }
        String title = DISC_PATTERN.matcher(value).replaceAll("");
        title = title.replaceAll("(?i)\\s*\\([^\\)]*\\)", " ");
        title = title.replaceAll("(?i)\\s*\\[[^\\]]*\\]", " ");
        title = title.replaceAll("\\s*-\\s*$", "");
        title = title.replaceAll("\\s{2,}", " ").trim();
        return title.isEmpty() ? value : title;
    }

    private String normalizedGameGroupKey(final String value) {
        final String base = value == null ? "" : value.toLowerCase(Locale.US);
        final String normalized = base.replaceAll("[^a-z0-9]+", " ").trim();
        return normalized.isEmpty() ? base : normalized;
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
