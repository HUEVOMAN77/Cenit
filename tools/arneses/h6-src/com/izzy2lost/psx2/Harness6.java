package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.PowerManager;

/**
 * Arnés 0.6.29 — el regidor REAL, conducido de punta a punta.
 *
 * No es un espejo de la lógica: compila y ejecuta DynamicResolutionGovernor tal
 * cual está en el repo, con sus clases reales (GameLoadClassifier,
 * AdaptiveProfile) y stubs mínimos de Android. La pregunta que contesta es la
 * que CI no puede contestar sola: ¿qué hace el regidor con cada lectura, y
 * cuántas veces la llevó a la acción?
 */
public class Harness6 {

    static int checks = 0, failures = 0;
    static final java.util.List<String> applied = new java.util.ArrayList<>();

    static class Host implements DynamicResolutionGovernor.Host {
        public boolean isGameRunning() { return true; }
        public boolean isTimeScaled() { return false; }
        public void applyUpscale(float value) { applied.add(String.valueOf(value)); }
        public String currentGameUri() { return "content://iso/g.bin"; }
    }

    static Context ctx;
    static SharedPreferences prefs;
    static DynamicResolutionGovernor gov;

    static void check(boolean ok, String name) {
        checks++;
        if (!ok) { failures++; System.out.println("FALLO: " + name); }
    }

