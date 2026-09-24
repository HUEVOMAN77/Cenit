package com.izzy2lost.psx2;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.bumptech.glide.RequestBuilder;
import com.bumptech.glide.load.engine.DiskCacheStrategy;
import com.google.android.material.imageview.ShapeableImageView;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/** Cuadrícula de la pantalla de inicio. No repite ítems: la biblioteca es finita. */
public class HomeGameAdapter extends RecyclerView.Adapter<HomeGameAdapter.VH> {

    public interface Callback {
        void onGameClick(Entry entry);
        void onGameLongClick(Entry entry);
    }

    /** Entrada de la biblioteca tal y como la resuelve el escáner de carátulas. */
    public static class Entry {
        public final String title;
        public final String uri;
        public String coverPath;   // archivo local o content://
        public String coverUrl;    // URL remota de respaldo

        Entry(String title, String uri) {
            this.title = title;
            this.uri = uri;
        }
    }

    private final Context context;
    // La lista maestra es la biblioteca completa; "shown" es lo que pinta la
    // cuadrícula tras aplicar el filtro de búsqueda.
    private final List<Entry> master = new ArrayList<>();
    private final List<Entry> shown = new ArrayList<>();
    private String query = "";
    private final Callback callback;
    private final int duration;
    private final int stagger;
    private final float riseDp;

    // Marca temporal de la última carga completa: al refrescar la biblioteca las
    // tarjetas entran escalonadas, pero al reciclearse durante el scroll no.
    private long animationBatchId = -1L;

    public HomeGameAdapter(Context context, Callback callback) {
        this.context = context;
        this.callback = callback;
        float density = context.getResources().getDisplayMetrics().density;
        duration = (int) (340 * density);
        stagger = (int) (45 * density);
        riseDp = 18f * density;
    }

    /** Reemplaza la biblioteca completa y repinta respetando la búsqueda activa. */
    public void setEntries(@Nullable List<Entry> entries) {
        master.clear();
        if (entries != null) master.addAll(entries);
        rebuildShown();
        notifyDataSetChanged();
    }

    /** Filtra por título. Cadena vacía o nula muestra todo. */
    public void setFilter(@Nullable String text) {
        String next = text == null ? "" : text.trim().toLowerCase(java.util.Locale.ROOT);
        if (next.equals(query)) return;
        query = next;
        rebuildShown();
        notifyDataSetChanged();
    }

    private void rebuildShown() {
        shown.clear();
        if (query.isEmpty()) {
            shown.addAll(master);
            return;
        }
        for (Entry e : master) {
            if (e.title != null && e.title.toLowerCase(java.util.Locale.ROOT).contains(query)) {
                shown.add(e);
            }
        }
    }

