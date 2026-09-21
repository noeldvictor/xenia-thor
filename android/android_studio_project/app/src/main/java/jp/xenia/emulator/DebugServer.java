package jp.xenia.emulator;

import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.PixelCopy;
import android.view.SurfaceView;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * The debug server inside the emulator process (2026-09-20). HTTP on port
 * 41337, every interface, debug builds only. It speaks MCP itself on /mcp
 * (JSON-RPC over the streamable HTTP transport: initialize, tools/list,
 * tools/call, ping), so Claude Code registers it in .mcp.json as an http
 * server (adb forward tcp:41337 tcp:41337, or the wifi address) and calls the
 * tools with no program in between. adb remains only for install, port
 * forward, launch, force-stop, and simpleperf. Each tool reads the running
 * emulator directly through the native debug API (debug_api_android.cc) or
 * the activity, so nothing depends on logcat rotation, screencap, or
 * broadcasts. The plain endpoints below stay for curl.
 *
 * POST /mcp         the MCP endpoint (tools: the endpoints below by name)
 * GET  /            endpoint list
 * GET  /status      title, uptime, swap count, precompile, memory, temps
 * GET  /fps         the badge history (fps, swaps, window_ms) of the last 60 windows
 * GET  /threads     kernel threads with host tid, CPU ticks, state, lr, r1, r3
 * GET  /log         ?lines=200&grep=text  the in-process log ring
 * GET  /memory      ?addr=hex&len=n       guest memory as hex
 * GET  /disasm      ?addr=hex&count=n     PowerPC disassembly
 * GET  /gpu         swap count, pipelines created, creation ms
 * GET  /stall       the last spin-lock stall and the three hottest threads
 * GET  /screenshot  PNG of the game surface
 * GET  /toggles     the app menu toggles with their values
 * GET  /cvar        ?name=                a cvar's value
 * POST /cvar        ?name=&value=         set a cvar now (diagnosis only)
 * POST /toggle      ?key=&enabled=1       set a menu toggle (next launch)
 * GET  /backtrace                         native frames of every thread (module+offset)
 * POST /trace_frame                       record the next GPU frame to files/traces (.xtr)
 * POST /trace_stream ?on=1|0              stream every frame to one .xtr until off
 * GET  /trace_frame                       the trace files present
 * GET  /drivers                           installed Vulkan drivers and the selected one
 * POST /drivers     ?use= | ?install_url= | ?install_file= | ?delete=
 * GET  /launch_cvars                      diagnostic cvars for the next launch
 * POST /launch_cvars ?name=&value= | ?clear=1  set, remove, or clear them
 * POST /press       ?button=A&hold_ms=120 one gamepad button
 * POST /route       ?seq=START:150,wait:800,A   a button sequence
 * GET  /frame_stats                       luma, black and hue fractions of the panel
 * POST /goto        ?screen=menu | ?steps=  drive the title to a screen by the panel
 * POST /pause       ?on=1                 pause or resume the emulator
 * POST /stop        end the emulator process
 */
public final class DebugServer {
    private static final String TAG = "xenia-debug";
    public static final int PORT = 41337;

    private static DebugServer sInstance;

    private final EmulatorActivity mActivity;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private ServerSocket mSocket;
    private Thread mAcceptThread;
    private volatile boolean mRunning;

    private static native String nativeStatus();
    private static native String nativeThreads();
    private static native String nativeLogTail(int maxLines, String filter);
    private static native String nativeReadMemory(int address, int length);
    private static native String nativeDisasm(int address, int count);
    private static native String nativeGpu();
    private static native String nativeStall();
    private static native String nativeCvarGet(String name);
    private static native boolean nativeCvarSet(String name, String value);
    private static native String nativeTrapSet(String name, boolean pause, int lr, String dump);
    private static native String nativeTraceFrame(String dir);
    private static native String nativeHostBacktraces();
    private static native String nativeTraceStream(String dir, boolean on);
    private static native String nativeTrapReport();
    private static native void nativeTrapRelease();
    private static native void nativeTrapClear();

    private DebugServer(final EmulatorActivity activity) {
        mActivity = activity;
    }

    public static synchronized void start(final EmulatorActivity activity) {
        if (!BuildConfig.DEBUG) {
            return;
        }
        if (sInstance != null) {
            sInstance.stop();
        }
        sInstance = new DebugServer(activity);
        sInstance.run();
    }

    public static synchronized void stopServer() {
        if (sInstance != null) {
            sInstance.stop();
            sInstance = null;
        }
    }

    private void run() {
        try {
            mSocket = new ServerSocket(PORT);
            mSocket.setReuseAddress(true);
        } catch (IOException e) {
            Log.e(TAG, "cannot listen on " + PORT + ": " + e);
            return;
        }
        mRunning = true;
        mAcceptThread = new Thread(() -> {
            while (mRunning) {
                try {
                    final Socket client = mSocket.accept();
                    final Thread t = new Thread(() -> handle(client), "xenia-debug-client");
                    t.setDaemon(true);
                    t.start();
                } catch (IOException e) {
                    if (mRunning) {
                        Log.w(TAG, "accept: " + e);
                    }
                }
            }
        }, "xenia-debug-accept");
        mAcceptThread.setDaemon(true);
        mAcceptThread.start();
        Log.i(TAG, "debug server listening on port " + PORT);
    }

    private void stop() {
        mRunning = false;
        try {
            if (mSocket != null) {
                mSocket.close();
            }
        } catch (IOException ignored) {
        }
        mSocket = null;
    }

    // ---- HTTP -------------------------------------------------------------

