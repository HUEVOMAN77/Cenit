package com.izzy2lost.psx2;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.Arrays;

/**
 * Cenit 0.6.30 — Ventanas, mediana, EWMA, persistencia de la mezcla.
 *
 * Fase 1.3, 1.6, 1.7 del prompt maestro: ventana de asentamiento, ventana de
 * medicion, mediana de FPS, P50/P95 de frametime, percentiles de GPU y emulacion,
 * EWMA termica, descarte de muestras sucias, calibracion por evidencia con
 * minimos duros.
 *
 * Esta clase no abre prefs: recibe y devuelve strings, para que el governor o
 * MainActivity sea el dueno del almacenamiento y se pueda probar en el arnes.
 */
public class LoadTrend {

    // --- Tamano del anillo deslizante ---
    /** Anillo de 32 muestras validas (~32 s a 1 tick/s). */
    public static final int RING_SIZE = 32;

    // --- Minimos de calibracion (fase 1.7) ---
    /** Minimo 3 juegos distintos para calibrar. */
    public static final int MIN_GAMES = 3;
    /** Minimo 5 ventanas validas por juego. */
    public static final int MIN_WINDOWS_PER_GAME = 5;
    /** Minimo 2 sesiones separadas por juego. */
    public static final int MIN_SESSIONS = 2;

    // --- Codigos de condicion para sustained() ---
    /** Espera MTVU alta (waitMs >= 10). */
    public static final int COND_VU_WAIT = 0;
    /** GPU caliente (gpuPct >= 85). */
    public static final int COND_GPU_HOT = 1;
    /** EE caliente (eePct >= 80). */
    public static final int COND_EE_HOT = 2;
    /** GPU holgada (gpuPct <= 35 y metrica valida). */
    public static final int COND_GPU_IDLE = 3;
    private static final int NUM_CONDITIONS = 4;

    // --- Motivos de descarte (bitmask) ---
    /** Muestra descartada por pausa. */
    public static final int DISCARD_PAUSED = 1;
    /** Muestra descartada por turbo/fast-forward. */
    public static final int DISCARD_TURBO = 2;
    /** Muestra descartada por cambio de resolucion en curso. */
    public static final int DISCARD_RES_CHANGING = 4;
    /** Muestra descartada por thermal throttle (thermal >= 2). */
    public static final int DISCARD_THERMAL = 8;
    /** Muestra descartada por cero presents. */
    public static final int DISCARD_NO_PRESENTS = 16;
    /** Muestra descartada por NaN en campos clave. */
    public static final int DISCARD_NAN = 32;
    /** Muestra descartada por ventana recortada (< 0.25 * UPDATE_INTERVAL). */
    public static final int DISCARD_WINDOW_SHORT = 64;

    // --- Anillos deslizantes ---
    private final double[] gpuRing = new double[RING_SIZE];
    private final double[] eeRing = new double[RING_SIZE];
    private final double[] speedRing = new double[RING_SIZE];
    private final double[] fpsRing = new double[RING_SIZE];
    private final double[] waitMsRing = new double[RING_SIZE];
    private int ringHead = 0;
    private int ringCount = 0;

    // --- Contador de descartes ---
    private int discardedCount = 0;
    private int discardMask = 0;

    // --- EWMA termica ---
    // Alpha ~0,1 por tick de 1 s: con tick de 1 s, 0,1 es ~10 s de media efectiva,
    // suficiente para no reaccionar a un pico y no tardar un minuto en ver el throttle.
    private double thermalEwma = 0;
    private boolean thermalInit = false;
    private static final double THERMAL_ALPHA = 0.1;

    // --- Rachas de condicion para sustained() ---
    private final int[] condStreak = new int[NUM_CONDITIONS];

    // --- Datos de calibracion ---
    private final Map<String, CalKey> calMap = new HashMap<>();

    /** Datos de calibracion por clave (juego-serial + dispositivo + backend + core). */
    private static class CalKey {
        int windows;
        final Set<String> sessions = new HashSet<>();
        final int[] gpuHist = new int[10]; // 10 bins de 10%
        final int[] eeHist = new int[10];
    }