    /**
     * Marca una nueva tanda de datos: las tarjetas que se creen a partir de ahora
     * entran escalonadas. Se usa notifyDataSetChanged porque la lista cambia completa.
     */
    public void startEntranceAnimation() {
        animationBatchId = System.currentTimeMillis();
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.item_home_game, parent, false);
        return new VH(v);
    }

    @Override
    public void onBindViewHolder(@NonNull VH holder, int position) {
        Entry entry = shown.get(position);
        loadImage(entry.coverPath, entry.coverUrl, holder.cover);
        holder.title.setText(displayTitle(entry.title));

        holder.itemView.setOnClickListener(v -> {
            int pos = holder.getBindingAdapterPosition();
            if (pos != RecyclerView.NO_POSITION && pos < shown.size() && callback != null) {
                callback.onGameClick(shown.get(pos));
            }
        });
        holder.itemView.setOnLongClickListener(v -> {
            int pos = holder.getBindingAdapterPosition();
            if (pos != RecyclerView.NO_POSITION && pos < shown.size() && callback != null) {
                callback.onGameLongClick(shown.get(pos));
                return true;
            }
            return false;
        });

        Object tagged = holder.itemView.getTag(R.id.home_entrance_tag);
        boolean alreadyPlayed = tagged instanceof Long && (Long) tagged == animationBatchId;
        if (!alreadyPlayed && animationBatchId >= 0) {
            holder.itemView.setTag(R.id.home_entrance_tag, animationBatchId);
            playEntrance(holder.itemView, position);
        }
    }

    /** Aparición suave: cada tarjeta sube un poco con retardo escalonado. */
    private void playEntrance(View itemView, int position) {
        int delay = Math.min(position, 14) * stagger;
        itemView.setAlpha(0f);
        itemView.setTranslationY(riseDp);
        itemView.setScaleX(0.94f);
        itemView.setScaleY(0.94f);
        itemView.animate()
                .alpha(1f)
                .translationY(0f)
                .scaleX(1f)
                .scaleY(1f)
                .setStartDelay(delay)
                .setDuration(duration)
                .setInterpolator(INTERPOLATOR)
                .start();
    }

    private static final android.view.animation.Interpolator INTERPOLATOR =
            new android.view.animation.PathInterpolator(0.16f, 1f, 0.3f, 1f);

    private void loadImage(@Nullable String localPath, @Nullable String remoteUrl, ShapeableImageView target) {
        RequestBuilder<Drawable> placeholder = Glide.with(context)
                .load("file:///android_asset/resources/no-cover.png")
                .diskCacheStrategy(DiskCacheStrategy.RESOURCE)
                .fitCenter();

        if (localPath != null && localPath.startsWith("content://")) {
            Glide.with(context)
                    .load(android.net.Uri.parse(localPath))
                    .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC)
                    .fitCenter()
                    .thumbnail(placeholder)
                    .error(placeholder)
                    .into(target);
            return;
        }
        if (localPath != null) {
            File f = new File(localPath);
            if (f.exists() && f.length() > 0) {
                Glide.with(context)
                        .load(f)
                        .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC)
                        .fitCenter()
                        .thumbnail(placeholder)
                        .error(placeholder)
                        .into(target);
                return;
            }
        }
        if (remoteUrl != null && !remoteUrl.isEmpty()) {
            Glide.with(context)
                    .load(remoteUrl)
                    .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC)
                    .fitCenter()
                    .thumbnail(placeholder)
                    .error(placeholder)
                    .into(target);
            return;
        }
        placeholder.into(target);
    }

    @Override
    public int getItemCount() {
        return shown.size();
    }

    /**
     * Convierte lo que trae el escáner (a veces el nombre del archivo tal cual)
     * en un título presentable debajo de la carátula: quita extensiones y
     * capítulos, puntos y guiones bajos, y el código de región entre paréntesis.
     * "Shadow.of.the.Colossus.(USA).ch1.iso" -> "Shadow of the Colossus".
     */
    static String displayTitle(String raw) {
        if (raw == null || raw.isEmpty()) return "—";
        String s = raw;
        int q = s.indexOf('?');
        if (q > 0) s = s.substring(0, q);            // por si llega una URI sin decodificar
        s = s.replaceAll("(?i)\\.(iso|bin|img|mdf|chd|cso|gz|zip|dump|cue|nrg|pbp)$", "");
        s = s.replaceAll("(?i)(\\.?\\s*ch\\.?\\d+)+$", ""); // dumps multicapítulo: .ch1.ch2.ch3
        s = s.replaceAll("(?i)\\(\\s*(usa|europe|pal|ntsc[ -]?[uj]?|japan|jap|us|eu)([^)]*)\\)", "");
        s = s.replaceAll("(?i)\\[[^]]*(read ?n?lock|best|fixed|scr[ée]?nshot)[^]]*\\]", "");
        s = s.replaceAll("[._]+", " ");
        s = s.replaceAll("\\s{2,}", " ").trim();
        if (s.isEmpty()) s = raw;
        return s;
    }

    static class VH extends RecyclerView.ViewHolder {
        final ShapeableImageView cover;
        final android.widget.TextView title;

        VH(@NonNull View itemView) {
            super(itemView);
            cover = itemView.findViewById(R.id.image_cover);
            title = itemView.findViewById(R.id.game_title);
        }
    }
}
