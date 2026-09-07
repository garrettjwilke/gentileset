/*
 * gentileset - convert a 5x3 autotile sheet to a Godot 12x4 tileset
 *
 * Usage:
 *   ./gentileset -i input.png -o output.png -w 8 -h 8
 *   ./gentileset -w 8 -h 8 -i input.png -o output.png [-l 0] [-r 0] [-u 0] [-b 0]
 *
 * Input is a 5x3 tile sheet. Output is the Godot bitmask layout (12x4 tiles).
 * Indexed-color PNGs keep their original palette.
 */

#include "gentileset.h"
#include "lodepng.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *input;
    const char *output;
    int width;
    int height;
    int left;
    int right;
    int up;
    int bottom;
    int got_width;
    int got_height;
} Options;

/* 3x3-to-4x4 subtile rules: target tile, then 9 source tiles in section order. */
static const int threexthree_data[][20] = {
    {0, 0,  0,0, 2,0, 2,0,  0,1, 2,1, 2,1,  0,1, 2,1, 2,1},
    {1, 0,  0,0, 0,0, 0,0,  0,0, 0,0, 0,0,  0,0, 0,0, 0,0},
    {2, 0,  1,0, 1,0, 1,0,  1,0, 1,0, 1,0,  1,0, 1,0, 1,0},
    {3, 0,  2,0, 2,0, 2,0,  2,0, 2,0, 2,0,  2,0, 2,0, 2,0},

    {0, 1,  0,1, 2,1, 2,1,  0,1, 2,1, 2,1,  0,1, 2,1, 2,1},
    {1, 1,  0,1, 0,1, 0,1,  0,1, 0,1, 0,1,  0,1, 0,1, 0,1},
    {2, 1,  1,1, 1,1, 1,1,  1,1, 1,1, 1,1,  1,1, 1,1, 1,1},
    {3, 1,  2,1, 2,1, 2,1,  2,1, 2,1, 2,1,  2,1, 2,1, 2,1},

    {0, 2,  0,1, 2,1, 2,1,  0,2, 2,2, 2,2,  0,2, 2,2, 2,2},
    {1, 2,  0,2, 0,2, 0,2,  0,2, 0,2, 0,2,  0,2, 0,2, 0,2},
    {2, 2,  1,2, 1,2, 1,2,  1,2, 1,2, 1,2,  1,2, 1,2, 1,2},
    {3, 2,  2,2, 2,2, 2,2,  2,2, 2,2, 2,2,  2,2, 2,2, 2,2},

    {0, 3,  0,0, 2,0, 2,0,  0,2, 2,2, 2,2,  0,2, 2,2, 2,2},
    {1, 3,  0,0, 0,0, 0,0,  0,2, 0,2, 0,2,  0,2, 0,2, 0,2},
    {2, 3,  1,0, 1,0, 1,0,  1,2, 1,2, 1,2,  1,2, 1,2, 1,2},
    {3, 3,  2,0, 2,0, 2,0,  2,2, 2,2, 2,2,  2,2, 2,2, 2,2},
};
static const int threexthree_count = (int)(sizeof(threexthree_data) / sizeof(threexthree_data[0]));

/* 4x4-to-Godot whole-tile copies: to_x, to_y, from_x, from_y. */
static const int fourxfour_data[][4] = {
    {0, 0, 0, 0}, {1, 0, 1, 0}, {2, 0, 2, 0}, {3, 0, 3, 0},
    {0, 1, 0, 1}, {1, 1, 1, 1}, {2, 1, 2, 1}, {3, 1, 3, 1},
    {0, 2, 0, 2}, {1, 2, 1, 2}, {2, 2, 2, 2}, {3, 2, 3, 2},
    {0, 3, 0, 3}, {1, 3, 1, 3}, {2, 3, 2, 3}, {3, 3, 3, 3},

    {4, 0, 2, 1}, {5, 0, 2, 0}, {6, 0, 2, 0}, {7, 0, 2, 1},
    {4, 1, 1, 1}, {5, 1, 2, 1}, {6, 1, 2, 1}, {7, 1, 3, 1},
    {4, 2, 1, 1}, {5, 2, 2, 1}, {6, 2, 2, 1}, {7, 2, 3, 1},
    {4, 3, 2, 1}, {5, 3, 2, 2}, {6, 3, 2, 2}, {7, 3, 2, 1},

    {8, 0, 1, 0}, {9, 0, 2, 1}, {10, 0, 2, 0}, {11, 0, 3, 0},
    {8, 1, 1, 1}, {9, 1, 2, 1}, {10, 1, -1, -1}, {11, 1, 2, 1},
    {8, 2, 2, 1}, {9, 2, 2, 1}, {10, 2, 2, 1}, {11, 2, 3, 1},
    {8, 3, 1, 2}, {9, 3, 2, 2}, {10, 3, 2, 1}, {11, 3, 3, 2},
};
static const int fourxfour_count = (int)(sizeof(fourxfour_data) / sizeof(fourxfour_data[0]));

