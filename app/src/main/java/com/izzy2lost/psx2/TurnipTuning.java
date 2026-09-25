package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Ajuste fino del driver Turnip (Cenit 0.6.20).
 *
 * Dos cosas, ambas sin recompilar Mesa:
 *
 * 1) Perfiles por gama de telefono. Turnip lee TU_DEBUG por getenv() en su init
 *    (src/util/os_misc.c: os_get_option cae a getenv), y Cenit fija esa variable
 *    justo antes de cargar el driver (VKLoader.cpp ApplyDriverEnvLocked). Los
 *    perfiles se eligen segun getDevicePerformanceTier() (0 baja, 1 media, 2 alta).
 *
 *    Por que importan en gama baja/media: el binning concurrente (CB, "nocb") y los
 *    resolves concurrentes usan passes extra que en GPUs Adreno recortadas cuestan
 *    ancho de banda de un presupuesto que no tienen. sysmem cambia donde vive el
 *    framebuffer, y es justamente lo que hay que medir (no afirmar) en cada gama.
 *
 * 2) Cache de shaders en disco. En Android Mesa la trae APAGADA por defecto
 *    (disk_cache_os.c: disable_by_default = true bajo DETECT_OS_ANDROID), asi que el
 *    driver recompila los shaders de cada juego en cada arranque. Encenderla con una
 *    ruta escribible es la ganancia mas grande posible para "universal en gama baja":
 *    menos compilaciones = menos tirones al empezar y al entrar a un area nueva.
 *
 * 3) Reglas POR JUEGO, dos caminos con la misma clave (el serial):
 *    a) tuDebugFor(...,serial) aplica banderas TU_DEBUG distintas para un juego
 *       concreto desde la propia app. Funciona con CUALQUIER driver Turnip ya
 *       importado, sin recompilar: es lo que el usuario puede usar hoy.
 *    b) buildDriconfXml/exportDriconf genera 00-cenit.conf, que el workflow
 *       build-turnip.yml inyecta en la fuente de Mesa para hornear opciones driconf
 *       dentro del driver. Requiere un rebuild del driver, y a cambio alcanza las
 *       opciones de correccion que solo existen ahi (vk_dont_care_as_load, LRZ,
 *       border color D24S8, etc.).
 *    Cenit declara el serial como applicationName de la instancia Vulkan
 *    (GSDeviceVK.cpp), y esa es exactamente la clave que el driconf empareja.
 */
final class TurnipTuning {

    private TurnipTuning() {}

    static final String PREFS = "app_prefs";
    /** -1 = seguir la gama del telefono (por defecto); 0..N = indice de PROFILES. */
    static final String KEY_PROFILE = "turnip_profile";
    /** Cache de shaders apagado por el usuario (para A/B contra el perfil). */
    static final String KEY_CACHE_OFF = "turnip_cache_off";

    /** Un perfil = una lista de banderas TU_DEBUG. Vacio = driver tal cual viene. */
    static final class Profile {
        final String id;
        final String label;
        final String flags;
        final String note;

        Profile(String id, String label, String flags, String note) {
            this.id = id;
            this.label = label;
            this.flags = flags;
            this.note = note;
        }
    }

    /**
     * Banderas usadas, con lo que hace cada una en Turnip (confirmado en
     * src/freedreno/vulkan/tu_util.cc de Mesa 25.3.6):
     *  nocb                    -> desactiva el concurrent binning
     *  noconcurrentresolves    -> desactiva los resolves concurrentes
     *  noubwc                  -> desactiva la compresion UBWC (menos banda, mas VRAM)
     *  nobinmerging            -> no fusiona bins (usa FDM; solo con fdm)
     *  forcebin                -> fuerza binning por hardware
     *  fdm                     -> fuerza el fragment density map
     *  perfc                   -> expone VK_KHR_performance_query (medicion en el HUD)
     */
    static final Profile[] PROFILES = {
            new Profile("stock",  "Sin cambio (como viene el driver)", "",
                    "Referencia para A/B."),
            new Profile("low",    "Gama baja: sin pases concurrentes",
                    "nocb,noconcurrentresolves",
                    "Libera ancho de banda para el render; suele ayudar en Adreno 6xx de gama baja."),
            new Profile("lowbw",  "Gama baja extrema: sin compresion + sin CB",
                    "nocb,noconcurrentresolves,noubwc",
                    "Para 3-4 GB de RAM; cambia compresion por estabilidad de memoria."),
            new Profile("mid",    "Gama media: sin CB",
                    "nocb",
                    "Un solo cambio: el binning concurrente es el que mas pasa extra cobra."),
            new Profile("high",   "Gama alta: binning y FDM forzados",
                    "forcebin,fdm",
                    "En Adreno 7xx+ el binning por hardware rinde; FDM recorta fragmentos."),
            new Profile("measure","Medicion: expone contadores de GPU",
                    "perfc",
                    "Solo para sacar numeros con el HUD; no es un perfil de juego."),
    };

