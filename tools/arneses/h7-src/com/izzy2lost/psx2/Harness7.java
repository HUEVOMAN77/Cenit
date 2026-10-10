package com.izzy2lost.psx2;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Arnés 7 — el CONTRATO, no la lógica.
 *
 * Los números de los estados de carga viven en tres sitios que no se ven entre
 * sí: el diccionario Java (CenitTelemetry), el HUD C++ (kStates[] de
 * ImGuiOverlays.cpp) y las preferencias del perfil por-juego. Un cambio de
 * orden en cualquiera de los tres compila, pasa los demás arneses y pinta
 * "GPU_BOUND" donde el clasificador dijo THERMAL_BOUND — y ese número es una
 * decisión de recorte. Este arnés es el único lugar donde ese error se ve sin
 * encender un teléfono.
 *
 * Lee el C++ REAL del repo (no una copia): si alguien mueve la tabla del HUD,
 * esto revienta antes del commit.
 */
public class Harness7 {

    static int checks = 0, failures = 0;
    static final String REPO = System.getProperty("cenit.repo", "/app/cenit");
    static final String OVERLAYS = REPO + "/app/src/main/cpp/pcsx2/ImGui/ImGuiOverlays.cpp";
    static final String NATIVE_LIB = REPO + "/app/src/main/cpp/native-lib.cpp";
    static final String TELEMETRY_H = REPO + "/app/src/main/cpp/CenitTelemetry.h";
    static final String SAMPLE_JAVA = REPO
            + "/app/src/main/java/com/izzy2lost/psx2/CenitLoadSample.java";

    static void check(boolean ok, String name) {
        checks++;
        if (!ok) { failures++; System.out.println("FALLO: " + name); }
    }