/* Inner-corner patches: target, then up to 4 (sx,sy) pairs before *2. */
static const int inner_corners_data[][11] = {
    {1, 0,  1,  1,1},
    {2, 0,  2,  0,1, 1,1},
    {3, 0,  1,  0,1},

    {1, 1,  2,  1,1, 1,0},
    {2, 1,  4,  0,0, 0,1, 1,0, 1,1},
    {3, 1,  2,  0,0, 0,1},

    {1, 2,  1,  1,0},
    {2, 2,  2,  0,0, 1,0},
    {3, 2,  1,  0,0},

    {4, 0,  3,  0,1, 1,0, 1,1},
    {5, 0,  1,  0,1},
    {6, 0,  1,  1,1},
    {7, 0,  3,  0,1, 0,0, 1,1},

    {4, 1,  1,  1,0},
    {5, 1,  1,  0,0},
    {6, 1,  1,  1,0},
    {7, 1,  1,  0,0},

    {4, 2,  1,  1,1},
    {5, 2,  1,  0,1},
    {6, 2,  1,  1,1},
    {7, 2,  1,  0,1},

    {4, 3,  3,  0,0, 1,0, 1,1},
    {5, 3,  1,  0,0},
    {6, 3,  1,  1,0},
    {7, 3,  3,  0,1, 0,0, 1,0},

    {9, 0,  2,  0,0, 1,0},
    {11, 1, 2,  1,0, 1,1},
    {8, 2,  2,  0,0, 0,1},
    {10, 3, 2,  0,1, 1,1},

    {9, 1,  2,  0,0, 1,1},
    {10, 2, 2,  1,0, 0,1},
};
static const int inner_corners_count = (int)(sizeof(inner_corners_data) / sizeof(inner_corners_data[0]));

static void usage(FILE *fp)
{
    fprintf(fp,
        "usage: gentileset -i INPUT -o OUTPUT -w WIDTH -h HEIGHT [options]\n"
        "\n"
        "Convert a 5x3 autotile PNG into a Godot 12x4 tileset.\n"
        "\n"
        "required:\n"
        "  -i, --input FILE     5x3 source tileset\n"
        "  -o, --output FILE    Godot tileset PNG to write\n"
        "  -w, --width N        tile width in pixels\n"
        "  -h, --height N       tile height in pixels\n"
        "\n"
        "optional (default 0):\n"
        "  -l, --left N         left cut offset\n"
        "  -r, --right N        right cut offset\n"
        "  -u, --up N           top cut offset\n"
        "  -b, --bottom N       bottom cut offset\n"
        "      --help           show this help\n");
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v;

    if (!s || !*s)
        return 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return 0;
    if (v > INT_MAX || v < INT_MIN)
        return 0;
    *out = (int)v;
    return 1;
}

static int take_arg(int *i, int argc, char **argv, const char *eq, const char **out)
{
    if (eq) {
        *out = eq;
        return 1;
    }
    if (*i + 1 >= argc) {
        fprintf(stderr, "error: %s requires a value\n", argv[*i]);
        return 0;
    }
    *out = argv[++(*i)];
    return 1;
}

