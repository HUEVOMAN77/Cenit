package com.izzy2lost.psx2;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.animation.LinearInterpolator;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.widget.AppCompatTextView;

/**
 * Texto de Cenit con degradado neón en movimiento (cian → azul → violeta).
 *
 * El degradado se pinta con TileMode.REPEAT sobre un ancho fijo y se desplaza con
 * una matriz: al recorrer exactamente un periodo el patrón vuelve a su origen, así
 * que el bucle es continuo y no se nota el salto. Se anima solo mientras la vista
 * está visible y adjunta a la ventana, para no gastar batería en segundo plano.
 */
public class NeonGradientTextView extends AppCompatTextView {

    /** Periodo del degradado en múltiplos del ancho del texto. */
    private static final float SPAN_FACTOR = 1.4f;
    private static final long SWEEP_DURATION_MS = 3200L;

    private static final int[] NEON_COLORS = {
            0xFF39E6FF, // cian cenit
            0xFF22A7FF, // azul neón
            0xFF6C7BFF, // índigo
            0xFF9A5CFF, // violeta
            0xFF22A7FF, // vuelta al azul
            0xFF39E6FF, // cierre igual al inicio (bucle sin salto)
    };
    private static final float[] NEON_STOPS = {0f, 0.22f, 0.45f, 0.62f, 0.82f, 1f};

    private final Matrix shaderMatrix = new Matrix();
    private LinearGradient gradient;
    private float spanPx = 0f;
    private float offsetPx = 0f;
    private ValueAnimator sweep;
    private boolean effectEnabled = true;

    public NeonGradientTextView(@NonNull Context context) {
        super(context);
        init();
    }

    public NeonGradientTextView(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public NeonGradientTextView(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        // Sombra de color: sin esto el degradado se ve plano sobre el negro.
        setShadowLayer(14f * getResources().getDisplayMetrics().density, 0f, 0f, 0x5939E6FF);
        setLayerType(LAYER_TYPE_SOFTWARE, null);
    }

    /** Permite apagar el efecto (por ejemplo para la versión sin animación). */
    public void setNeonEffectEnabled(boolean enabled) {
        if (effectEnabled == enabled) return;
        effectEnabled = enabled;
        if (!enabled) stopSweep();
        getPaint().setShader(null);
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        gradient = null; // el span depende del ancho, se reconstruye al dibujar
    }

    @Override
    protected void onDraw(@NonNull android.graphics.Canvas canvas) {
        if (effectEnabled && getWidth() > 0) {
            if (gradient == null) {
                spanPx = Math.max(1f, getWidth() * SPAN_FACTOR);
                gradient = new LinearGradient(0f, 0f, spanPx, 0f,
                        NEON_COLORS, NEON_STOPS, Shader.TileMode.REPEAT);
            }
            shaderMatrix.setTranslate(offsetPx - spanPx, 0f);
            gradient.setLocalMatrix(shaderMatrix);
            getPaint().setShader(gradient);
        } else {
            getPaint().setShader(null);
        }
        super.onDraw(canvas);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        startSweep();
    }

    @Override
    protected void onDetachedFromWindow() {
        stopSweep();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onVisibilityChanged(@NonNull android.view.View changedView, int visibility) {
        super.onVisibilityChanged(changedView, visibility);
        if (visibility == VISIBLE) startSweep(); else stopSweep();
    }

    private void startSweep() {
        if (!effectEnabled || !isAttachedToWindow() || getVisibility() != VISIBLE) return;
        if (sweep != null && sweep.isRunning()) return;
        if (sweep == null) {
            sweep = ValueAnimator.ofFloat(0f, 1f);
            sweep.setDuration(SWEEP_DURATION_MS);
            sweep.setRepeatCount(ValueAnimator.INFINITE);
            sweep.setInterpolator(new LinearInterpolator());
            sweep.addUpdateListener(a -> {
                // Un periodo exacto por ciclo => transición sin costura.
                offsetPx = spanPx * (float) a.getAnimatedValue();
                invalidate();
            });
        }
        sweep.start();
    }

    private void stopSweep() {
        if (sweep != null) {
            sweep.cancel();
            sweep = null;
        }
    }
}