    static void eq(int got, int want, String name) {
        checks++;
        if (got != want) {
            failures++;
            System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")");
        }
    }

    static String read(String path) throws IOException {
        return new String(Files.readAllBytes(Paths.get(path)), StandardCharsets.UTF_8);
    }

    /** Extrae los literales de un `static constexpr const char* kX[] = {...}`. */
    static List<String> stringArray(String src, String name) {
        final Matcher m = Pattern.compile("k" + name + "\\s*\\[\\]\\s*=\\s*\\{([^}]*)\\}",
                Pattern.DOTALL).matcher(src);
        final List<String> out = new ArrayList<>();
        if (!m.find()) return out;
        final Matcher q = Pattern.compile("\"([^\"]*)\"").matcher(m.group(1));
        while (q.find()) out.add(q.group(1));
        return out;
    }

    public static void main(String[] args) throws Exception {
        // ================================================================
        // 1) EL DICCIONARIO JAVA es la fuente de verdad. Se verifica contra los
        //    números del contrato, escritos aqui a mano una vez mas: si alguien
        //    cambia CenitTelemetry y el contrato, este arnes sigue verde — pero
        //    cambian las dos cosas a la vez y el HUD se queda atras, el punto 2
        //    lo pilla. La redundancia es el test.
        // ================================================================
        eq(CenitTelemetry.STATE_UNKNOWN_LOAD, 0, "STATE_UNKNOWN_LOAD = 0");
        eq(CenitTelemetry.STATE_NO_BOTTLENECK, 1, "STATE_NO_BOTTLENECK = 1");
        eq(CenitTelemetry.STATE_GPU_BOUND, 2, "STATE_GPU_BOUND = 2");
        eq(CenitTelemetry.STATE_CPU_BOUND, 3, "STATE_CPU_BOUND = 3");
        eq(CenitTelemetry.STATE_VU_BOUND, 4, "STATE_VU_BOUND = 4");
        eq(CenitTelemetry.STATE_THERMAL_BOUND, 5, "STATE_THERMAL_BOUND = 5");
        eq(CenitTelemetry.STATE_INVALID_SAMPLE, 6, "STATE_INVALID_SAMPLE = 6");
        eq(CenitTelemetry.STATE_COUNT, 7, "STATE_COUNT = 7 casillas");

        // ordinalForHud() tiene que coincidir con el diccionario, no con el orden
        // de declaracion de la enum.
        final GameLoadClassifier.LoadState[] hudOrder = {
            GameLoadClassifier.LoadState.UNKNOWN_LOAD,
            GameLoadClassifier.LoadState.NO_BOTTLENECK,
            GameLoadClassifier.LoadState.GPU_BOUND,
            GameLoadClassifier.LoadState.CPU_BOUND,
            GameLoadClassifier.LoadState.VU_BOUND,
            GameLoadClassifier.LoadState.THERMAL_BOUND,
            GameLoadClassifier.LoadState.INVALID_SAMPLE,
        };
        final int[] hudCodes = {
            CenitTelemetry.STATE_UNKNOWN_LOAD, CenitTelemetry.STATE_NO_BOTTLENECK,
            CenitTelemetry.STATE_GPU_BOUND, CenitTelemetry.STATE_CPU_BOUND,
            CenitTelemetry.STATE_VU_BOUND, CenitTelemetry.STATE_THERMAL_BOUND,
            CenitTelemetry.STATE_INVALID_SAMPLE,
        };
        for (int i = 0; i < hudOrder.length; i++)
            eq(hudOrder[i].ordinalForHud(), hudCodes[i],
                    "ordinalForHud de " + hudOrder[i] + " = " + hudCodes[i]);

        // Y las acciones, igual (el contrato define HOLD=0..WAIT=3).
        eq(GameLoadClassifier.Action.HOLD.codeForHud(), 0, "HOLD = 0");
        eq(GameLoadClassifier.Action.ALLOW_RESOLUTION_DROP.codeForHud(), 1, "ALLOW = 1");
        eq(GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP.codeForHud(), 2, "BLOCK = 2");
        eq(GameLoadClassifier.Action.WAIT_FOR_EVIDENCE.codeForHud(), 3, "WAIT = 3");

        // codeForHud() NO puede ser ordinal(): es justamente lo que se romperia en
        // silencio al reordenar. Se comprueba que el mapeo resiste un reordenamiento
        // simulando la pregunta, no el codigo: los cuatro codigos tienen que ser
        // distintos y estables.
        final java.util.Set<Integer> codes = new java.util.HashSet<>();
        for (GameLoadClassifier.Action a : GameLoadClassifier.Action.values())
            codes.add(a.codeForHud());
        eq(codes.size(), GameLoadClassifier.Action.values().length,
                "cada accion tiene un codigo propio (sin colisiones)");

        // ================================================================
        // 2) EL HUD C++ (archivo real del repo) dice lo mismo.
        // ================================================================
        final String overlays = read(OVERLAYS);
        final List<String> kStates = stringArray(overlays, "States");
        final List<String> kActions = stringArray(overlays, "Actions");
        eq(kStates.size(), 7, "el HUD tiene 7 nombres de estado, no " + kStates.size());
        eq(kActions.size(), 4, "el HUD tiene 4 nombres de accion, no " + kActions.size());
        final String[] wantStates = {"UNKNOWN", "NO_BOTTLENECK", "GPU_BOUND", "CPU_BOUND",
                "VU_BOUND", "THERMAL_BOUND", "INVALID_SAMPLE"};
        for (int i = 0; i < Math.min(kStates.size(), wantStates.length); i++)
            check(wantStates[i].equals(kStates.get(i)),
                    "HUD kStates[" + i + "] = " + wantStates[i] + ", dice " + kStates.get(i));
        // El orden de las palabras del HUD es EL ORDEN DEL CONTRATO: el indice del
        // array es el numero que manda Java. Si alguien reordena kStates[] sin mover
        // el diccionario, el HUD pinta THERMAL_BOUND donde iba GPU_BOUND.
        check(kStates.indexOf("GPU_BOUND") == CenitTelemetry.STATE_GPU_BOUND,
                "kStates[] pone GPU_BOUND en el indice 2");
        check(kStates.indexOf("THERMAL_BOUND") == CenitTelemetry.STATE_THERMAL_BOUND,
                "kStates[] pone THERMAL_BOUND en el indice 5");
        check(kStates.indexOf("INVALID_SAMPLE") == CenitTelemetry.STATE_INVALID_SAMPLE,
                "kStates[] pone INVALID_SAMPLE en el indice 6");
        check(kActions.indexOf("SCALE_DOWN") == 1 && kActions.indexOf("BLOCK_DROP") == 2,
                "kActions[] respeta ALLOW=1 / BLOCK=2");
        // La linea pintada tiene que incluir el termico: sin el, THERMAL_BOUND no
        // se puede auditar (es el unico estado que depende de un dato que solo Java
        // tiene).
        check(overlays.contains("THERM:"), "el HUD pinta THERM (auditoria de THERMAL_BOUND)");
        // Y no puede vivir dentro del bloque ARCH_ARM64: la telemetria es comun a
        // cualquier ABI y el clasificador corre tambien en x86 emulado.
        check(overlays.indexOf("s_cenit_load_line.format") > overlays.indexOf("#ifdef __ANDROID__"),
                "la linea LOAD esta en un bloque __ANDROID__ (no ARCH_ARM64)");

        // ================================================================
        // 3) LA FIRMA JNI de 6 argumentos, igual en los tres lados.
        // ================================================================
        final String nativeLib = read(NATIVE_LIB);
        final String javaNative = read(REPO
                + "/app/src/main/java/com/izzy2lost/psx2/NativeApp.java");
        check(nativeLib.contains("Java_com_izzy2lost_psx2_NativeApp_setCenitLoadVerdict"),
                "el JNI del veredicto existe");
        // Los argumentos del JNI, en orden: jint state, jint conf, jint action,
        // jdouble gpu, jdouble ee, jint thermal.
        final int fn = nativeLib.indexOf("NativeApp_setCenitLoadVerdict");
        check(fn > 0, "setCenitLoadVerdict presente en native-lib.cpp");
        final String sig = nativeLib.substring(fn, Math.min(nativeLib.length(), fn + 700));
        final int nState = count(sig.substring(0, indexOfClosedParen(sig)), "jint");
        final int nDouble = count(sig.substring(0, indexOfClosedParen(sig)), "jdouble");
        eq(nState, 4, "cuatro jint (estado, confianza, accion, termico)");
        eq(nDouble, 2, "dos jdouble (gpu, ee)");
        check(javaNative.contains("setCenitLoadVerdict(int stateOrdinal, int confidencePct,")
                        || Pattern.compile("setCenitLoadVerdict\\(int stateOrdinal,\\s*int confidencePct,")
                                .matcher(javaNative).find(),
                "Java declara el veredicto con el mismo primer argumento");
        eq(countParams(javaNative, "native void setCenitLoadVerdict"), 6,
                "Java declara 6 parametros (una firma JNI desalineada no falla al compilar)");
        // getCenitSample: double[] de 24, mismo tamano que la constante Java.
        check(nativeLib.contains("NewDoubleArray(" + CenitLoadSample.SIZE + ")"),
                "el nativo crea un array de " + CenitLoadSample.SIZE + " doubles");
        check(javaNative.contains("native double[] getCenitSample()"),
                "Java declara getCenitSample() como double[]");

        // ================================================================
        // 4) EL CONTRATO ESCRITO (docs) tampoco se puede quedar atras: si la
        //    tabla de indices del markdown dice otra cosa que CenitLoadSample,
        //    el dia que alguien lea el doc para agregar un campo va a poner el
        //    numero equivocado.
        // ================================================================
        final String doc = read(REPO + "/docs/CONTRATO_CENIT_LOAD_SAMPLE.md");
        check(doc.contains("IDX_GPU_PCT") && doc.contains("IDX_EE_PCT"),
                "el contrato menciona los indices que usa el clasificador");
        check(doc.contains("6=INVALID_SAMPLE"), "el contrato lista los 7 estados");
        check(doc.contains("3=WAIT_FOR_EVIDENCE"), "el contrato lista las 4 acciones");
        check(doc.contains("thermalStatus"), "el contrato documenta el parametro termico");

        // ================================================================
        // 5) TAMAÑO Y ORDEN DEL SNAPSHOT: los 24 indices de CenitLoadSample son
        //    consecutivos y sin huecos. Reordenar uno existente es romper el
        //    contrato con los perfiles ya guardados y con cualquier log viejo.
        // ================================================================
        final int[] idx = {
            CenitLoadSample.IDX_FPS, CenitLoadSample.IDX_SPEED_PCT,
            CenitLoadSample.IDX_FRAMETIME_AVG_MS, CenitLoadSample.IDX_FRAMETIME_MIN_MS,
            CenitLoadSample.IDX_FRAMETIME_MAX_MS, CenitLoadSample.IDX_FRAMETIME_P50_MS,
            CenitLoadSample.IDX_FRAMETIME_P95_MS, CenitLoadSample.IDX_GPU_PCT,
            CenitLoadSample.IDX_GPU_AVG_MS, CenitLoadSample.IDX_EE_PCT,
            CenitLoadSample.IDX_EE_AVG_MS, CenitLoadSample.IDX_GS_PCT,
            CenitLoadSample.IDX_VU_THREAD_PCT, CenitLoadSample.IDX_MTVU_WAIT_MS,
            CenitLoadSample.IDX_MTVU_WAIT_CALLS, CenitLoadSample.IDX_MTVU_EXEC_MS,
            CenitLoadSample.IDX_VERTICAL_HZ, CenitLoadSample.IDX_PRESENTS,
            CenitLoadSample.IDX_WINDOW_S, CenitLoadSample.IDX_UPSCALE,
            CenitLoadSample.IDX_EE_CYCLE_RATE, CenitLoadSample.IDX_EE_CYCLE_SKIP,
            CenitLoadSample.IDX_MTVU_ON, CenitLoadSample.IDX_FLAGS,
        };
        eq(idx.length, CenitLoadSample.SIZE, "hay una constante por cada indice del contrato");
        for (int i = 0; i < idx.length; i++) {
            eq(idx[i], i, "IDX #" + i + " sigue valiendo " + i);
        }
        // Y el nativo escribe cada posicion con el MISMO numero (se comprueba que
        // los 24 indices aparecen asignados en native-lib.cpp).
        for (int i = 0; i < CenitLoadSample.SIZE; i++)
            check(nativeLib.contains("s_cenit_sample[" + i + "]"),
                    "el nativo asigna la posicion " + i);

        // ================================================================
        // 6) EL RESET DEL CUADRO: CenitTelemetry.h define ResetLoadVerdict con un
        //    motivo concreto (no heredar el veredicto del disco anterior). Un reset
        //    sin llamantes es un adorno: compila, nunca corre y el fantasma sigue
        //    ahi. Se exige la cadena entera header -> JNI -> regidor, porque cada
        //    eslabon se puede perder por separado (borrar el JNI, o el safe*, o la
        //    llamada del start) y en los tres casos el HUD sigue mintiendo.
        // ================================================================
        final String telemetryH = read(TELEMETRY_H);
        check(telemetryH.contains("inline void ResetLoadVerdict()"),
                "el header define el reset");
        check(!telemetryH.contains("published_seq"),
                "el header no guarda un contador que nadie lee (serie de publicacion)");
        check(nativeLib.contains("Java_com_izzy2lost_psx2_NativeApp_resetCenitLoadVerdict")
                        && nativeLib.contains("CenitTelemetry::ResetLoadVerdict()"),
                "existe el JNI que llama al reset del header");
        final int rfn = nativeLib.indexOf("NativeApp_resetCenitLoadVerdict");
        check(rfn > 0, "resetCenitLoadVerdict presente en native-lib.cpp");
        final String rsig = nativeLib.substring(rfn, Math.min(nativeLib.length(), rfn + 400));
        eq(count(rsig.substring(0, indexOfClosedParen(rsig)), "jint")
                        + count(rsig.substring(0, indexOfClosedParen(rsig)), "jdouble"),
                0, "el reset JNI no lleva argumentos (una firma sin parametros no se desfasa)");
        check(javaNative.contains("static native void resetCenitLoadVerdict()"),
                "Java declara el nativo sin parametros");
        check(javaNative.contains("safeResetCenitLoadVerdict()"),
                "Java expone el envoltorio seguro (el crudo no se llama desde el regidor)");
        final String governor = read(REPO
                + "/app/src/main/java/com/izzy2lost/psx2/DynamicResolutionGovernor.java");
        eq(count(governor, "NativeApp.safeResetCenitLoadVerdict()"), 2,
                "el regidor borra el cuadro en start() y en stop() (ni una ni tres)");

        System.out.println("Harness7: " + checks + " Comprobaciones, " + failures + " Fallos");
        System.exit(failures == 0 ? 0 : 1);
    }

    static int indexOfClosedParen(String s) {
        int depth = 0;
        for (int i = 0; i < s.length(); i++) {
            final char c = s.charAt(i);
            if (c == '(') depth++;
            else if (c == ')') { depth--; if (depth == 0) return i; }
        }
        return s.length();
    }

    static int count(String s, String needle) {
        int n = 0, i = 0;
        while ((i = s.indexOf(needle, i)) >= 0) { n++; i += needle.length(); }
        return n;
    }

    /** Numero de parametros de una declaracion nativa de Java en el texto fuente. */
    static int countParams(String src, String head) {
        int p = src.indexOf(head);
        if (p < 0) return -1;
        p = src.indexOf('(', p + head.length() - 1);
        if (p < 0) return -1;
        int depth = 0, n = 1, end = -1;
        for (int i = p; i < src.length(); i++) {
            final char c = src.charAt(i);
            if (c == '(') depth++;
            else if (c == ')') { depth--; if (depth == 0) { end = i; break; } }
        }
        if (end < 0) return -1;
        final String body = src.substring(p + 1, end);
        if (body.trim().isEmpty()) return 0;
        depth = 0;
        n = 1;
        for (int i = 0; i < body.length(); i++) {
            final char c = body.charAt(i);
            if (c == '<') depth++;
            else if (c == '>') depth--;
            else if (c == ',' && depth == 0) n++;
        }
        return n;
    }
}
