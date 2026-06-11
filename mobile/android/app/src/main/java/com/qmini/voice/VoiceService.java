package com.qmini.voice;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

/**
 * Foreground service to keep audio capture alive when app is backgrounded.
 * Android kills background audio threads without this.
 */
public class VoiceService extends Service {
    private static final String TAG = "QminiVoiceSvc";
    private static final String CHANNEL_ID = "qmini_voice_channel";
    private static final int NOTIFICATION_ID = 1;

    public static final String ACTION_START = "com.qmini.voice.START";
    public static final String ACTION_STOP = "com.qmini.voice.STOP";
    public static final String EXTRA_ROOM = "room";
    public static final String EXTRA_NICK = "nick";

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }

        String action = intent.getAction();
        if (ACTION_STOP.equals(action)) {
            Log.i(TAG, "Stop requested");
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }

        if (ACTION_START.equals(action)) {
            String room = intent.getStringExtra(EXTRA_ROOM);
            String nick = intent.getStringExtra(EXTRA_NICK);
            startForeground(NOTIFICATION_ID, buildNotification(room, nick));
            Log.i(TAG, "Foreground service started for room: " + room);
        }

        return START_STICKY;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Qmini 语音通话",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("正在进行语音通话");
            channel.setShowBadge(false);
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) nm.createNotificationChannel(channel);
        }
    }

    private Notification buildNotification(String room, String nick) {
        Intent stopIntent = new Intent(this, VoiceService.class);
        stopIntent.setAction(ACTION_STOP);
        PendingIntent stopPending = PendingIntent.getService(
            this, 0, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE
        );

        Intent mainIntent = new Intent(this, MainActivity.class);
        mainIntent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent mainPending = PendingIntent.getActivity(
            this, 0, mainIntent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE
        );

        String title = (nick != null) ? nick : "语音通话";
        String text = (room != null) ? "房间: " + room : "已连接";

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        return builder
            .setContentTitle(title)
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentIntent(mainPending)
            .addAction(android.R.drawable.ic_media_pause, "离开", stopPending)
            .setOngoing(true)
            .build();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.i(TAG, "VoiceService destroyed");
    }
}
