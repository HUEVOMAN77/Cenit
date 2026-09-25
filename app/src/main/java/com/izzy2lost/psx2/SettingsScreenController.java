package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import com.google.android.material.button.MaterialButtonToggleGroup;
import com.google.android.material.chip.Chip;
import com.google.android.material.chip.ChipGroup;
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.slider.Slider;

/**
 * Pantalla completa de Ajustes de Cenit.
 *
 * Nace del cajón lateral, que con el tiempo acumuló tantas opciones que dejó de
 * parecer un cajón: había que scrollear un panel angosto para llegar a la mitad.
 * Acá el mismo estado vive en una pantalla de verdad: cabecera fija, chips que
 * saltan a la sección y tarjetas anchas por categoría.
 *
 * No guarda nada por su cuenta: lee y escribe los mismos "app_prefs" que el cajón
 * y el diálogo por juego, y empuja cada cambio al núcleo en caliente con los
 * setters de NativeApp. Así el cajón rápido del juego y esta pantalla siguen
 * siendo dos vistas de un mismo estado, nunca dos configuraciones distintas.
 */
public final class SettingsScreenController {

    /** Escalas disponibles, de 1x a 8x. El texto vive en scale_entries_cenit. */
    static final float[] SCALE_VALUES = {1f, 1.25f, 1.5f, 2f, 2.5f, 3f, 4f, 5f, 6f, 8f};

    public interface Host {
        void onBack();
        void onOpenCustomDriver();
        void onOpenSaves();
        void onOpenMemoryCards();
        void onOpenAchievements();
        void onOpenAbout();
        /** Copia el registro de emulación a una URI compartible y abre el selector. */
        void onShareLogs();
        void onOpenControllerTest();
        void onOpenSetupWizard();
        void onOpenGamesFolders();
        void onPickDataFolder();
        void onImportBios();
        void onDownloadCovers();
        void onRebootGame();
        void onPowerOff();
        void onOrientationRequested(int mode);
        void onBootBiosToggled(boolean enabled);
        boolean isGameRunning();
        /** URI del juego en curso ("" = ninguno). Para los ajustes por juego. */
        String runningGamePath();
    }

    private final Context context;
    private final Host host;
    private final SharedPreferences prefs;
    private final View root;
    @Nullable private androidx.core.widget.NestedScrollView scroll;
    @Nullable private ChipGroup chips;
    private boolean bound;
    // Última posición del spinner de medio píxel que se mostró (o -1 si no hay
    // juego delante). Evita que setSelection dispare la escritura por reflejo.
    private int halfPixelShown = -1;
    // Lo mismo para cuotas de EE (0.6.5): adoptar la primera posición sin escribir.
    private int cycleSkipShown = -1;
    // 0.6.12: y para el Ciclo EE, que pasó de global puro a por-juego con
    // fallback global (sin juego delante sigue escribiendo la preferencia de siempre).
    private int eeCycleShown = -1;
    // Y para los dos spinners de 0.6.6, que no son por-juego pero tampoco deben
    // escribirse solos al inflar la vista (el primer disparo es siempre posición 0).
    private int frameQueueShown = -1;
    private int preloadShown = -1;

