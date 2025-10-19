#include "isw_canvas.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>

static void set_error(char **err_msg, const char *msg) {
    if (err_msg) {
        *err_msg = g_strdup(msg ? msg : "unknown error");
    }
}

static int ensure_srgb(VipsImage *input, VipsImage **out) {
    if (vips_colourspace(input, out, VIPS_INTERPRETATION_sRGB, NULL) != 0) {
        return -1;
    }
    return 0;
}

static int ensure_u8(VipsImage *input, VipsImage **out) {
    if (vips_image_get_format(input) == VIPS_FORMAT_UCHAR) {
        *out = g_object_ref(input);
        return 0;
    }
    if (vips_cast(input, out, VIPS_FORMAT_UCHAR, NULL) != 0) {
        return -1;
    }
    return 0;
}

static int ensure_three_or_four_bands(VipsImage *input, VipsImage **out) {
    int bands = vips_image_get_bands(input);
    if (bands == 3 || bands == 4) {
        *out = g_object_ref(input);
        return 0;
    }
    if (bands == 1 || bands == 2) {
        VipsImage *converted = NULL;
        if (vips_colourspace(input, &converted, VIPS_INTERPRETATION_sRGB, NULL) != 0) {
            return -1;
        }
        *out = converted;
        return 0;
    }
    /* Fallback to just forcing to RGB */
    VipsImage *converted = NULL;
    if (vips_copy(input, &converted, "bands", 3, NULL) != 0) {
        return -1;
    }
    *out = converted;
    return 0;
}

struct bucket {
    uint32_t count;
    double r_sum;
    double g_sum;
    double b_sum;
};

