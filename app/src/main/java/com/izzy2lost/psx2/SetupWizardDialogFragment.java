package com.izzy2lost.psx2;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.app.Dialog;
import android.content.Context;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.DecelerateInterpolator;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.DialogFragment;
import androidx.recyclerview.widget.RecyclerView;
import androidx.viewpager2.widget.ViewPager2;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

public class SetupWizardDialogFragment extends DialogFragment {
    public static SetupWizardDialogFragment newInstance() { return new SetupWizardDialogFragment(); }

    private ViewPager2 pager;
    private MaterialButton btnNext;
    private LinearLayout indicatorContainer;
    private TextView tvStep;
    private TextView tvSubtitle;
    private View progressFill;
    private Runnable periodicCheck;
    private int lastAnimatedPosition = -1;

    private final List<SetupStep> steps = Arrays.asList(
            new SetupStep(StepType.BIOS, R.drawable.memory_24px, "Archivos BIOS", "Importa una BIOS verificada de USA, Europa o Japón. Con una basta; con las tres Cenit ajusta la región de cada juego automáticamente.", "Importar BIOS"),
            new SetupStep(StepType.DATA, R.drawable.data_table_24px, "Carpeta de datos", "Elige una carpeta con permiso de escritura para partidas, estados y configuración.", "Elegir datos"),
            new SetupStep(StepType.GAMES, R.drawable.stadia_controller_24px, "Biblioteca de juegos", "Indícale a Cenit dónde guardas tus juegos para que funcione el orden y las carátulas.", "Elegir juegos")
    );

    private SetupPagerAdapter adapter;

    private enum StepType {
        DATA, GAMES, BIOS
    }

    private static class SetupStep {
        final StepType type;
        final int iconRes;
        final String title;
        final String description;
        final String ctaText;

        SetupStep(StepType t, int iconRes, String title, String description, String cta) {
            this.type = t;
            this.iconRes = iconRes;
            this.title = title;
            this.description = description;
            this.ctaText = cta;
        }
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(@Nullable Bundle savedInstanceState) {
        try {
            if (getActivity() instanceof MainActivity) {
                ((MainActivity) getActivity()).onDialogOpened();
            }
        } catch (Throwable ignored) {}

        Dialog d = new Dialog(requireContext(), R.style.PSX2_FullScreenDialog);
        View content = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_setup_intro, null);
        d.setContentView(content);

        bindViews(content);
        setupPager();
        renderIndicators(0);
        updateHeader(0);
        updateNextButtonState(0);
        updateProgress(0);

        d.setOnDismissListener(dialog -> {
            if (getActivity() instanceof MainActivity) {
                ((MainActivity) getActivity()).onDialogClosed();
            }
        });

        return d;
    }

    @Override
    public void onResume() {
        super.onResume();
        MainActivity activity = UiUtils.getMainActivity(this);
        if (activity != null) activity.setSetupWizardActive(true);
        hideSystemUI();
        refreshAll();
        startPeriodicCheck();
    }

    @Override
    public void onPause() {
        super.onPause();
        stopPeriodicCheck();
        showSystemUI();
    }

    @Override
    public void onDismiss(@NonNull android.content.DialogInterface dialog) {
        super.onDismiss(dialog);
        MainActivity activity = UiUtils.getMainActivity(this);
        if (activity != null) activity.setSetupWizardActive(false);
    }

    private void bindViews(View root) {
        pager = root.findViewById(R.id.setup_pager);
        btnNext = root.findViewById(R.id.btn_next);
        indicatorContainer = root.findViewById(R.id.indicator_container);
        tvStep = root.findViewById(R.id.tv_step);
        tvSubtitle = root.findViewById(R.id.tv_subtitle);
        progressFill = root.findViewById(R.id.progress_fill);

        btnNext.setOnClickListener(v -> handleNextClick());
    }