    public SettingsScreenController(@NonNull Context context, @NonNull ViewGroup parent,
                                    @NonNull Host host) {
        this.context = context;
        this.host = host;
        this.prefs = context.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.root = LayoutInflater.from(context).inflate(R.layout.view_settings_screen, parent, false);

        if (parent instanceof androidx.constraintlayout.widget.ConstraintLayout cl) {
            androidx.constraintlayout.widget.ConstraintLayout.LayoutParams lp =
                    new androidx.constraintlayout.widget.ConstraintLayout.LayoutParams(0, 0);
            lp.topToTop = cl.getId();
            lp.bottomToBottom = cl.getId();
            lp.startToStart = cl.getId();
            lp.endToEnd = cl.getId();
            parent.addView(root, lp);
        } else {
            parent.addView(root, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }

        ViewCompat.setOnApplyWindowInsetsListener(root, (v, insets) -> {
            Insets bars = insets.getInsets(WindowInsetsCompat.Type.systemBars()
                    | WindowInsetsCompat.Type.displayCutout());
            View header = root.findViewById(R.id.settings_header);
            if (header != null) {
                header.setPadding(header.getPaddingLeft(), bars.top + dp(10),
                        header.getPaddingRight(), header.getPaddingBottom());
            }
            v.setPadding(bars.left, v.getPaddingTop(), bars.right, bars.bottom);
            return insets;
        });
    }

    public View getRoot() {
        return root;
    }

    // ------------------------------------------------------------------
    // Ciclo de vida
    // ------------------------------------------------------------------

    /** Muestra la pantalla con una entrada corta: sube desde abajo y aparece. */
    public void show() {
        bindOnce();
        refresh();
        root.setVisibility(View.VISIBLE);
        root.setAlpha(0f);
        root.setTranslationY(dp(26));
        root.animate().alpha(1f).translationY(0f)
                .setDuration(260)
                .setInterpolator(new android.view.animation.PathInterpolator(0.16f, 1f, 0.3f, 1f))
                .start();
    }

    public void hide() {
        root.animate().cancel();
        root.setVisibility(View.GONE);
    }

    public boolean isShowing() {
        return root.getVisibility() == View.VISIBLE;
    }

    /** Al volver a una partida, el estado del VM cambió: hay que volver a leerlo. */
    public void refresh() {
        if (!bound) return;
        readValuesIntoUi();
        View reboot = root.findViewById(R.id.settings_btn_reboot);
        View power = root.findViewById(R.id.settings_btn_power);
        boolean running = host.isGameRunning();
        if (reboot != null) reboot.setVisibility(running ? View.VISIBLE : View.GONE);
        if (power != null) power.setVisibility(running ? View.VISIBLE : View.GONE);
    }

    // ------------------------------------------------------------------
    // Enlazado
    // ------------------------------------------------------------------

    private void bindOnce() {
        if (bound) return;
        bound = true;
        scroll = root.findViewById(R.id.settings_scroll);
        chips = root.findViewById(R.id.settings_chips);

        View back = root.findViewById(R.id.btn_settings_back);
        if (back != null) back.setOnClickListener(v -> host.onBack());
        if (rebootBtn() != null) rebootBtn().setOnClickListener(v -> host.onRebootGame());
        if (powerBtn() != null) powerBtn().setOnClickListener(v -> host.onPowerOff());

        bindChips();
        bindGraphics();
        bindImage();
        bindTextures();
        bindAudio();
        bindPatches();
        bindController();
        bindSystem();
    }

    @Nullable
    private com.google.android.material.button.MaterialButton rebootBtn() {
        return root.findViewById(R.id.settings_btn_reboot);
    }

    @Nullable
    private com.google.android.material.button.MaterialButton powerBtn() {
        return root.findViewById(R.id.settings_btn_power);
    }

    /** Cada chip acerca la vista a su tarjeta; el chip activo sigue al scroll. */
    private void bindChips() {
        if (chips == null || scroll == null) return;
        final int[][] pairs = {
                {R.id.chip_graphics, R.id.sec_graphics},
                {R.id.chip_image, R.id.sec_image},
                {R.id.chip_textures, R.id.sec_textures},
                {R.id.chip_audio, R.id.sec_audio},
                {R.id.chip_patches, R.id.sec_patches},
                {R.id.chip_controller, R.id.sec_controller},
                {R.id.chip_system, R.id.sec_system},
        };
        for (int[] pair : pairs) {
            Chip chip = root.findViewById(pair[0]);
            View section = root.findViewById(pair[1]);
            if (chip == null || section == null) continue;
            chip.setOnClickListener(v -> {
                chip.setChecked(true);
                scroll.smoothScrollTo(0, section.getTop() - dp(12));
            });
        }
    }

    private void bindGraphics() {
        MaterialButtonToggleGroup renderer = root.findViewById(R.id.set_tg_renderer);
        View at = root.findViewById(R.id.set_tb_at);
        View vk = root.findViewById(R.id.set_tb_vk);
        View gl = root.findViewById(R.id.set_tb_gl);
        View sw = root.findViewById(R.id.set_tb_sw);
        if (renderer != null) {
            renderer.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
                if (!isChecked) return;
                int value = checkedId == (vk != null ? vk.getId() : -2) ? 14
                        : checkedId == (gl != null ? gl.getId() : -2) ? 12
                        : checkedId == (sw != null ? sw.getId() : -2) ? 13 : -1;
                if (value == prefs.getInt("renderer", -1)) return;
                prefs.edit().putInt("renderer", value).apply();
                NativeApp.renderGpuAsync(value);
            });
        }

        View driver = root.findViewById(R.id.set_btn_custom_driver);
        if (driver != null) driver.setOnClickListener(v -> host.onOpenCustomDriver());

