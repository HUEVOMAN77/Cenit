package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Pantalla de inicio de Cenit, réplica de la maqueta: cabecera negra con logo,
 * buscar y menú de tres puntos; rejilla de cajas PS2 a cuatro columnas sin
 * títulos, y barra inferior Inicio · Biblioteca · Carpetas · Ajustes.
 *
 * Existe porque la app usaba el surface del juego como pantalla raíz, así que al
 * abrir mostraba mandos táctiles sobre un lienzo negro en lugar de una interfaz.
 * Solo dibuja; quién escanea la biblioteca y lanza juegos es MainActivity.
 */
public final class HomeScreenController {

    private static final String COVER_BASE_URL =
            "https://raw.githubusercontent.com/izzy2lost/ps2-covers/main/covers/3d/";
    private static final ExecutorService LIBRARY_EXECUTOR = Executors.newSingleThreadExecutor();

    public interface Host {
        void onPlayGame(String gameUri);
        void onGameLongPress(String gameTitle, String gameUri);
        void onAddGamesFolder();
        void onOpenSetup();
        void onOpenSettings();
        void onImportBios();
        void onPickDataFolder();
        void onOpenGamesManager();
        void onRefreshLibrary();
    }

    private final Context context;
    private final Host host;
    private final android.os.Handler mainHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private HomeGameAdapter adapter;

    // Última publicación ganadora: si llegan dos escaneos, solo se pinta el más nuevo.
    private volatile long libraryToken = 0L;
    // La primera biblioteca que llega entra escalonada; al volver de un juego o
    // refrescar se pinta sin animación, salvo que la cortinilla la vuelva a pedir.
    private boolean animateNextLibrary = true;
    // Mientras la cortinilla está arriba no se anima nada: la función se corre al
    // bajar el telón, no bajo la cortina.
    private boolean entranceBlocked = false;
    // Biblioteca ya resuelta, para poder repintar al cambiar de pestaña.
    private final List<HomeGameAdapter.Entry> entries = new ArrayList<>();
    // Último estado de escaneo publicado, para no reescribir el aviso al cambiar de pestaña.
    private boolean lastScanning = false;

    private View root;
    private View statusCard;
    private TextView statusText;
    private View statusAction;
    private View searchRow;
    private EditText searchInput;
    private View scroll;
    private View settingsPanel;
    private View emptyState;
    private TextView emptyText;
    private View emptyAction;

    private View tabHome, tabLibrary, tabFolders, tabSettings;