    /**
     * Agrega una muestra al anillo, descartando si corresponde.
     * Actualiza EWMA termica, contadores de descarte y rachas de condicion.
     *
     * @param s muestra del snapshot
     * @param paused true si el juego esta pausado
     * @param turbo true si turbo/fast-forward activo
     * @param resChanging true si hay cambio de resolucion en curso
     * @param thermal estado termico del sistema (0..4)
     * @return true si la muestra entro al anillo, false si fue descartada. El
     *         llamante la necesita para no contar como evidencia de calibracion
     *         la misma ventana que el propio trend acabo de tirar a la basura.
     */
    public boolean add(CenitLoadSample s, boolean paused, boolean turbo, boolean resChanging, int thermal) {
        // Actualizar EWMA termica solo cuando HAY sensor. Los valores negativos no
        // son temperaturas bajas, son "sin senal": -2 = el telefono no expone la
        // API termica (API < 29 o ROM recortada) y -1 = normal sin aviso. Meter
        // -2 en la media arruinaría el indicador justo en los equipos donde no se
        // puede verificar nada, y un EWMA negativo se leería después como si el
        // SoC estuviera por debajo de cero grado. Se conserva el ultimo valor leido.
        if (thermal >= 0) {
            if (thermalInit) {
                thermalEwma = THERMAL_ALPHA * thermal + (1 - THERMAL_ALPHA) * thermalEwma;
            } else {
                thermalEwma = thermal;
                thermalInit = true;
            }
        }

        // Verificar condiciones de descarte
        int reason = 0;
        if (paused) reason |= DISCARD_PAUSED;
        if (turbo) reason |= DISCARD_TURBO;
        if (resChanging) reason |= DISCARD_RES_CHANGING;
        if (thermal >= 2) reason |= DISCARD_THERMAL;
        if (s == null || !s.isValid()) reason |= DISCARD_NAN;
        else {
            if (!s.windowComplete()) reason |= DISCARD_WINDOW_SHORT;
            if (!(s.presents() > 0)) reason |= DISCARD_NO_PRESENTS; // !(x > 0) captura NaN
            if (Double.isNaN(s.gpuPct()) || Double.isNaN(s.eePct()) || Double.isNaN(s.speedPct())) {
                reason |= DISCARD_NAN;
            }
        }

        if (reason != 0) {
            discardedCount++;
            discardMask |= reason;
            // Resetear rachas de condicion al descartar
            Arrays.fill(condStreak, 0);
            return false;
        }

        // Agregar al anillo
        gpuRing[ringHead] = s.gpuPct();
        eeRing[ringHead] = s.eePct();
        speedRing[ringHead] = s.speedPct();
        fpsRing[ringHead] = s.fps();
        waitMsRing[ringHead] = s.mtvuWaitMs();
        ringHead = (ringHead + 1) % RING_SIZE;
        if (ringCount < RING_SIZE) ringCount++;

        // Actualizar rachas de condicion
        updateConditions(s);
        return true;
    }

    /** Actualiza las rachas de condicion segun la muestra. */
    private void updateConditions(CenitLoadSample s) {
        // COND_VU_WAIT: mtvuWaitMs >= 10
        if (s.mtvuWaitMs() >= 10) condStreak[COND_VU_WAIT]++;
        else condStreak[COND_VU_WAIT] = 0;

        // COND_GPU_HOT: gpuPct >= 85
        if (s.gpuPct() >= 85) condStreak[COND_GPU_HOT]++;
        else condStreak[COND_GPU_HOT] = 0;

        // COND_EE_HOT: eePct >= 80
        if (s.eePct() >= 80) condStreak[COND_EE_HOT]++;
        else condStreak[COND_EE_HOT] = 0;

        // COND_GPU_IDLE: gpuPct <= 35 y metrica valida
        if (s.gpuPct() <= 35 && s.gpuMetricValid()) condStreak[COND_GPU_IDLE]++;
        else condStreak[COND_GPU_IDLE] = 0;
    }

    /**
     * True si la condicion especificada se repitio n ventanas seguidas.
     * Usado por el clasificador para "VU wait sostenido" y para no cambiar de
     * opinion a cada muestra.
     */
    public boolean sustained(int condition, int n) {
        if (condition < 0 || condition >= NUM_CONDITIONS) return false;
        return condStreak[condition] >= n;
    }

    // --- Mediana y percentiles ---

    /** Mediana de FPS en el anillo. */
    public double medianFps() { return median(fpsRing, ringCount); }

    /** Mediana de GPU% en el anillo. */
    public double medianGpuPct() { return median(gpuRing, ringCount); }

    /** Mediana de EE% en el anillo. */
    public double medianEePct() { return median(eeRing, ringCount); }

    /** P95 de GPU% en el anillo propio (no el del nativo). */
    public double p95GpuPct() { return percentile(gpuRing, ringCount, 0.95); }

    /** Mediana de un anillo. */
    private double median(double[] ring, int count) {
        return percentile(ring, count, 0.5);
    }

    /** Percentil p (0..1) de un anillo. */
    private double percentile(double[] ring, int count, double p) {
        if (count == 0) return Double.NaN;
        double[] sorted = new double[count];
        int start = (ringHead - count + RING_SIZE) % RING_SIZE;
        for (int i = 0; i < count; i++) {
            sorted[i] = ring[(start + i) % RING_SIZE];
        }
        Arrays.sort(sorted);
        int idx = (int) Math.ceil(p * count) - 1;
        if (idx < 0) idx = 0;
        if (idx >= count) idx = count - 1;
        return sorted[idx];
    }

