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
        void onRemoveGamesFolder(String folderUri);
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
    // La tarjeta de "falta la BIOS" solo cabe en Inicio; Biblioteca la ignora.
    private boolean statusWanted = false;

    private View root;
    private View statusCard;
    private TextView statusText;
    private View statusAction;
    private View searchRow;
    private EditText searchInput;
    private View scroll;
    private View emptyState;
    private TextView emptyText;
    private View emptyAction;

    // Secciones del rediseño 0.6.9: encabezados con marcador neón y contador,
    // más el carrusel de "siguiendo donde lo dejaste".
    private View recentSectionHeader;
    private TextView recentSectionTitle;
    private TextView recentSectionCount;
    private RecyclerView recentRow;
    private HomeRecentAdapter recentAdapter;
    private View librarySectionHeader;
    private TextView librarySectionTitle;
    private TextView librarySectionCount;

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
        emptyState = root.findViewById(R.id.home_empty);
        emptyText = root.findViewById(R.id.home_empty_text);
        emptyAction = root.findViewById(R.id.btn_home_empty_action);

        tabHome = root.findViewById(R.id.tab_home);
        tabLibrary = root.findViewById(R.id.tab_library);
        tabFolders = root.findViewById(R.id.tab_folders);
        tabSettings = root.findViewById(R.id.tab_settings);

        // ---- Secciones del rediseño ----
        recentSectionHeader = root.findViewById(R.id.home_section_recent);
        if (recentSectionHeader != null) {
            recentSectionTitle = recentSectionHeader.findViewById(R.id.section_title);
            recentSectionCount = recentSectionHeader.findViewById(R.id.section_count);
            if (recentSectionTitle != null) recentSectionTitle.setText("SIGUIENDO DONDE LO DEJASTE");
        }
        librarySectionHeader = root.findViewById(R.id.home_section_library);
        if (librarySectionHeader != null) {
            librarySectionTitle = librarySectionHeader.findViewById(R.id.section_title);
            librarySectionCount = librarySectionHeader.findViewById(R.id.section_count);
            if (librarySectionTitle != null) librarySectionTitle.setText("TU BIBLIOTECA");
        }
        recentRow = root.findViewById(R.id.home_recent_row);
        if (recentRow != null) {
            recentRow.setHasFixedSize(true);
            recentRow.setLayoutManager(
                    new androidx.recyclerview.widget.LinearLayoutManager(context,
                            androidx.recyclerview.widget.LinearLayoutManager.HORIZONTAL, false));
            recentAdapter = new HomeRecentAdapter(context, entry -> {
                if (entry != null) host.onPlayGame(entry.uri);
            });
            recentRow.setAdapter(recentAdapter);
        }

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
            // "Carpetas" ya no abre el selector a ciegas: enseña qué carpetas
            // sigue Cenit, deja quitarlas y añadir otra.
            tabFolders.setSelected(true);
            showFoldersDialog();
            mainHandler.postDelayed(() -> tabFolders.setSelected(false), 400);
        });
        // "Ajustes" ya no es una vista dentro del inicio: abre la pantalla completa.
        if (tabSettings != null) tabSettings.setOnClickListener(v -> {
            if (tabSettings.isSelected()) { selectTab(TAB_HOME); return; }
            selectTab(TAB_SETTINGS);
            host.onOpenSettings();
        });

        selectTab(TAB_HOME);
    }

    // ------------------------------------------------------------------
    // Pestañas
    // ------------------------------------------------------------------

    private static final int TAB_HOME = 0;
    private static final int TAB_LIBRARY = 1;
    private static final int TAB_SETTINGS = 2;
    private int currentTab = TAB_HOME;

    /** Inicio y Biblioteca comparten la rejilla, pero no la cabecera: Inicio es el
     *  panel de bienvenida (marca, estado, recientes) y Biblioteca es el catálogo
     *  completo, sin adorno, con la búsqueda delante. Ajustes solo marca el estado. */
    private void selectTab(int tab) {
        currentTab = tab;
        boolean settings = tab == TAB_SETTINGS;
        if (scroll != null) scroll.setVisibility(View.VISIBLE);
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
        applyTabChrome();
        if (!settings) renderEmpty();
    }

    /** Lo que distingue a Inicio de Biblioteca, para que no sean el mismo menú. */
    private void applyTabChrome() {
        boolean library = currentTab == TAB_LIBRARY;
        setShown(root.findViewById(R.id.home_logo), !library);
        setShown(root.findViewById(R.id.home_wordmark), !library);
        setShown(root.findViewById(R.id.home_tagline), !library);
        if (statusCard != null) {
            statusCard.setVisibility(!library && statusWanted ? View.VISIBLE : View.GONE);
        }
        if (librarySectionTitle != null) {
            librarySectionTitle.setText(library ? "TODOS TUS JUEGOS" : "TU BIBLIOTECA");
        }
        // En Biblioteca la búsqueda está siempre a la vista (es su única herramienta);
        // en Inicio se queda detrás de la lupa para no cargar el panel.
        if (searchRow != null) {
            if (library) {
                searchRow.setVisibility(View.VISIBLE);
            } else if (searchRow.getVisibility() == View.VISIBLE) {
                searchRow.setVisibility(View.GONE);
                if (searchInput != null) searchInput.setText("");
                if (adapter != null) adapter.setFilter("");
            }
        }
        if (library) {
            if (recentSectionHeader != null) recentSectionHeader.setVisibility(View.GONE);
            if (recentRow != null) recentRow.setVisibility(View.GONE);
        } else {
            renderSections();
        }
    }

    private static void setShown(@Nullable View view, boolean shown) {
        if (view != null) view.setVisibility(shown ? View.VISIBLE : View.GONE);
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
        rise(librarySectionHeader, 240);
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
                SharedPreferences.Editor serialEditor = prefs.edit();
                for (int i = 0; i < n; i++) {
                    // Preferencia, cabecera del disco y nombre del archivo: el
                    // mismo orden que el botón manual de portadas. Antes solo se
                    // probaba el nombre, y los juegos con nombres sin código se
                    // quedaban sin carátula.
                    String serial = GameSerialUtils.resolveSerial(prefs, uriSnapshot[i], true);
                    if (!serial.isEmpty()
                            && !serial.equals(prefs.getString("serial:" + uriSnapshot[i], null))) {
                        serialEditor.putString("serial:" + uriSnapshot[i], serial);
                    }
                    serials[i] = serial;
                    if (!serial.isEmpty()) serialSet.add(serial);
                }
                serialEditor.apply();
                Map<String, String> cached = CoverCache.findValidCoverPaths(context, serialSet);
                // Con "auto_covers" activo, lo que falte se descarga en segundo plano.
                CoverAutoSync.maybeSyncAsync(context, uriSnapshot);

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
        renderSections();
    }

    /**
     * Contadores y carrusel de recientes. La marca "last_played:<uri>" la escribe
     * MainActivity cada vez que se abre un juego; aquí solo se lee, y solo se
     * muestran los últimos 7 días, hasta 8 títulos.
     */
    private void renderSections() {
        if (librarySectionCount != null) {
            int total = entries.size();
            librarySectionCount.setText(String.valueOf(total));
            librarySectionCount.setVisibility(total > 0 ? View.VISIBLE : View.GONE);
        }
        List<HomeGameAdapter.Entry> recents = new ArrayList<>();
        try {
            SharedPreferences prefs = context.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
            long cutoff = System.currentTimeMillis() - 7L * 24 * 60 * 60 * 1000;
            java.util.Map<Long, HomeGameAdapter.Entry> timed = new java.util.TreeMap<>(
                    java.util.Collections.reverseOrder());
            for (HomeGameAdapter.Entry e : entries) {
                long t = prefs.getLong("last_played:" + e.uri, 0L);
                if (t >= cutoff) timed.put(t, e);
            }
            for (HomeGameAdapter.Entry e : timed.values()) {
                if (recents.size() >= 8) break;
                recents.add(e);
            }
        } catch (Throwable ignored) {
            recents.clear();
        }
        boolean show = !recents.isEmpty() && recentSectionHeader != null && recentRow != null
                && currentTab != TAB_LIBRARY;
        if (recentSectionHeader != null) recentSectionHeader.setVisibility(show ? View.VISIBLE : View.GONE);
        if (recentRow != null) recentRow.setVisibility(show ? View.VISIBLE : View.GONE);
        if (show) {
            if (recentSectionCount != null) recentSectionCount.setText(String.valueOf(recents.size()));
            if (recentAdapter != null) recentAdapter.setItems(recents);
        }
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

    /** Explica y permite administrar las carpetas que Cenit recorre buscando juegos. */
    private void showFoldersDialog() {
        final List<String> folders = GameFolders.list(context);
        final int pad = dp(20);
        final android.widget.LinearLayout box = new android.widget.LinearLayout(context);
        box.setOrientation(android.widget.LinearLayout.VERTICAL);
        box.setPadding(pad, dp(8), pad, 0);

        TextView intro = new TextView(context);
        intro.setText("Cenit busca aquí tus copias de PS2 (ISO, CHD o CSO). "
                + "Una carpeta puede contener subcarpetas y se recuerda entre sesiones.");
        intro.setTextSize(13f);
        intro.setTextColor(context.getColor(R.color.home_muted));
        box.addView(intro);

        final androidx.appcompat.app.AlertDialog[] dialogRef =
                new androidx.appcompat.app.AlertDialog[1];

        if (folders.isEmpty()) {
            TextView none = new TextView(context);
            none.setText("Todavía no hay ninguna carpeta añadida.");
            none.setTextSize(14f);
            none.setPadding(0, dp(18), 0, dp(6));
            box.addView(none);
        } else {
            for (String folderUri : folders) {
                android.widget.LinearLayout row = new android.widget.LinearLayout(context);
                row.setOrientation(android.widget.LinearLayout.HORIZONTAL);
                row.setGravity(android.view.Gravity.CENTER_VERTICAL);
                row.setPadding(0, dp(10), 0, dp(10));

                TextView name = new TextView(context);
                name.setText(folderLabel(folderUri));
                name.setTextSize(14f);
                name.setSingleLine(true);
                name.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
                row.addView(name, new android.widget.LinearLayout.LayoutParams(
                        0, android.view.ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

                TextView remove = new TextView(context);
                remove.setText("Quitar");
                remove.setTextSize(13f);
                remove.setPadding(dp(12), dp(6), 0, dp(6));
                remove.setTextColor(context.getColor(R.color.cenit_neon_blue));
                remove.setOnClickListener(v -> {
                    if (dialogRef[0] != null) dialogRef[0].dismiss();
                    host.onRemoveGamesFolder(folderUri);
                    mainHandler.post(this::showFoldersDialog);
                });
                row.addView(remove);

                box.addView(row);
            }
        }

        dialogRef[0] = new com.google.android.material.dialog.MaterialAlertDialogBuilder(context,
                com.google.android.material.R.style.ThemeOverlay_Material3_MaterialAlertDialog)
                .setTitle("Carpetas de juegos")
                .setView(box)
                .setPositiveButton("Añadir carpeta", (d, w) -> host.onAddGamesFolder())
                .setNegativeButton("Cerrar", null)
                .create();
        dialogRef[0].show();
    }

    private String folderLabel(String folderUri) {
        try {
            android.net.Uri uri = android.net.Uri.parse(folderUri);
            androidx.documentfile.provider.DocumentFile dir =
                    androidx.documentfile.provider.DocumentFile.fromTreeUri(context, uri);
            if (dir != null && dir.getName() != null && !dir.getName().isEmpty()) {
                return dir.getName();
            }
            String last = uri.getLastPathSegment();
            if (last != null) {
                last = android.net.Uri.decode(last);
                int colon = last.indexOf(':');
                return colon >= 0 && colon + 1 < last.length() ? last.substring(colon + 1) : last;
            }
        } catch (Throwable ignored) {}
        return folderUri;
    }

    /** Estado de configuración: qué falta antes de poder jugar. */
    public void submitStatus(boolean hasBios, boolean hasDataFolder, boolean hasGamesFolder) {
        List<String> missing = new ArrayList<>();
        if (!hasBios) missing.add("la BIOS");
        if (!hasDataFolder) missing.add("la carpeta de datos");
        if (!hasGamesFolder) missing.add("una carpeta de juegos");

        if (missing.isEmpty()) {
            statusWanted = false;
            if (statusCard != null) statusCard.setVisibility(View.GONE);
        } else {
            statusWanted = true;
            if (statusCard != null) {
                statusCard.setVisibility(currentTab == TAB_LIBRARY ? View.GONE : View.VISIBLE);
            }
            if (statusText != null) statusText.setText("Falta " + join(missing));
            if (statusAction != null) statusAction.setVisibility(View.VISIBLE);
        }
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