    static int defaultProfileIndex(int tier) {
        // Gama baja -> perfil "low"; media/alta y desconocido -> "mid" (un solo cambio,
        // el mas seguro). El usuario puede subir o bajar desde el dialogo.
        return tier == 0 ? 1 : 3;
    }

    /** Perfil efectivo: el elegido a mano, o el que dicta la gama del telefono. */
    static Profile effectiveProfile(Context ctx, int tier) {
        final int picked = prefs(ctx).getInt(KEY_PROFILE, -1);
        if (picked >= 0 && picked < PROFILES.length)
            return PROFILES[picked];
        return PROFILES[defaultProfileIndex(tier)];
    }

    /** true = el usuario fijo un perfil; false = va detras de la gama. */
    static boolean isManualProfile(Context ctx) {
        final int picked = prefs(ctx).getInt(KEY_PROFILE, -1);
        return picked >= 0 && picked < PROFILES.length;
    }

    static void setProfile(Context ctx, int index) {
        prefs(ctx).edit().putInt(KEY_PROFILE, index).apply();
    }

    /** Labels para el Spinner; el perfil activo por gama se marca en el texto. */
    static String[] profileLabels(Context ctx, int tier) {
        final int autoIdx = defaultProfileIndex(tier);
        String[] out = new String[PROFILES.length];
        for (int i = 0; i < PROFILES.length; i++) {
            out[i] = PROFILES[i].label + (i == autoIdx ? "  (auto para este telefono)" : "");
        }
        return out;
    }

    /**
     * Ruta del cache de shaders, o null si debe quedar apagado. Mesa crea el ultimo
     * segmento (mesa_shader_cache) solo, y con MESA_SHADER_CACHE_DISABLE=false queda
     * habilitado. Se escribe dentro del driver dir (app-private, siempre escribible).
     */
    static String shaderCacheDir(Context ctx, CustomDriverManager.InstalledDriver driver) {
        if (driver == null) return null;
        if (prefs(ctx).getBoolean(KEY_CACHE_OFF, false)) return null;
        File dir = new File(driver.redirectDir(), "shaders");
        if (!dir.exists() && !dir.mkdirs()) return null;
        return dir.getAbsolutePath();
    }

    /** Bandera TU_DEBUG a inyectar, o null para no tocar el entorno del driver. */
    static String tuDebugFor(Context ctx, int tier) {
        return tuDebugFor(ctx, tier, null);
    }

    /**
     * Banderas para el juego en curso. Si ese serial tiene una regla con banderas
     * propias, mandan ellas: el perfil por gama es la base, y el juego manda sobre la
     * base (un juego puede necesitar lo contrario de lo que le va bien a la mayoria).
     */
    static String tuDebugFor(Context ctx, int tier, String serial) {
        final String ruleFlags = ruleFlagsFor(ctx, serial);
        if (ruleFlags != null) return ruleFlags;
        final Profile p = effectiveProfile(ctx, tier);
        return p.flags.isEmpty() ? null : p.flags;
    }

    /** Banderas propias de la regla de este juego, o null si no tiene. */
    static String ruleFlagsFor(Context ctx, String serial) {
        if (serial == null || serial.isEmpty()) return null;
        for (GameRule r : loadRules(ctx)) {
            if (!r.enabled || !r.serial.equals(serial)) continue;
            if (r.flags == null || r.flags.isEmpty()) continue;
            return r.flags;
        }
        return null;
    }