    static void eqf(float got, float want, String name) {
        checks++;
        if (Math.abs(got - want) > 0.001f) {
            failures++;
            System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")");
        }
    }

    /** Igualdad de enteros, con los dos numeros impresos (un "1 vs 2" no se adivina). */
    static void eqi(int got, int want, String name) {
        checks++;
        if (got != want) {
            failures++;
            System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")");
        }
    }

    /** Arranca el regidor con el techo y las prefs que pida el caso. */
    static void boot(float ceiling, boolean adaptive, boolean turbo) {
        // 0.6.30: apagar el regidor del caso ANTERIOR antes de crear el nuevo. Sin
        // esto quedan DOS gobernadores activos con la cola del Handler de juguete
        // (estatica) compartida: cada ticks(N) reparte los N ticks entre los dos,
        // el caso ve la mitad de los suyos y las sentencias de "sube al enfriar" o
        // "el tally termico cuenta" fallan por basura del arnes, no por el regidor.
        // En la app esto no pasa: hay UN regidor y start() es idempotente.
        if (gov != null) gov.stop();
        applied.clear();
        NativeApp.reset();
        CpuVUTuner.calls.clear();
        CpuVUTuner.answer = CpuVUTuner.IDLE;
        CpuVUTuner.active = false;
        Handler.queue.clear();
        PowerManager.status = -1;
        ctx = new Context();
        ctx.power = new PowerManager(); // sin esto, getSystemService("power") = null
        prefs = ctx.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        prefs.edit().putFloat("upscale_multiplier", ceiling).apply();
        prefs.edit().putBoolean("dynamic_res", true).apply();
        prefs.edit().putBoolean("adaptive_perf", adaptive).apply();
        prefs.edit().putBoolean("auto_turbo", turbo).apply();
        // 0.6.30: cada caso arranca sin calibracion heredada del anterior. Sin
        // esto, el primer caso que juntera suficientes ventanas cambiaria los
        // umbrales y los casos de abajo se medirian contra otra politica.
        prefs.edit().putString(DynamicResolutionGovernor.CAL_PREF_KEY, "").apply();
        gov = new DynamicResolutionGovernor(ctx, new Host());
        gov.start();
        // Primer tick: applied==0 -> fija la escala inicial (techo o aprendida).
        Handler.run(1);
    }

    /** Corre N ticks con las lecturas que el caso dejó puestas. */
    static void ticks(int n) { Handler.run(n); }

    /** Lee una casilla del tally guardado en la cadena de prefs. */
    static int tallyOf(String raw, int slot) {
        if (raw == null) return -1;
        for (String part : raw.split(";")) {
            if (!part.startsWith("load=")) continue;
            String[] v = part.substring("load=".length()).split(",");
            if (slot >= v.length) return -1;
            try { return Integer.parseInt(v[slot].trim()); } catch (NumberFormatException e) { return -1; }
        }
        return -1;
    }

    /**
     * Dispara el aviso termico por el camino de verdad: el listener que el
     * regidor engancho en PowerManager (via Proxy). Meter el numero en el stub a
     * mano no servia, porque el regidor lee el estado UNA vez al arrancar y
     * despues solo escucha; por eso los casos termicos de 0.6.29 tenian un
     * check(true, ...) que no comprobaba nada.
     */
    static void fireThermal(int status) {
        android.os.PowerManager.status = status;
        for (Object l : android.os.PowerManager.listeners) {
            if (l instanceof android.os.PowerManager.OnThermalStatusChangedListener)
                ((android.os.PowerManager.OnThermalStatusChangedListener) l)
                        .onThermalStatusChanged(status);
        }
    }

    /** La última escala que el regidor mandó aplicar. */
    static float lastApplied() {
        return applied.isEmpty() ? -1f : Float.parseFloat(applied.get(applied.size() - 1));
    }

    public static void main(String[] args) {
        // ================================================================
        // 0) ARRANQUE: parte del techo del usuario y no toca nada.
        // ================================================================
        boot(2f, false, false);
        NativeApp.speed = 100f;
        check(applied.size() == 0, "arranque sin retraso no aplica nada todavia");
        eqf(lastApplied(), -1f, "nada aplicado todavia");

        // El tick inicial pone la escala en juego (applied=techo) sin recortar.
        boot(2f, false, false);
        NativeApp.speed = 70f; NativeApp.gpuPct = 96f; NativeApp.eePct = 40f;
        ticks(1); // este tick fija applied=2x
        check(applied.isEmpty(), "fijar la escala inicial no cuenta como recorte");
        ticks(2); // dos ticks lentos con GPU saturada -> bajar
        eqf(lastApplied(), 1.5f, "GPU 96% y 70%: baja un escalon (2 -> 1,5)");

        // ================================================================
        // 1) EL BUG HISTÓRICO, probado de punta a punta. GPU 7% + EE 99%:
        //    con la unidad vieja esto era GPU_BOUND y recortaba. Ahora el
        //    regidor tiene que quedarse quieto Y marcar cpuBound=true.
        // ================================================================
        boot(2f, true, false);
        NativeApp.speed = 70f; NativeApp.gpuPct = 7f; NativeApp.eePct = 99f;
        ticks(10);
        check(applied.isEmpty(),
                "GPU holgada + EE lleno: NO se recorta (antes se recortaba siempre)");
        // Las dos llamadas a aplicar del arranque no existen en este caso:
        check(applied.size() == 0, "cero ordenes de resolucion en 10 ticks emu-bound");
        // El perfil tiene que estar aprendiendo cpuBound (15 ticks lo confirman).
        ticks(6);
        gov.stop(); // el perfil se salva al apagar el juego, no en cada tick
        String saved = prefs.getString("adapt:content://iso/g.bin", null);
        check(saved != null && saved.contains("cpu=1"),
                "tras 15 ticks el perfil aprende cpu=1: " + saved);

        // 1b) EL MISMO CASO SIN METRICA DE EE: aqui es donde el bug de unidad se
        //     ve solo, sin que el EE tape la decision. GPU 7% medido y vivo: con
        //     el umbral viejo (0,65) esto era "GPU apretada" y recortaba imagen
        //     de gratis; con el umbral en la unidad real (35%) es EMULATION.
        boot(2f, true, false);
        NativeApp.speed = 70f; NativeApp.gpuPct = 7f; NativeApp.eePct = 0f;
        ticks(20);
        check(applied.isEmpty(),
                "GPU 7% sin metrica de EE: tampoco se recorta (bug puro de unidad)");
        gov.stop();
        saved = prefs.getString("adapt:content://iso/g.bin", null);
        check(saved != null && saved.contains("cpu=1"),
                "y el aprendizaje llega sin necesitar el hilo EE: " + saved);

        // 1c) TALLY DE VEREDICTOS (0.6.29, reformateado en 0.6.30): el regidor tiene
        //     que anotar QUÉ vio, tick a tick, en el perfil del juego. Es el unico
        //     numero que puede decir si los umbrales 85/35/80 estan bien puestos en un
        //     Snapdragon 6 o 7; sin el, la respuesta es mi criterio y no una medicion.
        //     El orden ahora es el del CONTRATO: unk,nog,gpu,cpu,vu,term,inv.
        boot(2f, true, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(6);                 // seis ticks de GPU llena (con enfriamientos)
        NativeApp.speed = 60f; NativeApp.gpuPct = 7f; NativeApp.eePct = 95f;
        ticks(3);                 // y tres de emulacion
        gov.stop();
        String raw = prefs.getString("adapt:content://iso/g.bin", null);
        check(raw != null, "el perfil se salvo con tally: " + raw);
        int gpuSeen = tallyOf(raw, CenitTelemetry.STATE_GPU_BOUND);
        int emuSeen = tallyOf(raw, CenitTelemetry.STATE_CPU_BOUND);
        check(gpuSeen > 0, "casilla GPU conta los ticks de GPU llena: " + raw);
        check(emuSeen > 0, "casilla CPU_BOUND conta los ticks de emulacion: " + raw);
        // Round-trip: un perfil leido de prefs tiene que conservar el tally.
        AdaptiveProfile again = new AdaptiveProfile(ctx, "content://iso/g.bin");
        check(again.loadMix[CenitTelemetry.STATE_GPU_BOUND] == gpuSeen
                        && again.loadMix[CenitTelemetry.STATE_CPU_BOUND] == emuSeen,
                "el tally sobrevive a guardar y reload: " + java.util.Arrays.toString(again.loadMix));

        // 1c-bis) UN perfil de 0.6.29 (4 casillas, orden gpu,emu,vu,sin) tiene que
        //     TRADUCIRSE, no leerse con el orden nuevo. Si se leyera tal cual, los
        //     recuentos de GPU acabarian en "desconocido" y los de emulacion en
        //     "sin cuello" — justo los numeros con los que alguien calibraria.
        prefs.edit().putString("adapt:content://iso/v29.bin", "load=7,3,1,2").apply();
        AdaptiveProfile heredado = new AdaptiveProfile(ctx, "content://iso/v29.bin");
        check(heredado.loadMix[CenitTelemetry.STATE_GPU_BOUND] == 7,
                "legacy: 7 ticks de GPU van a la casilla GPU_BOUND: "
                        + java.util.Arrays.toString(heredado.loadMix));
        check(heredado.loadMix[CenitTelemetry.STATE_CPU_BOUND] == 3,
                "legacy: 3 de emulacion a CPU_BOUND");
        check(heredado.loadMix[CenitTelemetry.STATE_VU_BOUND] == 1, "legacy: 1 de VU");
        check(heredado.loadMix[CenitTelemetry.STATE_UNKNOWN_LOAD] == 2, "legacy: 2 de desconocido");
        check(heredado.loadMix[CenitTelemetry.STATE_NO_BOTTLENECK] == 0
                        && heredado.loadMix[CenitTelemetry.STATE_INVALID_SAMPLE] == 0,
                "legacy: las casillas nuevas quedan en cero, no se inventan");
        heredado.save();
        String rescripto = prefs.getString("adapt:content://iso/v29.bin", null);
        check(rescripto.contains("load=2,0,7,3,1,0,0"),
                "y al salvarse ya sale con las SIETE casillas: " + rescripto);

        // 1d) Un ensayo del sintonizador (CONTINUE) NO contamina el tally NI la
        //     calibracion: sus lecturas son de prueba y el speed% baja a proposito.
        boot(2f, true, false);
        CpuVUTuner.answer = CpuVUTuner.CONTINUE;
        CpuVUTuner.active = true; // un ensayo en vuelo: isActive() tiene que decir si
        NativeApp.speed = 55f; NativeApp.gpuPct = 97f; NativeApp.eePct = 20f;
        ticks(6);
        gov.stop();
        raw = prefs.getString("adapt:content://iso/g.bin", null);
        check(raw != null && raw.contains("load=0,0,0,0,0,0,0"),
                "con CONTINUE del sintonizador el tally queda en cero: " + raw);

        // Un juego que va a tiempo tampoco cuenta: no hay cuello que diagnosticar.
        boot(2f, true, false);
        NativeApp.speed = 100f; NativeApp.gpuPct = 99f; NativeApp.eePct = 99f;
        ticks(8);
        gov.stop();
        raw = prefs.getString("adapt:content://iso/g.bin", null);
        check(raw != null && raw.contains("load=0,0,0,0,0,0,0"),
                "sin retraso no se llena ninguna casilla: " + raw);

        // Perfil de 0.6.28 (sin la clave "load"): se queda en ceros, no revienta.
        prefs.edit().putString("adapt:content://iso/old.bin",
                "floor=1.5;cpu=1;slow=9;turbo=2;tver=Rate%20-1;tad=;tap=0;tat=1;tnow=58").apply();
        AdaptiveProfile viejo = new AdaptiveProfile(ctx, "content://iso/old.bin");
        check(viejo.heldScale == 1.5f && viejo.cpuBound,
                "el perfil viejo sigue cargando lo que tenia");
        check(java.util.Arrays.stream(viejo.loadMix).allMatch(x -> x == 0),
                "y el tally ausente se queda en cero, no en basura");
        // Un tally roto a mano no desplaza veredictos de casilla.
        prefs.edit().putString("adapt:content://iso/roto.bin", "load=1,2").apply();
        AdaptiveProfile roto = new AdaptiveProfile(ctx, "content://iso/roto.bin");
        check(java.util.Arrays.stream(roto.loadMix).allMatch(x -> x == 0),
                "un load con tamano equivocado (2 cifras) se descarta entero");
        // Ni siquiera una cifra de mas puede correr un veredicto de casilla.
        prefs.edit().putString("adapt:content://iso/roto2.bin", "load=1,2,3,4,5,6,7,8").apply();
        AdaptiveProfile roto2 = new AdaptiveProfile(ctx, "content://iso/roto2.bin");
        check(java.util.Arrays.stream(roto2.loadMix).allMatch(x -> x == 0),
                "ocho cifras tambien se descartan: el formato no se adivina");


        // ================================================================
        // 2) SEÑAL AL SINTONIZADOR: cpuBoundNow y metricsAlive tienen que
        //    llegar como el clasificador las vio, no como las adivinaba el
        //    governor. Es lo que en 0.6.27 casi nunca se disparaba.
        // ================================================================
        boot(2f, false, false);
        CpuVUTuner.calls.clear();
        NativeApp.speed = 60f; NativeApp.gpuPct = 12f; NativeApp.eePct = 97f;
        ticks(1);
        String t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(t.contains("verdict=CPU_BOUND/BLOCK_RESOLUTION_DROP") && t.contains("metrics=true"),
                "GPU 12% + EE 97% -> CPU_BOUND con bloqueo de recorte al sintonizador: " + t);
        check(t.contains("behind=true"), "60% de velocidad es retraso real: " + t);

        // Con la MUESTRA invalida (sin flags) no se manda senal de emulacion a lo
        // loco y el regidor no puede recortar: es la Regla 3 del prompt contada por
        // el camino de verdad, no con un gpu=0 que ya no significa "sin metrica".
        boot(2f, false, false);
        CpuVUTuner.calls.clear();
        NativeApp.speed = 60f; NativeApp.gpuPct = 0f; NativeApp.eePct = 0f;
        NativeApp.flags = 0; // sin GPU_METRIC_VALID ni EMU_METRIC_VALID
        ticks(1);
        t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(t.contains("metrics=false"), "sin metricas validas, metricsAlive=false: " + t);
        check(!t.contains("BLOCK_RESOLUTION_DROP"), "sin metrica no se declara emulacion: " + t);
        check(applied.isEmpty(), "y con muestra invalida NO se recorta");
        // GPU sin metrica pero EE si: hay lectura, y dice emulacion (conf 55,
        // justo por encima de la barra de bloqueo). No es un "no se".
        boot(2f, false, false);
        CpuVUTuner.calls.clear();
        NativeApp.speed = 60f; NativeApp.gpuPct = 0f; NativeApp.eePct = 90f;
        NativeApp.flags = CenitLoadSample.FLAG_EMU_METRIC_VALID
                | CenitLoadSample.FLAG_WINDOW_COMPLETE | CenitLoadSample.FLAG_SAMPLE_FRESH;
        ticks(1);
        t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(t.contains("verdict=CPU_BOUND/BLOCK_RESOLUTION_DROP") && t.contains("metrics=true"),
                "EE valido sin GPU: emulacion declarada, metrica viva: " + t);

        // VU_WAIT también es cpuBound y su métrica cuenta como viva. OJO: desde
        // 0.6.30 hace falta la RACHA (tres ventanas), no un pico suelto — por eso
        // se corre el mismo numero cuatro ticks. Es el cambio de semantics que
        // pide la Fase 1.3 del prompt: "sostenido" tiene que significar sostenido.
        boot(2f, false, false);
        CpuVUTuner.calls.clear();
        NativeApp.speed = 60f; NativeApp.gpuPct = 0f; NativeApp.eePct = 0f;
        NativeApp.mtvuWaitMs = 40f; NativeApp.mtvuWaitCalls = 200L;
        ticks(1);
        t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(!t.contains("BLOCK_RESOLUTION_DROP"),
                "primer tick de espera MTVU: aun no es patron sostenido: " + t);
        ticks(3);
        t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(t.contains("verdict=VU_BOUND/BLOCK_RESOLUTION_DROP") && t.contains("metrics=true"),
                "MTVU esperando sostenido: VU_BOUND y metrica viva pese a gpu=0: " + t);

        // ================================================================
        // 3) CONTINUE del sintonizador congela subida/bajada (mismo contrato
        //    que 0.6.27): ni recorte ni aprendizaje con un ensayo en vuelo.
        // ================================================================
        boot(2f, false, false);
        CpuVUTuner.answer = CpuVUTuner.CONTINUE;
        CpuVUTuner.active = true;
        NativeApp.speed = 55f; NativeApp.gpuPct = 97f; NativeApp.eePct = 20f;
        ticks(6);
        check(applied.isEmpty(),
                "CONTINUE del sintonizador congela el recorte aunque la GPU este llena");

        // RETRY: el regidor reprograma corto (250 ms) en vez del segundo entero.
        boot(2f, false, false);
        ticks(1);
        CpuVUTuner.answer = CpuVUTuner.RETRY;
        NativeApp.speed = 55f; NativeApp.gpuPct = 97f; NativeApp.eePct = 20f;
        ticks(1);
        check(!Handler.queue.isEmpty(), "RETRY deja la cola con el siguiente tick");

        // ================================================================
        // 4) ESCALERA DE RECORTE: con GPU llena baja escalón a escalón con
        //    dos ticks por paso y sin saltarse ninguno del menú.
        // ================================================================
        boot(4f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        NativeApp.effectiveUpscale = 0f; // el nativo no objeta
        java.util.List<String> seen = new java.util.ArrayList<>();
        for (int i = 0; i < 40 && seen.size() < 6; i++) {
            ticks(1);
            if (!applied.isEmpty()) {
                String last = applied.get(applied.size() - 1);
                if (!seen.contains(last)) seen.add(last);
            }
        }
        // La escalera del menu: 4 -> 3 -> 2,5 -> 2 -> 1,5 -> 1,25. Si el regidor
        // tuviera lista propia, desde 4x saltaria a 2x (era el quejido de 0.6.1).
        check(seen.size() >= 5, "baja escalon a escalon: " + seen);
        check(seen.get(0).equals("3.0"), "primer paso 4x -> 3x: " + seen);
        check(seen.get(1).equals("2.5"), "segundo paso 3x -> 2,5x: " + seen);
        check(seen.get(2).equals("2.0"), "tercer paso 2,5x -> 2x: " + seen);
        check(seen.subList(0, 3).toString().equals("[3.0, 2.5, 2.0]"),
                "orden estricto del menu, sin saltos: " + seen);

        // ================================================================
        // 5) REBASA EL TECHO: si el usuario pone 1x, el regidor no sube de 1x
        //    aunque el juego vaya sobrado.
        // ================================================================
        boot(1f, false, false);
        NativeApp.speed = 100f; NativeApp.gpuPct = 10f; NativeApp.eePct = 10f;
        ticks(12);
        for (String a : applied)
            check(Float.parseFloat(a) <= 1f + 0.0001f, "nunca sube por encima del techo: " + a);

        // ================================================================
        // 6) SUBIDA LENTA: con el juego sobrado y escala recortada, sube tras
        //    8 ticks holgados.
        // ================================================================
        boot(2f, false, false);
        NativeApp.speed = 55f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(4); // baja 2 -> 1,5 (con los enfriamientos de por medio)
        float afterDrop = lastApplied();
        check(afterDrop < 2f, "primero recorto: " + afterDrop);
        NativeApp.speed = 100f; NativeApp.gpuPct = 40f; NativeApp.eePct = 40f;
        ticks(24);
        check(lastApplied() > afterDrop, "con el juego sobrado vuelve a subir: " + lastApplied());
        eqf(lastApplied(), 2f, "y sube hasta el techo, no mas alla");

        // ================================================================
        // 7) ENFRIAMIENTO TERMICO: con el SoC recortando, se baja pero no se
        //    sube (la regla de siempre, intacta).
        // ================================================================
        // Con el juego recortado y sobrado, el regidor sube (caso 6). Con el MISMO
        // retraso del termico encima, no sube: es la regla "con calor no se sube",
        // y hasta 0.6.30 estaba probada con un check(true, ...) que no comprobaba
        // nada porque no habia manera de inyectar el aviso.
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(4);
        check(lastApplied() < 2f, "con GPU llena si se recorta: " + lastApplied());
        final float recortado = lastApplied();
        applied.clear();
        fireThermal(2); // THERMAL_STATUS_MODERATE
        NativeApp.speed = 100f; NativeApp.gpuPct = 40f; NativeApp.eePct = 40f;
        ticks(24); // sin calor, esto sube hasta el techo (caso 6)
        check(applied.isEmpty(),
                "con throttle del sistema NO se sube, aunque el juego vaya sobrado: " + applied);
        fireThermal(0); // enfrio: vuelve a ser legal subir
        applied.clear();
        ticks(24);
        check(!applied.isEmpty(),
                "y al enfriar si vuelve a subir (el bloqueo era por calor, no permanente): "
                        + applied + " | publicados=" + NativeApp.published);

        // ================================================================
        // 8) CONGELAMIENTO: Cuota EE activa (ensayo o decision del usuario) ->
        //    el regidor no mueve la escala.
        // ================================================================
        boot(2f, false, false);
        NativeApp.effectiveRate = -1;
        NativeApp.speed = 50f; NativeApp.gpuPct = 97f; NativeApp.eePct = 20f;
        ticks(1); // el congelamiento empieza devolviendo el techo del usuario
        check(applied.contains("2.0"),
                "con Cuota EE el regidor devuelve el techo: " + applied);
        applied.clear();
        ticks(8);
        check(applied.isEmpty(),
                "y despues no mueve nada mas (eso es congelarse): " + applied);

        // Pausa / sin juego: se congela y suelta el turbo si estaba enganchado.
        boot(2f, false, true);
        NativeApp.paused = true;
        NativeApp.speed = 50f;
        ticks(3);
        check(applied.isEmpty(), "con la VM pausada no se toca la escala");

        // ================================================================
        // 9) TURBO DE CARGAS con la unidad corregida: carga = GPU < 22% y
        //    velocidad clavada. Con los numeros viejos (0,22) esto casi nunca
        //    pasaba; con 0,6% de GPU ya no se engancha el turbo "siempre".
        // ================================================================
        boot(2f, true, true);
        NativeApp.speed = 100f; NativeApp.gpuPct = 5f; NativeApp.eePct = 3f;
        ticks(1); // fija escala
        NativeApp.speed = 100f; NativeApp.gpuPct = 5f; NativeApp.eePct = 3f;
        ticks(6);
        check(NativeApp.actions.contains("limiter=1"),
                "carga detectada (GPU 5%, velocidad clavada) engancha Turbo");
        // Vuelve el juego real -> se suelta.
        NativeApp.gpuPct = 70f; NativeApp.speed = 100f;
        ticks(3);
        check(NativeApp.actions.contains("limiter=0"),
                "con la GPU trabajando se suelta el Turbo");

        // Con GPU al 30% NO es carga: antes, con la metrica leida como fraccion,
        // cualquier valor >= 0,40 liberaba el turbo al tick siguiente.
        boot(2f, true, true);
        NativeApp.speed = 100f; NativeApp.gpuPct = 30f; NativeApp.eePct = 30f;
        ticks(10);
        check(!NativeApp.actions.contains("limiter=1"),
                "GPU al 30% no es pantalla de carga");

        // ================================================================
        // 10) self-disabled: si la resolucion efectiva ignora lo pedido, el
        //     regidor se rinde una vez y avisa al sintonizador.
        // ================================================================
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(3); // pide bajar
        NativeApp.effectiveUpscale = 2f; // ...y no se entero: sigue en el techo
        CpuVUTuner.calls.clear();
        // cooldown(2) + los dos ticks de espera que el propio regidor se da para
        // no rendirse por un falso positivo, y aun asi hay que dejar un colchon:
        // el abono de pendingTicks consume ticks sin juzgar nada.
        ticks(8);
        check(CpuVUTuner.calls.stream().anyMatch(s -> s.startsWith("halt:")),
                "escala ignorada -> el regidor se rinde y detiene el sintonizador");

        // ================================================================
        // 11) dynamic_res apagado: halt del sintonizador y calma total.
        // ================================================================
        boot(2f, false, false);
        ticks(1);
        prefs.edit().putBoolean("dynamic_res", false).apply();
        NativeApp.speed = 50f; NativeApp.gpuPct = 97f; NativeApp.eePct = 30f;
        CpuVUTuner.calls.clear();
        applied.clear();
        ticks(6);
        check(applied.isEmpty(), "con dynamic_res apagado no se mueve la escala");
        check(CpuVUTuner.calls.contains("halt:dynamic_res switched off"),
                "apagar dynamic_res corta el ensayo en curso");

        // ================================================================
        // 12) stop(): devuelve el techo y salva el perfil.
        // ================================================================
        boot(2f, true, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(4);
        applied.clear();
        gov.stop();
        check(applied.contains("2.0"), "al parar se devuelve el techo del usuario: " + applied);


        // ================================================================
        // 13) FASE 1.8 (0.6.30): CADA tick publica el veredicto al HUD. No es
        //     adorno: sin esa linea el usuario no puede verificar por que el
        //     regidor recorto (o no), y el prompt pide que cada accion quede
        //     asociada a una muestra. Se comprueban los CODIGOS del contrato, no
        //     palabras, porque el numero es lo que cruza JNI.
        // ================================================================
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(4);
        check(!NativeApp.published.isEmpty(), "el regidor publica veredictos al HUD");
        String pub = NativeApp.published.get(NativeApp.published.size() - 1);
        check(pub.startsWith(CenitTelemetry.STATE_GPU_BOUND + ","),
                "GPU 96% + EE 30% publica codigo 2 (GPU_BOUND): " + pub);
        check(pub.contains(",1,") /* accion ALLOW_RESOLUTION_DROP */,
                "y publica la ACCION permitida, no solo el estado: " + pub);
        gov.stop();
        // Con la muestra invalida TAMBIEN se publica: es justo el caso donde el
        // usuario necesita ver por que no pasa nada.
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        NativeApp.flags = 0;
        ticks(2);
        pub = NativeApp.published.get(NativeApp.published.size() - 1);
        check(pub.startsWith(CenitTelemetry.STATE_INVALID_SAMPLE + ","),
                "sin metricas validas se publica codigo 6 (INVALID_SAMPLE): " + pub);
        gov.stop();

        // ================================================================
        // 14) REGLA 5 DEL PROMPT, de punta a punta, en el regidor (no solo en el
        //     clasificador): una GPU dominante por POCA diferencia no autoriza a
        //     estropear la imagen. GPU 88 / EE 85,6 son las dos calientes y el
        //     margen de 2,4 puntos da confianza 47, por debajo de la barra de
        //     recorte (55): el estado sigue siendo GPU_BOUND, la accion llega
        //     degradada y aqui NO se toca la escala.
        // ================================================================
        boot(2f, true, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 88f; NativeApp.eePct = 85.6f;
        ticks(10);
        check(applied.isEmpty(),
                "GPU 88 / EE 85,6 (margen 2,4, conf 47 < 55): NO se recorta: " + applied);
        gov.stop();
        String rawMargin = prefs.getString("adapt:content://iso/g.bin", null);
        check(rawMargin != null && tallyOf(rawMargin, CenitTelemetry.STATE_CPU_BOUND) == 0,
                "y tampoco se aprende CPU_BOUND de una lectura floja: " + rawMargin);
        // Con el MISMO par de cifras pero margen grande si se recorta: lo que
        // cambio no es la GPU, es la evidencia.
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 78f;
        ticks(4);
        check(!applied.isEmpty(),
                "GPU 96 / EE 78 (margen 18, conf 85): si baja un escalon: " + applied);
        gov.stop();

        // ================================================================
        // 15) THERMAL_BOUND con la matriz de la Fase 4. Con GPU LLENA y el SoC
        //     recortando por calor, 0.6.29 recortaba imagen: el retraso era de
        //     pixeles segun la metrica, y era verdad a medias — con el reloj
        //     recortado la GPU tambien tiene menos ciclos. La matriz nueva dice
        //     que durante un throttle no se recorta y NO se aprende nada, porque
        //     esa memoria se quedaria persiguiendo al juego cuando enfrie.
        // ================================================================
        boot(2f, true, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(1); // fija la escala inicial, todavia sin calor
        fireThermal(2); // THERMAL_STATUS_MODERATE por el listener real
        ticks(12);
        check(applied.isEmpty(),
                "throttle + GPU llena: la rama termica manda y NO se recorta: " + applied);
        gov.stop();
        String rawTherm = prefs.getString("adapt:content://iso/g.bin", null);
        check(rawTherm != null && !rawTherm.contains("cpu=1"),
                "y el throttle no escribe cpuBound en el perfil: " + rawTherm);
        check(rawTherm != null && tallyOf(rawTherm, CenitTelemetry.STATE_THERMAL_BOUND) > 0,
                "el tally cuenta los ticks termicos aparte: " + rawTherm
                        + " | publicados=" + NativeApp.published);
        // Las MISMAS cifras, con y sin calor, producen dos etiquetas distintas. El
        // tick de antes del aviso es GPU_BOUND (y es correcto que se cuente: esa
        // lectura NO estaba bajo throttle); los doce de despues son THERMAL_BOUND.
        // Lo que hay que probar no es "GPU==0" (eso era mi expectativa mal escrita),
        // sino que el throttle no se disfraza de pixeles: la casilla GPU no crece
        // mientras dura el aviso.
        check(tallyOf(rawTherm, CenitTelemetry.STATE_GPU_BOUND) == 1
                        && tallyOf(rawTherm, CenitTelemetry.STATE_THERMAL_BOUND) == 12,
                "y los ticks termicos no se disfrazan de GPU_BOUND: " + rawTherm);
        PowerManager.status = -1;

        // ================================================================
        // 16) Fallback de Regla 5 en la lectura: si el nativo no contesta el
        //     snapshot, el regidor NO vuelve a los getters sueltos (eso seria
        //     comparar ventanas distintas otra vez) y se queda quieto.
        // ================================================================
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        NativeApp.sampleAvailable = false;
        ticks(8);
        check(applied.isEmpty(), "sin snapshot el regidor no recorta a ciegas: " + applied);
        pub = NativeApp.published.get(NativeApp.published.size() - 1);
        check(pub.startsWith(CenitTelemetry.STATE_INVALID_SAMPLE + ","),
                "y lo dice en el HUD con INVALID_SAMPLE: " + pub);
        gov.stop();
        // Lo mismo con la longitud equivocada (un nativo viejo, un contrato roto).
        boot(2f, false, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        NativeApp.sampleSize = 23;
        ticks(6);
        check(applied.isEmpty(), "un snapshot de 23 cifras tampoco se usa: " + applied);
        gov.stop();

        // ================================================================
        // 17) MUESTRA NO FRESCA (bit3 = 0): el nativo dice "esta ventana ya la
        //     leiste". Contarla dos veces inflaria el tally y, peor, las rachas
        //     sostenidas del trend: un VU_BOUND de tres ticks seria dos muestras
        //     reales repetidas. El regidor la descarta y no aprende.
        // ================================================================
        boot(2f, true, false);
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        NativeApp.flags = CenitLoadSample.FLAG_GPU_METRIC_VALID
                | CenitLoadSample.FLAG_EMU_METRIC_VALID
                | CenitLoadSample.FLAG_WINDOW_COMPLETE; // sin SAMPLE_FRESH
        ticks(6);
        check(applied.isEmpty(), "una muestra repetida no recorta: " + applied);
        gov.stop();
        raw = prefs.getString("adapt:content://iso/g.bin", null);
        check(raw != null && tallyOf(raw, CenitTelemetry.STATE_INVALID_SAMPLE) > 0,
                "y se cuenta como muestra invalida, no como desconocido: " + raw);

        // ================================================================
        // 18) VU_BOUND SOSTENIDO: el clasificador pide tres ventanas seguidas con
        //     espera MTVU alta ANTES de declararlo (un pico no es patron). Aqui se
        //     verifica que el regidor le pasa el trend al clasificador: con el
        //     mismo numero, al primer tick aun no es VU y al tercero ya lo es.
        // ================================================================
        boot(2f, false, false);
        CpuVUTuner.calls.clear();
        NativeApp.speed = 60f; NativeApp.gpuPct = 60f; NativeApp.eePct = 60f;
        NativeApp.mtvuWaitMs = 40f; NativeApp.mtvuWaitCalls = 200L;
        ticks(1);
        t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(!t.contains("BLOCK_RESOLUTION_DROP"),
                "tick 1 de espera MTVU: aun no es VU sostenido: " + t);
        ticks(3);
        t = CpuVUTuner.calls.get(CpuVUTuner.calls.size() - 1);
        check(t.contains("verdict=VU_BOUND/BLOCK_RESOLUTION_DROP"),
                "tick 4: la racha sostiene el VU_BOUND y el sintonizador se entera: " + t);
        gov.stop();

        // ================================================================
        // 19) BORRAR LA EVIDENCIA DE UMBRALES (el gesto de Ajustes). La
        //     calibracion vive en DOS copias — prefs y el trend del regidor vivo —
        //     y la politica se deriva de ella. forgetCalibration() tiene que
        //     tumbar las tres de golpe: borrar solo una deja el boton mintiendo
        //     (el regidor vivo resalva, o el proximo start rel ee la prefs vieja).
        // ================================================================
        boot(2f, true, false);
        gov.stop();
        // Evidencia fabricada: 3 claves x 5 ventanas x 2 sesiones (los minimos del
        // prompt), con GPU concentrada en el bin 80-90 y EE en el 50-60.
        final String histGpu = "0,0,0,0,0,0,0,0,5,0";
        final String histEe = "0,0,0,0,0,5,0,0,0,0";
        final String cal3 = "SLUS-A#HUAWEI/NCO-LX3#-1:5|s1,s2|" + histGpu + "|" + histEe
                + ";SLUS-B#HUAWEI/NCO-LX3#-1:5|s1,s2|" + histGpu + "|" + histEe
                + ";SLUS-C#HUAWEI/NCO-LX3#-1:5|s1,s2|" + histGpu + "|" + histEe;
        prefs.edit().putString(DynamicResolutionGovernor.CAL_PREF_KEY, cal3).apply();
        gov.start(); // relee la prefs y reconstruye la politica
        check(!policyProvisional(gov),
                "con 3 juegos x 5 ventanas x 2 sesiones la politica deja de ser provisional");
        eqf(policyField(gov, "gpuHotPct"), 85f, "y el umbral sale de la evidencia (bin 80-90)");
        // El gesto del usuario:
        final boolean had = DynamicResolutionGovernor.forgetCalibration(ctx);
        check(had, "forgetCalibration dice que habia evidencia que borrar");
        check(prefs.getString(DynamicResolutionGovernor.CAL_PREF_KEY, "").isEmpty(),
                "la copia de prefs se fue: " + prefs.getString(DynamicResolutionGovernor.CAL_PREF_KEY, "QUEDA"));
        check(policyProvisional(gov),
                "y la politica del regidor VIVO vuelve a la de criterio (no hay que reiniciar)");
        gov.stop(); // stop salva la calibracion: si re-salvara la vieja, esto la revive
        check(prefs.getString(DynamicResolutionGovernor.CAL_PREF_KEY, "").isEmpty(),
                "el stop posterior NO resalva evidencia borrada: "
                        + prefs.getString(DynamicResolutionGovernor.CAL_PREF_KEY, "REVIVIÓ"));
        // Y sin evidencia, el boton no debe prometer que borró algo.
        boot(2f, true, false);
        check(!DynamicResolutionGovernor.forgetCalibration(ctx),
                "con nada que borrar, forgetCalibration lo dice (el aviso no miente)");
        gov.stop();

        // ================================================================
        // 20) EL CUADRO DEL HUD SE BORRA AL ABRIR Y AL CERRAR LA PARTIDA. El
        //     header (CenitTelemetry.h) justifica el reset con el fantasma del
        //     juego anterior; sin llamante era un adorno que nadie ejecutaba. Se
        //     comprueba el HECHO (start y stop lo piden) y que los ticks NO lo
        //     pidan: un reset por tick dejaria el LOAD parpadeando en "-".
        //     start() es idempotente, asi que un segundo start tampoco duplica.
        // ================================================================
        boot(2f, false, false);
        eqi(NativeApp.hudResets, 1, "el arranque de partida borra el cuadro del HUD");
        NativeApp.speed = 60f; NativeApp.gpuPct = 96f; NativeApp.eePct = 30f;
        ticks(6);
        eqi(NativeApp.hudResets, 1, "seis ticks NO piden borrar: solo el ciclo de partida");
        check(!NativeApp.published.isEmpty(),
                "y si publican veredictos: " + NativeApp.published.size());
        gov.start(); // idempotente: esta vivo, no vuelve a resetear
        eqi(NativeApp.hudResets, 1, "un start repetido sobre la misma partida no duplica");
        gov.stop();
        eqi(NativeApp.hudResets, 2, "el cierre de la partida tambien lo borra");

        System.out.println("Harness6: " + checks + " Comprobaciones, " + failures + " Fallos");
        System.exit(failures == 0 ? 0 : 1);
    }

    /** mPolicy.provisional del regidor vivo, por reflexion (solo el arnés). */
    static boolean policyProvisional(DynamicResolutionGovernor g) {
        try {
            java.lang.reflect.Field f = DynamicResolutionGovernor.class.getDeclaredField("mPolicy");
            f.setAccessible(true);
            return ((LoadPolicy) f.get(g)).provisional;
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }

    static float policyField(DynamicResolutionGovernor g, String name) {
        try {
            java.lang.reflect.Field f = DynamicResolutionGovernor.class.getDeclaredField("mPolicy");
            f.setAccessible(true);
            final LoadPolicy p = (LoadPolicy) f.get(g);
            java.lang.reflect.Field v = LoadPolicy.class.getDeclaredField(name);
            v.setAccessible(true);
            return ((Number) v.get(p)).floatValue();
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException(e);
        }
    }
}
