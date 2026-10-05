# Rejected packed ADPCM transition experiment

No PR195 production change is included. Starting from its exact bd101a722028 header and clean current-main afdde9eb62 header, replace two table loads with one portable biased32-bit transition. All1424 transitions and73,728 decoder cases agree with main on each compiler. Timed loops decode changing encoded blocks with varying starting predictors/indices; full output validation/digest is outside the stopwatch. Two million blocks per cell, ABBA/BAAB, checksums equal.

The packed representation is slower than the existing PR table by7.95% mono /0.93% stereo with GCC and5.98% mono /4.33% stereo with Clang19. It is rejected. The existing two-table decoder is faster than main in isolation, but main's production decoded-block cache may bypass decoding; these results do not establish a whole-VP or game benefit. Any further PR195 work needs actual decode exposure and current production fixture qualification, rather than claiming this representation is better.

Raw results and all three exact/proposed headers are retained for reproducibility. This is a throwaway representation probe, not a game benchmark or a modified saved procedure.
