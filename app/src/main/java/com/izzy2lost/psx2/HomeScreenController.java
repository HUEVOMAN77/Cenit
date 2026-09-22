package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.bumptech.glide.load.engine.DiskCacheStrategy;
import com.google.android.material.imageview.ShapeableImageView;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Pantalla de inicio real: cabecera, "continuar jugando" y biblioteca con carátulas.
 * Existe porque la app usaba el surface del juego como pantalla raíz, así que al
 * abrir mostraba mandos táctiles sobre un lienzo negro en lugar de una interfaz.
 *
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
    }

    private final Context context;
    private final Host host;
    private final List<HomeGameAdapter.Entry> entries = new ArrayList<>();
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

    private View root;
    private View continueSection;
    private ShapeableImageView continueCover;
    private TextView continueTitle;
    private TextView continueSubtitle;
    private TextView statusText;
    private View statusAction;
    private View emptyState;
    private TextView emptyText;
    private View emptyAction;
    private TextView libraryTitle;

    @Nullable private String lastPlayedUri;
    @Nullable private String lastPlayedTitle;
    @Nullable private String lastPlayedCoverPath;
    @Nullable private String lastPlayedCoverUrl;

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
        continueSection = root.findViewById(R.id.home_continue_section);
        continueCover = root.findViewById(R.id.home_continue_cover);
        continueTitle = root.findViewById(R.id.home_continue_title);
        continueSubtitle = root.findViewById(R.id.home_continue_subtitle);
        statusText = root.findViewById(R.id.home_status_text);
        statusAction = root.findViewById(R.id.btn_home_setup);
        emptyState = root.findViewById(R.id.home_empty);
        emptyText = root.findViewById(R.id.home_empty_text);
        emptyAction = root.findViewById(R.id.btn_home_empty_action);
        libraryTitle = root.findViewById(R.id.home_library_title);

        View grid = root.findViewById(R.id.home_grid);
        if (grid instanceof RecyclerView rv) {
            rv.setHasFixedSize(true);
            rv.setLayoutManager(new GridLayoutManager(context, spanCount()));
            adapter = new HomeGameAdapter(context, entries, new HomeGameAdapter.Callback() {
                @Override public void onGameClick(int position) {
                    if (position < 0 || position >= entries.size()) return;
                    host.onPlayGame(entries.get(position).uri);
                }

                @Override public void onGameLongClick(int position) {
                    if (position < 0 || position >= entries.size()) return;
                    HomeGameAdapter.Entry e = entries.get(position);
                    host.onGameLongPress(e.title, e.uri);
                }
            });
            rv.setAdapter(adapter);
        }

        View menu = root.findViewById(R.id.btn_home_menu);
        if (menu != null) menu.setOnClickListener(v -> host.onOpenSettings());
        if (statusAction != null) statusAction.setOnClickListener(v -> host.onOpenSetup());
        View add = root.findViewById(R.id.btn_home_add_folder);
        if (add != null) add.setOnClickListener(v -> host.onAddGamesFolder());
        if (emptyAction != null) emptyAction.setOnClickListener(v -> host.onAddGamesFolder());
        View play = root.findViewById(R.id.btn_home_play);
        if (play != null) play.setOnClickListener(v -> {
            if (!TextUtils.isEmpty(lastPlayedUri)) host.onPlayGame(lastPlayedUri);
        });
        View playCard = root.findViewById(R.id.home_continue_card);
        if (playCard != null) playCard.setOnClickListener(v -> {
            if (!TextUtils.isEmpty(lastPlayedUri)) host.onPlayGame(lastPlayedUri);
        });
    }

    private int spanCount() {
        int widthDp = (int) (context.getResources().getDisplayMetrics().widthPixels
                / context.getResources().getDisplayMetrics().density);
        if (widthDp >= 720) return 5;
        if (widthDp >= 480) return 4;
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
    private void paintEntranceIfNeeded() {
        if (entranceBlocked || !animateNextLibrary || !isVisible() || entries.isEmpty()) return;
        animateNextLibrary = false;
        animateHeader();
        if (adapter != null) adapter.startEntranceAnimation();
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
        rise(root.findViewById(R.id.home_status_card), 210);
        rise(root.findViewById(R.id.home_continue_section), 260);
        rise(root.findViewById(R.id.home_library_header), 320);
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
        root.setPadding(left, 0, right, bottom);
        View header = root.findViewById(R.id.home_header);
        if (header != null) header.setPadding(header.getPaddingLeft(), top + dp(14),
                header.getPaddingRight(), header.getPaddingBottom());
    }

    private int dp(int d) {
        return (int) (d * context.getResources().getDisplayMetrics().density + 0.5f);
    }

    /**
     * Publica la biblioteca. Resolver carátulas toca SAF y el disco, así que se hace
     * fuera del hilo de UI y se publica el resultado al terminar.
     */
    public void submitLibrary(String[] names, String[] uris, boolean scanning) {
        renderEmpty(scanning || names == null || uris == null || names.length == 0);
        final String[] nameSnapshot = names == null ? new String[0] : names;
        final String[] uriSnapshot = uris == null ? new String[0] : uris;
        final long token = ++libraryToken;
        LIBRARY_EXECUTOR.execute(() -> {
            List<HomeGameAdapter.Entry> built = new ArrayList<>();
            String[] newest = null; // {uri, title, coverPath, coverUrl}
            long newestAt = 0L;
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

                    long played = prefs.getLong("last_played:" + e.uri, 0L);
                    if (played > newestAt) {
                        newestAt = played;
                        newest = new String[]{e.uri, e.title, e.coverPath, e.coverUrl};
                    }
                }
            } catch (Throwable error) {
                android.util.Log.w("HomeScreen", "Unable to resolve covers", error);
            }
            final List<HomeGameAdapter.Entry> finalEntries = built;
            final String[] finalNewest = newest;
            mainHandler.post(() -> {
                if (token != libraryToken) return; // una publicación más nueva ya ganó
                applyLibrary(finalEntries, finalNewest);
            });
        });
    }

    private void applyLibrary(List<HomeGameAdapter.Entry> built, @Nullable String[] newest) {
        entries.clear();
        entries.addAll(built);
        lastPlayedUri = newest != null ? newest[0] : null;
        lastPlayedTitle = newest != null ? newest[1] : null;
        lastPlayedCoverPath = newest != null ? newest[2] : null;
        lastPlayedCoverUrl = newest != null ? newest[3] : null;

        if (libraryTitle != null) {
            libraryTitle.setText(entries.isEmpty() ? "Biblioteca" : "Biblioteca · " + entries.size());
        }
        if (adapter != null) adapter.notifyDataSetChanged();
        paintEntranceIfNeeded();
        renderContinue();
        renderEmpty(false);
    }

    private void renderContinue() {
        if (continueSection == null) return;
        if (TextUtils.isEmpty(lastPlayedUri)) {
            continueSection.setVisibility(View.GONE);
            return;
        }
        continueSection.setVisibility(View.VISIBLE);
        if (continueTitle != null) continueTitle.setText(lastPlayedTitle);
        if (continueSubtitle != null) continueSubtitle.setText("Toca para seguir donde quedaste");
        if (continueCover != null) {
            Glide.with(context)
                    .load(TextUtils.isEmpty(lastPlayedCoverPath) ? lastPlayedCoverUrl : lastPlayedCoverPath)
                    .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC)
                    .centerCrop()
                    .placeholder(R.drawable.bg_control_cluster)
                    .error(R.drawable.bg_control_cluster)
                    .into(continueCover);
        }
    }

    private void renderEmpty(boolean scanning) {
        if (emptyState == null) return;
        if (!entries.isEmpty()) {
            emptyState.setVisibility(View.GONE);
            return;
        }
        emptyState.setVisibility(View.VISIBLE);
        boolean hasFolders = !GameFolders.list(context).isEmpty();
        if (scanning) {
            emptyText.setText("Buscando tus juegos…");
            emptyAction.setVisibility(View.GONE);
        } else if (!hasFolders) {
            emptyText.setText("Todavía no elegiste una carpeta. Señala dónde guardas tus copias de PS2 (ISO, CHD o CSO).");
            emptyAction.setVisibility(View.VISIBLE);
        } else {
            emptyText.setText("No encontramos juegos en tus carpetas. Revisa que tengan formato de PS2 o añade otra carpeta.");
            emptyAction.setVisibility(View.VISIBLE);
        }
    }

    /** Estado de configuración: qué falta antes de poder jugar. */
    public void submitStatus(boolean hasBios, boolean hasDataFolder, boolean hasGamesFolder) {
        List<String> missing = new ArrayList<>();
        if (!hasBios) missing.add("la BIOS");
        if (!hasDataFolder) missing.add("la carpeta de datos");
        if (!hasGamesFolder) missing.add("una carpeta de juegos");

        if (missing.isEmpty()) {
            if (statusText != null) statusText.setText("Todo listo para jugar");
            if (statusAction != null) statusAction.setVisibility(View.GONE);
        } else {
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
