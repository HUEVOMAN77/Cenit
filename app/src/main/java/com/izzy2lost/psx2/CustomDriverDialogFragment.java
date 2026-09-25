package com.izzy2lost.psx2;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.google.android.material.materialswitch.MaterialSwitch;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.DialogFragment;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;

/**
 * Custom Vulkan GPU driver manager. Lets the user import a driver .zip
 * (e.g. a Mesa Turnip build from github.com/K11MCH1/AdrenoToolsDrivers)
 * and pick the active one; storage/extraction lives in
 * {@link CustomDriverManager}.
 */
public class CustomDriverDialogFragment extends DialogFragment {

    private static final String PREFS = "app_prefs";
    private static final String PREF_CUSTOM_DRIVER_ID = "custom_driver_id";

    private ListView driverListView;
    private ArrayAdapter<String> driverAdapter;
    private List<CustomDriverManager.InstalledDriver> installedDrivers;
    private ActivityResultLauncher<Intent> importLauncher;
    private int selectedIndex = 0; // 0 = System Default
    private int deviceTier = -1;
    private Spinner profileSpinner;
    private TextView profileNote;
    private TextView statsText;
    private MaterialSwitch cacheSwitch;

    /** Reads the id of the driver the user last picked, or null for the
     *  system default. */
    public static String getSelectedDriverId(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(PREF_CUSTOM_DRIVER_ID, null);
    }

    /** Resolves the saved driver id against what's actually installed
     *  (a saved id can go stale if the driver was deleted outside this
     *  dialog) and pushes the result to native. Safe to call
     *  unconditionally before every VM start.
     *
     *  Cenit 0.6.8: con juego delante, la selección pasa por el guardarraya de
     *  CustomDriverManager. Si ese driver ya reventó el arranque de ESTE juego,
     *  se arranca con el del sistema en lugar de repetir el cierre, y se avisa. */
    public static void applyStoredSelection(Context context) {
        applyStoredSelection(context, null, null);
    }

    public static void applyStoredSelection(Context context, String gameKey, String gameLabel) {
        String id = getSelectedDriverId(context);
        CustomDriverManager.InstalledDriver selected = null;
        if (id != null) {
            for (CustomDriverManager.InstalledDriver d : CustomDriverManager.listInstalled(context)) {
                if (d.id.equals(id)) {
                    selected = d;
                    break;
                }
            }
        }
        if (selected != null) {
            // Cenit 0.6.11: el driver personalizado solo toca huesos cuando el
            // GS renderiza por Vulkan (es una ICD de Vulkan cargada vía
            // adrenotools). Con OpenGL o Software el driver ni se carga, y si
            // además el arranque falla por otra cosa el guardarraya culparía al
            // driver fantasma. Se avisa y no se abre intento.
            final int renderer = context.getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                    .getInt("renderer", -1); // -1 Auto (-> Vulkan aquí), 14 Vulkan
            if (renderer == 12 || renderer == 13) {
                CustomDriverManager.clearAttempt(context);
                // El aviso por juego (al arrancar) usa la notificación de una vez;
                // en el diálogo basta con el Toast inmediato, y dejar la
                // notificación pendiente engañaría al usuario si luego cambia a
                // Vulkan antes de entrar a un juego.
                if (gameKey != null) {
                    CustomDriverManager.setFallbackNotice(context,
                            "Cambiaste el driver «" + selected.name + "», pero el renderer está en "
                                    + (renderer == 12 ? "OpenGL" : "Software")
                                    + ": los drivers personalizados solo se aplican con Vulkan."
                                    + " Pon el renderer en Vulkan o Automático para usarlo.");
                }
                CustomDriverManager.applyToNative(context, selected, gameKey);
                return;
            }
        }
        if (selected != null && gameKey != null
                && CustomDriverManager.isKnownBadCombo(context, selected.id, gameKey)) {
            CustomDriverManager.setFallbackNotice(context,
                    "El driver «" + selected.name + "» cerró " + (gameLabel == null ? "este juego" : gameLabel)
                            + " al arrancar la última vez. Para este juego Cenit usó el driver del sistema."
                            + " Puedes forzarlo otra vez con el botón «Forzar otra vez el driver seleccionado»."
                            + " Si vuelve a cerrarse, envía el reporte: ahora queda la señal exacta del choque.");
            CustomDriverManager.clearAttempt(context);
            CustomDriverManager.applyToNative(context, null);
            return;
        }
        if (selected != null && gameKey != null)
            CustomDriverManager.beginAttempt(context, selected.id, gameKey,
                    CustomDriverManager.willApplyTuning(context, selected, gameKey));
        else
            CustomDriverManager.clearAttempt(context); // sin driver o sin juego: nada que vigilar
        // gameKey es la URI del juego: con ella el driver sabe QUE juego va a correr y
        // puede aplicar su perfil TU_DEBUG y sus reglas por-juego compiladas.
        CustomDriverManager.applyToNative(context, selected, gameKey);
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        importLauncher = registerForActivityResult(
                new ActivityResultContracts.StartActivityForResult(),
                result -> {
                    if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null
                            && result.getData().getData() != null) {
                        importDriver(result.getData().getData());
                    }
                }
        );
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(@Nullable Bundle savedInstanceState) {
        View view = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_custom_driver, null);