    private void setupPager() {
        adapter = new SetupPagerAdapter(requireContext(), steps, new SetupPagerAdapter.StepListener() {
            @Override
            public boolean isComplete(StepType type) {
                return isStepComplete(type);
            }

            @Override
            public String getStatusText(StepType type) {
                return getStepStatusText(type);
            }

            @Override
            public void onAction(StepType type) {
                triggerAction(type);
            }
        });
        pager.setAdapter(adapter);
        pager.registerOnPageChangeCallback(new ViewPager2.OnPageChangeCallback() {
            @Override
            public void onPageSelected(int position) {
                super.onPageSelected(position);
                renderIndicators(position);
                updateHeader(position);
                updateNextButtonState(position);
                updateProgress(position);
                animatePageEntry(position);
            }

            @Override
            public void onPageScrolled(int position, float positionOffset, int positionOffsetPixels) {
                super.onPageScrolled(position, positionOffset, positionOffsetPixels);
                // Fade out the exiting page as the new one slides in
                if (pager.getAdapter() != null) {
                    RecyclerView rv = (RecyclerView) pager.getChildAt(0);
                    if (rv != null) {
                        View current = rv.findViewHolderForAdapterPosition(position) != null
                                ? rv.findViewHolderForAdapterPosition(position).itemView : null;
                        int nextPos = position + 1;
                        View next = rv.findViewHolderForAdapterPosition(nextPos) != null
                                ? rv.findViewHolderForAdapterPosition(nextPos).itemView : null;
                        if (current != null) {
                            current.setAlpha(1f - 0.5f * positionOffset);
                            current.setTranslationY(-20f * positionOffset);
                        }
                        if (next != null) {
                            next.setAlpha(0.5f + 0.5f * positionOffset);
                            next.setTranslationY(40f * (1f - positionOffset));
                        }
                    }
                }
            }
        });
    }

    private void animatePageEntry(int position) {
        if (position == lastAnimatedPosition) return;
        lastAnimatedPosition = position;

        RecyclerView rv = (RecyclerView) pager.getChildAt(0);
        if (rv == null) return;
        RecyclerView.ViewHolder vh = rv.findViewHolderForAdapterPosition(position);
        if (vh == null) return;

        final View page = vh.itemView;
        page.setAlpha(0f);
        page.setTranslationY(40f);
        page.animate()
                .alpha(1f)
                .translationY(0f)
                .setDuration(350)
                .setInterpolator(new DecelerateInterpolator(2f))
                .setListener(null)
                .start();
    }

    private void updateProgress(int position) {
        if (progressFill == null) return;
        progressFill.post(() -> {
            int totalWidth = progressFill.getParent() != null ? ((View) progressFill.getParent()).getWidth() : 0;
            if (totalWidth <= 0) return;
            float fraction = (position + 1) / (float) steps.size();
            ViewGroup.LayoutParams lp = progressFill.getLayoutParams();
            lp.width = (int) (totalWidth * fraction);
            progressFill.setLayoutParams(lp);
        });
    }

    private void handleNextClick() {
        int current = pager.getCurrentItem();
        if (current < steps.size() - 1) {
            pager.setCurrentItem(current + 1, true);
            return;
        }
        if (areAllStepsComplete()) {
            animateExitAndComplete();
        } else {
            // Antes el botón se deshabilitaba aquí y el asistente no era cancelable,
            // así que quedabas atrapado en la última página sin poder avanzar.
            showFinishAnywayPrompt(firstIncompleteIndex());
        }
    }

    private void animateExitAndComplete() {
        View dialogView = getDialog() != null ? getDialog().findViewById(android.R.id.content) : null;
        if (dialogView != null) {
            dialogView.animate()
                    .alpha(0f)
                    .setDuration(300)
                    .setInterpolator(new DecelerateInterpolator())
                    .withEndAction(this::completeAndDismiss)
                    .start();
        } else {
            completeAndDismiss();
        }
    }

    private void showFinishAnywayPrompt(int target) {
        MaterialAlertDialogBuilder b = new MaterialAlertDialogBuilder(requireContext());
        b.setTitle("Aún hay pasos pendientes");
        b.setMessage("Puedes terminar ahora. La BIOS se puede importar después desde el menú lateral, y la carpeta de juegos desde la biblioteca.");
        b.setPositiveButton("Terminar de todos modos", (d, w) -> completeAndDismiss());
        b.setNeutralButton("Ir al paso pendiente", (d, w) -> {
            if (target >= 0) pager.setCurrentItem(target, true);
        });
        b.setNegativeButton("Seguir revisando", null);
        b.show();
    }

