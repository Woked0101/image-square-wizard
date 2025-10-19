#include "isw_color.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_triplet(const char *spec, struct isw_color *out, char **err_msg) {
    size_t len = strlen(spec);
    if (len != 4 && len != 5 && len != 7 && len != 9) {
        if (err_msg) *err_msg = g_strdup("hex colors must be #rgb, #rrggbb, #rgba, or #rrggbbaa");
        return false;
    }

    int bands = (len == 4 || len == 7) ? 3 : 4;
    int digits_per = (len == 4 || len == 5) ? 1 : 2;

    double comps[4] = {0};
    for (int i = 0; i < bands; ++i) {
        int hi = hex_to_int(spec[1 + i * digits_per]);
        int lo = digits_per == 2 ? hex_to_int(spec[1 + i * digits_per + 1]) : hi;
        if (hi < 0 || lo < 0) {
            if (err_msg) *err_msg = g_strdup("hex colors may only contain 0-9A-F");
            return false;
        }
        int value = digits_per == 2 ? (hi << 4 | lo) : (hi << 4 | hi);
        comps[i] = (double) value;
    }

    out->bands = bands;
    for (int i = 0; i < bands; ++i) {
        out->comps[i] = comps[i];
    }
    return true;
}

static bool parse_csv(const char *spec, struct isw_color *out, char **err_msg) {
    double comps[4] = {0};
    int count = 0;

    const char *p = spec;
    while (*p != '\0') {
        char *end = NULL;
        errno = 0;
        double val = strtod(p, &end);
        if (errno != 0 || end == p) {
            if (err_msg) *err_msg = g_strdup("invalid numeric value in color");
            return false;
        }
        if (val < 0.0 || val > 255.0) {
            if (err_msg) *err_msg = g_strdup("color components must be between 0 and 255");
            return false;
        }
        if (count >= 4) {
            if (err_msg) *err_msg = g_strdup("too many components in color");
            return false;
        }
        comps[count++] = val;
        if (*end == ',') {
            p = end + 1;
        } else if (*end == '\0') {
            break;
        } else {
            if (err_msg) *err_msg = g_strdup("color components must be comma separated");
            return false;
        }
    }

    if (count != 3 && count != 4) {
        if (err_msg) *err_msg = g_strdup("color must have 3 or 4 components");
        return false;
    }
    out->bands = count;
    for (int i = 0; i < count; ++i) {
        out->comps[i] = comps[i];
    }
    return true;
}

bool isw_color_parse(const char *spec, struct isw_color *out, bool *out_is_transparent, char **err_msg) {
    if (err_msg) *err_msg = NULL;
    if (!spec || !out) {
        if (err_msg) *err_msg = g_strdup("missing color specification");
        return false;
    }

    if (out_is_transparent) {
        *out_is_transparent = false;
    }

    if (strcasecmp(spec, "transparent") == 0) {
        if (out_is_transparent) {
            *out_is_transparent = true;
        }
        return true;
    }

    if (spec[0] == '#') {
        return parse_hex_triplet(spec, out, err_msg);
    }

    return parse_csv(spec, out, err_msg);
}
