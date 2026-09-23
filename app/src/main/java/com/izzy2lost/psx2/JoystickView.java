package com.izzy2lost.psx2;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import androidx.core.content.ContextCompat;

public class JoystickView extends View {
    public interface OnMoveListener {
        void onMove(float nx, float ny, int action);
    }

    private final Paint basePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint innerRingPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint outerRingPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobRimPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float centerX, centerY, radius, knobX, knobY, knobRadius;
    private boolean isDragging = false;
    private int activePointerId = MotionEvent.INVALID_POINTER_ID;
    private OnMoveListener listener;

    // Textura de puntos de la perla: un bitmap pequeño que se repite sobre el
    // degradado (grip estilo DualShock).
    private Paint gripPaint;

    public JoystickView(Context ctx) { super(ctx); init(); }
    public JoystickView(Context ctx, AttributeSet attrs) { super(ctx, attrs); init(); }
    public JoystickView(Context ctx, AttributeSet attrs, int defStyle) { super(ctx, attrs, defStyle); init(); }

    private void init() {
        int baseFill = ContextCompat.getColor(getContext(), R.color.overlay_stick_base);
        int ring = ContextCompat.getColor(getContext(), R.color.overlay_stick_ring);
        int ringActive = ContextCompat.getColor(getContext(), R.color.overlay_stick_ring_active);
        int knobHi = ContextCompat.getColor(getContext(), R.color.overlay_stick_knob_hi);
        int knobLo = ContextCompat.getColor(getContext(), R.color.overlay_stick_knob_lo);

        basePaint.setColor(baseFill);
        basePaint.setStyle(Paint.Style.FILL);

        // Anillo interno fino (el "cráter" alrededor de la perla)
        innerRingPaint.setColor(ring);
        innerRingPaint.setStyle(Paint.Style.STROKE);
        innerRingPaint.setStrokeWidth(dp(1));
        innerRingPaint.setAlpha(120);

        // Anillo exterior: el aro azul/violeta que se ve en reposo
        outerRingPaint.setColor(ring);
        outerRingPaint.setStyle(Paint.Style.STROKE);
        outerRingPaint.setStrokeWidth(dp(2.5f));

        knobPaint.setStyle(Paint.Style.FILL);
        knobRimPaint.setStyle(Paint.Style.STROKE);
        knobRimPaint.setStrokeWidth(dp(1));
        knobRimPaint.setColor(ringActive);

        gripPaint = new Paint();
        gripPaint.setShader(new android.graphics.BitmapShader(
                makeGripBitmap(), android.graphics.Shader.TileMode.REPEAT));

        // El shader del degradado de la perla se crea en onSizeChanged (necesita radios).
        this.knobHi = knobHi;
        this.knobLo = knobLo;
        setClickable(true);
    }

    private int knobHi, knobLo;

    private static Bitmap makeGripBitmap() {
        // Rejilla de puntos finos sobre fondo transparente, 12x12 px @hdpi-ish.
        int size = 14;
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bmp);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(Color.argb(26, 255, 255, 255));
        float r = size * 0.09f;
        c.drawCircle(size * 0.25f, size * 0.25f, r, p);
        c.drawCircle(size * 0.75f, size * 0.25f, r, p);
        c.drawCircle(size * 0.50f, size * 0.50f, r, p);
        c.drawCircle(size * 0.25f, size * 0.75f, r, p);
        c.drawCircle(size * 0.75f, size * 0.75f, r, p);
        return bmp;
    }

    public void setOnMoveListener(OnMoveListener l) { this.listener = l; }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        centerX = w / 2f;
        centerY = h / 2f;
        // Make the visible base circle smaller relative to the view size
        radius = Math.min(w, h) * 0.36f;
        knobRadius = radius * 0.46f;
        // Degradado de la perla: luz arriba-izquierda, sombra abajo-derecha.
        knobPaint.setShader(new RadialGradient(
                centerX - knobRadius * 0.35f, centerY - knobRadius * 0.45f, knobRadius * 1.9f,
                new int[]{knobHi, knobLo, Color.argb(230, 4, 6, 10)},
                new float[]{0f, 0.65f, 1f},
                Shader.TileMode.CLAMP));
        resetKnob();
    }

    private void resetKnob() {
        knobX = centerX;
        knobY = centerY;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        // Base del cráter
        canvas.drawCircle(centerX, centerY, radius, basePaint);
        // Anillo exterior (aro)
        canvas.drawCircle(centerX, centerY, radius, outerRingPaint);
        // Anillo interior alrededor de la perla
        canvas.drawCircle(centerX, centerY, radius * 0.78f, innerRingPaint);
        // Perla: degradado + textura de puntos + borde iluminado
        canvas.drawCircle(knobX, knobY, knobRadius, knobPaint);
        if (gripPaint != null) {
            canvas.save();
            android.graphics.Path clip = new android.graphics.Path();
            clip.addCircle(knobX, knobY, knobRadius, android.graphics.Path.Direction.CW);
            canvas.clipPath(clip);
            canvas.drawCircle(knobX, knobY, knobRadius, gripPaint);
            canvas.restore();
        }
        canvas.drawCircle(knobX, knobY, knobRadius, knobRimPaint);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int actionIndex = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                isDragging = true;
                activePointerId = event.getPointerId(0);
                outerRingPaint.setColor(ContextCompat.getColor(getContext(), R.color.overlay_stick_ring_active));
                invalidate();
                // fallthrough to move
            case MotionEvent.ACTION_MOVE:
                if (isDragging) {
                    int pointerIndex = 0;
                    if (activePointerId != MotionEvent.INVALID_POINTER_ID) {
                        int idx = event.findPointerIndex(activePointerId);
                        if (idx >= 0) pointerIndex = idx;
                    }
                    updateFromPointer(event, pointerIndex);
                }
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                // Ignore additional pointers while dragging; if we aren't dragging, adopt the new pointer.
                if (!isDragging) {
                    isDragging = true;
                    activePointerId = event.getPointerId(actionIndex);
                    updateFromPointer(event, actionIndex);
                    return true;
                }
                return true;
            case MotionEvent.ACTION_POINTER_UP:
                // If the active pointer goes up, release.
                if (event.getPointerId(actionIndex) == activePointerId) {
                    releaseDrag();
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                releaseDrag();
                return true;
        }
        return super.onTouchEvent(event);
    }

    private void releaseDrag() {
        activePointerId = MotionEvent.INVALID_POINTER_ID;
        isDragging = false;
        outerRingPaint.setColor(ContextCompat.getColor(getContext(), R.color.overlay_stick_ring));
        resetKnob();
        if (listener != null) listener.onMove(0f, 0f, MotionEvent.ACTION_UP);
    }

    private void updateFromPointer(MotionEvent event, int pointerIndex) {
        float dx = event.getX(pointerIndex) - centerX;
        float dy = event.getY(pointerIndex) - centerY;
        // Clamp to circle
        float dist = (float) Math.hypot(dx, dy);
        if (dist > radius) {
            float scale = radius / dist;
            dx *= scale;
            dy *= scale;
        }
        knobX = centerX + dx;
        knobY = centerY + dy;
        invalidate();
        if (listener != null) {
            float nx = dx / radius;
            float ny = dy / radius;
            listener.onMove(clamp(nx), clamp(ny), MotionEvent.ACTION_MOVE);
        }
    }

    private static float clamp(float v) { return Math.max(-1f, Math.min(1f, v)); }

    private float dp(float d) {
        return d * getResources().getDisplayMetrics().density;
    }
}