    private void handle(final Socket client) {
        try {
            client.setSoTimeout(15000);
            // Bytes, not a Reader: Content-Length counts bytes, and a UTF-8
            // argument would leave a char reader waiting for chars that never
            // come.
            final java.io.InputStream in = new java.io.BufferedInputStream(client.getInputStream());
            final String requestLine = readLine(in);
            if (requestLine == null) {
                return;
            }
            final String[] parts = requestLine.split(" ");
            if (parts.length < 2) {
                return;
            }
            final String method = parts[0];
            final String target = parts[1];
            int contentLength = 0;
            String line;
            while ((line = readLine(in)) != null && !line.isEmpty()) {
                final String lower = line.toLowerCase(Locale.US);
                if (lower.startsWith("content-length:")) {
                    contentLength = Integer.parseInt(line.substring(15).trim());
                }
            }
            String requestBody = "";
            if (contentLength > 0) {
                final byte[] bytes = new byte[Math.min(contentLength, 1 << 20)];
                int read = 0;
                while (read < bytes.length) {
                    final int n = in.read(bytes, read, bytes.length - read);
                    if (n < 0) {
                        break;
                    }
                    read += n;
                }
                requestBody = new String(bytes, 0, read, StandardCharsets.UTF_8);
            }
            final String path = target.contains("?") ? target.substring(0, target.indexOf('?')) : target;
            final Map<String, String> q = query(target);
            byte[] body;
            String type = "application/json";
            int status = 200;
            try {
                if ("/mcp".equals(path)) {
                    if (!"POST".equals(method)) {
                        status = 405;
                        body = "{\"error\":\"POST JSON-RPC to /mcp\"}".getBytes(StandardCharsets.UTF_8);
                    } else {
                        final String reply = mcp(requestBody);
                        if (reply == null) {
                            status = 202;
                            body = new byte[0];
                        } else {
                            body = reply.getBytes(StandardCharsets.UTF_8);
                        }
                    }
                } else if ("/screenshot".equals(path)) {
                    body = screenshotPng();
                    type = body.length > 0 ? "image/png" : "application/json";
                    if (body.length == 0) {
                        body = "{\"error\":\"no surface or copy failed\"}".getBytes(StandardCharsets.UTF_8);
                    }
                } else {
                    body = route(method, path, q).getBytes(StandardCharsets.UTF_8);
                }
            } catch (Throwable t) {
                Log.e(TAG, path + ": " + t, t);
                body = ("{\"error\":\"" + jsonEscape(String.valueOf(t)) + "\"}")
                        .getBytes(StandardCharsets.UTF_8);
            }
            final OutputStream out = client.getOutputStream();
            final String statusText = status == 200 ? "OK" : status == 202 ? "Accepted" : "Method Not Allowed";
            out.write(("HTTP/1.1 " + status + " " + statusText + "\r\nContent-Type: " + type
                    + "\r\nContent-Length: " + body.length
                    + "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n")
                    .getBytes(StandardCharsets.UTF_8));
            out.write(body);
            out.flush();
        } catch (IOException e) {
            Log.w(TAG, "client: " + e);
        } finally {
            try {
                client.close();
            } catch (IOException ignored) {
            }
        }
    }

    /** One CRLF-terminated line as ISO-8859-1 (headers are ASCII); null at EOF. */
    private static String readLine(final java.io.InputStream in) throws IOException {
        final ByteArrayOutputStream buf = new ByteArrayOutputStream(128);
        int c;
        while ((c = in.read()) >= 0) {
            if (c == '\n') {
                break;
            }
            if (c != '\r') {
                buf.write(c);
            }
        }
        if (c < 0 && buf.size() == 0) {
            return null;
        }
        return new String(buf.toByteArray(), StandardCharsets.ISO_8859_1);
    }

    private static Map<String, String> query(final String target) {
        final Map<String, String> q = new HashMap<>();
        final int i = target.indexOf('?');
        if (i < 0) {
            return q;
        }
        for (final String pair : target.substring(i + 1).split("&")) {
            final int eq = pair.indexOf('=');
            try {
                if (eq < 0) {
                    q.put(URLDecoder.decode(pair, "UTF-8"), "");
                } else {
                    q.put(URLDecoder.decode(pair.substring(0, eq), "UTF-8"),
                            URLDecoder.decode(pair.substring(eq + 1), "UTF-8"));
                }
            } catch (Exception ignored) {
            }
        }
        return q;
    }

    private static int intParam(final Map<String, String> q, final String key, final int def) {
        final String v = q.get(key);
        if (v == null || v.isEmpty()) {
            return def;
        }
        try {
            return Integer.parseInt(v);
        } catch (NumberFormatException e) {
            return def;
        }
    }

    private static int hexParam(final Map<String, String> q, final String key, final int def) {
        String v = q.get(key);
        if (v == null || v.isEmpty()) {
            return def;
        }
        if (v.startsWith("0x") || v.startsWith("0X")) {
            v = v.substring(2);
        }
        try {
            return (int) Long.parseLong(v, 16);
        } catch (NumberFormatException e) {
            return def;
        }
    }

    // ---- routes -----------------------------------------------------------

