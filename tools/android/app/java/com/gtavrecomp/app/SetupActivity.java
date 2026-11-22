package com.gtavrecomp.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * First screen of the public APK, which holds no game code and no game files, only the tools to
 * make them, as the desktop downloads do. The player brings their discs: Disc 1 and Disc 2 (ISO
 * images) and Title Update 27 (the TU file). Setup then runs on the phone, like the PC installer:
 *  1. the game files are copied from the discs and checked file by file into the app's own folder
 *     (libgtav_setup.so, the same installer as on the PC and iPhone);
 *  2. the game executable is recompiled to C++ and compiled into the player's own game library
 *     (codegen, clang and lld from the APK's native library folder, the build kit from its assets)
 *     in the app's private storage (shared storage is mounted noexec);
 *  3. shaders are translated while playing (XenosRecomp + dxc from the APK, shader_compiler.cpp).
 * When the game files and a game library for this APK are in place, this screen starts the game.
 */
public class SetupActivity extends Activity {
    private static final String TAG = "GtavSetup";
    static {
        System.loadLibrary("gtav_setup");
    }

    private native String nativeInstall(String home, String expected, String disc1, String disc2, String tu);
    private static native boolean nativeIsDataInstalled(String home);
    private native String nativeAdopt(String home, String expected);
    private native String nativeBuildGame(String kit, String tools, String home, String build, String outLib);

    // Slot 0 is unused (it held the PC-made game pack before the phone built the game itself).
    private static final String[] LABELS = {
        null,
        "Disc 1 (Install) image",
        "Disc 2 (Play) image",
        "Title Update 27 (tu00000009_00000000)",
    };

    private final Uri[] picked = new Uri[4];
    private final TextView[] pickedText = new TextView[4];
    private final Button[] pickButtons = new Button[4];
    private TextView status, log;
    private ProgressBar progress;
    private Button install;
    private volatile boolean busy;
    private final AtomicBoolean cancelled = new AtomicBoolean(false);
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences("setup", MODE_PRIVATE);
        if (!GameActivity.isReleaseLayout(this)) {
            // Developer APK (game code inside libmain.so, files pushed with adb): no setup.
            startGame();
            return;
        }
        addSetupShortcut();
        // "Run setup again" (long-press the app icon) opens this screen even when the game is
        // installed: to install from other discs or build the game again.
        boolean again = getIntent() != null && getIntent().getBooleanExtra(EXTRA_SETUP, false);
        if (!again && gameCurrent() == null && nativeIsDataInstalled(home().getAbsolutePath())) {
            startGame();
            return;
        }
        for (int i = 1; i < 4; i++) {
            String s = prefs.getString("uri" + i, null);
            if (s != null) picked[i] = Uri.parse(s);
        }
        setContentView(buildUi());
        refresh();
    }

    private static final String EXTRA_SETUP = "com.gtavrecomp.app.SETUP";

    /** The launcher shortcut "Run setup again" (dynamic, so the APK needs no resources). */
    private void addSetupShortcut() {
        try {
            android.content.pm.ShortcutManager sm = getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null) return;
            Intent i = new Intent(this, SetupActivity.class)
                .setAction(Intent.ACTION_MAIN)
                .putExtra(EXTRA_SETUP, true)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
            android.content.pm.ShortcutInfo info = new android.content.pm.ShortcutInfo.Builder(this, "setup")
                .setShortLabel("Run setup again")
                .setLongLabel("Run setup again")
                .setIntent(i)
                .build();
            sm.setDynamicShortcuts(java.util.Collections.singletonList(info));
        } catch (Exception e) {
            Log.w(TAG, "shortcut: " + e);
        }
    }

    // --- state ----------------------------------------------------------------------------------

    /** Game data folder: the app's external files dir (data/{game,update,user}, spirv/, logs/). */
    private File home() {
        return getExternalFilesDir(null);
    }

    /** Game files copied over from a PC install (not yet checked). */
    private boolean copied() {
        File data = new File(home(), "data");
        return new File(data, "game/default.xex").exists() && new File(data, "update").isDirectory()
            && new File(data, "user").isDirectory()
            && !new File(data, "install_state.txt").exists();  // that is a disc install to resume
    }

    /** The build kit unpacked from the APK's assets (manifest, hashes, headers: no game code). */
    private File kitDir() {
        return new File(getFilesDir(), "kit");
    }

    private File expectedFile() {
        return new File(kitDir(), "tools/game_dll/expected.txt");
    }

    private File gameLib() {
        return new File(getFilesDir(), "libgtav_game.so");
    }

    /** Which libgtav_fast.so the game library was built against (it binds to its hooks). */
    private File gameInfo() {
        return new File(getFilesDir(), "game.txt");
    }

    /** null when the built game library fits this APK, else what is wrong (for the status line). */
    private String gameCurrent() {
        if (!gameLib().exists() || !gameInfo().exists()) return "The game is not built yet.";
        String shell = readShell(gameInfo());
        String mine = sha256(new File(getApplicationInfo().nativeLibraryDir, "libgtav_fast.so"));
        if (shell == null || !shell.equals(mine))
            return "This app was updated: the game is built again (the game files stay).";
        return null;
    }

    private static String readShell(File info) {
        try (BufferedReader r = new BufferedReader(new FileReader(info))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("shell ")) return line.substring(6).trim();
            }
        } catch (Exception e) {
            Log.w(TAG, "pack.txt: " + e);
        }
        return null;
    }

    private static String sha256(File f) {
        try (InputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0;) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (Exception e) {
            return null;
        }
    }

    // --- UI -------------------------------------------------------------------------------------

    private View buildUi() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);
        box.setBackgroundColor(Color.rgb(16, 20, 24));

        TextView title = text("GTA V (Xbox 360 recompilation)", 24);
        box.addView(title);
        box.addView(text("This app contains no game. It installs your own copy, the same way the PC "
            + "version does: copy your disc images and title update to this phone (for example to "
            + "Download), choose them below and press Install. The game files are copied and checked "
            + "(about 16 GB), then the game is recompiled and built on this phone (about 30 to 60 "
            + "minutes: keep the phone plugged in; you can switch apps, the build carries on). The "
            + "first minutes of play are slower while the shaders are translated.", 14));
        box.addView(text("Short on space? Copy the game files from a PC install instead of the discs: open this "
            + "app once, then copy the folders game, update and user from the data folder of your PC "
            + "install into " + new File(home(), "data") + " (by USB) and press Install without choosing "
            + "anything. That needs about 16 GB instead of about 40.", 13));
        for (int i = 1; i < 4; i++) {
            final int slot = i;
            box.addView(text(LABELS[i], 15));
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            pickButtons[i] = new Button(this);
            pickButtons[i].setText("Choose...");
            pickButtons[i].setOnClickListener(v -> pick(slot));
            row.addView(pickButtons[i]);
            pickedText[i] = text("", 13);
            pickedText[i].setPadding(pad / 2, 0, 0, 0);
            row.addView(pickedText[i]);
            box.addView(row);
        }
        install = new Button(this);
        install.setText("Install");
        install.setOnClickListener(v -> onInstall());
        box.addView(install);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        box.addView(progress);
        status = text("", 15);
        box.addView(status);
        log = text("", 12);
        log.setTextColor(Color.rgb(150, 160, 170));
        box.addView(log);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(box);
        return scroll;
    }

    private TextView text(String s, int sp) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(Color.WHITE);
        t.setPadding(0, 8, 0, 4);
        return t;
    }

    private void refresh() {
        String game = gameCurrent();
        boolean data = nativeIsDataInstalled(home().getAbsolutePath());
        for (int i = 1; i < 4; i++) {
            String name = picked[i] != null ? displayName(picked[i]) : null;
            if (data && picked[i] == null) name = "(game files installed)";
            else if (copied() && picked[i] == null) name = "(not needed: copied game files found)";
            pickedText[i].setText(name != null ? name : "not chosen");
            pickButtons[i].setEnabled(!busy);
        }
        install.setText(busy ? "Cancel" : "Install");
        if (!busy) {
            if (game == null && data) status.setText("Ready.");
            else if (data) status.setText(game + " Press Install.");
            else status.setText(copied() ? "Game files copied from a PC found: press Install to check them."
                : "Game files not installed yet.");
        }
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception e) {
            // the file was moved or deleted
        }
        return uri.getLastPathSegment();
    }

    private void pick(int slot) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, slot);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null || request < 1 || request > 3) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "persist " + uri + ": " + e);
        }
        picked[request] = uri;
        prefs.edit().putString("uri" + request, uri.toString()).apply();
        refresh();
    }

    // --- install --------------------------------------------------------------------------------

    private void onInstall() {
        if (busy) {
            cancelled.set(true);
            status.setText("Cancelling...");
            return;
        }
        cancelled.set(false);
        busy = true;
        log.setText("");
        refresh();
        // A foreground service (and its wake lock) keeps the process and the CPU going while the
        // player switches apps or the screen turns off: the build takes a while.
        BuildService.start(this);
        new Thread(() -> {
            String error = runInstall();
            BuildService.stop(this);
            runOnUiThread(() -> {
                busy = false;
                refresh();
                if (error == null) {
                    // Installed: the discs are not needed again (they may be deleted to save space), so
                    // forget them; a later setup then checks copied files instead.
                    android.content.SharedPreferences.Editor e = prefs.edit();
                    for (int i = 1; i < 4; i++) {
                        e.remove("uri" + i);
                        picked[i] = null;
                    }
                    e.apply();
                    startGame();
                } else {
                    status.setText(cancelled.get() ? "Cancelled. Install again to continue where it stopped." : "Stopped.");
                    if (!cancelled.get()) message("Setup stopped", error);
                }
            });
        }, "gtav-setup").start();
    }

    /** On the install thread. null = done. */
    private String runInstall() {
        try {
            String e = ensureKit();
            if (e != null) return e;
            if (!nativeIsDataInstalled(home().getAbsolutePath())) {
                // Copied from a PC (and no discs chosen): check those instead of installing from discs.
                if (copied() && picked[1] == null && picked[2] == null && picked[3] == null) {
                    e = nativeAdopt(home().getAbsolutePath(), expectedFile().getAbsolutePath());
                } else {
                    e = installFromDiscs();
                }
                if (e != null) return e;
            }
            if (cancelled.get()) return "Cancelled.";
            return gameCurrent() == null ? null : buildGame();
        } catch (Exception e) {
            Log.e(TAG, "install", e);
            return e.toString();
        }
    }

    private String installFromDiscs() throws Exception {
        for (int i = 1; i < 4; i++) {
            if (picked[i] == null) return "Choose " + LABELS[i] + ".";
        }
        ParcelFileDescriptor[] fds = new ParcelFileDescriptor[4];
        try {
            String[] paths = new String[4];
            for (int i = 1; i < 4; i++) {
                fds[i] = getContentResolver().openFileDescriptor(picked[i], "r");
                if (fds[i] == null) return "Could not open " + displayName(picked[i]) + ".";
                // The installer reads the open document like a file.
                paths[i] = "/proc/self/fd/" + fds[i].getFd();
            }
            return nativeInstall(home().getAbsolutePath(), expectedFile().getAbsolutePath(),
                                 paths[1], paths[2], paths[3]);
        } finally {
            for (ParcelFileDescriptor f : fds) {
                if (f != null) try { f.close(); } catch (Exception ignored) { }
            }
        }
    }

    /** Codegen + compile + link on the phone, into files/libgtav_game.so (read-only). */
    private String buildGame() {
        File build = new File(getFilesDir(), "build");
        String tools = getApplicationInfo().nativeLibraryDir;
        gameInfo().delete();
        String e = nativeBuildGame(kitDir().getAbsolutePath(), tools, home().getAbsolutePath(),
                                   build.getAbsolutePath(), gameLib().getAbsolutePath());
        deleteTree(build);
        if (e != null) return e;
        gameLib().setReadOnly();
        String shell = sha256(new File(tools, "libgtav_fast.so"));
        try (OutputStream o = new FileOutputStream(gameInfo())) {
            o.write(("gtav-recomp android game 1\nshell " + shell + "\n").getBytes("UTF-8"));
        } catch (Exception ex) {
            return "Could not write " + gameInfo() + ": " + ex;
        }
        onLog("Game built.");
        return null;
    }

    /**
     * assets/kit.zip -> files/kit (once per APK build: kit.txt holds the APK's update time), and the
     * shader header the game's shader translation reads -> <external>/tools/shader_common.h.
     */
    private String ensureKit() {
        String stamp;
        try {
            android.content.pm.PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            stamp = pi.versionCode + " " + pi.lastUpdateTime;
        } catch (Exception ex) {
            return "Could not read this app's version: " + ex;
        }
        File kit = kitDir(), mark = new File(kit, "kit.txt");
        if (!stamp.equals(readLine(mark))) {
            onStage("Unpacking the build tools...");
            File stage = new File(getFilesDir(), "kit.new");
            deleteTree(stage);
            stage.mkdirs();
            byte[] buf = new byte[1 << 20];
            try (InputStream raw = getAssets().open("kit.zip"); ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry z;
                while ((z = zip.getNextEntry()) != null) {
                    String name = z.getName();
                    if (z.isDirectory() || name.contains("..") || name.startsWith("/")) continue;
                    File out = new File(stage, name);
                    out.getParentFile().mkdirs();
                    try (OutputStream o = new FileOutputStream(out)) {
                        for (int n; (n = zip.read(buf)) > 0;) o.write(buf, 0, n);
                    }
                }
            } catch (Exception ex) {
                return "Could not unpack the build tools: " + ex;
            }
            try (OutputStream o = new FileOutputStream(new File(stage, "kit.txt"))) {
                o.write((stamp + "\n").getBytes("UTF-8"));
            } catch (Exception ex) {
                return "Could not write kit.txt: " + ex;
            }
            deleteTree(kit);
            if (!stage.renameTo(kit)) return "Could not put the build tools in place.";
        }
        File tools = new File(home(), "tools");
        tools.mkdirs();
        try (InputStream in = new FileInputStream(new File(kit, "tools/shader_common.h"));
             OutputStream o = new FileOutputStream(new File(tools, "shader_common.h"))) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0;) o.write(buf, 0, n);
        } catch (Exception ex) {
            return "Could not install the shader header: " + ex;
        }
        return null;
    }

    private static String readLine(File f) {
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line = r.readLine();
            return line == null ? null : line.trim();
        } catch (Exception e) {
            return null;
        }
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private void message(String title, String text) {
        new AlertDialog.Builder(this).setTitle(title).setMessage(text).setPositiveButton("OK", null).show();
    }

    // --- called by libgtav_setup.so on the install thread (InstallUi) ----------------------------

    void onStage(String s) {
        runOnUiThread(() -> status.setText(s));
    }

    void onLog(String s) {
        runOnUiThread(() -> {
            CharSequence old = log.getText();
            String all = old + (old.length() > 0 ? "\n" : "") + s;
            // The last lines only: the full log is data/logs/install.log.
            String[] lines = all.split("\n");
            int from = Math.max(0, lines.length - 12);
            StringBuilder sb = new StringBuilder();
            for (int i = from; i < lines.length; i++) sb.append(i > from ? "\n" : "").append(lines[i]);
            log.setText(sb);
        });
    }

    void onProgress(double f) {
        runOnUiThread(() -> progress.setProgress((int) (f * 1000)));
    }

    boolean isCancelled() {
        return cancelled.get();
    }

    boolean onAsk(String question) {
        CountDownLatch done = new CountDownLatch(1);
        boolean[] yes = {false};
        runOnUiThread(() -> new AlertDialog.Builder(this).setMessage(question).setCancelable(false)
            .setPositiveButton("Yes", (d, w) -> { yes[0] = true; done.countDown(); })
            .setNegativeButton("No", (d, w) -> done.countDown()).show());
        try {
            done.await();
        } catch (InterruptedException e) {
            return false;
        }
        return yes[0];
    }
}