static int detect_dominant_color(VipsImage *input, struct isw_color *out, char **err_msg) {
    VipsImage *work = NULL;
    VipsImage *u8 = NULL;
    VipsImage *resized = NULL;
    void *data = NULL;

    if (ensure_srgb(input, &work) != 0) {
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }

    if (ensure_u8(work, &u8) != 0) {
        g_object_unref(work);
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    g_object_unref(work);

    if (ensure_three_or_four_bands(u8, &work) != 0) {
        g_object_unref(u8);
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    g_object_unref(u8);

    const int width = vips_image_get_width(work);
    const int height = vips_image_get_height(work);
    const int max_dim = width > height ? width : height;
    const int target = 160;
    double scale = 1.0;
    if (max_dim > target && max_dim > 0) {
        scale = (double) target / (double) max_dim;
    }

    if (scale < 1.0) {
        if (vips_resize(work, &resized, scale, NULL) != 0) {
            set_error(err_msg, vips_error_buffer());
            vips_error_clear();
            g_object_unref(work);
            return -1;
        }
    } else {
        resized = g_object_ref(work);
    }
    g_object_unref(work);

    size_t len = 0;
    data = vips_image_write_to_memory(resized, &len);
    if (!data) {
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        g_object_unref(resized);
        return -1;
    }

    uint8_t *bytes = (uint8_t *) data;
    struct bucket buckets[4096];
    memset(buckets, 0, sizeof buckets);

    const int rbands = vips_image_get_bands(resized);
    const int width_r = vips_image_get_width(resized);
    const int height_r = vips_image_get_height(resized);

    uint64_t samples = 0;
    for (int y = 0; y < height_r; ++y) {
        uint8_t *row = bytes + (size_t) y * width_r * rbands;
        for (int x = 0; x < width_r; ++x) {
            uint8_t *px = row + (size_t) x * rbands;
            uint8_t r = px[0];
            uint8_t g = rbands > 1 ? px[1] : r;
            uint8_t b = rbands > 2 ? px[2] : r;
            if (rbands == 4) {
                uint8_t a = px[3];
                if (a < 8) continue;
            }
            int key = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            struct bucket *bucket = &buckets[key];
            bucket->count += 1;
            bucket->r_sum += (double) r;
            bucket->g_sum += (double) g;
            bucket->b_sum += (double) b;
            samples += 1;
        }
    }

    int best_index = -1;
    uint32_t best_count = 0;
    for (int i = 0; i < 4096; ++i) {
        if (buckets[i].count > best_count) {
            best_count = buckets[i].count;
            best_index = i;
        }
    }

    if (best_index < 0 || best_count == 0 || samples == 0) {
        /* fallback to mid-gray */
        out->bands = 3;
        out->comps[0] = 128.0;
        out->comps[1] = 128.0;
        out->comps[2] = 128.0;
    } else {
        struct bucket *best = &buckets[best_index];
        out->bands = 3;
        out->comps[0] = best->r_sum / (double) best->count;
        out->comps[1] = best->g_sum / (double) best->count;
        out->comps[2] = best->b_sum / (double) best->count;
    }

    g_free(data);
    g_object_unref(resized);
    return 0;
}

static void clamp_color(struct isw_color *color) {
    for (int i = 0; i < color->bands; ++i) {
        if (color->comps[i] < 0.0) color->comps[i] = 0.0;
        if (color->comps[i] > 255.0) color->comps[i] = 255.0;
    }
}

int isw_process(const char *input_path,
    const char *output_path,
    const struct isw_options *opts,
    char **err_msg) {
    if (err_msg) *err_msg = NULL;

    if (!input_path || !output_path || !opts) {
        set_error(err_msg, "invalid arguments");
        return -1;
    }

    VipsImage *raw = vips_image_new_from_file(input_path, NULL);
    if (!raw) {
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }

    VipsImage *work = NULL;
    if (ensure_srgb(raw, &work) != 0) {
        g_object_unref(raw);
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    g_object_unref(raw);

    VipsImage *casted = NULL;
    if (ensure_u8(work, &casted) != 0) {
        g_object_unref(work);
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    g_object_unref(work);

    VipsImage *prepared = NULL;
    if (ensure_three_or_four_bands(casted, &prepared) != 0) {
        g_object_unref(casted);
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    g_object_unref(casted);

    int bands = vips_image_get_bands(prepared);
    bool has_alpha = vips_image_hasalpha(prepared);

    struct isw_color background = {{0}, 0};
    if (opts->background_mode == ISW_BG_MANUAL) {
        background = opts->manual_color;
    } else if (opts->background_mode == ISW_BG_TRANSPARENT) {
        background.bands = 4;
        background.comps[0] = 0.0;
        background.comps[1] = 0.0;
        background.comps[2] = 0.0;
        background.comps[3] = 0.0;
    } else {
        if (detect_dominant_color(prepared, &background, err_msg) != 0) {
            g_object_unref(prepared);
            return -1;
        }
    }

    if (opts->background_mode == ISW_BG_TRANSPARENT && !has_alpha) {
        VipsImage *with_alpha = NULL;
        if (vips_addalpha(prepared, &with_alpha, NULL) != 0) {
            g_object_unref(prepared);
            set_error(err_msg, vips_error_buffer());
            vips_error_clear();
            return -1;
        }
        g_object_unref(prepared);
        prepared = with_alpha;
        has_alpha = true;
    }

    bands = vips_image_get_bands(prepared);

    if (opts->background_mode == ISW_BG_MANUAL) {
        if (background.bands == 4 && bands == 3) {
            VipsImage *with_alpha = NULL;
            if (vips_addalpha(prepared, &with_alpha, NULL) != 0) {
                g_object_unref(prepared);
                set_error(err_msg, vips_error_buffer());
                vips_error_clear();
                return -1;
            }
            g_object_unref(prepared);
            prepared = with_alpha;
            bands = vips_image_get_bands(prepared);
        } else if (background.bands == 3 && bands == 4) {
            background.comps[3] = 255.0;
            background.bands = 4;
        }
    } else if (opts->background_mode == ISW_BG_AUTO) {
        if (bands == 4) {
            background.comps[3] = 255.0;
            background.bands = 4;
        }
    } else if (opts->background_mode == ISW_BG_TRANSPARENT) {
        background.bands = bands;
    }

    clamp_color(&background);

    const int width = vips_image_get_width(prepared);
    const int height = vips_image_get_height(prepared);
    const int target = width > height ? width : height;

    int left = (target - width) / 2;
    int top = (target - height) / 2;

    VipsArrayDouble *bg_array = vips_array_double_new(background.comps, background.bands);
    if (!bg_array) {
        g_object_unref(prepared);
        set_error(err_msg, "failed to allocate background array");
        return -1;
    }
    VipsImage *embedded = NULL;
    if (vips_embed(prepared, &embedded,
        left, top, target, target,
        "extend", VIPS_EXTEND_BACKGROUND,
        "background", bg_array,
        NULL) != 0) {
        vips_area_unref(VIPS_AREA(bg_array));
        g_object_unref(prepared);
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    vips_area_unref(VIPS_AREA(bg_array));
    g_object_unref(prepared);

    if (vips_image_write_to_file(embedded, output_path, NULL) != 0) {
        set_error(err_msg, vips_error_buffer());
        vips_error_clear();
        g_object_unref(embedded);
        return -1;
    }

    g_object_unref(embedded);
    return 0;
}
