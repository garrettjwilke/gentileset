#ifndef GENTILESET_H
#define GENTILESET_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *px;
    unsigned w;
    unsigned h;
    int bpp;
    unsigned char clear[4];
    unsigned char palette[256][4];
    unsigned palettesize;
    int indexed;
} Image;

typedef struct {
    int tw;
    int th;
    int left;
    int right;
    int up;
    int bottom;
} Cuts;

int image_alloc(Image *im, unsigned w, unsigned h, const Image *like);
void image_free(Image *im);
int load_png(const char *path, Image *im);
int save_png(const char *path, const Image *im);
int convert_5x3(const Image *src, Image *out, const Cuts *c);
void gentileset_default_cuts(Cuts *c, int tw, int th);

#ifdef __cplusplus
}
#endif

#endif