    public HomeScreenController(@NonNull Context context, @NonNull ViewGroup parent, @NonNull Host host) {
        this.context = context;
        this.host = host;
        this.root = LayoutInflater.from(context).inflate(R.layout.view_home_screen, parent, false);
        // ConstraintLayout no respeta bien wrap_content heredado, así que se ancla a
        // los cuatro bordes del padre para que el inicio ocupe toda la pantalla.
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
        bindViews();
        // El root del layout está en modo edge-to-edge, así que los insets se aplican
        // aquí, sobre el propio contenido del inicio.
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(root, (v, insets) -> {
            androidx.core.graphics.Insets bars = insets.getInsets(
                    androidx.core.view.WindowInsetsCompat.Type.systemBars()
                            | androidx.core.view.WindowInsetsCompat.Type.displayCutout());
            applyInsets(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
    }

    private void bindViews() {
        statusCard = root.findViewById(R.id.home_status_card);
        statusText = root.findViewById(R.id.home_status_text);
        statusAction = root.findViewById(R.id.btn_home_setup);
        searchRow = root.findViewById(R.id.home_search_row);
        searchInput = root.findViewById(R.id.home_search_input);
        scroll = root.findViewById(R.id.home_scroll);
        settingsPanel = root.findViewById(R.id.home_settings_panel);
        emptyState = root.findViewById(R.id.home_empty);
        emptyText = root.findViewById(R.id.home_empty_text);
        emptyAction = root.findViewById(R.id.btn_home_empty_action);

        tabHome = root.findViewById(R.id.tab_home);
        tabLibrary = root.findViewById(R.id.tab_library);
        tabFolders = root.findViewById(R.id.tab_folders);
        tabSettings = root.findViewById(R.id.tab_settings);

        View grid = root.findViewById(R.id.home_grid);
        if (grid instanceof RecyclerView rv) {
            rv.setHasFixedSize(false);
            rv.setNestedScrollingEnabled(false);
            rv.setLayoutManager(new GridLayoutManager(context, spanCount()));
            adapter = new HomeGameAdapter(context, new HomeGameAdapter.Callback() {
                @Override public void onGameClick(HomeGameAdapter.Entry e) {
                    if (e != null) host.onPlayGame(e.uri);
                }

                @Override public void onGameLongClick(HomeGameAdapter.Entry e) {
                    if (e != null) host.onGameLongPress(e.title, e.uri);
                }
            });
            rv.setAdapter(adapter);
        }

        // Cabecera: lupa que despliega el campo de búsqueda y menú de tres puntos.
        ImageButton searchBtn = root.findViewById(R.id.btn_home_search);
        if (searchBtn != null) searchBtn.setOnClickListener(v -> toggleSearch());
        ImageButton overflow = root.findViewById(R.id.btn_home_overflow);
        if (overflow != null) overflow.setOnClickListener(this::showOverflowMenu);

        if (searchInput != null) {
            searchInput.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence s, int st, int c, int a) {}
                @Override public void onTextChanged(CharSequence s, int st, int b, int c) {}
                @Override public void afterTextChanged(Editable s) {
                    if (adapter != null) {
                        adapter.setFilter(s == null ? "" : s.toString());
                        renderEmpty();
                    }
                }
            });
        }

        if (statusAction != null) statusAction.setOnClickListener(v -> host.onOpenSetup());
        if (emptyAction != null) emptyAction.setOnClickListener(v -> host.onAddGamesFolder());

        if (tabHome != null) tabHome.setOnClickListener(v -> selectTab(TAB_HOME));
        if (tabLibrary != null) tabLibrary.setOnClickListener(v -> {
            // "Biblioteca" repintea la rejilla y la recarga desde el disco.
            selectTab(TAB_LIBRARY);
            host.onRefreshLibrary();
        });
        if (tabFolders != null) tabFolders.setOnClickListener(v -> {
            // "Carpetas" no es una vista propia: lleva directo a añadir una carpeta.
            tabFolders.setSelected(true);
            host.onAddGamesFolder();
            mainHandler.postDelayed(() -> tabFolders.setSelected(false), 400);
        });
        if (tabSettings != null) tabSettings.setOnClickListener(v ->
                selectTab(tabSettings.isSelected() ? TAB_HOME : TAB_SETTINGS));

        bindSettingsPanelRows();
        selectTab(TAB_HOME);
    }

    /** Las filas del panel "Ajustes" reutilizan los flujos que ya existen. */
    private void bindSettingsPanelRows() {
        View bios = root.findViewById(R.id.home_set_bios);
        if (bios != null) bios.setOnClickListener(v -> host.onImportBios());
        View data = root.findViewById(R.id.home_set_data);
        if (data != null) data.setOnClickListener(v -> host.onPickDataFolder());
        View games = root.findViewById(R.id.home_set_games);
        if (games != null) games.setOnClickListener(v -> host.onOpenGamesManager());
        View setup = root.findViewById(R.id.home_set_setup);
        if (setup != null) setup.setOnClickListener(v -> host.onOpenSetup());
        View advanced = root.findViewById(R.id.home_set_advanced);
        if (advanced != null) advanced.setOnClickListener(v -> host.onOpenSettings());
    }

    // ------------------------------------------------------------------
    // Pestañas
    // ------------------------------------------------------------------

    private static final int TAB_HOME = 0;
    private static final int TAB_LIBRARY = 1;
    private static final int TAB_SETTINGS = 2;
    private int currentTab = TAB_HOME;