static int parse_args(int argc, char **argv, Options *opt)
{
    int i;

    memset(opt, 0, sizeof(*opt));
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *eq = NULL;
        const char *val = NULL;
        char name[32];
        int n;

        if (strcmp(a, "--help") == 0) {
            usage(stdout);
            exit(0);
        }

        eq = strchr(a, '=');
        if (eq && strncmp(a, "--", 2) == 0) {
            n = (int)(eq - a);
            if (n >= (int)sizeof(name))
                n = (int)sizeof(name) - 1;
            memcpy(name, a, (size_t)n);
            name[n] = '\0';
            a = name;
            eq++;
        } else {
            eq = NULL;
        }

        if (strcmp(a, "-i") == 0 || strcmp(a, "--input") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            opt->input = val;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            opt->output = val;
        } else if (strcmp(a, "-w") == 0 || strcmp(a, "--width") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            if (!parse_int(val, &opt->width)) {
                fprintf(stderr, "error: invalid width '%s'\n", val);
                return 0;
            }
            opt->got_width = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--height") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            if (!parse_int(val, &opt->height)) {
                fprintf(stderr, "error: invalid height '%s'\n", val);
                return 0;
            }
            opt->got_height = 1;
        } else if (strcmp(a, "-l") == 0 || strcmp(a, "--left") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            if (!parse_int(val, &opt->left)) {
                fprintf(stderr, "error: invalid left offset '%s'\n", val);
                return 0;
            }
        } else if (strcmp(a, "-r") == 0 || strcmp(a, "--right") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            if (!parse_int(val, &opt->right)) {
                fprintf(stderr, "error: invalid right offset '%s'\n", val);
                return 0;
            }
        } else if (strcmp(a, "-u") == 0 || strcmp(a, "--up") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            if (!parse_int(val, &opt->up)) {
                fprintf(stderr, "error: invalid up offset '%s'\n", val);
                return 0;
            }
        } else if (strcmp(a, "-b") == 0 || strcmp(a, "--bottom") == 0) {
            if (!take_arg(&i, argc, argv, eq, &val))
                return 0;
            if (!parse_int(val, &opt->bottom)) {
                fprintf(stderr, "error: invalid bottom offset '%s'\n", val);
                return 0;
            }
        } else {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 0;
        }
    }

    if (!opt->input || !opt->output || !opt->got_width || !opt->got_height) {
        fprintf(stderr, "error: -i, -o, -w, and -h are required\n");
        usage(stderr);
        return 0;
    }
    if (opt->width <= 0 || opt->height <= 0) {
        fprintf(stderr, "error: width and height must be positive\n");
        return 0;
    }
    return 1;
}

static void clamp_offsets(Options *opt)
{
    int halfw = opt->width / 2;
    int halfh = opt->height / 2;
    int minx = -halfw;
    int maxx = opt->width - halfw;
    int miny = -halfh;
    int maxy = opt->height - halfh;

    if (opt->left < minx) opt->left = minx;
    if (opt->left > maxx) opt->left = maxx;
    if (opt->right < minx) opt->right = minx;
    if (opt->right > maxx) opt->right = maxx;
    if (opt->up < miny) opt->up = miny;
    if (opt->up > maxy) opt->up = maxy;
    if (opt->bottom < miny) opt->bottom = miny;
    if (opt->bottom > maxy) opt->bottom = maxy;
    if (opt->left > opt->right) opt->right = opt->left;
    if (opt->up > opt->bottom) opt->bottom = opt->up;
}

static unsigned char *pixel_at(const Image *im, int x, int y)
{
    return im->px + ((size_t)y * im->w + (size_t)x) * (size_t)im->bpp;
}

int image_alloc(Image *im, unsigned w, unsigned h, const Image *like)
{
    size_t n;

    memset(im, 0, sizeof(*im));
    im->w = w;
    im->h = h;
    im->bpp = like->bpp;
    im->indexed = like->indexed;
    im->palettesize = like->palettesize;
    memcpy(im->clear, like->clear, 4);
    memcpy(im->palette, like->palette, sizeof(im->palette));
    n = (size_t)w * (size_t)h * (size_t)im->bpp;
    im->px = (unsigned char *)malloc(n);
    if (!im->px)
        return 0;
    if (im->bpp == 1) {
        memset(im->px, im->clear[0], n);
    } else if (im->clear[0] == 0 && im->clear[1] == 0 && im->clear[2] == 0 && im->clear[3] == 0) {
        memset(im->px, 0, n);
    } else {
        size_t i;
        for (i = 0; i < (size_t)w * (size_t)h; i++)
            memcpy(im->px + i * 4, im->clear, 4);
    }
    return 1;
}

