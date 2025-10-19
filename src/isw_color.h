#ifndef ISW_COLOR_H
#define ISW_COLOR_H

#include <stdbool.h>

#include "isw_canvas.h"

bool isw_color_parse(const char *spec, struct isw_color *out, bool *out_is_transparent, char **err_msg);

#endif /* ISW_COLOR_H */