    private String route(final String method, final String path, final Map<String, String> q) {
        switch (path) {
            case "/":
                return "{\"endpoints\":[\"/status\",\"/fps\",\"/threads\",\"/log\",\"/memory\","
                        + "\"/disasm\",\"/gpu\",\"/stall\",\"/screenshot\",\"/toggles\",\"/cvar\","
                        + "\"/toggle\",\"/press\",\"/route\",\"/pause\",\"/stop\"],\"port\":" + PORT + "}";
            case "/status":
                return status();
            case "/fps":
                return mActivity.debugFpsHistoryJson();
            case "/threads":
                return threads();
            case "/log":
                return nativeLogTail(intParam(q, "lines", 200), q.containsKey("grep") ? q.get("grep") : "");
            case "/memory": {
                final String hex = nativeReadMemory(hexParam(q, "addr", 0), intParam(q, "len", 256));
                return "{\"addr\":\"" + String.format(Locale.US, "%08X", hexParam(q, "addr", 0))
                        + "\",\"hex\":\"" + hex + "\"}";
            }
            case "/disasm":
                return nativeDisasm(hexParam(q, "addr", 0), intParam(q, "count", 32));
            case "/gpu":
                return nativeGpu();
            case "/stall":
                return stall();
            case "/toggles":
                return toggles();
            case "/cvar":
                if ("POST".equals(method)) {
                    final boolean ok = nativeCvarSet(q.get("name"), q.get("value"));
                    return "{\"applied\":" + ok + "}";
                }
                return "{\"name\":\"" + jsonEscape(q.get("name")) + "\",\"value\":\""
                        + jsonEscape(nativeCvarGet(q.get("name"))) + "\"}";
            case "/toggle":
                return setToggle(q.get("key"), "1".equals(q.get("enabled")) || "true".equals(q.get("enabled")));
            case "/launch_cvars":
                return launchCvars(method, q);
            case "/backtrace":
                return nativeHostBacktraces();
            case "/drivers":
                return drivers(method, q);
            case "/trace_stream": {
                final java.io.File dir = new java.io.File(mActivity.getFilesDir(), "traces");
                final boolean on = "1".equals(q.get("on")) || "true".equals(q.get("on"));
                return nativeTraceStream(dir.getAbsolutePath(), on);
            }
            case "/trace_frame": {
                final java.io.File dir = new java.io.File(mActivity.getFilesDir(), "traces");
                if ("POST".equals(method)) {
                    return nativeTraceFrame(dir.getAbsolutePath());
                }
                // GET: the trace files present, newest last.
                final java.io.File[] files = dir.listFiles();
                final StringBuilder sb = new StringBuilder("{\"dir\":\"")
                        .append(jsonEscape(dir.getAbsolutePath())).append("\",\"files\":[");
                if (files != null) {
                    java.util.Arrays.sort(files, (a, b) -> Long.compare(a.lastModified(), b.lastModified()));
                    boolean first = true;
                    for (final java.io.File f : files) {
                        if (!first) {
                            sb.append(',');
                        }
                        first = false;
                        sb.append("{\"name\":\"").append(jsonEscape(f.getName()))
                                .append("\",\"bytes\":").append(f.length()).append('}');
                    }
                }
                return sb.append("]}").toString();
            }
            case "/trap":
                if ("POST".equals(method) && q.containsKey("name")) {
                    return nativeTrapSet(q.get("name"),
                            "1".equals(q.get("pause")) || "true".equals(q.get("pause")),
                            hexParam(q, "lr", 0), q.containsKey("dump") ? q.get("dump") : "");
                }
                return nativeTrapReport();
            case "/trap_release":
                nativeTrapRelease();
                return "{\"released\":true}";
            case "/trap_clear":
                nativeTrapClear();
                return "{\"cleared\":true}";
            case "/press":
                return press(q.get("button"), intParam(q, "hold_ms", 120));
            case "/route":
                return routeSequence(q.get("seq"));
            case "/frame_stats":
                return frameStats();
            case "/goto":
                return gotoScreen(q);
            case "/pause": {
                final boolean on = "1".equals(q.get("on")) || "true".equals(q.get("on"));
                mMainHandler.post(() -> mActivity.debugSetPaused(on));
                return "{\"paused\":" + on + "}";
            }
            case "/stop":
                mMainHandler.postDelayed(() -> {
                    Log.i(TAG, "stop requested through the debug API");
                    mActivity.finish();
                    android.os.Process.killProcess(android.os.Process.myPid());
                }, 200);
                return "{\"stopping\":true}";
            default:
                return "{\"error\":\"unknown endpoint " + jsonEscape(path) + "\"}";
        }
    }

    private String status() {
        final String nativeJson = nativeStatus();
        final StringBuilder sb = new StringBuilder(nativeJson.substring(0, nativeJson.length() - 1));
        sb.append(",\"pid\":").append(android.os.Process.myPid());
        sb.append(",\"resumed\":").append(mActivity.debugIsResumed());
        sb.append(",\"rss_kb\":").append(readProcValue("/proc/self/status", "VmRSS:"));
        sb.append(",\"native_heap_kb\":").append(android.os.Debug.getNativeHeapAllocatedSize() / 1024);
        sb.append(",\"gpu_temp_c\":").append(readSysMilli("/sys/class/kgsl/kgsl-3d0/temp"));
        sb.append(",\"gpu_busy\":\"").append(readSysText("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage")).append("\"");
        sb.append(",\"model\":\"").append(jsonEscape(Build.MODEL)).append("\"");
        sb.append("}");
        return sb.toString();
    }

    /** Per-thread CPU ticks and state from /proc/self/task, keyed by host tid. */
    private static Map<Integer, String> hostThreadRows() {
        final Map<Integer, String> rows = new HashMap<>();
        final File[] tasks = new File("/proc/self/task").listFiles();
        if (tasks == null) {
            return rows;
        }
        for (final File t : tasks) {
            try {
                final int tid = Integer.parseInt(t.getName());
                final String stat = readFile(new File(t, "stat"));
                // "tid (comm) S ... utime stime" - comm may hold spaces.
                final int close = stat.lastIndexOf(')');
                final String[] f = stat.substring(close + 2).split(" ");
                final String comm = stat.substring(stat.indexOf('(') + 1, close);
                final long ticks = Long.parseLong(f[11]) + Long.parseLong(f[12]);
                final String wchan = readFile(new File(t, "wchan")).trim();
                rows.put(tid, "\"comm\":\"" + jsonEscape(comm) + "\",\"host_state\":\"" + f[0]
                        + "\",\"cpu_ticks\":" + ticks + ",\"wchan\":\"" + jsonEscape(wchan) + "\"");
            } catch (Exception ignored) {
            }
        }
        return rows;
    }

    private String threads() {
        // The kernel table walk takes the object table lock. A thread that
        // died inside the fault handler holds the global lock forever, so the
        // walk runs with a 5 s limit and the host rows come back on their own.
        final java.util.concurrent.FutureTask<String> task =
                new java.util.concurrent.FutureTask<>(DebugServer::nativeThreads);
        final Thread t = new Thread(task, "xenia-debug-threads");
        t.setDaemon(true);
        t.start();
        String kernel;
        try {
            kernel = task.get(5, TimeUnit.SECONDS);
        } catch (Exception e) {
            final StringBuilder sb = new StringBuilder(
                    "[{\"note\":\"kernel table locked (a thread died in the fault handler?); host rows only\"}");
            for (final Map.Entry<Integer, String> h : hostThreadRows().entrySet()) {
                sb.append(",{\"host_tid\":").append(h.getKey()).append(',').append(h.getValue()).append('}');
            }
            return sb.append(']').toString();
        }
        final Map<Integer, String> host = hostThreadRows();
        // Merge the host row into each kernel thread by host tid.
        final StringBuilder sb = new StringBuilder();
        int pos = 0;
        while (true) {
            final int i = kernel.indexOf("\"host_tid\":", pos);
            if (i < 0) {
                sb.append(kernel.substring(pos));
                break;
            }
            final int end = kernel.indexOf(',', i);
            // A thread without a host thread yet (or one already gone) reports
            // a host tid outside the int range; it keeps its kernel row only.
            String row = null;
            try {
                final long tid = Long.parseLong(kernel.substring(i + 11, end));
                if (tid > 0 && tid <= Integer.MAX_VALUE) {
                    row = host.get((int) tid);
                }
            } catch (NumberFormatException ignored) {
            }
            sb.append(kernel, pos, end);
            if (row != null) {
                sb.append(',').append(row);
            }
            pos = end;
        }
        return sb.toString();
    }