void image_free(Image *im)
{
    free(im->px);
    im->px = NULL;
}

static void blit_rect(const Image *src, Image *dst,
                      int sx, int sy, int dx, int dy, int rw, int rh)
{
    int y;

    if (rw <= 0 || rh <= 0)
        return;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0)
        return;
    if (sx + rw > (int)src->w || sy + rh > (int)src->h)
        return;
    if (dx + rw > (int)dst->w || dy + rh > (int)dst->h)
        return;

    for (y = 0; y < rh; y++)
        memcpy(pixel_at(dst, dx, dy + y), pixel_at(src, sx, sy + y), (size_t)rw * (size_t)dst->bpp);
}

static void clear_rect(Image *im, int x, int y, int rw, int rh)
{
    int yy;

    if (rw <= 0 || rh <= 0)
        return;
    if (x < 0 || y < 0 || x + rw > (int)im->w || y + rh > (int)im->h)
        return;
    for (yy = 0; yy < rh; yy++) {
        if (im->bpp == 1) {
            memset(pixel_at(im, x, y + yy), im->clear[0], (size_t)rw);
        } else {
            int xx;
            unsigned char *p = pixel_at(im, x, y + yy);
            for (xx = 0; xx < rw; xx++, p += 4) {
                p[0] = im->clear[0];
                p[1] = im->clear[1];
                p[2] = im->clear[2];
                p[3] = im->clear[3];
            }
        }
    }
}

static void copy_tile(const Image *src, Image *dst, const Cuts *c,
                      int to_x, int to_y, int from_x, int from_y)
{
    int dx = to_x * c->tw;
    int dy = to_y * c->th;

    clear_rect(dst, dx, dy, c->tw, c->th);
    if (from_x < 0 || from_y < 0)
        return;
    blit_rect(src, dst, from_x * c->tw, from_y * c->th, dx, dy, c->tw, c->th);
}

static int section_geom(const Cuts *c, int sx, int sy,
                        int *ox, int *oy, int *ow, int *oh)
{
    int offx1 = c->tw / 2 + c->left;
    int offx2 = offx1 - c->left + c->right;
    int offy1 = c->th / 2 + c->up;
    int offy2 = offy1 - c->up + c->bottom;
    int midwx = c->right - c->left;
    int midwy = c->bottom - c->up;
    int endwx = c->tw - offx2;
    int endwy = c->th - offy2;
    int oxa[3], oya[3], sxa[3], sya[3];

    if (sx < 0 || sx > 2 || sy < 0 || sy > 2)
        return 0;

    oxa[0] = 0; oxa[1] = offx1; oxa[2] = offx2;
    oya[0] = 0; oya[1] = offy1; oya[2] = offy2;
    sxa[0] = offx1; sxa[1] = midwx; sxa[2] = endwx;
    sya[0] = offy1; sya[1] = midwy; sya[2] = endwy;

    *ox = oxa[sx];
    *oy = oya[sy];
    *ow = sxa[sx];
    *oh = sya[sy];
    return *ow > 0 && *oh > 0;
}

static void copy_tile_section_raw(const Image *src, Image *dst, const Cuts *c,
                                  int to_x, int to_y, int sx, int sy,
                                  int from_x, int from_y)
{
    int ox, oy, ow, oh;

    if (!section_geom(c, sx, sy, &ox, &oy, &ow, &oh))
        return;
    clear_rect(dst, to_x * c->tw + ox, to_y * c->th + oy, ow, oh);
    if (from_x < 0 || from_y < 0)
        return;
    blit_rect(src, dst,
              from_x * c->tw + ox, from_y * c->th + oy,
              to_x * c->tw + ox, to_y * c->th + oy,
              ow, oh);
}

