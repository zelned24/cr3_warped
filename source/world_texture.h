#ifndef WORLD_TEXTURE_H
#define WORLD_TEXTURE_H
#include <citro3d.h>
#include "level.h"
int world_texture_init(void);
int world_texture_load(unsigned level_id);
int world_texture_draw(const C3D_Mtx *projection,const C3D_Mtx *view,float x,float y,float z,float radius);
void world_texture_unload(void);
void world_texture_exit(void);
int world_texture_load_actor(void);
int world_texture_draw_actor(const C3D_Mtx *projection,const C3D_Mtx *view,const level_vertex *vertices,int count);
int world_texture_load_props(void);
int world_texture_props_ready(void);
int world_texture_draw_prop(unsigned type,const C3D_Mtx *projection,const C3D_Mtx *view,float x,float y,float z,float angle,float sx,float sy,float sz,float seconds);
#endif