    private String stall() {
        // Two samples one second apart give the hottest threads by ticks.
        final Map<Integer, String> a = hostThreadRows();
        try {
            Thread.sleep(1000);
        } catch (InterruptedException ignored) {
        }
        final Map<Integer, String> b = hostThreadRows();
        final StringBuilder hot = new StringBuilder("[");
        long[] best = new long[3];
        int[] bestTid = new int[3];
        for (final Map.Entry<Integer, String> e : b.entrySet()) {
            final String before = a.get(e.getKey());
            if (before == null) {
                continue;
            }
            final long delta = ticksOf(e.getValue()) - ticksOf(before);
            for (int k = 0; k < 3; ++k) {
                if (delta > best[k]) {
                    for (int m = 2; m > k; --m) {
                        best[m] = best[m - 1];
                        bestTid[m] = bestTid[m - 1];
                    }
                    best[k] = delta;
                    bestTid[k] = e.getKey();
                    break;
                }
            }
        }
        for (int k = 0; k < 3; ++k) {
            if (best[k] <= 0) {
                break;
            }
            if (k > 0) {
                hot.append(',');
            }
            // 100 ticks per second on this kernel: ticks per second = percent.
            hot.append("{\"host_tid\":").append(bestTid[k]).append(",\"cpu_pct\":").append(best[k])
                    .append(',').append(b.get(bestTid[k])).append('}');
        }
        hot.append(']');
        return "{\"spinlock\":" + nativeStall() + ",\"hot_threads\":" + hot
                + ",\"fps\":" + mActivity.debugFpsHistoryJson() + ",\"gpu\":" + nativeGpu() + "}";
    }

    private static long ticksOf(final String row) {
        final int i = row.indexOf("\"cpu_ticks\":");
        final int end = row.indexOf(',', i);
        return Long.parseLong(row.substring(i + 12, end));
    }

    private String toggles() {
        final SharedPreferences prefs = XeniaAndroidSettings.getPreferences(mActivity);
        final StringBuilder sb = new StringBuilder("[");
        boolean first = true;
        for (final XeniaOptimizations.Optimization opt : XeniaOptimizations.ALL) {
            if (!first) {
                sb.append(',');
            }
            first = false;
            sb.append("{\"key\":\"").append(opt.prefKey).append("\",\"title\":\"")
                    .append(jsonEscape(opt.title)).append("\",\"default\":").append(opt.defaultEnabled)
                    .append(",\"enabled\":").append(prefs.getBoolean(opt.prefKey, opt.defaultEnabled))
                    .append('}');
        }
        return sb.append(']').toString();
    }

    private String setToggle(final String key, final boolean enabled) {
        if (key == null) {
            return "{\"ok\":false,\"reason\":\"key missing\"}";
        }
        for (final XeniaOptimizations.Optimization opt : XeniaOptimizations.ALL) {
            if (opt.prefKey.equals(key)) {
                XeniaAndroidSettings.getPreferences(mActivity).edit().putBoolean(key, enabled).commit();
                return "{\"ok\":true,\"key\":\"" + key + "\",\"enabled\":" + enabled
                        + ",\"note\":\"applies at the next launch\"}";
            }
        }
        return "{\"ok\":false,\"reason\":\"" + jsonEscape(key) + " is not an app toggle\"}";
    }

    /**
     * GET /drivers: the installed Vulkan drivers (GpuDriverManager) and the
     * selected id. POST ?use=<id> selects one for the next launch ("" = the
     * system driver); ?install_url=<url> installs a Turnip zip from a URL
     * (latest://owner/repo for a GitHub latest release), ?install_file=<path>
     * a zip already on the device (the PC pushes it with run-as). A driver
     * is a paradigm axis on its own (Turnip weekly builds, a custom fork), so
     * a run must be able to name the one it used: the launch log prints it.
     */
    private String drivers(final String method, final Map<String, String> q) {
        String note = "";
        if ("POST".equals(method)) {
            try {
                if (q.containsKey("install_url")) {
                    final GpuDriverPackage pkg = GpuDriverManager.installFromUrl(mActivity, q.get("install_url"));
                    note = "installed " + pkg.id;
                } else if (q.containsKey("install_file")) {
                    try (java.io.FileInputStream in = new java.io.FileInputStream(q.get("install_file"))) {
                        final GpuDriverPackage pkg = GpuDriverManager.installFromZip(mActivity, in);
                        note = "installed " + pkg.id;
                    }
                }
                if (q.containsKey("use")) {
                    final String id = q.get("use");
                    boolean known = id.isEmpty();
                    for (final GpuDriverPackage pkg : GpuDriverManager.listInstalled(mActivity)) {
                        if (pkg.id.equals(id)) {
                            known = true;
                        }
                    }
                    if (!known) {
                        return "{\"ok\":false,\"reason\":\"no installed driver " + jsonEscape(id) + "\"}";
                    }
                    GpuDriverManager.setSelectedId(mActivity, id);
                    note = (note.isEmpty() ? "" : note + "; ") + "selected " + (id.isEmpty() ? "system" : id)
                            + " for the next launch";
                }
                if (q.containsKey("delete")) {
                    GpuDriverManager.delete(mActivity, q.get("delete"));
                    note = (note.isEmpty() ? "" : note + "; ") + "deleted " + q.get("delete");
                }
            } catch (Exception e) {
                return "{\"ok\":false,\"reason\":\"" + jsonEscape(String.valueOf(e)) + "\"}";
            }
        }
        final StringBuilder sb = new StringBuilder("{\"ok\":true,\"selected\":\"")
                .append(jsonEscape(GpuDriverManager.getSelectedId(mActivity)))
                .append("\",\"bundled_version\":\"").append(jsonEscape(GpuDriverManager.BUNDLED_TURNIP_VERSION))
                .append("\",\"note\":\"").append(jsonEscape(note)).append("\",\"installed\":[");
        boolean first = true;
        for (final GpuDriverPackage pkg : GpuDriverManager.listInstalled(mActivity)) {
            if (!first) {
                sb.append(',');
            }
            first = false;
            sb.append("{\"id\":\"").append(jsonEscape(pkg.id)).append("\",\"name\":\"").append(jsonEscape(pkg.name))
                    .append("\",\"driver_version\":\"").append(jsonEscape(pkg.driverVersion))
                    .append("\",\"package_version\":\"").append(jsonEscape(pkg.packageVersion))
                    .append("\",\"library\":\"").append(jsonEscape(pkg.libraryName)).append("\"}");
        }
        sb.append("],\"recommended\":[");
        first = true;
        for (final GpuDriverManager.Recommended r : GpuDriverManager.RECOMMENDED) {
            if (!first) {
                sb.append(',');
            }
            first = false;
            sb.append("{\"title\":\"").append(jsonEscape(r.title)).append("\",\"url\":\"").append(jsonEscape(r.url))
                    .append("\",\"recommended\":").append(r.recommended).append('}');
        }
        return sb.append("]}").toString();
    }

