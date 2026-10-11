/* BlakeStone32X: faster 3D view drawing (bs32x_render.cpp) */
#ifndef BS32X_RENDER_H
#define BS32X_RENDER_H

extern bool bs32x_fast_render;          /* false: the game's own drawing */

void bs32x_render_frame(bool planes);   /* walls (+ floor and ceiling) of the 3D view */
bool bs32x_sprite_record(int xcenter, int shapenum, int height, int lighting, bool shaded);
void bs32x_render_sprites(void);        /* after DrawScaleds: draw the listed sprites */
void bs32x_render_second(void);         /* the second CPU's half */

/* platform: start bs32x_render_second on the other CPU (false: none) */
bool bs32x_second_cpu_start(void);
void bs32x_second_cpu_wait(void);       /* also waits for a page being shown */
void bs32x_sync_first(int ready, int go);   /* first CPU: wait for 'ready' from the second, then 'go' */
void bs32x_sync_second(int ready, int go);  /* second CPU: say 'ready', wait for 'go' */
extern bool bs32x_present_async;        /* the next present may run on the second CPU */

#endif
