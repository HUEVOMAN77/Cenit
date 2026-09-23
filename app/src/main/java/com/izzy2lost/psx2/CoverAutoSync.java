package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.work.BackoffPolicy;
import androidx.work.Constraints;
import androidx.work.ExistingWorkPolicy;
import androidx.work.NetworkType;
import androidx.work.OneTimeWorkRequest;
import androidx.work.WorkContinuation;
import androidx.work.WorkManager;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

/**
 * Descarga automática de carátulas: recibe la biblioteca ya publicada por el
 * inicio, resuelve seriales, descarta las que ya están en caché y encola lotes
 * de CoverDownloadWorker. Se dispara solo cuando la preferencia "auto_covers"
 * está activada, y nunca interrumpe una descarga que ya esté corriendo.
 */
final class CoverAutoSync {

    private static final int BATCH_SIZE = 100;
    // Para que cada repintado de la biblioteca no dispare una descarga nueva:
    // como mucho una corrida cada 6 h.
    private static final long COOLDOWN_MS = 6 * 60 * 60 * 1000L;
    private static final String PREF_LAST_RUN = "auto_covers_last_run";
    private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor();

    private CoverAutoSync() {}

    /** Enciende la sincronización si toca. Barato: solo mira preferencias aquí. */
    static void maybeSyncAsync(Context context, String[] uris) {
        syncAsync(context, uris, false);
    }

    /** La pidió el usuario desde Ajustes: ignora el enfriamiento, respeta lo manual. */
    static void forceSyncAsync(Context context, String[] uris) {
        syncAsync(context, uris, true);
    }

    private static void syncAsync(Context context, String[] uris, boolean force) {
        if (context == null || uris == null || uris.length == 0) return;
        SharedPreferences prefs = context.getSharedPreferences(
                "app_prefs", Context.MODE_PRIVATE);
        if (!force && !prefs.getBoolean("auto_covers", true)) return;
        long lastRun = prefs.getLong(PREF_LAST_RUN, 0L);
        if (!force && android.os.SystemClock.elapsedRealtime() - lastRun < COOLDOWN_MS) return;
        final Context appContext = context.getApplicationContext();
        final String[] snapshot = uris.clone();
        EXECUTOR.execute(() -> {
            try {
                syncBlocking(appContext, snapshot);
            } catch (Throwable error) {
                android.util.Log.w("CoverAutoSync", "Auto cover sync failed", error);
            }
        });
    }

    private static void syncBlocking(Context appContext, String[] uris) {
        // Si ya hay una corrida activa, no se duplica el trabajo.
        try {
            java.util.List<androidx.work.WorkInfo> existing =
                    WorkManager.getInstance(appContext)
                            .getWorkInfosForUniqueWork(CoverDownloadWorker.UNIQUE_WORK_NAME)
                            .get();
            for (androidx.work.WorkInfo info : existing) {
                if (!info.getState().isFinished()) return;
            }
        } catch (Throwable ignored) {}

        SharedPreferences prefs = appContext.getSharedPreferences(
                "app_prefs", Context.MODE_PRIVATE);
        LinkedHashSet<String> serials = new LinkedHashSet<>();
        SharedPreferences.Editor serialEditor = prefs.edit();
        for (String gameUri : uris) {
            String serial = GameSerialUtils.normalizeLibrarySerial(
                    prefs.getString("serial:" + gameUri, null));
            if (serial.isEmpty()) serial = GameSerialUtils.serialFromUri(gameUri);
            if (serial.isEmpty()) continue;
            serialEditor.putString("serial:" + gameUri, serial);
            if (prefs.getBoolean("custom_cover:" + serial, false)) continue;
            serials.add(serial);
        }
        serialEditor.apply();
        if (serials.isEmpty()) return;

        serials.removeAll(CoverCache.findValidCoverPaths(appContext, serials).keySet());
        if (serials.isEmpty()) return;

        final String runToken = java.util.UUID.randomUUID().toString();
        prefs.edit().putString("active_cover_download_run", runToken)
                .putLong(PREF_LAST_RUN, android.os.SystemClock.elapsedRealtime()).apply();

        ArrayList<String> list = new ArrayList<>(serials);
        ArrayList<OneTimeWorkRequest> requests = new ArrayList<>();
        Constraints constraints = new Constraints.Builder()
                .setRequiredNetworkType(NetworkType.CONNECTED)
                .build();
        for (int start = 0; start < list.size(); start += BATCH_SIZE) {
            String[] batch = list.subList(start, Math.min(start + BATCH_SIZE, list.size()))
                    .toArray(new String[0]);
            androidx.work.Data input = new androidx.work.Data.Builder()
                    .putStringArray(CoverDownloadWorker.KEY_SERIALS, batch)
                    .putString(CoverDownloadWorker.KEY_RUN_TOKEN, runToken)
                    .build();
            requests.add(new OneTimeWorkRequest.Builder(CoverDownloadWorker.class)
                    .setInputData(input)
                    .setConstraints(constraints)
                    .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 10, TimeUnit.SECONDS)
                    .addTag(CoverDownloadWorker.RUN_TAG_PREFIX + runToken)
                    .build());
        }

        WorkContinuation chain = WorkManager.getInstance(appContext).beginUniqueWork(
                CoverDownloadWorker.UNIQUE_WORK_NAME, ExistingWorkPolicy.KEEP, requests.get(0));
        for (int i = 1; i < requests.size(); i++) chain = chain.then(requests.get(i));
        chain.enqueue();
        android.util.Log.i("CoverAutoSync", "Enqueued " + list.size() + " covers, run=" + runToken);
    }
}