        spinner(R.id.set_sp_scale, R.array.scale_entries_cenit, position -> {
            if (position < 0 || position >= SCALE_VALUES.length) return;
            float value = SCALE_VALUES[position];
            if (Math.abs(prefs.getFloat("upscale_multiplier", 1f) - value) < 0.001f) return;
            prefs.edit().putFloat("upscale_multiplier", value).apply();
            NativeApp.renderUpscalemultiplierAsync(value);
        });

        // Resolución dinámica: el regidor lee la preferencia en cada tick y solo
        // necesita que le devuelvan el techo cuando se apaga con el juego corriendo.
        toggle(R.id.set_sw_dynres, "dynamic_res", true, null);

        // 0.6.5 (invento Cenit): memoria por juego y turbo de cargas. El regidor
        // lee ambas preferencias en cada start(); no hay que notificarle nada.
        toggle(R.id.set_sw_adaptive, "adaptive_perf", true, null);
        toggle(R.id.set_sw_autoturbo, "auto_turbo", false, null);

        // 0.6.6 (bloques 1-3): pinning, cola de cuadros y precarga. El pinning
        // vive en EmuCore del INI base, que en este port es memoria rellena por
        // ApplyHardwarePerformanceProfile en cada arranque — por eso Java debe
        // re-escribir la voluntad del usuario SIEMPRE (applySavedSettings), no
        // solo cuando se toca aquí.
        toggle(R.id.set_sw_pinning, "thread_pinning", true,
                checked -> NativeApp.setThreadPinningAsync(checked));

        // Cenit 0.6.7: los dos speedhacks que el perfil aplicaba en silencio.
        // Fast CDVD cambia de "forzado encendido" a "apagado por defecto con
        // interruptor", porque era la causa más probable del cierre de Shadow of
        // the Colossus al arrancar. MTVU conserva el default por hardware, pero
        // ahora se puede apagar sin recompilar. Igual que el pinning: la
        // preferencia se re-aplica en cada arranque (MainActivity).
        toggle(R.id.set_sw_fastcdvd, "fast_cdvd", false,
                checked -> NativeApp.setFastCDVDAsync(checked));
        toggle(R.id.set_sw_mtvu, "mtvu", NativeApp.defaultMTVU(),
                checked -> NativeApp.setMTVUAsync(checked));

        // Cenit 0.6.15 (Fase 1): sonda de trazas VU. Es medición pura, pero
        // NO es gratis: encenderla recompila todo instrumentado y pausa la
        // caché de programas VU en disco. Por eso el default es apagado y no
        // la toca ningún perfil; el informe se vuelca solo al apagarla o al
        // parar el juego (logs/vu_probe.txt + resumen en el registro).
        toggle(R.id.set_sw_vu_probe, "vu_trace_probe", false,
                checked -> NativeApp.setVUTraceProbeAsync(checked));

        // La posición del spinner ES el valor de la clave (ver arrays.xml). Como
        // con medio píxel y cuotas: el primer disparo del adaptador se ADOPTA sin
        // escribir, para no guardar "Óptima" en el teléfono de todo el mundo.
        spinner(R.id.set_sp_framequeue, R.array.frame_queue_entries, position -> {
            if (frameQueueShown < 0) { frameQueueShown = position; return; }
            if (position == frameQueueShown) return;
            prefs.edit().putInt("frame_queue", position).apply();
            NativeApp.setFrameLatencyQueueAsync(position);
            frameQueueShown = position;
        });

        spinner(R.id.set_sp_preload, R.array.preload_entries, position -> {
            if (preloadShown < 0) { preloadShown = position; return; }
            if (position == preloadShown) return;
            prefs.edit().putInt("texture_preload", position).apply();
            NativeApp.setTexturePreloadingAsync(position);
            preloadShown = position;
        });

        // Mantener pulsado el interruptor de memoria borra lo aprendido del juego
        // actual — el usuario no tiene por qué abrir adb para empezar de cero.
        View adaptiveSw = root.findViewById(R.id.set_sw_adaptive);
        if (adaptiveSw != null) {
            adaptiveSw.setOnLongClickListener(v -> {
                final String uri = host.runningGamePath();
                if (uri == null || uri.isEmpty()) return false;
                AdaptiveProfile.forget(context, uri);
                android.widget.Toast.makeText(context, "Memoria borrada para este juego",
                        android.widget.Toast.LENGTH_SHORT).show();
                readValuesIntoUi();
                return true;
            });
        }

