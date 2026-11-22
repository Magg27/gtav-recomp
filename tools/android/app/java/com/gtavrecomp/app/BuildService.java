package com.gtavrecomp.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

/**
 * Keeps the app's process (and the CPU) running while setup copies the game files and builds the
 * game on the phone: a foreground service with a notification and a partial wake lock. The work
 * itself runs on SetupActivity's install thread; this service only holds the process up.
 */
public class BuildService extends Service {
    private static final String TAG = "GtavSetup";
    private static final String CHANNEL = "setup";
    private PowerManager.WakeLock wake;

    static void start(Context c) {
        try {
            c.startForegroundService(new Intent(c, BuildService.class));
        } catch (Exception e) {
            Log.w(TAG, "build service: " + e);
        }
    }

    static void stop(Context c) {
        c.stopService(new Intent(c, BuildService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Setup", NotificationManager.IMPORTANCE_LOW));
        Notification n = new Notification.Builder(this, CHANNEL)
            .setContentTitle("GTA V setup")
            .setContentText("Installing and building the game...")
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setOngoing(true)
            .build();
        try {
            startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } catch (Exception e) {
            Log.w(TAG, "startForeground: " + e);
        }
        if (wake == null) {
            wake = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "gtav:setup");
            wake.acquire(6 * 60 * 60 * 1000L);
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        if (wake != null && wake.isHeld()) wake.release();
        wake = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
