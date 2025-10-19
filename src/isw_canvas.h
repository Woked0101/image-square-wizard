#ifndef ISW_CANVAS_H
#define ISW_CANVAS_H

#include <stdbool.h>

#include <vips/vips.h>

enum isw_background_mode {
    ISW_BG_AUTO = 0,
    ISW_BG_MANUAL,
    ISW_BG_TRANSPARENT
};

struct isw_color {
    double comps[4];
    int bands;
};

struct isw_options {
    enum isw_background_mode background_mode;
    struct isw_color manual_color;
};

int isw_process(const char *input_path,
    const char *output_path,
    const struct isw_options *opts,
    char **err_msg);

#endif /* ISW_CANVAS_H */
