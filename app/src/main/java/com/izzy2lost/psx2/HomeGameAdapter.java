package com.izzy2lost.psx2;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.recyclerview.widget.RecyclerView;

import com.bumptech.glide.Glide;
import com.bumptech.glide.RequestBuilder;
import com.bumptech.glide.load.engine.DiskCacheStrategy;
import com.google.android.material.imageview.ShapeableImageView;

import java.io.File;
import java.util.List;

/** Cuadrícula de la pantalla de inicio. No repite ítems: la biblioteca es finita. */
public class HomeGameAdapter extends RecyclerView.Adapter<HomeGameAdapter.VH> {

    public interface Callback {
        void onGameClick(int position);
        void onGameLongClick(int position);
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
    private final List<Entry> entries;
    private final Callback callback;
    private final int duration;
    private final int stagger;
    private final float riseDp;

    // Marca temporal de la última carga completa: al refrescar la biblioteca las
    // tarjetas entran escalonadas, pero al reciclearse durante el scroll no.
    private long animationBatchId = -1L;

    public HomeGameAdapter(Context context, List<Entry> entries, Callback callback) {
        this.context = context;
        this.entries = entries;
        this.callback = callback;
        float density = context.getResources().getDisplayMetrics().density;
        duration = (int) (340 * density);
        stagger = (int) (45 * density);
        riseDp = 18f * density;
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
        Entry entry = entries.get(position);
        holder.title.setText(entry.title);
        loadImage(entry.coverPath, entry.coverUrl, holder.cover);

        holder.itemView.setOnClickListener(v -> {
            int pos = holder.getBindingAdapterPosition();
            if (pos != RecyclerView.NO_POSITION && callback != null) callback.onGameClick(pos);
        });
        holder.itemView.setOnLongClickListener(v -> {
            int pos = holder.getBindingAdapterPosition();
            if (pos != RecyclerView.NO_POSITION && callback != null) {
                callback.onGameLongClick(pos);
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
        return entries.size();
    }

    static class VH extends RecyclerView.ViewHolder {
        final ShapeableImageView cover;
        final TextView title;

        VH(@NonNull View itemView) {
            super(itemView);
            cover = itemView.findViewById(R.id.image_cover);
            title = itemView.findViewById(R.id.text_title);
        }
    }
}