    /**
     * Etiqueta de LO QUE REALMENTE se va a aplicar, para atribuir las medidas de FPS.
     * Si se etiquetara por el nombre del perfil, una regla por juego o el cache apagado
     * quedarían medidos como si fueran el perfil limpio y la comparacion mentiria.
     */
    static String appliedBucket(Context ctx, int tier, String serial, boolean suspended, boolean cacheOff) {
        if (suspended) return DriverStats.PROFILE_NO_TUNING;
        final String ruleFlags = ruleFlagsFor(ctx, serial);
        final String base = ruleFlags != null ? "juego:" + ruleFlags : effectiveProfile(ctx, tier).id;
        return cacheOff ? base : base + "+cache";
    }

    static String describe(Context ctx, int tier) {
        final Profile p = effectiveProfile(ctx, tier);
        return p.label + (isManualProfile(ctx) ? "" : " [por gama]")
                + (p.flags.isEmpty() ? "" : " — TU_DEBUG=" + p.flags);
    }

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    // ---- Nivel 3: reglas por juego (driconf) ---------------------------------

    /**
     * Regla por juego entendida por el driconf de Turnip. En Android Mesa compila la
     * config estatica (src/util/00-mesa-defaults.conf + lo que el workflow añada), y
     * empareja por application_name_match contra el applicationName que Cenit declara
     * (el serial). Los valores posibles son los nombres de las opciones driconf de
     * Turnip en tu_device.cc: vk_dont_care_as_load, disable_conservative_lrz,
     * tu_dont_reserve_descriptor_set, tu_allow_oob_indirect_ubo_loads,
     * tu_disable_d24s8_border_color_workaround, tu_use_tex_coord_round_nearest_even_mode,
     * tu_ignore_frag_depth_direction.
     */
    static final class GameRule {
        final String serial;
        final List<String[]> options; // {nombre, "true"/"false"}
        /** Banderas TU_DEBUG para este juego (camino a, se aplica desde la app). */
        String flags = "";
        boolean enabled = true;
        String note = "";

        GameRule(String serial, List<String[]> options) {
            this.serial = serial;
            this.options = options;
        }
    }

    /** Banderas TU_DEBUG que pueden pedir por juego. Son subconjunto de las de
     *  Mesa 25.3.6 (tu_util.cc); las que cambian de verdad el camino de render. */
    static final String[] TU_FLAG_NAMES = {
            "nocb", "forcecb", "noconcurrentresolves", "noubwc", "nobin",
            "forcebin", "fdm", "nofdm", "sysmem", "gmem", "nolrz", "nolrzfc",
    };

    static String normalizeFlagList(String raw) {
        if (raw == null) return "";
        StringBuilder out = new StringBuilder();
        for (String tok : raw.split("[,\s]+")) {
            final String f = tok.trim().toLowerCase(java.util.Locale.ROOT);
            if (f.isEmpty()) continue;
            boolean known = false;
            for (String k : TU_FLAG_NAMES)
                if (k.equals(f)) { known = true; break; }
            if (!known) continue;
            boolean dup = false;
            for (String already : out.toString().split(","))
                if (already.equals(f)) { dup = true; break; }
            if (dup) continue;
            if (out.length() > 0) out.append(',');
            out.append(f);
        }
        return out.toString();
    }

    static final String[] DRICONF_OPTION_NAMES = {
            "vk_dont_care_as_load",
            "disable_conservative_lrz",
            "tu_dont_reserve_descriptor_set",
            "tu_allow_oob_indirect_ubo_loads",
            "tu_disable_d24s8_border_color_workaround",
            "tu_use_tex_coord_round_nearest_even_mode",
            "tu_ignore_frag_depth_direction",
    };

    private static final String RULES_KEY = "turnip_rules";

    /** Reglas guardadas, en orden de creacion. */
    static List<GameRule> loadRules(Context ctx) {
        List<GameRule> out = new ArrayList<>();
        final String raw = prefs(ctx).getString(RULES_KEY, "");
        if (raw == null || raw.isEmpty()) return out;
        // Formato: serial|opt=val,opt=val|1|nota|flags ;; serial|...|0|...
        for (String chunk : raw.split(";;")) {
            final String[] parts = chunk.split("\\|", 5);
            if (parts.length < 3) continue;
            final String serial = parts[0].trim();
            if (serial.isEmpty()) continue;
            final List<String[]> opts = new ArrayList<>();
            for (String kv : parts[1].split(",")) {
                final int eq = kv.indexOf('=');
                if (eq <= 0) continue;
                final String name = kv.substring(0, eq).trim();
                final String val = kv.substring(eq + 1).trim();
                if (!isKnownOption(name)) continue;
                if (!"true".equals(val) && !"false".equals(val)) continue;
                opts.add(new String[]{name, val});
            }
            final GameRule r = new GameRule(serial, opts);
            r.enabled = "1".equals(parts[2].trim());
            if (parts.length > 3) r.note = parts[3].trim();
            if (parts.length > 4) r.flags = normalizeFlagList(parts[4]);
            // Una regla puede ser solo de banderas (se aplica desde la app, vale con el
            // driver ya importado) o solo de driconf; vacia de las dos no significa nada.
            if (opts.isEmpty() && r.flags.isEmpty()) continue;
            out.add(r);
        }
        return out;
    }