    /** EWMA termica. Alpha ~0,1 por tick de 1 s → ~10 s de media efectiva. */
    public double thermalEwma() { return thermalEwma; }

    /** Total de muestras descartadas. */
    public int discardedCount() { return discardedCount; }

    /** Bitmask de todos los motivos de descarte vistos. */
    public int discardMask() { return discardMask; }

    /** Numero de muestras validas en el anillo. */
    public int ringCount() { return ringCount; }

    /**
     * Tira el ANILLO (y con él las medianas, el P95 propio, las rachas y los
     * contadores de descarte) pero conserva la CALIBRACION.
     *
     * Por que se separan: el anillo alimenta decisiones de "sostenido" sobre lo
     * que esta pasando AHORA, y mezclar dos juegos distintos en el mismo anillo
     * haria ver como patron sostenido algo que es mitad un disco y mitad otro. La
     * calibracion, en cambio, vive indexada por clave (juego + dispositivo +
     * backend), asi que sumar evidencia de juegos distintos no contamina nada:
     * para eso esta la clave.
     */
    public void clearRing() {
        Arrays.fill(gpuRing, 0.0);
        Arrays.fill(eeRing, 0.0);
        Arrays.fill(speedRing, 0.0);
        Arrays.fill(fpsRing, 0.0);
        Arrays.fill(waitMsRing, 0.0);
        ringHead = 0;
        ringCount = 0;
        discardedCount = 0;
        discardMask = 0;
        Arrays.fill(condStreak, 0);
        thermalEwma = 0;
        thermalInit = false;
    }

    // --- Calibracion (fase 1.7) ---

    /**
     * Olvidar TODA la calibracion acumulada (el mapa por clave), dejando el anillo
     * de la partida intacto. Es el espejo de clearRing(): alli se tiro el anillo y
     * se guardo la calibracion porque son cosas distintas; aqui es el boton "Borrar
     * evidencia de umbrales" de Ajustes, donde el usuario pide lo contrario.
     *
     * El llamante (DynamicResolutionGovernor.forgetCalibration) se encarga de
     * borrar tambien la copia en prefs y de reconstruir la politica: si solo se
     * limpiara la memoria, el proximo stop() resalvaria la calibracion vieja y el
     * borrado pareceria un boton que no hace nada.
     */
    public void clearCalibration() {
        calMap.clear();
    }

    /**
     * Agrega una muestra a la calibracion para la clave dada.
     * La clave es "juego-serial + clase de dispositivo + backend + version de core",
     * construida por el llamante.
     *
     * @param key clave de calibracion
     * @param sessionId identificador de sesion (para contar sesiones distintas)
     * @param s muestra del snapshot
     */
    public void observe(String key, String sessionId, CenitLoadSample s) {
        if (key == null || s == null || !s.isValid()) return;
        if (Double.isNaN(s.gpuPct()) || Double.isNaN(s.eePct())) return;

        CalKey ck = calMap.computeIfAbsent(key, k -> new CalKey());
        ck.windows++;
        if (sessionId != null) ck.sessions.add(sessionId);

        // Agregar a histogramas
        int gpuBin = (int) (s.gpuPct() / 10);
        if (gpuBin < 0) gpuBin = 0;
        if (gpuBin > 9) gpuBin = 9;
        ck.gpuHist[gpuBin]++;

        int eeBin = (int) (s.eePct() / 10);
        if (eeBin < 0) eeBin = 0;
        if (eeBin > 9) eeBin = 9;
        ck.eeHist[eeBin]++;
    }

