/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "ui/xemu-shortcut-evidence.h"

static bool parse_uint(const char *value, uint64_t *out)
{
    uint64_t number = 0;
    if (!*value) {
        return false;
    }
    for (const char *p = value; *p; p++) {
        if (*p < '0' || *p > '9' || number > (UINT64_MAX - (*p - '0')) / 10) {
            return false;
        }
        number = number * 10 + (*p - '0');
    }
    *out = number;
    return true;
}

static bool valid_digest(const char *value)
{
    if (strlen(value) != 64) {
        return false;
    }
    for (const char *p = value; *p; p++) {
        if (!(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f')) {
            return false;
        }
    }
    return true;
}

bool xemu_shortcut_evidence_parse_early(int argc, char **argv,
                                        XemuShortcutEvidenceOptions *options,
                                        GArray **overrides, Error **errp)
{
    static const char *const flags[] = {
        "-xemu-shortcut-evidence",    "-xemu-shortcut-session",
        "-xemu-shortcut-workload",    "-xemu-shortcut-input-sha256",
        "-xemu-shortcut-order-group", "-xemu-shortcut-order",
        "-xemu-shortcut-position",    "-xemu-shortcut-start-frame",
        "-xemu-shortcut-frames",
    };
    XemuShortcutEvidenceOptions parsed = {0};
    GArray *typed = g_array_new(false, false, sizeof(XemuTweakOverride));
    unsigned int seen = 0;
    bool success = false;
    if (argc < 1 || !argv || !options || !overrides) {
        error_setg(errp, "Invalid shortcut option parse arguments");
        g_array_unref(typed);
        return false;
    }
    g_autofree bool *consumed = g_new0(bool, argc);
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }
        unsigned int option;
        for (option = 0; option < ARRAY_SIZE(flags); option++) {
            if (!strcmp(argv[i], flags[option])) {
                break;
            }
        }
        bool tweak = !strcmp(argv[i], "-xemu-tweak");
        if (!tweak && option == ARRAY_SIZE(flags)) {
            continue;
        }
        const char *flag = argv[i];
        if (i + 1 >= argc || !argv[i + 1]) {
            error_setg(errp, "Missing value for %s", flag);
            goto done;
        }
        const char *value = argv[i + 1];
        if (tweak) {
            XemuTweakOverride record;
            if (!xemu_tweak_parse_override(value, &record, errp)) {
                goto done;
            }
            g_array_append_val(typed, record);
        } else {
            char **strings[] = {
                &parsed.output_path,  &parsed.session_id,  &parsed.workload,
                &parsed.input_sha256, &parsed.order_group, &parsed.order,
            };
            uint64_t number;
            if (!*value || (option == 3 && !valid_digest(value)) ||
                (option == 5 && strcmp(value, "ABBA") &&
                 strcmp(value, "BAAB")) ||
                (option >= 6 && !parse_uint(value, &number))) {
                error_setg(errp, "Invalid value '%s' for %s", value, flag);
                goto done;
            }
            if (option < ARRAY_SIZE(strings)) {
                g_free(*strings[option]);
                *strings[option] = g_strdup(value);
            } else if (option == 6) {
                if (number < 1 || number > 4) {
                    error_setg(errp, "Invalid position '%s': expected 1–4",
                               value);
                    goto done;
                }
                parsed.position = number;
            } else if (option == 7) {
                parsed.start_frame = number;
            } else {
                if (!number) {
                    error_setg(errp,
                               "Invalid frame count '%s': expected positive",
                               value);
                    goto done;
                }
                parsed.frame_count = number;
            }
            seen |= 1u << option;
        }
        consumed[i] = consumed[i + 1] = true;
        i++;
    }
    if (seen && !(seen & 1)) {
        error_setg(errp,
                   "Shortcut metadata requires -xemu-shortcut-evidence PATH");
        goto done;
    }
    if (seen && seen != (1u << ARRAY_SIZE(flags)) - 1) {
        for (unsigned int i = 0; i < ARRAY_SIZE(flags); i++) {
            if (!(seen & (1u << i))) {
                error_setg(errp, "Missing required shortcut metadata %s",
                           flags[i]);
                goto done;
            }
        }
    }
    if (parsed.frame_count > UINT64_MAX - parsed.start_frame) {
        error_setg(errp,
                   "Shortcut measurement window exceeds guest frame range");
        goto done;
    }
    xemu_shortcut_evidence_options_clear(options);
    *options = parsed;
    parsed = (XemuShortcutEvidenceOptions){0};
    if (*overrides) {
        g_array_unref(*overrides);
    }
    *overrides = typed;
    typed = NULL;
    for (int i = 1; i < argc; i++) {
        if (consumed[i]) {
            argv[i] = NULL;
        }
    }
    success = true;
done:
    xemu_shortcut_evidence_options_clear(&parsed);
    if (typed) {
        g_array_unref(typed);
    }
    return success;
}

void xemu_shortcut_evidence_options_clear(XemuShortcutEvidenceOptions *options)
{
    g_free(options->output_path);
    g_free(options->session_id);
    g_free(options->workload);
    g_free(options->input_sha256);
    g_free(options->order_group);
    g_free(options->order);
    *options = (XemuShortcutEvidenceOptions){0};
}
