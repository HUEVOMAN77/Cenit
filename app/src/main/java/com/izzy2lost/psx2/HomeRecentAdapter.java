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

/**
 * Carrusel horizontal "Siguiendo donde lo dejaste". No es una biblioteca aparte:
 * son las mismas entradas de la rejilla, filtradas a los juegos abiertos en los
 * últimos siete días y ordenadas de más reciente a más antiguo. Sin historial,
 * la sección entera se oculta — un carrusel vacío no es una función, es ruido.
 */
public class HomeRecentAdapter extends RecyclerView.Adapter<HomeRecentAdapter.VH> {

    public interface Callback {
        void onGameClick(HomeGameAdapter.Entry entry);
    }

    private final Context context;
    private final Callback callback;
    private final List<HomeGameAdapter.Entry> items = new ArrayList<>();

    public HomeRecentAdapter(Context context, Callback callback) {
        this.context = context;
        this.callback = callback;
    }

    public void setItems(@Nullable List<HomeGameAdapter.Entry> entries) {
        items.clear();
        if (entries != null) items.addAll(entries);
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.item_home_recent, parent, false);
        return new VH(v);
    }

    @Override
    public void onBindViewHolder(@NonNull VH holder, int position) {
        HomeGameAdapter.Entry entry = items.get(position);
        holder.title.setText(HomeGameAdapter.displayTitle(entry.title));
        loadImage(entry.coverPath, entry.coverUrl, holder.cover);
        holder.itemView.setOnClickListener(v -> {
            int pos = holder.getBindingAdapterPosition();
            if (pos != RecyclerView.NO_POSITION && pos < items.size() && callback != null) {
                callback.onGameClick(items.get(pos));
            }
        });
    }

    private void loadImage(@Nullable String localPath, @Nullable String remoteUrl,
                           ShapeableImageView target) {
        RequestBuilder<Drawable> placeholder = Glide.with(context)
                .load("file:///android_asset/resources/no-cover.webp")
                .diskCacheStrategy(DiskCacheStrategy.RESOURCE)
                .fitCenter();

        if (localPath != null && localPath.startsWith("content://")) {
            Glide.with(context).load(android.net.Uri.parse(localPath))
                    .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC).fitCenter()
                    .thumbnail(placeholder).error(placeholder).into(target);
            return;
        }
        if (localPath != null) {
            File f = new File(localPath);
            if (f.exists() && f.length() > 0) {
                Glide.with(context).load(f)
                        .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC).fitCenter()
                        .thumbnail(placeholder).error(placeholder).into(target);
                return;
            }
        }
        if (remoteUrl != null && !remoteUrl.isEmpty()) {
            Glide.with(context).load(remoteUrl)
                    .diskCacheStrategy(DiskCacheStrategy.AUTOMATIC).fitCenter()
                    .thumbnail(placeholder).error(placeholder).into(target);
            return;
        }
        placeholder.into(target);
    }

    @Override
    public int getItemCount() {
        return items.size();
    }

    static class VH extends RecyclerView.ViewHolder {
        final ShapeableImageView cover;
        final android.widget.TextView title;

        VH(@NonNull View itemView) {
            super(itemView);
            cover = itemView.findViewById(R.id.recent_cover);
            title = itemView.findViewById(R.id.recent_title);
        }
    }
}