    // Diagnostic cvars for the next launch (debug builds): the properties
    // file EmulatorActivity applies last. GET lists; POST name+value sets one,
    // clear=1 removes all, or name with an empty value removes one.
    private String launchCvars(final String method, final Map<String, String> q) {
        final java.io.File file = new java.io.File(mActivity.getFilesDir(),
                EmulatorActivity.DEBUG_LAUNCH_CVARS_FILE);
        final java.util.Properties props = new java.util.Properties();
        if (file.isFile()) {
            try (java.io.FileInputStream in = new java.io.FileInputStream(file)) {
                props.load(in);
            } catch (java.io.IOException ignored) {
            }
        }
        if ("POST".equals(method)) {
            if ("1".equals(q.get("clear")) || "true".equals(q.get("clear"))) {
                props.clear();
            } else if (q.containsKey("name")) {
                final String value = q.get("value");
                if (value == null || value.isEmpty()) {
                    props.remove(q.get("name"));
                } else {
                    props.setProperty(q.get("name"), value);
                }
            }
            try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
                props.store(out, "diagnostic cvars for the next launch (debug server)");
            } catch (java.io.IOException e) {
                return "{\"error\":\"" + jsonEscape(String.valueOf(e)) + "\"}";
            }
        }
        final StringBuilder sb = new StringBuilder("{\"file\":\"").append(jsonEscape(file.getAbsolutePath()))
                .append("\",\"debug_build\":").append(BuildConfig.DEBUG).append(",\"cvars\":{");
        boolean first = true;
        for (final String name : new java.util.TreeSet<>(props.stringPropertyNames())) {
            if (!first) {
                sb.append(',');
            }
            first = false;
            sb.append('"').append(jsonEscape(name)).append("\":\"")
                    .append(jsonEscape(props.getProperty(name))).append('"');
        }
        return sb.append("},\"note\":\"applies at the next launch, after the profile and the toggles\"}").toString();
    }

    private static final Map<String, Integer> BUTTONS = new HashMap<>();

    static {
        BUTTONS.put("A", 96);
        BUTTONS.put("B", 97);
        BUTTONS.put("X", 99);
        BUTTONS.put("Y", 100);
        BUTTONS.put("DPAD_UP", 19);
        BUTTONS.put("DPAD_DOWN", 20);
        BUTTONS.put("DPAD_LEFT", 21);
        BUTTONS.put("DPAD_RIGHT", 22);
        BUTTONS.put("START", 108);
        BUTTONS.put("BACK", 109);
        BUTTONS.put("LB", 102);
        BUTTONS.put("RB", 103);
        BUTTONS.put("LT", 104);
        BUTTONS.put("RT", 105);
        BUTTONS.put("LS", 106);
        BUTTONS.put("RS", 107);
        BUTTONS.put("GUIDE", 110);
    }

    private String press(final String button, final int holdMs) {
        if (button == null) {
            return "{\"ok\":false,\"reason\":\"button missing\"}";
        }
        final Integer code = BUTTONS.get(button.toUpperCase(Locale.US));
        if (code == null) {
            return "{\"ok\":false,\"reason\":\"unknown button " + jsonEscape(button) + "\"}";
        }
        final int hold = Math.max(1, Math.min(2000, holdMs));
        mMainHandler.post(() -> mActivity.debugInjectKey(code, hold));
        return "{\"ok\":true,\"button\":\"" + button.toUpperCase(Locale.US) + "\",\"hold_ms\":" + hold + "}";
    }

    /** "START:150,wait:800,A" - presses and waits, run on this client thread. */
    private String routeSequence(final String seq) {
        if (seq == null || seq.isEmpty()) {
            return "{\"ok\":false,\"reason\":\"seq missing\"}";
        }
        final StringBuilder done = new StringBuilder("[");
        for (final String item : seq.split(",")) {
            final String[] p = item.trim().split(":");
            if (p.length == 0 || p[0].isEmpty()) {
                continue;
            }
            if (p[0].equalsIgnoreCase("wait")) {
                final int ms = p.length > 1 ? Integer.parseInt(p[1]) : 500;
                try {
                    Thread.sleep(Math.min(ms, 30000));
                } catch (InterruptedException ignored) {
                }
                done.append("\"wait ").append(ms).append("\",");
                continue;
            }
            final int hold = p.length > 1 ? Integer.parseInt(p[1]) : 120;
            press(p[0], hold);
            try {
                Thread.sleep(hold + 250);
            } catch (InterruptedException ignored) {
            }
            done.append('"').append(p[0].toUpperCase(Locale.US)).append("\",");
        }
        if (done.length() > 1) {
            done.setLength(done.length() - 1);
        }
        return "{\"ok\":true,\"done\":" + done + "]}";
    }

    /** The game surface as a bitmap (PixelCopy on the main thread), or null. */
    private Bitmap captureBitmap() {
        final SurfaceView view = mActivity.debugSurfaceView();
        if (view == null || view.getWidth() == 0 || view.getHeight() == 0) {
            return null;
        }
        final Bitmap bitmap = Bitmap.createBitmap(view.getWidth(), view.getHeight(), Bitmap.Config.ARGB_8888);
        final CountDownLatch latch = new CountDownLatch(1);
        final int[] result = {-1};
        mMainHandler.post(() -> {
            try {
                PixelCopy.request(view, bitmap, r -> {
                    result[0] = r;
                    latch.countDown();
                }, mMainHandler);
            } catch (Throwable t) {
                latch.countDown();
            }
        });
        try {
            latch.await(5, TimeUnit.SECONDS);
        } catch (InterruptedException ignored) {
        }
        if (result[0] != PixelCopy.SUCCESS) {
            Log.w(TAG, "PixelCopy result " + result[0]);
            return null;
        }
        return bitmap;
    }

    private long swapCount() {
        try {
            return new JSONObject(nativeGpu()).optLong("swap_count", 0);
        } catch (JSONException e) {
            return 0;
        }
    }

    private ScreenRoutes.Stats currentStats() {
        final Bitmap bitmap = captureBitmap();
        if (bitmap == null) {
            return null;
        }
        return ScreenRoutes.compute(bitmap, swapCount());
    }

    /** GET /frame_stats: what is on the panel as numbers a predicate can use. */
    private String frameStats() {
        final ScreenRoutes.Stats stats = currentStats();
        return stats == null ? "{\"error\":\"no surface or copy failed\"}" : stats.toJson();
    }

    /**
     * POST /goto: drive the title to a screen by what is on the panel.
     * screen=<preset of the running title> or steps=<raw steps>; runs on this
     * client thread and answers when the last predicate holds or a step
     * times out: {ok, reached, step, seconds, stats, log[]}.
     */
    private String gotoScreen(final Map<String, String> q) {
        String steps = q.get("steps");
        if (steps == null || steps.isEmpty()) {
            // A preset needs the title id; right after a launch it is still 0
            // while the module loads, so wait for it.
            String titleId = "";
            final long waitStart = System.currentTimeMillis();
            while (System.currentTimeMillis() - waitStart < 90000) {
                try {
                    titleId = new JSONObject(nativeStatus()).optString("title_id", "");
                } catch (JSONException ignored) {
                }
                if (!titleId.isEmpty() && !"00000000".equals(titleId)) {
                    break;
                }
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    break;
                }
            }
            steps = ScreenRoutes.preset(titleId, q.get("screen"));
            if (steps == null) {
                return "{\"ok\":false,\"reason\":\"no preset " + jsonEscape(String.valueOf(q.get("screen")))
                        + " for title " + jsonEscape(titleId) + "; pass steps=\"}";
            }
        }
        final java.util.List<ScreenRoutes.Step> route = ScreenRoutes.parse(steps);
        if (route.isEmpty()) {
            return "{\"ok\":false,\"reason\":\"no steps\"}";
        }
        final long t0 = System.currentTimeMillis();
        final StringBuilder log = new StringBuilder("[");
        ScreenRoutes.Stats stats = null;
        int index = 0;
        for (final ScreenRoutes.Step step : route) {
            final long stepStart = System.currentTimeMillis();
            boolean holds = false;
            while (System.currentTimeMillis() - stepStart < step.timeoutS * 1000L) {
                stats = currentStats();
                if (stats != null && ScreenRoutes.eval(step.until, stats)) {
                    holds = true;
                    break;
                }
                try {
                    Thread.sleep(700);
                } catch (InterruptedException e) {
                    break;
                }
            }
            if (log.length() > 1) {
                log.append(',');
            }
            log.append(String.format(Locale.US, "{\"step\":%d,\"name\":\"%s\",\"holds\":%b,\"at_s\":%.1f,\"stats\":%s}",
                    index, jsonEscape(step.name), holds, (System.currentTimeMillis() - t0) / 1000.0,
                    stats == null ? "null" : stats.toJson()));
            if (!holds) {
                return "{\"ok\":false,\"reached\":false,\"step\":" + index + ",\"reason\":\"timeout waiting for "
                        + jsonEscape(step.until) + "\",\"seconds\":" + (System.currentTimeMillis() - t0) / 1000
                        + ",\"stats\":" + (stats == null ? "null" : stats.toJson()) + ",\"log\":" + log + "]}";
            }
            if (!step.press.isEmpty()) {
                press(step.press, step.holdMs);
                try {
                    Thread.sleep(step.settleMs);
                } catch (InterruptedException e) {
                    break;
                }
            }
            ++index;
        }
        return "{\"ok\":true,\"reached\":true,\"step\":" + (index - 1) + ",\"seconds\":"
                + (System.currentTimeMillis() - t0) / 1000 + ",\"stats\":" + (stats == null ? "null" : stats.toJson())
                + ",\"log\":" + log + "]}";
    }

    private byte[] screenshotPng() {
        final SurfaceView view = mActivity.debugSurfaceView();
        if (view == null || view.getWidth() == 0 || view.getHeight() == 0) {
            return new byte[0];
        }
        final Bitmap bitmap = Bitmap.createBitmap(view.getWidth(), view.getHeight(), Bitmap.Config.ARGB_8888);
        final CountDownLatch latch = new CountDownLatch(1);
        final int[] result = {-1};
        mMainHandler.post(() -> {
            try {
                PixelCopy.request(view, bitmap, r -> {
                    result[0] = r;
                    latch.countDown();
                }, mMainHandler);
            } catch (Throwable t) {
                latch.countDown();
            }
        });
        try {
            latch.await(5, TimeUnit.SECONDS);
        } catch (InterruptedException ignored) {
        }
        if (result[0] != PixelCopy.SUCCESS) {
            Log.w(TAG, "PixelCopy result " + result[0]);
            return new byte[0];
        }
        final ByteArrayOutputStream bytes = new ByteArrayOutputStream(1 << 20);
        bitmap.compress(Bitmap.CompressFormat.PNG, 90, bytes);
        return bytes.toByteArray();
    }

    // ---- MCP ----------------------------------------------------------------

    private static final String[][] TOOLS = {
            // name, description, JSON schema properties (a JSON object text)
            {"status", "Title, guest and host uptime, swap count, precompile state, memory, GPU temperature and busy.", "{}"},
            {"fps", "The FPS badge history: the last 60 half-second windows with fps, swaps, window_ms.", "{}"},
            {"threads", "Every kernel thread: id, name, host tid, CPU ticks, host state, wait channel, guest state and wait reason, lr, r1, r3, r13. Each guest thread also carries its call chain and the printable text on its stack.", "{}"},
            {"log", "The in-process log ring (last 8,192 lines; logcat rotation never touches it). lines (default 200), grep (substring filter).",
                    "{\"lines\":{\"type\":\"integer\"},\"grep\":{\"type\":\"string\"}}"},
            {"memory", "Guest memory as hex. addr (hex string), len (bytes, max 4 MB).",
                    "{\"addr\":{\"type\":\"string\"},\"len\":{\"type\":\"integer\"}}"},
            {"disasm", "PowerPC disassembly from guest memory. addr (hex string), count (instructions, max 4096).",
                    "{\"addr\":{\"type\":\"string\"},\"count\":{\"type\":\"integer\"}}"},
            {"gpu", "Swap count, graphics pipelines created, milliseconds spent creating them. A rising count during play is shader compilation on the command processor thread.", "{}"},
            {"stall", "The stall picture: the last spin-lock stall, the three hottest threads over one second, the fps history, the GPU counters. Takes one second.", "{}"},
            {"screenshot", "PNG of the game surface (PixelCopy). Always this app, never another one.", "{}"},
            {"toggles", "The app menu toggles (XeniaOptimizations) with default and current value.", "{}"},
            {"toggle_set", "Set a menu toggle exactly as tapping it; applies at the next launch. key, enabled.",
                    "{\"key\":{\"type\":\"string\"},\"enabled\":{\"type\":\"boolean\"}}"},
            {"cvar_get", "Read a cvar's current value. name.", "{\"name\":{\"type\":\"string\"}}"},
            {"backtrace", "Host (native) backtrace of every thread from inside the process: module+offset frames per thread with its wchan. The PC symbolizes them (xenia_backtrace). The hang picture: which host lock or wait each guest thread sits in.", "{}"},
            {"trace_frame", "Record the next GPU frame as an .xtr trace in files/traces (POST); GET lists the files. The PC replays a trace with xenia-gpu-vulkan-trace-dump: a device-only glitch splits into the command stream and the device's execution of it.", "{}"},
            {"trace_stream", "Stream every GPU frame to one .xtr in files/traces while on (on=true), stop with on=false. For the last frame before a title stops swapping; the PC dumps any frame of it with --trace_dump_frame.", "{\"on\":{\"type\":\"boolean\"}}"},
            {"frame_stats", "What is on the panel as numbers: mean luma of the frame and each half, black fractions, saturated-hue fractions (red yellow green cyan blue magenta), gold, swaps. The keys a goto predicate uses.", "{}"},
            {"goto", "Drive the running title to a screen by what is on the panel, not by a clock. screen: a preset of the title (Banjo: title, menu, world), or steps: 'until:gold>0.35;press:START;settle:1500|until:lower_black>0.4'. Answers when the last predicate holds: {ok, reached, step, seconds, stats, log}.",
                    "{\"screen\":{\"type\":\"string\"},\"steps\":{\"type\":\"string\"}}"},
            {"drivers", "The Vulkan drivers installed in the app (Turnip builds) and the selected one. use=<id> selects one for the next launch ('' = system); install_url=<url or latest://owner/repo> installs a zip; install_file=<device path> installs a pushed zip; delete=<id>. No arguments: list.",
                    "{\"use\":{\"type\":\"string\"},\"install_url\":{\"type\":\"string\"},\"install_file\":{\"type\":\"string\"},\"delete\":{\"type\":\"string\"}}"},
            {"launch_cvars", "Diagnostic cvars applied at the NEXT launch, after the profile and the toggles (debug builds; init-time cvars such as render_target_path_vulkan or a GPU trace). No arguments: list. name+value: set one; empty value: remove one; clear=1: remove all.",
                    "{\"name\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"},\"clear\":{\"type\":\"boolean\"}}"},
            {"cvar_set", "Set a cvar in the running process (diagnosis only; toggles are the control surface). name, value.",
                    "{\"name\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"}}"},
            {"press", "Press one gamepad button: A B X Y DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT START BACK LB RB LT RT LS RS GUIDE. hold_ms default 120.",
                    "{\"button\":{\"type\":\"string\"},\"hold_ms\":{\"type\":\"integer\"}}"},
            {"route", "A button sequence, comma separated: START:150,wait:800,A. Waits are in ms.",
                    "{\"seq\":{\"type\":\"string\"}}"},
            {"pause", "Pause (on=true) or resume the emulator.", "{\"on\":{\"type\":\"boolean\"}}"},
            {"trap", "A breakpoint on a kernel export, no rebuild: arm with name (e.g. XamShowDirtyDiscErrorUI) and pause=true to hold the calling guest thread at the hit; the other threads run. lr (hex) limits hits to one guest call site. dump (rN:len or hexaddr:len, comma list) writes memory as hex to the log at every hit. Without name: the report of the last hit: registers, 256 stack words, the guest call chain, the memory at r24-r31 with one dereference (request objects), and the text on the stack (thread, all 32 guest registers, lr, ctr, 256 stack words from r1). Then use memory and disasm, and trap_release.",
                    "{\"name\":{\"type\":\"string\"},\"pause\":{\"type\":\"boolean\"},\"lr\":{\"type\":\"string\"},\"dump\":{\"type\":\"string\"}}"},
            {"trap_release", "Release the guest thread held by the trap.", "{}"},
            {"trap_clear", "Disarm the trap and release any held thread.", "{}"},
            {"stop", "End the emulator process.", "{}"},
    };

    /** One JSON-RPC message (or a batch) in; the reply text, or null for a notification (202). */
    private String mcp(final String body) {
        try {
            final String trimmed = body.trim();
            if (trimmed.startsWith("[")) {
                final JSONArray in = new JSONArray(trimmed);
                final JSONArray out = new JSONArray();
                for (int i = 0; i < in.length(); ++i) {
                    final JSONObject r = mcpOne(in.getJSONObject(i));
                    if (r != null) {
                        out.put(r);
                    }
                }
                return out.length() == 0 ? null : out.toString();
            }
            final JSONObject r = mcpOne(new JSONObject(trimmed));
            return r == null ? null : r.toString();
        } catch (JSONException e) {
            return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\""
                    + jsonEscape(e.getMessage()) + "\"}}";
        }
    }

    private JSONObject mcpOne(final JSONObject msg) throws JSONException {
        final String method = msg.optString("method", "");
        if (!msg.has("id")) {
            return null;  // a notification (notifications/initialized) or a client response
        }
        final Object id = msg.get("id");
        final JSONObject reply = new JSONObject();
        reply.put("jsonrpc", "2.0");
        reply.put("id", id);
        try {
            switch (method) {
                case "initialize": {
                    final JSONObject result = new JSONObject();
                    final JSONObject params = msg.optJSONObject("params");
                    result.put("protocolVersion", params != null
                            ? params.optString("protocolVersion", "2025-03-26") : "2025-03-26");
                    result.put("capabilities", new JSONObject().put("tools", new JSONObject()));
                    result.put("serverInfo", new JSONObject().put("name", "xenia-thor-device")
                            .put("version", "1.0"));
                    result.put("instructions", "This server runs inside the xenia-thor emulator on the "
                            + "AYN Thor. Every tool reads or drives the running game directly. "
                            + "Rules: never use adb input keyevent; use press and route. "
                            + "Say the battery level from status when you launch.");
                    reply.put("result", result);
                    break;
                }
                case "ping":
                    reply.put("result", new JSONObject());
                    break;
                case "tools/list": {
                    final JSONArray tools = new JSONArray();
                    for (final String[] t : TOOLS) {
                        final JSONObject schema = new JSONObject();
                        schema.put("type", "object");
                        schema.put("properties", new JSONObject(t[2]));
                        tools.put(new JSONObject().put("name", t[0]).put("description", t[1])
                                .put("inputSchema", schema));
                    }
                    reply.put("result", new JSONObject().put("tools", tools));
                    break;
                }
                case "tools/call": {
                    final JSONObject params = msg.getJSONObject("params");
                    final String name = params.getString("name");
                    final JSONObject args = params.optJSONObject("arguments");
                    reply.put("result", callTool(name, args != null ? args : new JSONObject()));
                    break;
                }
                default:
                    reply.put("error", new JSONObject().put("code", -32601)
                            .put("message", "unknown method " + method));
            }
        } catch (Throwable t) {
            Log.e(TAG, "mcp " + method + ": " + t, t);
            reply.put("error", new JSONObject().put("code", -32603).put("message", String.valueOf(t)));
        }
        return reply;
    }

    private static final Map<String, String> TOOL_PATHS = new HashMap<>();

    static {
        TOOL_PATHS.put("status", "/status");
        TOOL_PATHS.put("fps", "/fps");
        TOOL_PATHS.put("threads", "/threads");
        TOOL_PATHS.put("log", "/log");
        TOOL_PATHS.put("memory", "/memory");
        TOOL_PATHS.put("disasm", "/disasm");
        TOOL_PATHS.put("gpu", "/gpu");
        TOOL_PATHS.put("stall", "/stall");
        TOOL_PATHS.put("toggles", "/toggles");
        TOOL_PATHS.put("toggle_set", "/toggle");
        TOOL_PATHS.put("cvar_get", "/cvar");
        TOOL_PATHS.put("launch_cvars", "/launch_cvars");
        TOOL_PATHS.put("drivers", "/drivers");
        TOOL_PATHS.put("frame_stats", "/frame_stats");
        TOOL_PATHS.put("goto", "/goto");
        TOOL_PATHS.put("trace_frame", "/trace_frame");
        TOOL_PATHS.put("backtrace", "/backtrace");
        TOOL_PATHS.put("trace_stream", "/trace_stream");
        TOOL_PATHS.put("cvar_set", "/cvar");
        TOOL_PATHS.put("press", "/press");
        TOOL_PATHS.put("route", "/route");
        TOOL_PATHS.put("pause", "/pause");
        TOOL_PATHS.put("trap", "/trap");
        TOOL_PATHS.put("trap_release", "/trap_release");
        TOOL_PATHS.put("trap_clear", "/trap_clear");
        TOOL_PATHS.put("stop", "/stop");
    }

    private JSONObject callTool(final String name, final JSONObject args) throws JSONException {
        final JSONObject result = new JSONObject();
        final JSONArray content = new JSONArray();
        if ("screenshot".equals(name)) {
            final byte[] png = screenshotPng();
            if (png.length == 0) {
                content.put(new JSONObject().put("type", "text").put("text", "no surface or copy failed"));
                result.put("isError", true);
            } else {
                content.put(new JSONObject().put("type", "image")
                        .put("data", android.util.Base64.encodeToString(png, android.util.Base64.NO_WRAP))
                        .put("mimeType", "image/png"));
            }
            result.put("content", content);
            return result;
        }
        final String path = TOOL_PATHS.get(name);
        if (path == null) {
            content.put(new JSONObject().put("type", "text").put("text", "unknown tool " + name));
            result.put("content", content).put("isError", true);
            return result;
        }
        final Map<String, String> q = new HashMap<>();
        final java.util.Iterator<String> keys = args.keys();
        while (keys.hasNext()) {
            final String k = keys.next();
            q.put(k, String.valueOf(args.get(k)));
        }
        final String method = "cvar_get".equals(name) || "frame_stats".equals(name) || "backtrace".equals(name) ? "GET" : "POST";
        final String text = route(method, path, q);
        content.put(new JSONObject().put("type", "text").put("text", text));
        result.put("content", content);
        result.put("isError", text.startsWith("{\"error\""));
        return result;
    }

    // ---- small helpers ----------------------------------------------------

    static String jsonEscape(final String s) {
        if (s == null) {
            return "";
        }
        final StringBuilder sb = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); ++i) {
            final char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) {
                        sb.append(String.format(Locale.US, "\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
            }
        }
        return sb.toString();
    }

    private static String readFile(final File f) {
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                new java.io.FileInputStream(f), StandardCharsets.UTF_8))) {
            final StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) {
                sb.append(line).append('\n');
            }
            return sb.toString();
        } catch (IOException e) {
            return "";
        }
    }

    private static String readSysText(final String path) {
        return readFile(new File(path)).trim();
    }

    private static String readSysMilli(final String path) {
        final String t = readSysText(path);
        try {
            return String.format(Locale.US, "%.1f", Long.parseLong(t) / 1000.0);
        } catch (NumberFormatException e) {
            return "-1";
        }
    }

    private static long readProcValue(final String path, final String key) {
        for (final String line : readFile(new File(path)).split("\n")) {
            if (line.startsWith(key)) {
                final String[] f = line.substring(key.length()).trim().split("\\s+");
                try {
                    return Long.parseLong(f[0]);
                } catch (NumberFormatException e) {
                    return -1;
                }
            }
        }
        return -1;
    }
}