    /** Inicio y Biblioteca muestran la rejilla; Ajustes abre su panel a pantalla completa. */
    private void selectTab(int tab) {
        currentTab = tab;
        boolean settings = tab == TAB_SETTINGS;
        if (scroll != null) scroll.setVisibility(settings ? View.GONE : View.VISIBLE);
        if (settingsPanel != null) settingsPanel.setVisibility(settings ? View.VISIBLE : View.GONE);
        if (searchRow != null && searchRow.getVisibility() == View.VISIBLE && settings) {
            searchRow.setVisibility(View.GONE);
            if (searchInput != null) {
                searchInput.setText("");
                if (adapter != null) adapter.setFilter("");
            }
        }
        setSelected(tabHome, tab == TAB_HOME);
        setSelected(tabLibrary, tab == TAB_LIBRARY);
        setSelected(tabSettings, settings);
        if (!settings) renderEmpty();
    }

    private void setSelected(@Nullable View tab, boolean selected) {
        if (tab != null) tab.setSelected(selected);
    }

    private void toggleSearch() {
        if (searchRow == null || searchInput == null) return;
        boolean open = searchRow.getVisibility() == View.VISIBLE;
        if (open) {
            searchInput.setText("");
            if (adapter != null) adapter.setFilter("");
            searchRow.setVisibility(View.GONE);
        } else {
            selectTab(currentTab == TAB_SETTINGS ? TAB_HOME : currentTab);
            searchRow.setVisibility(View.VISIBLE);
            searchInput.requestFocus();
        }
        // Con filtro vacío vuelve la biblioteca completa; si estaba vacía, el aviso.
        renderEmpty();
    }

    private void showOverflowMenu(View anchor) {
        android.widget.PopupMenu menu = new android.widget.PopupMenu(context, anchor);
        menu.getMenu().add("Añadir carpeta de juegos");
        menu.getMenu().add("Gestionar juegos");
        menu.getMenu().add("Asistente de configuración");
        menu.setOnMenuItemClickListener(item -> {
            String t = item.getTitle() == null ? "" : item.getTitle().toString();
            switch (t) {
                case "Añadir carpeta de juegos": host.onAddGamesFolder(); return true;
                case "Gestionar juegos": host.onOpenGamesManager(); return true;
                case "Asistente de configuración": host.onOpenSetup(); return true;
                default: return false;
            }
        });
        menu.show();
    }

    private int spanCount() {
        int widthDp = (int) (context.getResources().getDisplayMetrics().widthPixels
                / context.getResources().getDisplayMetrics().density);
        if (widthDp >= 720) return 5;
        if (widthDp >= 400) return 4;
        return 3;
    }

    /**
     * Llamado al terminar la cortinilla: la biblioteca que esté en pantalla entra
     * escalonada. Si todavía no llegó ninguna, la animación queda armada (latch) y
     * se usa con la primera tanda de datos, cuando el inicio ya sea visible.
     */
    public void playEntranceAnimation() {
        animateNextLibrary = true;
        paintEntranceIfNeeded();
    }

    /** Consume el latch solo cuando tiene sentido animar: inicio visible y algo que mostrar. */
    private boolean paintEntranceIfNeeded() {
        if (entranceBlocked || !animateNextLibrary || !isVisible() || entries.isEmpty()) return false;
        animateNextLibrary = false;
        animateHeader();
        if (adapter != null) adapter.startEntranceAnimation();
        return true;
    }

    public void setEntranceBlocked(boolean blocked) {
        entranceBlocked = blocked;
        if (!blocked) paintEntranceIfNeeded();
    }

    /** La cabecera sube suavemente: logo, nombre, lema y estado, uno tras otro. */
    private void animateHeader() {
        rise(root.findViewById(R.id.home_logo), 0);
        rise(root.findViewById(R.id.home_wordmark), 60);
        rise(root.findViewById(R.id.home_tagline), 130);
        rise(statusCard, 210);
        rise(root.findViewById(R.id.home_grid), 260);
        rise(root.findViewById(R.id.home_bottom_bar), 320);
    }

    private void rise(@Nullable View view, int startDelayMs) {
        if (view == null) return;
        view.setAlpha(0f);
        view.setTranslationY(dp(12));
        view.animate().alpha(1f).translationY(0f)
                .setStartDelay(startDelayMs)
                .setDuration(420)
                .setInterpolator(ENTRANCE_INTERPOLATOR)
                .start();
    }

