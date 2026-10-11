// BlakeStone32X: declarations the ray caster copy needs before its code
// (the definitions are in bs32x_ray_state.inc, after it)
extern int pixx;
extern int xtilestep;
extern int ytilestep;
extern int xstep;
extern int ystep;
extern int xintercept;
extern int yintercept;
extern int xtile;
extern int ytile;
extern int tilehit;
extern int lastside;
extern int lastintercept;
extern int lasttilehit;
extern int postx;
extern int postheight;
extern const uint8_t* postsource;
extern int last_texture_offset;
extern const uint8_t* last_texture_data;
void ScalePost();