    private static boolean isKnownOption(String name) {
        for (String known : DRICONF_OPTION_NAMES)
            if (known.equals(name)) return true;
        return false;
    }

    private static void saveRules(Context ctx, List<GameRule> rules) {
        StringBuilder sb = new StringBuilder();
        for (GameRule r : rules) {
            StringBuilder opts = new StringBuilder();
            for (String[] kv : r.options) {
                if (opts.length() > 0) opts.append(',');
                opts.append(kv[0]).append('=').append(kv[1]);
            }
            if (sb.length() > 0) sb.append(";;");
            sb.append(r.serial).append('|').append(opts).append('|')
              .append(r.enabled ? "1" : "0").append('|')
              .append(r.note == null ? "" : r.note.replace("|", " ").replace(";", ",")).append('|')
              .append(r.flags == null ? "" : r.flags);
        }
        prefs(ctx).edit().putString(RULES_KEY, sb.toString()).apply();
    }

    /** Anade (o reemplaza por serial) una regla de driconf. Devuelve el total. */
    static int putRule(Context ctx, String serial, List<String[]> options, boolean enabled, String note) {
        return putRule(ctx, serial, options, "", enabled, note);
    }

    /** Regla completa: opciones driconf (horneadas en el driver) + banderas TU_DEBUG
     *  (aplicadas desde la app, valen con cualquier driver ya importado). */
    static int putRule(Context ctx, String serial, List<String[]> options, String flags,
                       boolean enabled, String note) {
        final String norm = GameSerialUtils.normalizeLibrarySerial(serial);
        final String cleanFlags = normalizeFlagList(flags);
        if (norm.isEmpty() || (options.isEmpty() && cleanFlags.isEmpty())) return loadRules(ctx).size();
        final List<GameRule> rules = loadRules(ctx);
        rules.removeIf(r -> r.serial.equals(norm));
        final GameRule r = new GameRule(norm, new ArrayList<>(options));
        r.flags = cleanFlags;
        r.enabled = enabled;
        r.note = note == null ? "" : note;
        rules.add(r);
        saveRules(ctx, rules);
        return rules.size();
    }

    static boolean setRuleEnabled(Context ctx, String serial, boolean enabled) {
        final List<GameRule> rules = loadRules(ctx);
        boolean hit = false;
        for (GameRule r : rules)
            if (r.serial.equals(serial)) { r.enabled = enabled; hit = true; }
        if (hit) saveRules(ctx, rules);
        return hit;
    }

    static boolean deleteRule(Context ctx, String serial) {
        final List<GameRule> rules = loadRules(ctx);
        final boolean removed = rules.removeIf(r -> r.serial.equals(serial));
        if (removed) saveRules(ctx, rules);
        return removed;
    }

    /**
     * Serial efectiva que Cenit declarara a la instancia Vulkan: la del disco leida
     * por el core si hay, y si no, la que la biblioteca ya tiene resuelta por URI.
     */
    static String serialForGame(Context ctx, String gameUri) {
        // 1) La serial que la biblioteca ya resolucion y guardo (TitleResolver).
        //    Es la ruta barata: leer el disco otra vez aqui, justo antes de arrancar
        //    la VM, cuesta un acceso al ISO en el momento menos oportuno.
        String serial = "";
        if (gameUri != null) {
            try {
                final String saved = ctx.getApplicationContext()
                        .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                        .getString("serial:" + gameUri, null);
                serial = GameSerialUtils.normalizeLibrarySerial(saved);
            } catch (Throwable ignored) {
            }
        }
        // 2) Pista en el nombre del archivo (SLUS_207.80 + SLUS-20780 + ...).
        if (serial.isEmpty())
            serial = GameSerialUtils.serialFromUri(gameUri);
        // 3) VM ya corriendo: el core la tiene en memoria, es gratis.
        if (serial.isEmpty()) {
            try {
                if (NativeApp.isVMActive())
                    serial = GameSerialUtils.normalizeLibrarySerial(NativeApp.getCurrentGameSerial());
            } catch (Throwable ignored) {
            }
        }
        return serial;
    }