    /**
     * Sugiere una politica calibrada si hay evidencia suficiente.
     * Exige minimo 3 juegos distintos, 5 ventanas validas por juego y 2 sesiones
     * separadas. Si no se cumplen, devuelve la politica inicial con provisional=true.
     *
     * Reemplazar umbrales con una sola sesion fue el error que costo el ciclo
     * 0.6.25→0.6.26 (documentado en el README del repo).
     */
    public LoadPolicy suggestPolicy() {
        // Contar claves que cumplen minimos
        int qualifying = 0;
        for (CalKey ck : calMap.values()) {
            if (ck.windows >= MIN_WINDOWS_PER_GAME && ck.sessions.size() >= MIN_SESSIONS) {
                qualifying++;
            }
        }

        if (qualifying < MIN_GAMES) {
            return LoadPolicy.defaults(); // provisional=true
        }

        // Agregar histogramas de claves calificadas
        int[] aggGpu = new int[10];
        int[] aggEe = new int[10];
        for (CalKey ck : calMap.values()) {
            if (ck.windows >= MIN_WINDOWS_PER_GAME && ck.sessions.size() >= MIN_SESSIONS) {
                for (int i = 0; i < 10; i++) {
                    aggGpu[i] += ck.gpuHist[i];
                    aggEe[i] += ck.eeHist[i];
                }
            }
        }

        // Calcular percentiles
        double gpuP85 = histogramPercentile(aggGpu, 0.85);
        double eeP85 = histogramPercentile(aggEe, 0.85);
        double gpuP10 = histogramPercentile(aggGpu, 0.10);

        // Construir la politica calibrada SOBRE la de criterio y con las
        // comprobaciones de sanidad de withCalibration, no con el percentil crudo.
        //
        // Por que esto importa (encontrado por el arnés 0.6.30): el histograma tiene
        // bins de 10 puntos, asi que un telefono cuyo juego pasa toda la ventana en
        // el bin 80-90 devuelve P10 = 85 para el piso de "GPU holgada". Con la
        // cifra cruda, el clasificador diria "GPU holgada" cuando la GPU esta al
        // 85% y el regidor JAMAS recortaria pixeles otra vez: un umbral calculado
        // con evidencia puede estar MAS roto que el de criterio, no por medir poco
        // sino por medir concentrado. withCalibration ya tenia la guarda (el piso
        // tiene que quedar por debajo del techo de saturacion y encima de 10) y
        // nadie la llamaba; ahora la llamada es esta, asi que la regla vive en un
        // solo sitio.
        //
        // Los umbrales de accion (confianza) NO se calibran aqui: el prompt los
        // trata como politica del producto, y cambiarlos con tres juegos seria
        // aflojar la Regla 5 con la muestra mas pequena del catalogo.
        return LoadPolicy.defaults().withCalibration(gpuP85, eeP85, gpuP10);
    }

    /** Percentil de un histograma de 10 bins (cada bin = 10%). */
    private double histogramPercentile(int[] hist, double p) {
        int total = 0;
        for (int h : hist) total += h;
        if (total == 0) return 50; // default
        int target = (int) Math.ceil(p * total);
        int cum = 0;
        for (int i = 0; i < hist.length; i++) {
            cum += hist[i];
            if (cum >= target) return (i + 0.5) * 10; // punto medio del bin
        }
        return 95; // ultimo bin
    }

    // --- Persistencia ---

    /**
     * Serializa los datos de calibracion a String compacto.
     * Formato: "key1:windows|sess1,sess2|gpu0,...,gpu9|ee0,...,ee9;key2:..."
     * El governor o MainActivity guarda este string en SharedPreferences.
     */
    public String serialize() {
        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, CalKey> e : calMap.entrySet()) {
            if (sb.length() > 0) sb.append(';');
            CalKey ck = e.getValue();
            sb.append(e.getKey()).append(':');
            sb.append(ck.windows).append('|');
            sb.append(String.join(",", ck.sessions)).append('|');
            for (int i = 0; i < 10; i++) {
                if (i > 0) sb.append(',');
                sb.append(ck.gpuHist[i]);
            }
            sb.append('|');
            for (int i = 0; i < 10; i++) {
                if (i > 0) sb.append(',');
                sb.append(ck.eeHist[i]);
            }
        }
        return sb.toString();
    }

    /**
     * Deserializa datos de calibracion desde String.
     * Formato: "key1:windows|sess1,sess2|gpu0,...,gpu9|ee0,...,ee9;key2:..."
     */
    public void deserialize(String data) {
        calMap.clear();
        if (data == null || data.isEmpty()) return;
        for (String pair : data.split(";")) {
            int colon = pair.indexOf(':');
            if (colon < 0) continue;
            String key = pair.substring(0, colon);
            String value = pair.substring(colon + 1);
            String[] fields = value.split("\\|", -1);
            if (fields.length < 4) continue;
            CalKey ck = new CalKey();
            try {
                ck.windows = Integer.parseInt(fields[0]);
            } catch (NumberFormatException ex) { continue; }
            if (!fields[1].isEmpty()) {
                for (String sess : fields[1].split(",")) {
                    ck.sessions.add(sess);
                }
            }
            String[] gpuBins = fields[2].split(",", -1);
            String[] eeBins = fields[3].split(",", -1);
            for (int i = 0; i < 10 && i < gpuBins.length; i++) {
                try { ck.gpuHist[i] = Integer.parseInt(gpuBins[i]); }
                catch (NumberFormatException ex) { /* ignore */ }
            }
            for (int i = 0; i < 10 && i < eeBins.length; i++) {
                try { ck.eeHist[i] = Integer.parseInt(eeBins[i]); }
                catch (NumberFormatException ex) { /* ignore */ }
            }
            calMap.put(key, ck);
        }
    }

    /** Numero de claves de calibracion registradas. */
    public int calibrationKeyCount() { return calMap.size(); }
}