    private static final android.view.animation.Interpolator ENTRANCE_INTERPOLATOR =
            new android.view.animation.PathInterpolator(0.16f, 1f, 0.3f, 1f);

    public void setVisible(boolean visible) {
        if (root == null) return;
        boolean wasVisible = isVisible();
        root.setVisibility(visible ? View.VISIBLE : View.GONE);
        if (visible && !wasVisible) paintEntranceIfNeeded();
    }

    public boolean isVisible() {
        return root != null && root.getVisibility() == View.VISIBLE;
    }

    /** Ajusta los márgenes superiores/inferiores a las barras del sistema. */
    public void applyInsets(int left, int top, int right, int bottom) {
        if (root == null) return;
        View header = root.findViewById(R.id.home_header);
        if (header != null) header.setPadding(header.getPaddingLeft(), top + dp(14),
                header.getPaddingRight(), header.getPaddingBottom());
        View bar = root.findViewById(R.id.home_bottom_bar);
        if (bar != null) bar.setPadding(left, bar.getPaddingTop(), right, bottom);
    }

    private int dp(int d) {
        return (int) (d * context.getResources().getDisplayMetrics().density + 0.5f);
    }

    /**
     * Publica la biblioteca. Resolver carátulas toca SAF y el disco, así que se hace
     * fuera del hilo de UI y se publica el resultado al terminar.
     */
    public void submitLibrary(String[] names, String[] uris, boolean scanning) {
        lastScanning = scanning || names == null || uris == null || names.length == 0;
        renderEmpty();
        final String[] nameSnapshot = names == null ? new String[0] : names;
        final String[] uriSnapshot = uris == null ? new String[0] : uris;
        final long token = ++libraryToken;
        LIBRARY_EXECUTOR.execute(() -> {
            List<HomeGameAdapter.Entry> built = new ArrayList<>();
            try {
                SharedPreferences prefs = context.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
                File coversDir = coversDir();
                int n = Math.min(nameSnapshot.length, uriSnapshot.length);
                String[] serials = new String[n];
                LinkedHashSet<String> serialSet = new LinkedHashSet<>();
                for (int i = 0; i < n; i++) {
                    String serial = GameSerialUtils.normalizeLibrarySerial(
                            prefs.getString("serial:" + uriSnapshot[i], null));
                    if (serial.isEmpty()) serial = GameSerialUtils.serialFromUri(uriSnapshot[i]);
                    serials[i] = serial;
                    if (!serial.isEmpty()) serialSet.add(serial);
                }
                Map<String, String> cached = CoverCache.findValidCoverPaths(context, serialSet);

                for (int i = 0; i < n; i++) {
                    HomeGameAdapter.Entry e = new HomeGameAdapter.Entry(nameSnapshot[i], uriSnapshot[i]);
                    String serial = serials[i];
                    if (!serial.isEmpty()) {
                        String path = cached.get(serial);
                        e.coverPath = path != null ? path : new File(coversDir, serial + ".png").getAbsolutePath();
                        e.coverUrl = COVER_BASE_URL + serial + ".png";
                    }
                    built.add(e);
                }
            } catch (Throwable error) {
                android.util.Log.w("HomeScreen", "Unable to resolve covers", error);
            }
            final List<HomeGameAdapter.Entry> finalEntries = built;
            mainHandler.post(() -> {
                if (token != libraryToken) return; // una publicación más nueva ya ganó
                applyLibrary(finalEntries);
            });
        });
    }

    private void applyLibrary(List<HomeGameAdapter.Entry> built) {
        entries.clear();
        entries.addAll(built);

        if (adapter != null) adapter.setEntries(entries);
        // paintEntranceIfNeeded ya refresca (con animación) si la entrada quedó armada;
        // si no, hay que repintar la cuadrícula de todos modos.
        boolean animated = paintEntranceIfNeeded();
        if (adapter != null && !animated) adapter.notifyDataSetChanged();
        lastScanning = false;
        renderEmpty();
    }