    /** Nombre que Cenit declara como applicationName de la instancia Vulkan. */
    static String appNameForGame(Context ctx, String gameUri) {
        final String serial = serialForGame(ctx, gameUri);
        return serial.isEmpty() ? null : serial;
    }

    /**
     * Genera el driconf estatico que el workflow de Mesa compila dentro del driver.
     * Cabecera identica al formato de 00-mesa-defaults.conf para que driconf_static.py
     * lo entienda; cada regla se empareja por application_name_match con el serial
     * (regex anclada, porque el campo se evalua como expresion regular).
     */
    static String buildDriconfXml(List<GameRule> rules) {
        final StringBuilder sb = new StringBuilder();
        sb.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        sb.append("<!-- Generado por Cenit: reglas por juego para Turnip. No editar a mano. -->\n");
        sb.append("<!-- Las reglas se reescriben desde Ajustes -> Controlador grafico personalizado. -->\n");
        sb.append("<driconf>\n");
        sb.append("  <device driver=\"turnip\">\n");
        boolean any = false;
        for (GameRule r : rules) {
            if (!r.enabled) continue;
            // Una regla solo con banderas TU_DEBUG no va al driconf: no tiene opciones.
            if (r.options.isEmpty()) continue;
            any = true;
            sb.append("    <application name=\"").append(xml(r.serial))
              .append("\" application_name_match=\"^").append(xml(r.serial)).append("$\">\n");
            for (String[] kv : r.options) {
                sb.append("      <option name=\"").append(xml(kv[0]))
                  .append("\" value=\"").append(xml(kv[1])).append("\" />\n");
            }
            if (r.note != null && !r.note.isEmpty())
                sb.append("      <!-- ").append(xml(r.note)).append(" -->\n");
            sb.append("    </application>\n");
        }
        if (!any) {
            sb.append("    <!-- sin reglas activas -->\n");
        }
        sb.append("  </device>\n");
        sb.append("</driconf>\n");
        return sb.toString();
    }

    private static String xml(String s) {
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;");
    }

    /** El driconf generado se deja junto al driver para que el usuario lo suba al repo. */
    static File driconfFile(Context ctx) {
        return new File(new File(ctx.getFilesDir(), "drivers"), "00-cenit.conf");
    }

    /** Escribe el driconf actual. Devuelve la ruta o null si no hay nada que escribir. */
    static String exportDriconf(Context ctx) {
        final List<GameRule> rules = loadRules(ctx);
        final String xml = buildDriconfXml(rules);
        File f = driconfFile(ctx);
        f.getParentFile().mkdirs();
        try (java.io.FileOutputStream fos = new java.io.FileOutputStream(f)) {
            fos.write(xml.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (Exception e) {
            android.util.Log.w("TurnipTuning", "exportDriconf failed", e);
            return null;
        }
        return f.getAbsolutePath();
    }

    /** Numero de reglas activas (para mostrar en el dialogo). */
    static int activeRuleCount(Context ctx) {
        int n = 0;
        for (GameRule r : loadRules(ctx))
            if (r.enabled) n++;
        return n;
    }

    static Map<String, String> rulesAsText(Context ctx) {
        Map<String, String> out = new LinkedHashMap<>();
        for (GameRule r : loadRules(ctx)) {
            StringBuilder sb = new StringBuilder();
            for (String[] kv : r.options) {
                if (sb.length() > 0) sb.append(", ");
                sb.append(kv[0]).append('=').append(kv[1]);
            }
            if (r.flags != null && !r.flags.isEmpty()) {
                if (sb.length() > 0) sb.append(" · ");
                sb.append("TU_DEBUG=").append(r.flags);
            }
            out.put(r.serial, (r.enabled ? "" : "apagada: ") + sb);
        }
        return out;
    }
}
