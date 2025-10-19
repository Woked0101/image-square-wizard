#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vips/vips.h>

#include "isw_canvas.h"
#include "isw_color.h"

static const char *program_name = "isw";

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", program_name);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

static void print_version(void) {
    const char *ver =
#ifdef ISW_VERSION
        ISW_VERSION;
#else
        "0.0.0";
#endif
    printf("%s %s\n", program_name, ver);
}

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage:\n"
        "  %s [--rcb SPEC] input output\n"
        "  %s --help\n"
        "  %s --version\n"
        "\n"
        "Options:\n"
        "  -r, --rcb SPEC    Set resizer canvas background. Use 'transparent' or colors.\n"
        "  --help            Show help and exit.\n"
        "  --version         Show version and exit.\n",
        program_name, program_name, program_name);
}

static void print_help(void) {
    print_usage(stdout);
    fputc('\n', stdout);
    fputs(
        "Description:\n"
        "  image-square-wizard pads images to a square canvas using libvips.\n"
        "  By default it probes the dominant color and uses it as the padding color.\n"
        "  Override the padding color with --rcb. Supported color formats:\n"
        "    - 'transparent' for an alpha background\n"
        "    - #rgb, #rrggbb, #rgba, #rrggbbaa\n"
        "    - r,g,b or r,g,b,a with components between 0-255\n",
        stdout);
}

static bool extract_extension(const char *path, char *buf, size_t buf_len) {
    if (!path || !buf || buf_len == 0) return false;
    const char *dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') return false;
    ++dot;
    size_t len = strlen(dot);
    if (len + 1 > buf_len) return false;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (char) ((dot[i] >= 'A' && dot[i] <= 'Z') ? dot[i] - 'A' + 'a' : dot[i]);
    }
    buf[len] = '\0';
    return true;
}

static bool is_supported_extension(const char *ext) {
    if (!ext) return false;
    static const char *const supported[] = {
        "jpg", "jpeg", "png", "webp", "heic", "heif", "tif", "tiff", "avif"
    };
    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (strcmp(ext, supported[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool extension_supports_alpha(const char *ext) {
    if (!ext) return false;
    static const char *const alpha_exts[] = {
        "png", "webp", "heic", "heif", "tif", "tiff", "avif"
    };
    for (size_t i = 0; i < sizeof(alpha_exts) / sizeof(alpha_exts[0]); ++i) {
        if (strcmp(ext, alpha_exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv) {
    struct isw_options opts = {
        .background_mode = ISW_BG_AUTO,
        .manual_color = { .comps = {0}, .bands = 0 }
    };
    char *background_spec = NULL;

    enum {
        OPT_HELP = 1,
        OPT_VERSION,
        OPT_RCB
    };

    static const struct option long_opts[] = {
        {"help",    no_argument,       NULL, OPT_HELP},
        {"version", no_argument,       NULL, OPT_VERSION},
        {"rcb",     required_argument, NULL, OPT_RCB},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'r':
            case OPT_RCB:
                background_spec = optarg;
                break;
            case OPT_HELP:
                print_help();
                return EXIT_SUCCESS;
            case OPT_VERSION:
                print_version();
                return EXIT_SUCCESS;
            case '?':
            default:
                print_usage(stderr);
                return EXIT_FAILURE;
        }
    }

    if (background_spec) {
        bool is_transparent = false;
        char *parse_err = NULL;
        struct isw_color parsed = {{0}, 0};
        if (!isw_color_parse(background_spec, &parsed, &is_transparent, &parse_err)) {
            if (parse_err) {
                die("%s", parse_err);
            }
            die("invalid background specification");
        }
        if (is_transparent) {
            opts.background_mode = ISW_BG_TRANSPARENT;
        } else {
            opts.background_mode = ISW_BG_MANUAL;
            opts.manual_color = parsed;
        }
    }

    if (optind + 2 != argc) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    const char *input_path = argv[optind];
    const char *output_path = argv[optind + 1];

    char input_ext[16];
    if (!extract_extension(input_path, input_ext, sizeof input_ext) || !is_supported_extension(input_ext)) {
        die("unsupported input extension for '%s'", input_path);
    }
    char output_ext[16];
    if (!extract_extension(output_path, output_ext, sizeof output_ext) || !is_supported_extension(output_ext)) {
        die("unsupported output extension for '%s'", output_path);
    }
    if (opts.background_mode == ISW_BG_TRANSPARENT && !extension_supports_alpha(output_ext)) {
        die("transparent background requires alpha-capable formats (png, webp, heic, heif, tif, tiff, avif)");
    }
    if (opts.background_mode == ISW_BG_MANUAL && opts.manual_color.bands == 4 &&
        !extension_supports_alpha(output_ext)) {
        die("4-component backgrounds require alpha-capable output formats");
    }

    if (VIPS_INIT(program_name)) {
        die("failed to initialise libvips (%s)", vips_error_buffer());
    }

    char *err_msg = NULL;
    int status = isw_process(input_path, output_path, &opts, &err_msg);
    if (status != 0) {
        if (err_msg) {
            fprintf(stderr, "%s: %s\n", program_name, err_msg);
            g_free(err_msg);
        } else {
            fprintf(stderr, "%s: processing failed\n", program_name);
        }
        vips_shutdown();
        return EXIT_FAILURE;
    }

    vips_shutdown();
    return EXIT_SUCCESS;
}
