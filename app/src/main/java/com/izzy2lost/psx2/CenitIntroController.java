package com.izzy2lost.psx2;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.Interpolator;
import android.widget.FrameLayout;

/**
 * Cortinilla de presentación: logo que se revela con luz y escala, nombre con
 * espaciado abierto y luego fundido hacia la interfaz. Se puede saltar tocando.
 *
 * Se apoya en android.R.id.content, así que cubre toda la ventana sin tocar el layout.
 */
public final class CenitIntroController {

    public interface FinishListener {
        void onIntroFinished();
    }

    // Curva "expo out": arranca rápido y se frena con suavidad.
    private static final Interpolator EASE_OUT_EXPONENT =
            new android.view.animation.PathInterpolator(0.16f, 1f, 0.3f, 1f);

    private final android.os.Handler handler = new android.os.Handler(android.os.Looper.getMainLooper());
    private View overlay;
    private boolean dismissed;

    /** Muestra la intro si corresponde y devuelve si se mostró. */
    public boolean show(Context context, boolean playNow, FinishListener onFinish) {
        if (!playNow) return false;
        ViewGroup content = context instanceof android.app.Activity
                ? ((android.app.Activity) context).findViewById(android.R.id.content) : null;
        if (!(content instanceof FrameLayout)) return false;
        dismissed = false;
        overlay = LayoutInflater.from(context).inflate(R.layout.view_cenit_intro, content, false);
        overlay.setAlpha(1f);
        content.addView(overlay, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        View logo = overlay.findViewById(R.id.intro_logo);
        View wordmark = overlay.findViewById(R.id.intro_wordmark);
        View tagline = overlay.findViewById(R.id.intro_tagline);
        View line = overlay.findViewById(R.id.intro_line);
        View skip = overlay.findViewById(R.id.intro_skip);

        Interpolator ease = EASE_OUT_EXPONENT;

        logo.setAlpha(0f);
        logo.setScaleX(0.72f);
        logo.setScaleY(0.72f);
        logo.setRotation(-12f);
        wordmark.setAlpha(0f);
        wordmark.setTranslationY(dp(context, 14));
        tagline.setAlpha(0f);
        line.setScaleX(0f);
        line.setAlpha(0f);
        skip.setAlpha(0f);

        logo.animate().alpha(1f).scaleX(1f).scaleY(1f).rotation(0f)
                .setStartDelay(120).setDuration(900).setInterpolator(ease).start();
        wordmark.animate().alpha(1f).translationY(0f)
                .setStartDelay(520).setDuration(700).setInterpolator(ease).start();
        tagline.animate().alpha(0.72f)
                .setStartDelay(820).setDuration(600).setInterpolator(ease).start();
        line.animate().alpha(1f).scaleX(1f)
                .setStartDelay(900).setDuration(600).setInterpolator(ease).start();
        skip.animate().alpha(0.4f)
                .setStartDelay(1150).setDuration(400).start();

        overlay.setOnClickListener(v -> hide(onFinish));
        // ~2 s de coreografía + 360 ms de fundido; un toque la corta en cualquier momento.
        handler.postDelayed(() -> hide(onFinish), 2000L);
        return true;
    }

    public void hide(FinishListener onFinish) {
        if (dismissed || overlay == null) return;
        dismissed = true;
        handler.removeCallbacksAndMessages(null);
        final View closing = overlay;
        overlay = null;
        closing.animate().alpha(0f).setDuration(360L)
                .withEndAction(() -> {
                    try {
                        ViewGroup parent = (ViewGroup) closing.getParent();
                        if (parent != null) parent.removeView(closing);
                    } catch (Throwable ignored) {}
                    if (onFinish != null) onFinish.onIntroFinished();
                })
                .start();
    }

    public boolean isShowing() {
        return !dismissed && overlay != null;
    }

    /** Quita la cortinilla sin animación ni callback; para onDestroy(). */
    public void cancel() {
        dismissed = true;
        handler.removeCallbacksAndMessages(null);
        if (overlay != null) {
            View stale = overlay;
            overlay = null;
            ViewGroup parent = (ViewGroup) stale.getParent();
            if (parent != null) parent.removeView(stale);
        }
    }

    private static float dp(Context context, int value) {
        return value * context.getResources().getDisplayMetrics().density;
    }
}
