// BlakeStone32X: the state of one ray caster copy (one per CPU); included by
// port/bs32x_render.cpp inside the CPU's namespace, before bs32x_ray.inc.
// These names hide the game's globals of the same name in this namespace.
int pixx;
int xtilestep;
int ytilestep;
int xstep;
int ystep;
int xintercept;
int yintercept;
int xtile;
int ytile;
int tilehit;
int lastside;
int lastintercept;
int lasttilehit;
int postx;
int postheight;
const uint8_t* postsource;
int last_texture_offset = -1;
const uint8_t* last_texture_data = nullptr;

Post posts[max_posts];
int post_count;

// the game's ScalePost, but the column is only recorded (drawn by draw_posts)
void ScalePost()
{
    int height = wallheight[postx] / 8;
    postheight = height;
    if (post_count < max_posts) {
        Post& p = posts[post_count++];
        p.src = postsource;
        p.sh = shade_row(height);
        p.x = static_cast<int16_t>(postx);
        p.height = static_cast<int16_t>(height);
    }
}

// cast the rays of every second column from first_x, then draw the walls
void walls(int first_x)
{
    post_count = 0;
    lastside = -1;
    last_texture_offset = -1;
    last_texture_data = nullptr;
    tilehit = 0;
    cast_rays(first_x);
    ScalePost();
    draw_posts(posts, post_count);
}