        driverListView = view.findViewById(R.id.custom_driver_list);
        MaterialButton btnImport = view.findViewById(R.id.btn_import_custom_driver);
        MaterialButton btnDelete = view.findViewById(R.id.btn_delete_custom_driver);

        loadDrivers();

        driverListView.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
        driverListView.setItemChecked(selectedIndex, true);
        driverListView.setOnItemClickListener((parent, v, position, id) -> {
            selectedIndex = position;
            Context ctx = requireContext();
            SharedPreferences.Editor editor = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
            if (position == 0) {
                editor.remove(PREF_CUSTOM_DRIVER_ID).apply();
            } else {
                final String picked = installedDrivers.get(position - 1).id;
                final String previous = getSelectedDriverId(ctx);
                editor.putString(PREF_CUSTOM_DRIVER_ID, picked).apply();
                // Cenit 0.6.10: el historial de fallos SOLO se borra cuando el
                // usuario cambia a un driver distinto del que estaba activo. En
                // 0.6.8 se limpiaba con solo tocar el diálogo — y como la costumbre
                // es re-picar el mismo driver antes de entrar al juego, el guardarraya
                // se quedaba sin memoria justo antes de cada intento.
                if (previous == null || !previous.equals(picked)) {
                    CustomDriverManager.clearFailuresForDriver(ctx, picked);
                    CustomDriverManager.clearTuningSuspensionForDriver(ctx, picked);
                }
            }
            applyStoredSelection(ctx);
            // Cenit 0.6.11: si el renderer no es Vulkan, el driver elegido no se
            // va a usar aunque esté activo. Decirlo aquí, en el momento, en vez
            // de guardarlo para un aviso que nadie leería a tiempo.
            final int rendererNow = ctx.getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                    .getInt("renderer", -1);
            if (position != 0 && (rendererNow == 12 || rendererNow == 13)) {
                Toast.makeText(ctx, "Ojo: solo funciona con el renderer en Vulkan"
                        + " (ahora está en " + (rendererNow == 12 ? "OpenGL" : "Software") + ")",
                        Toast.LENGTH_LONG).show();
            } else {
                Toast.makeText(ctx, "Se aplica al iniciar el próximo juego", Toast.LENGTH_SHORT).show();
            }
        });

