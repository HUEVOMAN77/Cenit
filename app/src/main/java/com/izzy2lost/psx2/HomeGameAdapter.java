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

    public HomeGameAdapter(Context context, List<Entry> entries, Callback callback) {
        this.context = context;
        this.entries = entries;
        this.callback = callback;
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
    }

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