static void apply_subtile_data(const Image *src, Image *dst, const Cuts *c)
{
    int i, x, y;

    for (i = 0; i < threexthree_count; i++) {
        int tx = threexthree_data[i][0];
        int ty = threexthree_data[i][1];
        for (y = 0; y < 3; y++) {
            for (x = 0; x < 3; x++) {
                int idx = y * 3 + x;
                int fx = threexthree_data[i][2 + idx * 2];
                int fy = threexthree_data[i][3 + idx * 2];
                if (fx < 0 || fy < 0)
                    continue;
                copy_tile_section_raw(src, dst, c, tx, ty, x, y, fx, fy);
            }
        }
    }
}

static void apply_tile_data(const Image *src, Image *dst, const Cuts *c)
{
    int i;

    for (i = 0; i < fourxfour_count; i++)
        copy_tile(src, dst, c,
                  fourxfour_data[i][0], fourxfour_data[i][1],
                  fourxfour_data[i][2], fourxfour_data[i][3]);
}

static void fix_inner_corners(const Image *src, Image *dst, const Cuts *c,
                              int source_x, int source_y)
{
    int i, s;

    for (i = 0; i < inner_corners_count; i++) {
        int tx = inner_corners_data[i][0];
        int ty = inner_corners_data[i][1];
        int n = inner_corners_data[i][2];
        for (s = 0; s < n; s++) {
            int sx = inner_corners_data[i][3 + s * 2] * 2;
            int sy = inner_corners_data[i][4 + s * 2] * 2;
            copy_tile_section_raw(src, dst, c, tx, ty, sx, sy, source_x, source_y);
        }
    }
}

static unsigned png_fail(unsigned err, const char *what)
{
    fprintf(stderr, "error: %s: %s\n", what, lodepng_error_text(err));
    return err;
}

int load_png(const char *path, Image *im)
{
    unsigned char *file = NULL;
    size_t filesize = 0;
    unsigned err;
    LodePNGState state;
    unsigned w = 0, h = 0;
    unsigned char *px = NULL;
    unsigned i;

    memset(im, 0, sizeof(*im));
    err = lodepng_load_file(&file, &filesize, path);
    if (err) {
        png_fail(err, path);
        return 0;
    }

    lodepng_state_init(&state);
    err = lodepng_inspect(&w, &h, &state, file, filesize);
    if (err) {
        png_fail(err, path);
        lodepng_state_cleanup(&state);
        free(file);
        return 0;
    }

    if (state.info_png.color.colortype == LCT_PALETTE) {
        err = lodepng_color_mode_copy(&state.info_raw, &state.info_png.color);
        if (err) {
            png_fail(err, path);
            lodepng_state_cleanup(&state);
            free(file);
            return 0;
        }
        state.info_raw.bitdepth = 8;
        state.decoder.color_convert = 1;
        err = lodepng_decode(&px, &w, &h, &state, file, filesize);
        if (err) {
            png_fail(err, path);
            lodepng_state_cleanup(&state);
            free(file);
            return 0;
        }
        im->indexed = 1;
        im->bpp = 1;
        im->palettesize = (unsigned)state.info_png.color.palettesize;
        if (im->palettesize > 256)
            im->palettesize = 256;
        for (i = 0; i < im->palettesize; i++) {
            im->palette[i][0] = state.info_png.color.palette[i * 4 + 0];
            im->palette[i][1] = state.info_png.color.palette[i * 4 + 1];
            im->palette[i][2] = state.info_png.color.palette[i * 4 + 2];
            im->palette[i][3] = state.info_png.color.palette[i * 4 + 3];
        }
        im->clear[0] = 0;
        for (i = 0; i < im->palettesize; i++) {
            if (im->palette[i][3] == 0) {
                im->clear[0] = (unsigned char)i;
                break;
            }
        }
        lodepng_state_cleanup(&state);
    } else {
        lodepng_state_cleanup(&state);
        err = lodepng_decode32(&px, &w, &h, file, filesize);
        if (err) {
            png_fail(err, path);
            free(file);
            return 0;
        }
        im->indexed = 0;
        im->bpp = 4;
        im->clear[0] = im->clear[1] = im->clear[2] = im->clear[3] = 0;
    }

    free(file);
    im->px = px;
    im->w = w;
    im->h = h;
    return 1;
}