        spinner(R.id.set_sp_ee_cycle, R.array.ee_cycle_entries, position -> {
            // La posición 3 es el 100% (normal); EECycleRate = posición - 3.
            int rate = position - 3;
            if (rate < -3) rate = -3;
            if (rate > 3) rate = 3;
            // Primera pasada tras inflar: ADOPTAR, no escribir (mismo guard que
            // medio píxel y cuotas; aquí el centinela no puede ser -1 porque el
            // valor -1 ES válido, por eso se compara contra eeCycleShown).
            if (eeCycleShown < 0) { eeCycleShown = position; return; }
            if (position == eeCycleShown) return;
            final String uri = host.runningGamePath();
            if (uri != null && !uri.isEmpty()) {
                // 0.6.12: con juego delante, el Ciclo EE se guarda SOLO para ese
                // juego (capa por-juego, misma ruta probada del modo cuotas:
                // escribe el INI y recarga la capa en caliente). Así SOTC puede
                // arrancar siempre en -1 sin que el resto de la librería se vea
                // afectada.
                if (NativeApp.safeSetGameSettingInt(uri, "EmuCore/Speedhacks", "EECycleRate", rate))
                    eeCycleShown = position;
                return;
            }
            if (rate == prefs.getInt("ee_cycle_rate", 0)) return;
            prefs.edit().putInt("ee_cycle_rate", rate).apply();
            NativeApp.setEECycleRateAsync(rate);
            eeCycleShown = position;
        });

        spinner(R.id.set_sp_blending, R.array.blending_accuracy_entries, position -> {
            if (position == prefs.getInt("blending_accuracy", 1)) return;
            prefs.edit().putInt("blending_accuracy", position).apply();
            NativeApp.setBlendingAccuracyAsync(position);
        });

        spinner(R.id.set_sp_filtering, R.array.texture_filtering_entries, position -> {
            if (position == prefs.getInt("texture_filtering", 2)) return;
            prefs.edit().putInt("texture_filtering", position).apply();
            NativeApp.setTextureFilteringAsync(position);
        });

        spinner(R.id.set_sp_mipmap, R.array.mipmap_entries, position -> {
            boolean enabled = position == 1;
            if (enabled == prefs.getBoolean("hw_mipmap", true)) return;
            prefs.edit().putBoolean("hw_mipmap", enabled).apply();
            NativeApp.setHWMipmapAsync(enabled);
        });

        spinner(R.id.set_sp_anisotropy, R.array.anisotropy_entries, position -> {
            int level = anisotropyValueFor(position);
            if (level == prefs.getInt("max_anisotropy", 0)) return;
            prefs.edit().putInt("max_anisotropy", level).apply();
            NativeApp.setMaxAnisotropyAsync(level);
        });

        // Desde 0.6.4 esto es POR JUEGO: el INI global perdía el valor por
        // MaskUserHacks (ver NativeApp.setGameUserHackInt). Solo editable con un
        // juego delante; el cambio impacta en caliente vía ReloadGameSettings.
        spinner(R.id.set_sp_halfpixel, R.array.half_pixel_entries, position -> {
            // El spinner dispara solo con poner el adaptador (posición 0) antes de
            // leer el estado real. Sin valor mostrado todavía, se ADOPTA sin
            // escribir: lo contrario pondría Apagado en el juego de nadie.
            if (halfPixelShown < 0) { halfPixelShown = position; return; }
            if (position == halfPixelShown) return;
            final String uri = host.runningGamePath();
            if (uri == null || uri.isEmpty()) return;
            if (NativeApp.safeSetGameUserHackInt(uri, NativeApp.HACK_HALF_PIXEL_OFFSET, position))
                halfPixelShown = position;
        });

        // Cuotas de EE (0.6.5): también por juego, pero en la sección de Speedhacks
        // del core — no es un UserHack del GS, así que la capa por-juego genérica
        // lo escribe sin sembrar MaskUserHacks. Misma guardia de adopción.
        spinner(R.id.set_sp_cycleskip, R.array.cycle_skip_entries, position -> {
            if (cycleSkipShown < 0) { cycleSkipShown = position; return; }
            if (position == cycleSkipShown) return;
            final String uri = host.runningGamePath();
            if (uri == null || uri.isEmpty()) return;
            if (NativeApp.safeSetGameSettingInt(uri, "EmuCore/Speedhacks", "EECycleSkip", position))
                cycleSkipShown = position;
        });

        spinner(R.id.set_sp_cas, R.array.cas_entries, position -> {
            if (position == prefs.getInt("cas_mode", 0)) return;
            prefs.edit().putInt("cas_mode", position).apply();
            NativeApp.setCASModeAsync(position, prefs.getInt("cas_sharpness", 50));
            updateCasVisibility(position);
        });

