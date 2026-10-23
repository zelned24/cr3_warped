#ifndef RENDER_FRUSTUM_H
#define RENDER_FRUSTUM_H
#include <math.h>

/* Named x/y/z/w matrix rows, matching the vertex shader's dp4 operations. */
static inline void render_frustum_matrix(const float projection[4][4],
                                        const float view[4][4], double clip[4][4])
{
    for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
        clip[r][c]=0;
        for (int k=0;k<4;k++) clip[r][c]+=(double)projection[r][k]*view[k][c];
    }
}

/* Conservative homogeneous side-plane test. Never divide by w or reject a
 * box crossing a side plane. Depth clipping remains with the GPU. */
static inline int render_frustum_visible(const double clip[4][4],
                                         const float low[3],const float high[3])
{
    for (int a=0;a<3;a++)
        if (!isfinite(low[a]) || !isfinite(high[a]) || low[a]>high[a]) return 1;
    for (int r=0;r<4;r++) for (int c=0;c<4;c++)
        if (!isfinite(clip[r][c])) return 1;
    for (int axis=0;axis<2;axis++) {
        /* A linear plane reaches its maximum at a box corner. Testing that
         * support corner is equivalent to rejecting all eight corners, without
         * eight complete matrix transforms on the ARM11. */
        double scale=1+fabs(clip[3][3])+fabs(clip[axis][3]);
        for (int c=0;c<3;c++)
            scale+=(fabs(clip[3][c])+fabs(clip[axis][c]))*
                   fmax(fabs(low[c]),fabs(high[c]));
        for (int sign=-1;sign<=1;sign+=2) {
            double maximum=clip[3][3]+sign*clip[axis][3];
            for (int c=0;c<3;c++) {
                double coefficient=clip[3][c]+sign*clip[axis][c];
                maximum+=coefficient*(coefficient<0?low[c]:high[c]);
            }
            if (!isfinite(maximum) || !isfinite(scale)) return 1;
            if (maximum < -scale*1e-5) return 0;
        }
    }
    return 1;
}
#endif
