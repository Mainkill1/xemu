# PR #200 XISO controls

32 complete attempts; 64 leaf records. Median of four per-attempt guest means per side, with separate ABBA/BAAB directions. This diagnostic extraction preserves the server admission: Deck 8/8 per campaign, Windows 0/8 (cache); suite remains unverified. Full paired coverage is blocked by 22 missing oracles. These CPU controls do not qualify the affected audio path.

| Host / backend | Test | Before us | After us | Difference us | Improvement % | ABBA % | BAAB % | Correctness / admission |
|---|---|---:|---:|---:|---:|---:|---:|---|
| deck / gl | cpu_floating_point.x87_scalar | 1089281.70 | 1089765.10 | -483.40 | -0.04% | -0.81% | +0.28% | 8/8 PASS; 8/8 admitted |
| deck / gl | cpu_translation_blocks.direct_loop | 23265.55 | 23558.75 | -293.20 | -1.26% | +0.61% | -3.02% | 8/8 PASS; 8/8 admitted |
| deck / vk | cpu_floating_point.x87_scalar | 1091301.50 | 1089339.00 | +1962.50 | +0.18% | -0.08% | +0.01% | 8/8 PASS; 8/8 admitted |
| deck / vk | cpu_translation_blocks.direct_loop | 23554.35 | 23233.90 | +320.45 | +1.36% | -0.04% | +1.80% | 8/8 PASS; 8/8 admitted |
| win / gl | cpu_floating_point.x87_scalar | 810366.15 | 819348.10 | -8981.95 | -1.11% | -1.21% | -0.54% | 8/8 PASS; 0/8 admitted |
| win / gl | cpu_translation_blocks.direct_loop | 9945.05 | 10121.50 | -176.45 | -1.77% | -1.77% | -1.59% | 8/8 PASS; 0/8 admitted |
| win / vk | cpu_floating_point.x87_scalar | 815993.70 | 821121.85 | -5128.15 | -0.63% | -1.47% | -0.06% | 8/8 PASS; 0/8 admitted |
| win / vk | cpu_translation_blocks.direct_loop | 9977.60 | 10151.40 | -173.80 | -1.74% | -0.80% | -3.28% | 8/8 PASS; 0/8 admitted |

## Changes exceeding 1%

- deck/gl cpu_translation_blocks.direct_loop, MeanUs: -1.26% (ABBA +0.61%, BAAB -3.02%; directions disagree).
- deck/gl cpu_translation_blocks.direct_loop, MinUs: +3.16% (ABBA +4.38%, BAAB -1.80%; directions disagree).
- deck/vk cpu_floating_point.x87_scalar, MaxUs: +1.30% (ABBA -1.08%, BAAB +1.79%; directions disagree).
- deck/vk cpu_floating_point.x87_scalar, P95Us: +1.30% (ABBA -1.08%, BAAB +1.79%; directions disagree).
- deck/vk cpu_translation_blocks.direct_loop, MeanUs: +1.36% (ABBA -0.04%, BAAB +1.80%; directions disagree).
- deck/vk cpu_translation_blocks.direct_loop, MedianUs: +1.17% (ABBA +0.41%, BAAB +1.17%; directions agree).
- deck/vk cpu_translation_blocks.direct_loop, MinUs: -2.40% (ABBA -0.81%, BAAB -2.89%; directions agree).
- win/gl cpu_floating_point.x87_scalar, MeanUs: -1.11% (ABBA -1.21%, BAAB -0.54%; directions agree).
- win/gl cpu_floating_point.x87_scalar, MedianUs: -1.08% (ABBA -1.18%, BAAB -0.49%; directions agree).
- win/gl cpu_floating_point.x87_scalar, MinUs: -1.10% (ABBA -1.17%, BAAB -0.56%; directions agree).
- win/gl cpu_floating_point.x87_scalar, MaxUs: -1.14% (ABBA -1.34%, BAAB -0.53%; directions agree).
- win/gl cpu_floating_point.x87_scalar, P95Us: -1.14% (ABBA -1.34%, BAAB -0.53%; directions agree).
- win/gl cpu_translation_blocks.direct_loop, MeanUs: -1.77% (ABBA -1.77%, BAAB -1.59%; directions agree).
- win/gl cpu_translation_blocks.direct_loop, MedianUs: -1.26% (ABBA -1.24%, BAAB -1.83%; directions agree).
- win/gl cpu_translation_blocks.direct_loop, MinUs: -1.55% (ABBA -1.69%, BAAB -0.66%; directions agree).
- win/gl cpu_translation_blocks.direct_loop, MaxUs: -2.83% (ABBA -3.84%, BAAB -2.35%; directions agree).
- win/gl cpu_translation_blocks.direct_loop, P95Us: -2.83% (ABBA -3.84%, BAAB -2.35%; directions agree).
- win/vk cpu_translation_blocks.direct_loop, MeanUs: -1.74% (ABBA -0.80%, BAAB -3.28%; directions agree).
- win/vk cpu_translation_blocks.direct_loop, MaxUs: -7.72% (ABBA +2.26%, BAAB -22.27%; directions disagree).
- win/vk cpu_translation_blocks.direct_loop, P95Us: -7.72% (ABBA +2.26%, BAAB -22.27%; directions disagree).

All raw numeric attempt means/medians/minima/maxima/p95 and source/work identities are retained. CPU suite changes are guest timing observations, not measured whole-game CPU/FPS gains. Official maintained reports are included separately; Windows reports correctly publish no comparison gains.