        Slider slider = root.findViewById(R.id.set_sl_cas_sharpness);
        if (slider != null) {
            slider.addOnChangeListener((s, value, fromUser) -> {
                int sharpness = Math.round(value);
                TextView label = root.findViewById(R.id.set_tv_cas_value);
                if (label != null) label.setText(sharpness + "%");
                if (!fromUser) return;
                if (sharpness == prefs.getInt("cas_sharpness", 50)) return;
                prefs.edit().putInt("cas_sharpness", sharpness).apply();
                NativeApp.setCASModeAsync(prefs.getInt("cas_mode", 0), sharpness);
            });
        }
    }

    /** Índice del desplegable -> multiplicador real guardado en el núcleo. */
    static int anisotropyValueFor(int position) {
        switch (position) {
            case 1: return 2;
            case 2: return 4;
            case 3: return 8;
            case 4: return 16;
            default: return 0; // 0 = automático
        }
    }

    static int anisotropyPositionFor(int value) {
        switch (value) {
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            case 16: return 4;
            default: return 0;
        }
    }

    /** La barra de cantidad solo tiene sentido con CAS encendido. */
    private void updateCasVisibility(int mode) {
        View row = root.findViewById(R.id.set_row_cas_sharpness);
        View slider = root.findViewById(R.id.set_sl_cas_sharpness);
        int vis = mode == 0 ? View.GONE : View.VISIBLE;
        if (row != null) row.setVisibility(vis);
        if (slider != null) slider.setVisibility(vis);
    }

    private void bindImage() {
        spinner(R.id.set_sp_aspect, R.array.aspect_ratio_entries, position -> {
            if (position == prefs.getInt("aspect_ratio", 1)) return;
            prefs.edit().putInt("aspect_ratio", position).apply();
            NativeApp.setAspectRatioAsync(position);
        });

        spinner(R.id.set_sp_edge_crop, R.array.edge_crop_entries, position -> {
            int pixels = edgeCropIndexToPixels(position);
            if (pixels == prefs.getInt("edge_crop", 0)) return;
            prefs.edit().putInt("edge_crop", pixels).apply();
            NativeApp.setEdgeCropAsync(pixels);
        });

        toggle(R.id.set_sw_vsync, "vsync_enabled", false,
                NativeApp::setVsyncEnabledAsync);
    }

    private void bindTextures() {
        toggle(R.id.set_sw_load_textures, "load_textures", false,
                NativeApp::setLoadTexturesAsync);
        toggle(R.id.set_sw_async_textures, "async_texture_loading", true,
                NativeApp::setAsyncTextureLoadingAsync);
        toggle(R.id.set_sw_precache_textures, "precache_textures", false,
                NativeApp::setPrecacheTextureReplacementsAsync);
    }

    private void bindAudio() {
        spinner(R.id.set_sp_audio_output, R.array.audio_output_entries, position -> {
            if (position == AudioOutputPreference.getMode(context)) return;
            AudioOutputPreference.setMode(context, position);
            AudioOutputPreference.apply(context);
        });
    }

    private void bindPatches() {
        toggle(R.id.set_sw_widescreen, "widescreen_patches", true,
                NativeApp::setWidescreenPatchesAsync);
        toggle(R.id.set_sw_no_interlacing, "no_interlacing_patches", true,
                NativeApp::setNoInterlacingPatchesAsync);
    }

    private void bindController() {
        View test = root.findViewById(R.id.set_btn_test_controller);
        if (test != null) test.setOnClickListener(v -> host.onOpenControllerTest());
    }

    private void bindSystem() {
        click(R.id.set_btn_games, host::onOpenGamesFolders);
        click(R.id.set_btn_data, host::onPickDataFolder);
        click(R.id.set_btn_download_covers, host::onDownloadCovers);
        click(R.id.set_btn_game_state, host::onOpenSaves);
        click(R.id.set_btn_memcard, host::onOpenMemoryCards);
        click(R.id.set_btn_bios, host::onImportBios);
        click(R.id.set_btn_setup, host::onOpenSetupWizard);
        click(R.id.set_btn_achievements, host::onOpenAchievements);
        click(R.id.set_btn_about, host::onOpenAbout);
        click(R.id.set_btn_send_log, host::onShareLogs);

        MaterialSwitch autoCovers = root.findViewById(R.id.set_sw_auto_covers);
        if (autoCovers != null) {
            autoCovers.setChecked(prefs.getBoolean("auto_covers", true));
            autoCovers.setOnCheckedChangeListener((b, checked) ->
                    prefs.edit().putBoolean("auto_covers", checked).apply());
        }

        // Arrancar el menú de PS2 necesita una decisión de la actividad (puede
        // encender el VM ahora mismo), así que no usa el toggle genérico.
        MaterialSwitch bootBios = root.findViewById(R.id.set_sw_boot_bios);
        if (bootBios != null) {
            bootBios.setOnCheckedChangeListener((b, checked) -> {
                if (checked == prefs.getBoolean(MainActivity.PREF_BOOT_BIOS_ON_START, false)) return;
                prefs.edit().putBoolean(MainActivity.PREF_BOOT_BIOS_ON_START, checked).apply();
                host.onBootBiosToggled(checked);
            });
        }
        toggle(R.id.set_sw_dev_hud, "hud_visible", false, NativeApp::setHudVisibleAsync);

        MaterialButtonToggleGroup orientation = root.findViewById(R.id.set_tg_orientation);
        if (orientation != null) {
            orientation.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
                if (!isChecked) return;
                View land = root.findViewById(R.id.set_tb_orient_land);
                View port = root.findViewById(R.id.set_tb_orient_port);
                host.onOrientationRequested(
                        checkedId == (land != null ? land.getId() : -2) ? 1
                                : checkedId == (port != null ? port.getId() : -2) ? 2 : 0);
            });
        }
    }

    private void click(int id, Runnable action) {
        View v = root.findViewById(id);
        if (v != null) v.setOnClickListener(x -> action.run());
    }

    private void toggle(int id, String key, boolean fallback,
                        @Nullable java.util.function.Consumer<Boolean> apply) {
        MaterialSwitch sw = root.findViewById(id);
        if (sw == null) return;
        sw.setOnCheckedChangeListener((b, checked) -> {
            if (checked == prefs.getBoolean(key, fallback)) return;
            prefs.edit().putBoolean(key, checked).apply();
            if (apply != null) apply.accept(checked);
        });
    }

    private interface OnPick { void onPick(int position); }

    private void spinner(int id, int arrayRes, OnPick onPick) {
        Spinner sp = root.findViewById(id);
        if (sp == null) return;
        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(context,
                arrayRes, android.R.layout.simple_spinner_item);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(adapter);
        sp.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                onPick.onPick(position);
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    // ------------------------------------------------------------------
    // Estado -> interfaz
    // ------------------------------------------------------------------

    private void readValuesIntoUi() {
        int renderer = prefs.getInt("renderer", -1);
        MaterialButtonToggleGroup tg = root.findViewById(R.id.set_tg_renderer);
        View target = tg == null ? null : root.findViewById(
                renderer == 14 ? R.id.set_tb_vk
                        : renderer == 12 ? R.id.set_tb_gl
                        : renderer == 13 ? R.id.set_tb_sw : R.id.set_tb_at);
        if (tg != null && target != null) tg.check(target.getId());

        setSpinner(R.id.set_sp_scale, scaleIndexFor(prefs.getFloat("upscale_multiplier", 1f)));
        check(R.id.set_sw_dynres, prefs.getBoolean("dynamic_res", true));
        check(R.id.set_sw_adaptive, prefs.getBoolean("adaptive_perf", true));
        check(R.id.set_sw_autoturbo, prefs.getBoolean("auto_turbo", false));
        check(R.id.set_sw_pinning, prefs.getBoolean("thread_pinning", true));
        check(R.id.set_sw_fastcdvd, prefs.getBoolean("fast_cdvd", false));
        check(R.id.set_sw_mtvu, prefs.getBoolean("mtvu", NativeApp.defaultMTVU()));
        check(R.id.set_sw_vu_probe, prefs.getBoolean("vu_trace_probe", false));
        setSpinner(R.id.set_sp_framequeue,
                prefs.getInt("frame_queue", NativeApp.defaultFrameLatencyQueue()));
        setSpinner(R.id.set_sp_preload,
                prefs.getInt("texture_preload", NativeApp.defaultTexturePreloading()));
        // Ciclo EE (0.6.12): con juego delante se lee SOLO su INI; si el juego no
        // tiene valor guardado, manda la preferencia global (que es lo que el
        // núcleo va a aplicar). seteeCycleShown ANTES de setSpinner, para que el
        // disparo reflejo del cambio de selección no escriba nada.
        final String cycUri = host.runningGamePath();
        final boolean cycAvailable = cycUri != null && !cycUri.isEmpty();
        final int cycGlobal = prefs.getInt("ee_cycle_rate", 0);
        final int cycShown = cycAvailable
                ? NativeApp.safeGetGameSettingInt(cycUri, "EmuCore/Speedhacks",
                        "EECycleRate", cycGlobal)
                : cycGlobal;
        eeCycleShown = Math.max(0, Math.min(6, cycShown + 3));
        setSpinner(R.id.set_sp_ee_cycle, eeCycleShown);
        TextView eeLabel = root.findViewById(R.id.set_tv_ee_cycle_label);
        if (eeLabel != null) eeLabel.setText(cycAvailable
                ? "Velocidad de la CPU emulada (por juego)"
                : "Velocidad de la CPU emulada");
        TextView eeNote = root.findViewById(R.id.set_tv_ee_cycle_note);
        if (eeNote != null) {
            if (cycAvailable) {
                String extra = "";
                if (cycShown != cycGlobal) {
                    final String[] cycEntries =
                            context.getResources().getStringArray(R.array.ee_cycle_entries);
                    final int gp = Math.max(0, Math.min(cycEntries.length - 1, cycGlobal + 3));
                    extra = " (El global está en " + cycEntries[gp] + ")";
                }
                eeNote.setText("Se guarda solo para el juego que está delante y se "
                        + "aplica al instante. Por debajo de 100% el juego va más lento "
                        + "pero clavado: es la salida para los que nunca llegan a tiempo "
                        + "(SOTC, God of War)." + extra);
            } else {
                eeNote.setText("Ajuste global (ningún juego abierto): los juegos que "
                        + "tengan su propio valor aquí lo ignoran. Abre uno para guardarlo "
                        + "solo para él.");
            }
        }
        setSpinner(R.id.set_sp_blending, prefs.getInt("blending_accuracy", 1));
        setSpinner(R.id.set_sp_filtering, prefs.getInt("texture_filtering", 2));
        setSpinner(R.id.set_sp_mipmap, prefs.getBoolean("hw_mipmap", true) ? 1 : 0);
        setSpinner(R.id.set_sp_anisotropy, anisotropyPositionFor(prefs.getInt("max_anisotropy", 0)));
        // Medio píxel: por juego. Con juego delante se lee su INI (el nativo cae
        // al GameDB y de ahí a Normal si nadie lo tocó); sin juego, bloqueado.
        final String hpUri = host.runningGamePath();
        final boolean hpAvailable = hpUri != null && !hpUri.isEmpty();
        halfPixelShown = hpAvailable
                ? NativeApp.safeGetGameUserHackInt(hpUri, NativeApp.HACK_HALF_PIXEL_OFFSET, 1)
                : 1;
        setSpinner(R.id.set_sp_halfpixel, halfPixelShown);
        Spinner hpSp = root.findViewById(R.id.set_sp_halfpixel);
        if (hpSp != null) hpSp.setEnabled(hpAvailable);
        TextView hpNote = root.findViewById(R.id.set_tv_halfpixel_note);
        if (hpNote != null) {
            hpNote.setText(hpAvailable
                    ? "Guardado solo para el juego que está delante, y se aplica al instante."
                    : "Ajuste por juego: abre un juego para cambiarlo.");
        }
        // Cuotas de EE: mismo patrón por-juego. La nota usa la EVIDENCIA real que
        // guardó el regidor (ticks en 1x yendo atrasado) en vez de adivinar.
        cycleSkipShown = hpAvailable
                ? Math.max(0, Math.min(3, NativeApp.safeGetGameSettingInt(
                        hpUri, "EmuCore/Speedhacks", "EECycleSkip", 0)))
                : 0;
        setSpinner(R.id.set_sp_cycleskip, cycleSkipShown);
        Spinner csSp = root.findViewById(R.id.set_sp_cycleskip);
        if (csSp != null) csSp.setEnabled(hpAvailable);
        TextView csNote = root.findViewById(R.id.set_tv_cycleskip_note);
        if (csNote != null) {
            if (!hpAvailable) {
                csNote.setText("Ajuste por juego: abre un juego para cambiarlo.");
            } else {
                final AdaptiveProfile prof = new AdaptiveProfile(context, hpUri);
                // ~16 ticks = en torno a 15 s clavado por debajo del 95% en 1x
                // (el regidor mide una vez por segundo).
                if (prof.slowFloorTicks >= 16) {
                    csNote.setText("Cenit notó este juego atrasado "
                            + (prof.slowFloorTicks / 16) + " s en 1x. Aquí es donde las "
                            + "cuotas pueden ayudar: prueba Suave con el HUD puesto.");
                } else {
                    csNote.setText("Solo ayuda en juegos que nunca llegan a tiempo (SOTC). "
                            + "En todo lo demás quita velocidad real — este juego no muestra "
                            + "ese patrón, así que lo normal es dejarlo en Normal.");
                }
            }
        }
        int casMode = prefs.getInt("cas_mode", 0);
        setSpinner(R.id.set_sp_cas, casMode);
        int sharpness = prefs.getInt("cas_sharpness", 50);
        Slider slider = root.findViewById(R.id.set_sl_cas_sharpness);
        if (slider != null) slider.setValue(Math.max(0f, Math.min(100f, sharpness)));
        TextView casLabel = root.findViewById(R.id.set_tv_cas_value);
        if (casLabel != null) casLabel.setText(sharpness + "%");
        updateCasVisibility(casMode);

        setSpinner(R.id.set_sp_aspect, prefs.getInt("aspect_ratio", 1));
        setSpinner(R.id.set_sp_edge_crop, edgeCropPixelsToIndex(prefs.getInt("edge_crop", 0)));
        setSpinner(R.id.set_sp_audio_output, AudioOutputPreference.getMode(context));

        check(R.id.set_sw_vsync, prefs.getBoolean("vsync_enabled", false));
        check(R.id.set_sw_load_textures, prefs.getBoolean("load_textures", false));
        check(R.id.set_sw_async_textures, prefs.getBoolean("async_texture_loading", true));
        check(R.id.set_sw_precache_textures, prefs.getBoolean("precache_textures", false));
        check(R.id.set_sw_widescreen, prefs.getBoolean("widescreen_patches", true));
        check(R.id.set_sw_no_interlacing, prefs.getBoolean("no_interlacing_patches", true));
        check(R.id.set_sw_auto_covers, prefs.getBoolean("auto_covers", true));
        check(R.id.set_sw_boot_bios, prefs.getBoolean(MainActivity.PREF_BOOT_BIOS_ON_START, false));
        check(R.id.set_sw_dev_hud, prefs.getBoolean("hud_visible", false));

        MaterialButtonToggleGroup orient = root.findViewById(R.id.set_tg_orientation);
        View orientTarget = orient == null ? null : root.findViewById(
                orientationPref() == 1 ? R.id.set_tb_orient_land
                        : orientationPref() == 2 ? R.id.set_tb_orient_port : R.id.set_tb_orient_auto);
        if (orient != null && orientTarget != null) orient.check(orientTarget.getId());

        TextView bios = root.findViewById(R.id.set_tv_bios_status);
        if (bios != null) {
            boolean ok = BiosVerifier.hasAnyVerifiedBios(context);
            bios.setText(ok ? "BIOS detectada y verificada" : "Necesita BIOS verificada");
            bios.setTextColor(0xFF39E6FF);
        }
    }

    private int orientationPref() {
        return prefs.getInt("orientation_lock", 0);
    }

    static int scaleIndexFor(float value) {
        int best = 0;
        float bestDelta = Float.MAX_VALUE;
        for (int i = 0; i < SCALE_VALUES.length; i++) {
            float delta = Math.abs(SCALE_VALUES[i] - value);
            if (delta < bestDelta) {
                bestDelta = delta;
                best = i;
            }
        }
        return best;
    }

    static int edgeCropIndexToPixels(int index) {
        switch (index) {
            case 1: return 4;
            case 2: return 8;
            case 3: return 12;
            case 4: return 16;
            default: return 0;
        }
    }

    static int edgeCropPixelsToIndex(int pixels) {
        if (pixels >= 16) return 4;
        if (pixels >= 12) return 3;
        if (pixels >= 8) return 2;
        if (pixels >= 4) return 1;
        return 0;
    }

    private void setSpinner(int id, int position) {
        Spinner sp = root.findViewById(id);
        if (sp == null || sp.getAdapter() == null) return;
        int safe = Math.max(0, Math.min(position, sp.getAdapter().getCount() - 1));
        sp.setSelection(safe, false);
    }

    private void check(int id, boolean value) {
        MaterialSwitch sw = root.findViewById(id);
        if (sw != null) sw.setChecked(value);
    }

    private int dp(int d) {
        return (int) (d * context.getResources().getDisplayMetrics().density + 0.5f);
    }
}