        btnImport.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
            intent.setType("*/*");
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            importLauncher.launch(intent);
        });

        btnDelete.setOnClickListener(v -> {
            if (selectedIndex == 0) {
                Toast.makeText(requireContext(), "Primero selecciona un driver importado", Toast.LENGTH_SHORT).show();
                return;
            }
            CustomDriverManager.InstalledDriver target = installedDrivers.get(selectedIndex - 1);
            Context ctx = requireContext();
            CustomDriverManager.delete(target);
            if (target.id.equals(getSelectedDriverId(ctx))) {
                ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .remove(PREF_CUSTOM_DRIVER_ID).apply();
                applyStoredSelection(ctx);
            }
            Toast.makeText(ctx, "Se eliminó " + target.name, Toast.LENGTH_SHORT).show();
            loadDrivers();
            refreshAdapter();
        });

        // Cenit 0.6.11: "Forzar otra vez". El guardarraya recuerda los pares
        // driver+juego que cerraron el arranque y la próxima vez usa el driver
        // del sistema sin avisar a medias. Este botón borra el historial del
        // driver elegido AHORA MISMO y lo deja activo, para que el próximo
        // arranque sea con él pase lo que pase. Si vuelve a morir, la evidencia
        // del choque (señal nativa + qué driver estaba cargado) ya queda en el
        // reporte por otro camino del guardarraya.
        MaterialButton btnForce = view.findViewById(R.id.btn_force_custom_driver);
        if (btnForce != null) {
            btnForce.setOnClickListener(v -> {
                if (selectedIndex == 0) {
                    Toast.makeText(requireContext(), "Primero selecciona un driver importado",
                            Toast.LENGTH_SHORT).show();
                    return;
                }
                Context ctx = requireContext();
                final CustomDriverManager.InstalledDriver target = installedDrivers.get(selectedIndex - 1);
                CustomDriverManager.clearFailuresForDriver(ctx, target.id);
                // Cenit 0.6.20: "forzar" también le devuelve la oportunidad al ajuste
                // fino; la suspensión existía justamente porque el usuario no la pidió.
                CustomDriverManager.clearTuningSuspensionForDriver(ctx, target.id);
                ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putString(PREF_CUSTOM_DRIVER_ID, target.id).apply();
                applyStoredSelection(ctx);
                Toast.makeText(ctx, "Se fuerza «" + target.name
                        + "» en el próximo arranque, con su ajuste fino", Toast.LENGTH_LONG).show();
                loadDrivers();
                refreshAdapter();
            });
        }

        setupTuningSection(view);

        return new MaterialAlertDialogBuilder(requireContext())
                .setCustomTitle(UiUtils.centeredDialogTitle(requireContext(), "CONTROLADOR GRÁFICO PERSONALIZADO"))
                .setView(view)
                .setNegativeButton("Cerrar", null)
                .create();
    }

    /**
     * Sección de ajuste fino (Cenit 0.6.20). El perfil va detras de la gama del
     * telefono salvo que el usuario elija uno a mano; el cache de shaders va ON por
     * defecto porque en Android Mesa lo trae apagado y es ganancia sin riesgo para el
     * render (solo ocupa disco dentro de la carpeta del driver).
     */
    private void setupTuningSection(View root) {
        profileSpinner = root.findViewById(R.id.sp_turnip_profile);
        profileNote = root.findViewById(R.id.tv_turnip_profile_note);
        statsText = root.findViewById(R.id.tv_turnip_stats);
        cacheSwitch = root.findViewById(R.id.sw_turnip_cache);
        MaterialButton btnRule = root.findViewById(R.id.btn_turnip_rule);
        MaterialButton btnExport = root.findViewById(R.id.btn_turnip_export);

        try {
            deviceTier = NativeApp.getDevicePerformanceTier();
        } catch (Throwable t) {
            deviceTier = 1;
        }

        final String[] labels = TurnipTuning.profileLabels(requireContext(), deviceTier);
        final ArrayAdapter<String> adapter = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        profileSpinner.setAdapter(adapter);
        final int picked = requireContext().getSharedPreferences(TurnipTuning.PREFS,
                Context.MODE_PRIVATE).getInt(TurnipTuning.KEY_PROFILE, -1);
        profileSpinner.setSelection(picked >= 0 && picked < labels.length ? picked
                : TurnipTuning.defaultProfileIndex(deviceTier));
        // setAdapter/setSelection disparan onItemSelected una vez solos. Sin este guard,
        // abrir el dialogo fijaria un perfil "a mano" y apagara la eleccion automatica
        // por gama, que es justamente el comportamiento por defecto que buscamos.
        profileSpinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            private boolean skipFirst = true;
            @Override
            public void onItemSelected(android.widget.AdapterView<?> parent, View v, int position, long id) {
                if (skipFirst) { skipFirst = false; return; }
                TurnipTuning.setProfile(requireContext(), position);
                refreshProfileNote();
                Toast.makeText(requireContext(), "Se aplica al iniciar el próximo juego",
                        Toast.LENGTH_SHORT).show();
            }
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) {}
        });

        final SharedPreferences p = requireContext().getSharedPreferences(
                TurnipTuning.PREFS, Context.MODE_PRIVATE);
        cacheSwitch.setChecked(!p.getBoolean(TurnipTuning.KEY_CACHE_OFF, false));
        cacheSwitch.setOnCheckedChangeListener((b, checked) -> {
            p.edit().putBoolean(TurnipTuning.KEY_CACHE_OFF, !checked).apply();
            Toast.makeText(requireContext(), checked
                            ? "Cache de shaders activado: se aplica al iniciar el próximo juego"
                            : "Cache de shaders apagado (para comparar perfiles)",
                    Toast.LENGTH_SHORT).show();
        });

        btnRule.setOnClickListener(v -> showRuleEditor());
        btnExport.setOnClickListener(v -> {
            final String path = TurnipTuning.exportDriconf(requireContext());
            new MaterialAlertDialogBuilder(requireContext())
                    .setTitle("Reglas exportadas")
                    .setMessage(path == null
                            ? "No se pudo escribir el archivo (revisa el espacio de almacenamiento)."
                            : "Se guardó 00-cenit.conf en:\n" + path
                            + "\n\nEse archivo es el que el repositorio hornea dentro del driver al recompilar "
                            + "(build-turnip.yml). Las banderas del perfil y las banderas por juego YA se aplican "
                            + "con el driver que tienes importado; lo que requiere recompilar son solo las "
                            + "correcciones driconf.")
                    .setPositiveButton(android.R.string.ok, null)
                    .show();
        });

        refreshProfileNote();
    }

    private void refreshProfileNote() {
        if (profileNote == null || statsText == null) return;
        final TurnipTuning.Profile prof = TurnipTuning.effectiveProfile(requireContext(), deviceTier);
        profileNote.setText((TurnipTuning.isManualProfile(requireContext()) ? "Elegido a mano. "
                        : "Automatico segun la gama de este telefono. ")
                + prof.note
                + (prof.flags.isEmpty() ? "" : "\nTU_DEBUG=" + prof.flags));

        // Lo que este telefono realmente midio con cada perfil, para el mismo juego.
        final String driverId = getSelectedDriverId(requireContext());
        final String serial = TurnipTuning.serialForGame(requireContext(), currentGameForTuning());
        final List<DriverStats.Row> rows = DriverStats.rowsFor(requireContext(), driverId, serial);
        if (driverId == null || driverId.isEmpty()) {
            statsText.setText("Importa y selecciona un driver Turnip para empezar a medir perfiles.");
            return;
        }
        if (rows.isEmpty()) {
            statsText.setText("Sin medidas todavia: juega unos minutos con el driver y este perfil, "
                    + "y Cenit guardara el FPS que realmente sostuvo.");
            return;
        }
        StringBuilder sb = new StringBuilder("Medido en este telefono");
        if (!serial.isEmpty()) sb.append(" con ").append(serial);
        sb.append(":\n");
        for (DriverStats.Row r : rows) {
            sb.append("· ").append(DriverStats.labelFor(r.profileId))
              .append(": ").append(DriverStats.brief(r)).append('\n');
        }
        statsText.setText(sb.toString());
    }

    /**
     * Editor de la regla del juego en curso. Dos partes, y conviene tener claro cual
     * sirve ya y cual no:
     *  - Banderas TU_DEBUG: se aplican desde la app al cargar el driver. Valen con
     *    CUALQUIER driver Turnip importado, incluida la build actual.
     *  - Opciones driconf: van horneadas dentro del .so. Con el driver que ya tienes
     *    NO cambian nada; solo cobran efecto al recompilar con 00-cenit.conf.
     */
    private void showRuleEditor() {
        final Context ctx = requireContext();
        final String gameUri = currentGameForTuning();
        final String knownSerial = TurnipTuning.serialForGame(ctx, gameUri);

        final LinearLayout box = new LinearLayout(ctx);
        box.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * ctx.getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad, pad, 0);

        final TextView hint = new TextView(ctx);
        hint.setTextSize(13f);
        hint.setText(knownSerial.isEmpty()
                ? "Cenit no pudo identificar la serial de este juego. Escríbela "
                  + "(formato SLUS-20780 / SLES-50001): es la clave con la que el driver reconoce el juego."
                : "Juego detectado: " + knownSerial + ". Puedes corregirla si la lectura del disco salió rara.");
        box.addView(hint);

        final EditText serialInput = new EditText(ctx);
        serialInput.setText(knownSerial);
        serialInput.setHint("SLUS-20780");
        box.addView(serialInput);

        TurnipTuning.GameRule existing = null;
        for (TurnipTuning.GameRule r : TurnipTuning.loadRules(ctx)) {
            if (r.serial.equals(knownSerial) && !knownSerial.isEmpty()) { existing = r; break; }
        }

        final TextView flagsHelp = new TextView(ctx);
        flagsHelp.setTextSize(12f);
        flagsHelp.setTextColor(0xFF9E9E9E);
        flagsHelp.setText("Banderas TU_DEBUG solo para ESTE juego (separadas por coma). Válidas: "
                + String.join(", ", TurnipTuning.TU_FLAG_NAMES)
                + ".\nEstas SÍ se aplican con el driver que ya tienes importado.");
        box.addView(flagsHelp);

        final EditText flagsInput = new EditText(ctx);
        flagsInput.setText(existing == null ? "" : existing.flags);
        flagsInput.setHint("nocb,noconcurrentresolves");
        box.addView(flagsInput);

        final boolean[] checked = new boolean[TurnipTuning.DRICONF_OPTION_NAMES.length];
        if (existing != null) {
            for (String[] kv : existing.options) {
                for (int i = 0; i < TurnipTuning.DRICONF_OPTION_NAMES.length; i++)
                    if (TurnipTuning.DRICONF_OPTION_NAMES[i].equals(kv[0]) && "true".equals(kv[1]))
                        checked[i] = true;
            }
        }

        final TextView driconfHelp = new TextView(ctx);
        driconfHelp.setTextSize(12f);
        driconfHelp.setTextColor(0xFF9E9E9E);
        driconfHelp.setText("Correcciones driconf (marcar = true). Van horneadas dentro del driver: "
                + "requieren recompilar con el archivo exportado y con el driver actual no cambian nada.");
        box.addView(driconfHelp);

        for (int i = 0; i < TurnipTuning.DRICONF_OPTION_NAMES.length; i++) {
            final MaterialSwitch sw = new MaterialSwitch(ctx);
            sw.setText(TurnipTuning.DRICONF_OPTION_NAMES[i]);
            sw.setTextSize(13f);
            sw.setChecked(checked[i]);
            final int idx = i;
            sw.setOnCheckedChangeListener((b, v) -> checked[idx] = v);
            box.addView(sw);
        }

        final android.widget.ScrollView sv = new android.widget.ScrollView(ctx);
        sv.addView(box);

        final TurnipTuning.GameRule toDelete = existing;
        new MaterialAlertDialogBuilder(ctx)
                .setTitle("Reglas del driver para este juego")
                .setView(sv)
                .setPositiveButton("Guardar", (d, w) -> {
                    final String s = serialInput.getText().toString().trim();
                    final List<String[]> opts = new ArrayList<>();
                    for (int i = 0; i < checked.length; i++)
                        if (checked[i])
                            opts.add(new String[]{TurnipTuning.DRICONF_OPTION_NAMES[i], "true"});
                    final String fl = TurnipTuning.normalizeFlagList(flagsInput.getText().toString());
                    if (s.isEmpty() || (opts.isEmpty() && fl.isEmpty())) {
                        Toast.makeText(ctx, "Nada que guardar: falta la serial o el ajuste",
                                Toast.LENGTH_LONG).show();
                        return;
                    }
                    TurnipTuning.putRule(ctx, s, opts, fl, true, "");
                    Toast.makeText(ctx, "Regla guardada para " + s
                                    + (fl.isEmpty() ? " (solo driconf: necesita driver recompilado)"
                                                    : " (banderas activas desde el próximo arranque)"),
                            Toast.LENGTH_LONG).show();
                    refreshProfileNote();
                })
                .setNeutralButton(toDelete != null ? "Borrar regla" : "Cerrar", (d, w) -> {
                    if (toDelete != null && TurnipTuning.deleteRule(ctx, toDelete.serial)) {
                        Toast.makeText(ctx, "Regla borrada", Toast.LENGTH_SHORT).show();
                        refreshProfileNote();
                    }
                })
                .show();
    }

    /** URI del juego que debe usarse para etiquetar medidas y reglas: el que está
     *  delante ahora, y si el diálogo se abre desde Ajustes sin juego, el último jugado. */
    private String currentGameForTuning() {
        final Activity a = getActivity();
        if (a instanceof MainActivity) {
            final String cur = ((MainActivity) a).getSelectedGameUri();
            if (cur != null && !cur.isEmpty()) return cur;
        }
        return MainActivity.lastBootedGameUri();
    }

    private void loadDrivers() {
        installedDrivers = CustomDriverManager.listInstalled(requireContext());
        String currentId = getSelectedDriverId(requireContext());
        selectedIndex = 0;
        List<String> labels = new ArrayList<>();
        labels.add("Driver del sistema (sin cambio)");
        for (int i = 0; i < installedDrivers.size(); i++) {
            CustomDriverManager.InstalledDriver d = installedDrivers.get(i);
            String label = d.version.isEmpty() ? d.name : d.name + " (" + d.version + ")";
            // Cenit 0.6.11: marcar los drivers con cierres registrados, para que
            // el usuario entienda por qué el guardarraya lo cambió y sepa que
            // "Forzar otra vez" existe para darle una oportunidad limpia.
            if (CustomDriverManager.hasAnyFailure(requireContext(), d.id))
                label += " — cerró un juego (toca Forzar para reintentar)";
            labels.add(label);
            if (d.id.equals(currentId))
                selectedIndex = i + 1;
        }
        driverAdapter = new ArrayAdapter<>(requireContext(), android.R.layout.simple_list_item_single_choice, labels);
    }

    private void refreshAdapter() {
        if (driverListView == null)
            return;
        driverListView.setAdapter(driverAdapter);
        driverListView.setItemChecked(selectedIndex, true);
    }

    @Override
    public void onStart() {
        super.onStart();
        refreshAdapter();
    }

    private void importDriver(Uri uri) {
        Context appCtx = requireContext().getApplicationContext();
        Toast.makeText(appCtx, "Importando driver...", Toast.LENGTH_SHORT).show();
        Executors.newSingleThreadExecutor().execute(() -> {
            CustomDriverManager.InstalledDriver installed = CustomDriverManager.installFromUri(appCtx, uri);
            if (getActivity() == null)
                return;
            requireActivity().runOnUiThread(() -> {
                if (installed == null) {
                    Toast.makeText(appCtx, "La importación falló: el .zip no es un driver válido", Toast.LENGTH_LONG).show();
                    return;
                }
                appCtx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putString(PREF_CUSTOM_DRIVER_ID, installed.id).apply();
                applyStoredSelection(appCtx);
                Toast.makeText(appCtx, "Instalado " + installed.name, Toast.LENGTH_SHORT).show();
                loadDrivers();
                refreshAdapter();
            });
        });
    }
}