    private void triggerAction(StepType type) {
        MainActivity a = UiUtils.getMainActivity(this);
        if (a == null) return;
        switch (type) {
            case DATA -> a.pickDataRootFolder();
            case GAMES -> a.pickGamesFolder();
            case BIOS -> a.showBiosPrompt();
        }
    }

    private void renderIndicators(int activeIndex) {
        indicatorContainer.removeAllViews();
        int dotW = (int) (28 * getResources().getDisplayMetrics().density);
        int dotH = (int) (6 * getResources().getDisplayMetrics().density);
        int margin = (int) (4 * getResources().getDisplayMetrics().density);
        for (int i = 0; i < steps.size(); i++) {
            View indicator = new View(requireContext());
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    i == activeIndex ? dotW : dotH, dotH);
            lp.setMargins(margin, 0, margin, 0);
            indicator.setLayoutParams(lp);
            indicator.setBackground(ContextCompat.getDrawable(requireContext(),
                    i == activeIndex ? R.drawable.bg_cenit_onboard_indicator_active
                            : R.drawable.bg_cenit_onboard_indicator_inactive));
            indicatorContainer.addView(indicator);
        }
    }

    private void updateHeader(int position) {
        String stepLabel = String.format(Locale.getDefault(), "Paso %d de %d", position + 1, steps.size());
        tvStep.setText(stepLabel);
        tvSubtitle.setText(steps.get(position).title);
    }

    private void updateNextButtonState(int position) {
        boolean allDone = areAllStepsComplete();
        boolean last = position == steps.size() - 1;
        btnNext.setText(allDone ? "Empezar a jugar" : (last ? "Listo" : "Siguiente"));
        // Siempre habilitado: en la última página "Done" ofrece terminar aunque falten pasos.
        btnNext.setEnabled(true);
    }

    private void refreshAll() {
        if (adapter != null) {
            // notifyDataSetChanged() sobre ViewPager2 recrea todas las páginas cada 800 ms
            // y se sentía como congelamiento; refrescar solo el contenido de cada posición.
            for (int i = 0; i < adapter.getItemCount(); i++) {
                adapter.notifyItemChanged(i);
            }
        }
        updateNextButtonState(pager != null ? pager.getCurrentItem() : 0);
        if (areAllStepsComplete()) {
            tryCompleteSoon();
        }
    }

    private void startPeriodicCheck() {
        stopPeriodicCheck();
        View root = getView();
        if (root != null) {
            periodicCheck = new Runnable() {
                @Override
                public void run() {
                    try {
                        if (isAdded()) {
                            refreshAll();
                            if (getView() != null && !areAllStepsComplete()) {
                                getView().postDelayed(this, 800);
                            }
                        }
                    } catch (Throwable ignored) {}
                }
            };
            root.postDelayed(periodicCheck, 800);
        }
    }

    private void stopPeriodicCheck() {
        View root = getView();
        if (root != null && periodicCheck != null) {
            root.removeCallbacks(periodicCheck);
        }
        periodicCheck = null;
    }

    private void completeAndDismiss() {
        MainActivity a = null;
        try {
            Context context = getContext();
            if (context != null) {
                context.getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                        .edit().putBoolean("first_run_done", true).apply();
            }
            a = UiUtils.getMainActivity(this);
            if (a != null) a.setSetupWizardActive(false);
        } catch (Throwable ignored) {}
        dismissAllowingStateLoss();
        // El asistente era lo único que tapaba la interfaz: al cerrarlo se muestra la
        // pantalla de inicio con la biblioteca recién configurada.
        if (a != null) {
            final MainActivity act = a;
            act.getWindow().getDecorView().postDelayed(() -> {
                try {
                    if (!act.isFinishing() && !act.isDestroyed()) act.onSetupWizardFinished();
                } catch (Throwable ignored) {}
            }, 250);
        }
    }

    private void tryCompleteSoon() {
        View decor = getDialog() != null && getDialog().getWindow() != null ? getDialog().getWindow().getDecorView() : null;
        if (decor != null) {
            decor.postDelayed(this::completeAndDismiss, 1200);
        }
    }

    public void refreshUi() {
        try {
            refreshAll();
        } catch (Throwable ignored) {}
    }

    private void hideSystemUI() {
        try {
            if (getDialog() != null && getDialog().getWindow() != null) {
                View decorView = getDialog().getWindow().getDecorView();
                decorView.setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_FULLSCREEN);
            }
        } catch (Throwable ignored) {}
    }

    private void showSystemUI() {
        try {
            if (getDialog() != null && getDialog().getWindow() != null) {
                View decorView = getDialog().getWindow().getDecorView();
                decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
        } catch (Throwable ignored) {}
    }

    private boolean isStepComplete(StepType type) {
        return switch (type) {
            case DATA -> SafManager.getDataRootUri(requireContext()) != null;
            case GAMES -> {
                String s = requireContext().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                        .getString("games_folder_uri", null);
                yield s != null && !s.isEmpty();
            }
            case BIOS -> isBiosPresent();
        };
    }

    private boolean areAllStepsComplete() {
        for (SetupStep step : steps) {
            if (!isStepComplete(step.type)) return false;
        }
        return true;
    }

    private int firstIncompleteIndex() {
        for (int i = 0; i < steps.size(); i++) {
            if (!isStepComplete(steps.get(i).type)) return i;
        }
        return -1;
    }

    private boolean isBiosPresent() {
        return BiosVerifier.hasAnyVerifiedBios(requireContext());
    }

    private String getStepStatusText(StepType type) {
        return switch (type) {
            case BIOS -> BiosVerifier.describeVerifiedRegions(requireContext());
            default -> isStepComplete(type) ? "Listo" : "Pendiente";
        };
    }

    private static class SetupPagerAdapter extends RecyclerView.Adapter<SetupPagerAdapter.VH> {
        interface StepListener {
            boolean isComplete(StepType type);
            String getStatusText(StepType type);
            void onAction(StepType type);
        }

        private final Context ctx;
        private final List<SetupStep> steps;
        private final StepListener listener;

        SetupPagerAdapter(Context ctx, List<SetupStep> steps, StepListener listener) {
            this.ctx = ctx;
            this.steps = new ArrayList<>(steps);
            this.listener = listener;
        }

        @NonNull
        @Override
        public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View v = LayoutInflater.from(ctx).inflate(R.layout.item_setup_intro_page, parent, false);
            return new VH(v);
        }

        @Override
        public void onBindViewHolder(@NonNull VH holder, int position) {
            bind(holder, position);
        }

        private void bind(@NonNull VH holder, int position) {
            SetupStep step = steps.get(position);
            holder.title.setText(step.title);
            holder.description.setText(step.description);
            holder.stepChip.setText("PASO " + (position + 1));
            holder.action.setText(step.ctaText);
            holder.icon.setImageResource(step.iconRes);

            boolean complete = listener.isComplete(step.type);

            holder.status.setText(listener.getStatusText(step.type));
            holder.status.setBackground(ContextCompat.getDrawable(ctx,
                    complete ? R.drawable.bg_cenit_onboard_status_done : R.drawable.bg_cenit_onboard_status_pending));
            holder.status.setTextColor(complete ? 0xFF050B18 : 0xFFFFFFFF);

            holder.action.setIcon(ContextCompat.getDrawable(ctx, complete ? R.drawable.check_circle_24px : step.iconRes));
            holder.action.setIconTint(android.content.res.ColorStateList.valueOf(0xFF050B18));
            holder.action.setEnabled(true);
            holder.action.setOnClickListener(v -> listener.onAction(step.type));
        }

        @Override
        public int getItemCount() {
            return steps.size();
        }

        static class VH extends RecyclerView.ViewHolder {
            final TextView title;
            final TextView description;
            final TextView stepChip;
            final TextView status;
            final MaterialButton action;
            final ImageView icon;

            VH(@NonNull View itemView) {
                super(itemView);
                title = itemView.findViewById(R.id.tv_title);
                description = itemView.findViewById(R.id.tv_description);
                stepChip = itemView.findViewById(R.id.tv_step_chip);
                status = itemView.findViewById(R.id.tv_status);
                action = itemView.findViewById(R.id.btn_action);
                icon = itemView.findViewById(R.id.iv_icon);
            }
        }
    }
}
