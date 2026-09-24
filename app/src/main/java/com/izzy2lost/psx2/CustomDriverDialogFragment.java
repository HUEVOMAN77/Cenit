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
import android.widget.ListView;
import android.widget.Toast;

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
                CustomDriverManager.applyToNative(context, selected);
                return;
            }
        }
        if (selected != null && gameKey != null
                && CustomDriverManager.isKnownBadCombo(context, selected.id, gameKey)) {
            CustomDriverManager.setFallbackNotice(context,
                    "El driver «" + selected.name + "» cerró " + (gameLabel == null ? "este juego" : gameLabel)
                            + " al arrancar la última vez. Para este juego Cenit usó el driver del sistema."
                            + " Puedes forzar el driver otra vez en «Controlador gráfico personalizado»."
                            + " Si vuelve a cerrarse, envía el reporte: ahora queda la señal exacta del choque.");
            CustomDriverManager.clearAttempt(context);
            CustomDriverManager.applyToNative(context, null);
            return;
        }
        if (selected != null && gameKey != null)
            CustomDriverManager.beginAttempt(context, selected.id, gameKey);
        else
            CustomDriverManager.clearAttempt(context); // sin driver o sin juego: nada que vigilar
        CustomDriverManager.applyToNative(context, selected);
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
                if (previous == null || !previous.equals(picked))
                    CustomDriverManager.clearFailuresForDriver(ctx, picked);
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
                ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putString(PREF_CUSTOM_DRIVER_ID, target.id).apply();
                applyStoredSelection(ctx);
                Toast.makeText(ctx, "Se fuerza «" + target.name
                        + "» en el próximo arranque", Toast.LENGTH_LONG).show();
                loadDrivers();
                refreshAdapter();
            });
        }

        return new MaterialAlertDialogBuilder(requireContext())
                .setCustomTitle(UiUtils.centeredDialogTitle(requireContext(), "CONTROLADOR GRÁFICO PERSONALIZADO"))
                .setView(view)
                .setNegativeButton("Cerrar", null)
                .create();
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