int save_png(const char *path, const Image *im)
{
    unsigned err;
    unsigned char *png = NULL;
    size_t pngsize = 0;
    LodePNGState state;

    if (!im->indexed) {
        err = lodepng_encode32_file(path, im->px, im->w, im->h);
        if (err) {
            png_fail(err, path);
            return 0;
        }
        return 1;
    }

    lodepng_state_init(&state);
    state.encoder.auto_convert = 0;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    err = 0;
    {
        unsigned i;
        for (i = 0; i < im->palettesize; i++) {
            err = lodepng_palette_add(&state.info_raw,
                                      im->palette[i][0], im->palette[i][1],
                                      im->palette[i][2], im->palette[i][3]);
            if (err)
                break;
            err = lodepng_palette_add(&state.info_png.color,
                                      im->palette[i][0], im->palette[i][1],
                                      im->palette[i][2], im->palette[i][3]);
            if (err)
                break;
        }
    }
    if (!err)
        err = lodepng_encode(&png, &pngsize, im->px, im->w, im->h, &state);
    lodepng_state_cleanup(&state);
    if (err) {
        png_fail(err, path);
        free(png);
        return 0;
    }
    err = lodepng_save_file(png, pngsize, path);
    free(png);
    if (err) {
        png_fail(err, path);
        return 0;
    }
    return 1;
}

void gentileset_default_cuts(Cuts *c, int tw, int th)
{
    if (!c)
        return;
    c->tw = tw;
    c->th = th;
    c->left = 0;
    c->right = 0;
    c->up = 0;
    c->bottom = 0;
}

int convert_5x3(const Image *src, Image *out, const Cuts *c)
{
    Image temp;

    if (!image_alloc(&temp, (unsigned)(c->tw * 4), (unsigned)(c->th * 4), src)) {
        fprintf(stderr, "error: out of memory\n");
        return 0;
    }
    if (!image_alloc(out, (unsigned)(c->tw * 12), (unsigned)(c->th * 4), src)) {
        fprintf(stderr, "error: out of memory\n");
        image_free(&temp);
        return 0;
    }

    apply_subtile_data(src, &temp, c);
    apply_tile_data(&temp, out, c);
    image_free(&temp);

    fix_inner_corners(src, out, c, 4, 0);
    copy_tile(src, out, c, 0, 0, 3, 0);
    copy_tile(src, out, c, 0, 2, 3, 1);
    copy_tile(src, out, c, 0, 3, 4, 1);
    copy_tile(src, out, c, 1, 3, 3, 2);
    copy_tile(src, out, c, 3, 3, 4, 2);
    return 1;
}

#ifndef GENTILESET_NO_MAIN
int main(int argc, char **argv)
{
    Options opt;
    Cuts cuts;
    Image src;
    Image dst;
    int ok;

    if (!parse_args(argc, argv, &opt))
        return 1;
    clamp_offsets(&opt);

    if (!load_png(opt.input, &src))
        return 1;

    if ((int)src.w < opt.width * 5 || (int)src.h < opt.height * 3) {
        fprintf(stderr,
                "error: input is %ux%u, need at least %dx%d for a 5x3 sheet of %dx%d tiles\n",
                src.w, src.h, opt.width * 5, opt.height * 3, opt.width, opt.height);
        image_free(&src);
        return 1;
    }

    cuts.tw = opt.width;
    cuts.th = opt.height;
    cuts.left = opt.left;
    cuts.right = opt.right;
    cuts.up = opt.up;
    cuts.bottom = opt.bottom;

    memset(&dst, 0, sizeof(dst));
    ok = convert_5x3(&src, &dst, &cuts);
    image_free(&src);
    if (!ok)
        return 1;

    ok = save_png(opt.output, &dst);
    image_free(&dst);
    return ok ? 0 : 1;
}
#endif