    private void renderEmpty() {
        if (emptyState == null) return;
        boolean filtering = searchRow != null && searchRow.getVisibility() == View.VISIBLE
                && searchInput != null && !TextUtils.isEmpty(searchInput.getText());
        boolean noMatches = filtering && adapter != null && adapter.getItemCount() == 0;
        if (!entries.isEmpty() && !noMatches && !lastScanning) {
            emptyState.setVisibility(View.GONE);
            return;
        }
        emptyState.setVisibility(View.VISIBLE);
        if (lastScanning && entries.isEmpty()) {
            emptyText.setText("Buscando tus juegos…");
            emptyAction.setVisibility(View.GONE);
        } else if (noMatches) {
            emptyText.setText("Ningún juego coincide con la búsqueda.");
            emptyAction.setVisibility(View.GONE);
        } else if (!hasFoldersNow()) {
            emptyText.setText("Todavía no hay juegos. Añade una carpeta con tus copias de PS2 (ISO, CHD o CSO).");
            emptyAction.setVisibility(View.VISIBLE);
        } else {
            emptyText.setText("No encontramos juegos en tus carpetas. Revisa que tengan formato de PS2 o añade otra carpeta.");
            emptyAction.setVisibility(View.VISIBLE);
        }
    }

    private boolean hasFoldersNow() {
        return !GameFolders.list(context).isEmpty();
    }

    /** Estado de configuración: qué falta antes de poder jugar. */
    public void submitStatus(boolean hasBios, boolean hasDataFolder, boolean hasGamesFolder) {
        List<String> missing = new ArrayList<>();
        if (!hasBios) missing.add("la BIOS");
        if (!hasDataFolder) missing.add("la carpeta de datos");
        if (!hasGamesFolder) missing.add("una carpeta de juegos");

        if (missing.isEmpty()) {
            if (statusCard != null) statusCard.setVisibility(View.GONE);
        } else {
            if (statusCard != null) statusCard.setVisibility(View.VISIBLE);
            if (statusText != null) statusText.setText("Falta " + join(missing));
            if (statusAction != null) statusAction.setVisibility(View.VISIBLE);
        }

        TextView biosSub = root.findViewById(R.id.home_set_bios_sub);
        if (biosSub != null) biosSub.setText(hasBios
                ? "BIOS detectada y verificada" : "Necesaria para iniciar cualquier juego");
        TextView dataSub = root.findViewById(R.id.home_set_data_sub);
        if (dataSub != null) dataSub.setText(hasDataFolder
                ? "Carpeta de datos elegida" : "Partidas, estados y configuración");
        TextView gamesSub = root.findViewById(R.id.home_set_games_sub);
        if (gamesSub != null) gamesSub.setText(hasGamesFolder
                ? "Carpetas configuradas" : "Añade o cambia las carpetas de tu biblioteca");
    }

    private static String join(List<String> parts) {
        if (parts.size() == 1) return parts.get(0);
        return String.join(" y ", parts);
    }

    private File coversDir() {
        File base = context.getExternalFilesDir("covers");
        if (base == null) base = new File(context.getFilesDir(), "covers");
        if (!base.exists()) base.mkdirs();
        return base;
    }

    /** Carpetas de juegos configuradas, compartidas con MainActivity. */
    static final class GameFolders {
        static List<String> list(Context context) {
            SharedPreferences prefs = context.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
            List<String> out = new ArrayList<>();
            String json = prefs.getString("games_folder_uris_json", null);
            if (!TextUtils.isEmpty(json)) {
                try {
                    org.json.JSONArray arr = new org.json.JSONArray(json);
                    for (int i = 0; i < arr.length(); i++) {
                        String u = arr.optString(i, "");
                        if (!TextUtils.isEmpty(u)) out.add(u);
                    }
                } catch (Throwable ignored) {}
            }
            String legacy = prefs.getString("games_folder_uri", null);
            if (!TextUtils.isEmpty(legacy) && !out.contains(legacy)) out.add(legacy);
            return out;
        }
    }
}
