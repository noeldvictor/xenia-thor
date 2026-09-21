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
 * POST /press       ?button=A&hold_ms=120 one gamepad button
 * POST /route       ?seq=START:150,wait:800,A   a button sequence
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
            case "/press":
                return press(q.get("button"), intParam(q, "hold_ms", 120));
            case "/route":
                return routeSequence(q.get("seq"));
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
            final int tid = Integer.parseInt(kernel.substring(i + 11, end));
            sb.append(kernel, pos, end);
            final String row = host.get(tid);
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
            {"threads", "Every kernel thread: id, name, host tid, CPU ticks, host state, wait channel, guest state and wait reason, lr, r1, r3, r13.", "{}"},
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
            {"cvar_set", "Set a cvar in the running process (diagnosis only; toggles are the control surface). name, value.",
                    "{\"name\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"}}"},
            {"press", "Press one gamepad button: A B X Y DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT START BACK LB RB LT RT LS RS GUIDE. hold_ms default 120.",
                    "{\"button\":{\"type\":\"string\"},\"hold_ms\":{\"type\":\"integer\"}}"},
            {"route", "A button sequence, comma separated: START:150,wait:800,A. Waits are in ms.",
                    "{\"seq\":{\"type\":\"string\"}}"},
            {"pause", "Pause (on=true) or resume the emulator.", "{\"on\":{\"type\":\"boolean\"}}"},
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
        TOOL_PATHS.put("cvar_set", "/cvar");
        TOOL_PATHS.put("press", "/press");
        TOOL_PATHS.put("route", "/route");
        TOOL_PATHS.put("pause", "/pause");
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
        final String method = "cvar_get".equals(name) ? "GET" : "POST";
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
