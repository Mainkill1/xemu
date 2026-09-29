# Asset Browser integration

Actual ImGui/native GL32-use fixture passes immediate Down/End selection and close while a foreign capture stays active. Eleven controller tests pass strict and ASan/UBSan. Named assembly and HUD acquisition-generation regressions each failed before their fix. The UI fixture initially used an invalid recorder context (missing scope generation); setup was corrected, not counted as a product regression. The UI arrangement itself was implemented before its interaction fixture; no fabricated layout RED is claimed.

Six changed production translation units compile under the existing host build flags plus Werror, current generated xemu-config.h and current compatible ImGui headers. This reuses existing generated/dependency files; it is not a clean full product build. Exact owned part sources/inputs open the existing Shader Browser editor without enabling replacements. Native PGR2 recognition, motion, overhead and full-head CI remain later gates.
