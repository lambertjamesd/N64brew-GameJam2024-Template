#include <libdragon.h>
#include <math.h>
#include "setup.h"
#include "core.h"
#include "rdpq_font_internal.h"


/*=============================================================

=============================================================*/

#define POPTIME 0.4f
#define BLOCKSIZE  16
#define BLOCKSW    (320/BLOCKSIZE)
#define BLOCKSH    (240/BLOCKSIZE)
#define RECTCORNERDIST (BLOCKSIZE/1.41421356237)

#define FONTDEF_LARGE  1
#define FONTDEF_XLARGE 2

#define UNLIKELY(x) __builtin_expect(!!(x), 0)


/*=============================================================

=============================================================*/

typedef enum {
    MENU_START,
    MENU_MODE,
    MENU_PLAYERS,
    MENU_GAMESETUP,
} CurrentMenu;

typedef enum {
    TRANS_NONE,
    TRANS_FORWARD,
    TRANS_BACKWARD,
} Transition;


/*=============================================================

=============================================================*/

typedef struct {
    float h; // Angle (in degrees)
    float s; // 0 - 1
    float v; // 0 - 1
} hsv_t;

typedef struct {
    sprite_t* boxcorner;
    sprite_t* boxedge;
    sprite_t* boxback;
    surface_t boxedgesurface;
    surface_t boxbacksurface;
    int cornersize;
} BoxSpriteDef;

typedef struct {
    int w;
    int h;
    int x;
    int y;
    BoxSpriteDef spr;
} BoxDef;


/*=============================================================

=============================================================*/

static Transition global_transition;
static CurrentMenu global_curmenu;

static int global_selection;
static bool  global_cursoractive;
static float global_cursory;

static float global_backtime;
static surface_t global_zbuff;
static float global_rotsel1;
static float global_rotsel2;
static float global_rotcursor;

static BoxSpriteDef sprdef_backbox;
static BoxSpriteDef sprdef_button;
static BoxDef* bdef_backbox_mode;
static BoxDef* bdef_backbox_plycount;
static BoxDef* bdef_button_freeplay;
static BoxDef* bdef_button_compete;
static sprite_t* spr_toybox;
static sprite_t* spr_trophy;
static sprite_t* spr_pointer;
static sprite_t* spr_robot;
static sprite_t* spr_player;
static rdpq_font_t* global_font1;
static rdpq_font_t* global_font2;


/*=============================================================

=============================================================*/

static void setup_draw(float deltatime);
static void drawbox(BoxDef* bd, color_t col, bool cullable);
static void drawfade(float time);
static void culledges(BoxDef* back);
static void drawculledsprite(sprite_t* spr, int x, int y, rdpq_blitparms_t* bp);
static void drawcustomtext(rdpq_font_t* font, int fontid, rdpq_textparms_t* params, int x, int y, char* text);
static int libdragon_render_text(const rdpq_font_t *fnt, const rdpq_paragraph_char_t *chars, float x0, float y0);
static bool is_menuvisible(CurrentMenu menu);
static void font_callback(void* arg);


/*=============================================================

=============================================================*/

static inline float lerp(float from, float to, float frac)
{
    return from + (to - from)*frac;
}

static float elasticlerp(float from, float to, float frac)
{
    const float c4 = (2*M_PI)/3;
    if (frac <= 0)
        return from;
    if (frac >= 1)
        return to;
    const float ease = pow(2, -8.0f*frac)*sin((frac*8.0f - 0.75f)*c4) + 1;
    return from + (to - from)*ease;
}

static float deglerp(float from, float to, float frac)
{
    float delta = to - from;
    if (delta > 180)
        from += 360;
    else if (delta < -180)
        from -= 360;
    from = lerp(from, to, frac);
    if (from < 0)
        from += 360;
    return from;
}

static float clamp(float val, float min, float max)
{
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

static hsv_t rgb2hsv(color_t rgb)
{
    hsv_t ret = {0, 0, 0};
    const float r = ((float)rgb.r)/255.0f;
    const float g = ((float)rgb.g)/255.0f;
    const float b = ((float)rgb.b)/255.0f;
    const float max = fmaxf(fmaxf(r, g), b);
    const float min = fminf(fminf(r, g), b);
    const float c = max - min;

    // Get value
    ret.v = max;

    // Get saturation
    if (ret.v != 0)
        ret.s = c/max;

    // Get hue
    if (c != 0)
    {
        if (max == r)
            ret.h = ((g - b)/c) + 0;
        else if (max == g)
            ret.h = ((b - r)/c) + 2;
        else
            ret.h = ((r - g)/c) + 4;
        ret.h *= 60;
        if (ret.h < 0)
            ret.h += 360;
    }
    return ret;
}

static color_t hsv2rgb(hsv_t hsv)
{
    float r, g, b;
    color_t ret = {0, 0, 0, 255};
    float h = hsv.h/360;
    int i = h*6;
    float f = h * 6 - i;
    float p = hsv.v*(1 - hsv.s);
    float q = hsv.v*(1 - f*hsv.s);
    float t = hsv.v*(1 - (1 - f)*hsv.s);
    
    switch (i % 6)
    {
        case 0: r = hsv.v; g = t;     b = p;     break;
        case 1: r = q;     g = hsv.v; b = p;     break;
        case 2: r = p;     g = hsv.v; b = t;     break;
        case 3: r = p;     g = q;     b = hsv.v; break;
        case 4: r = t;     g = p;     b = hsv.v; break;
        default:r = hsv.v; g = p;     b = q;     break;
    }
    
    ret.r = r*255;
    ret.g = g*255;
    ret.b = b*255;
    return ret;
}

static color_t lerpcolor(color_t from, color_t to, float frac)
{
    hsv_t result;
    hsv_t hsv_from = rgb2hsv(from);
    hsv_t hsv_to = rgb2hsv(to);
    result.h = deglerp(hsv_from.h, hsv_to.h, frac);
    result.s = lerp(hsv_from.s, hsv_to.s, frac);
    result.v = lerp(hsv_from.v, hsv_to.v, frac);
    return hsv2rgb(result);
}


/*=============================================================

=============================================================*/

void setup_init()
{
    const color_t plyclrs[] = {
        PLAYERCOLOR_1,
        PLAYERCOLOR_2,
        PLAYERCOLOR_3,
        PLAYERCOLOR_4,
    };
    global_selection = 0;
    global_transition = TRANS_FORWARD;
    global_curmenu = MENU_START;
    global_cursoractive = false;
    global_backtime = 0;
    global_cursory = 0;
    global_rotsel1 = 0;
    global_rotsel2 = 0;
    global_rotcursor = 0;

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    global_zbuff = surface_alloc(FMT_RGBA16, 320, 240);

    bdef_backbox_mode = (BoxDef*)malloc(sizeof(BoxDef));
    bdef_backbox_plycount = (BoxDef*)malloc(sizeof(BoxDef));
    bdef_button_freeplay = (BoxDef*)malloc(sizeof(BoxDef));
    bdef_button_compete = (BoxDef*)malloc(sizeof(BoxDef));
    spr_toybox = sprite_load("rom:/core/ToyBox.rgba32.sprite");
    spr_trophy = sprite_load("rom:/core/Trophy.rgba32.sprite");
    spr_pointer = sprite_load("rom:/core/Pointer.rgba32.sprite");
    spr_player = sprite_load("rom:/core/Controller.rgba32.sprite");
    spr_robot = sprite_load("rom:/core/Robot.rgba32.sprite");
    global_font1 = rdpq_font_load("rom:/squarewave_l.font64");
    global_font2 = rdpq_font_load("rom:/squarewave_xl.font64");
    rdpq_text_register_font(FONTDEF_LARGE, global_font1);
    rdpq_text_register_font(FONTDEF_XLARGE, global_font2);
    rdpq_font_style(global_font1, 1, &(rdpq_fontstyle_t){.custom=font_callback});
    rdpq_font_style(global_font2, 1, &(rdpq_fontstyle_t){.custom=font_callback});
    for (int i=0; i<MAXPLAYERS; i++)
        rdpq_font_style(global_font1, i+2, &(rdpq_fontstyle_t){.color = plyclrs[i]});

    sprdef_backbox.boxcorner = sprite_load("rom:/core/Box_Corner.rgba32.sprite");
    sprdef_backbox.boxedge = sprite_load("rom:/core/Box_Edge.rgba32.sprite");
    sprdef_backbox.boxback = sprite_load("rom:/pattern.i8.sprite");
    sprdef_backbox.boxedgesurface = sprite_get_pixels(sprdef_backbox.boxedge);
    sprdef_backbox.boxbacksurface = sprite_get_pixels(sprdef_backbox.boxback);
    sprdef_backbox.cornersize = 16;

    sprdef_button.boxcorner = sprite_load("rom:/core/Box2_Corner.rgba32.sprite");
    sprdef_button.boxedge = sprite_load("rom:/core/Box2_Edge.rgba32.sprite");
    sprdef_button.boxback = sprite_load("rom:/core/Box_Back.rgba32.sprite");
    sprdef_button.boxedgesurface = sprite_get_pixels(sprdef_button.boxedge);
    sprdef_button.boxbacksurface = sprite_get_pixels(sprdef_button.boxback);
    sprdef_button.cornersize = 8;

    bdef_backbox_mode->w = 0;
    bdef_backbox_mode->h = 0;
    bdef_backbox_mode->x = 320/2;
    bdef_backbox_mode->y = 240/2;
    bdef_backbox_mode->spr = sprdef_backbox;

    bdef_backbox_plycount->w = 280;
    bdef_backbox_plycount->h = 200;
    bdef_backbox_plycount->x = bdef_backbox_plycount->w*2;
    bdef_backbox_plycount->y = 240/2;
    bdef_backbox_plycount->spr = sprdef_backbox;

    bdef_button_freeplay->w = 128;
    bdef_button_freeplay->h = 40;
    bdef_button_freeplay->spr = sprdef_button;

    bdef_button_compete->w = 128;
    bdef_button_compete->h = 40;
    bdef_button_compete->spr = sprdef_button;
}

void setup_loop(float deltatime)
{
    int maxselect;
    joypad_buttons_t btns = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    // Handle parenting of objects to the main backbox
    bdef_button_freeplay->x = bdef_backbox_mode->x;
    bdef_button_freeplay->y = bdef_backbox_mode->y - 26;
    bdef_button_compete->x = bdef_backbox_mode->x;
    bdef_button_compete->y = bdef_backbox_mode->y + 26;

    // Handle controls
    switch (global_curmenu)
    {
        case MENU_START:
        {
            if (global_transition == TRANS_FORWARD && bdef_backbox_mode->w >= 270)
            {
                global_selection = 0;
                global_transition = TRANS_NONE;
                global_curmenu = MENU_MODE;
                global_cursory = bdef_button_freeplay->y - 12;
                global_cursoractive = true;
            }
            break;
        }
        case MENU_MODE:
        {
            maxselect = 2;

            if (global_cursoractive)
            {
                if (btns.a)
                {
                    global_selection = 0;
                    global_curmenu = MENU_PLAYERS;
                    global_transition = TRANS_FORWARD;
                    global_cursoractive = false;
                }

                if (btns.d_down || btns.c_down)
                    global_selection = (global_selection + 1) % maxselect;
                else if (btns.d_up || btns.c_up)
                    global_selection = (global_selection - 1) % maxselect;
            }
            break;
        }
        case MENU_PLAYERS:
        {
            maxselect = 0;

            if (global_cursoractive)
            {
                if (btns.a)
                {
                    global_selection = 0;
                    global_curmenu = MENU_GAMESETUP;
                    global_transition = TRANS_FORWARD;
                    global_cursoractive = false;
                }
            }
            break;
        }
        default:
        {
            break;
        }
    }

    if (global_cursoractive)
    {
        if (btns.d_down || btns.c_down)
            global_selection = (global_selection + 1) % maxselect;
        else if (btns.d_up || btns.c_up)
            global_selection = (global_selection - 1) % maxselect;
    }

    // Handle animations and transitions
    if (is_menuvisible(MENU_MODE))
    {
        bdef_backbox_mode->w = elasticlerp(0, 280, global_backtime - POPTIME);
        bdef_backbox_mode->h = elasticlerp(0, 200, global_backtime - POPTIME);
        if (global_curmenu == MENU_PLAYERS && global_transition == TRANS_FORWARD)
            bdef_backbox_mode->x = lerp(bdef_backbox_mode->x, -bdef_backbox_mode->w, 7*deltatime);

        if (global_selection == 0)
        {
            global_rotsel1 = lerp(global_rotsel1, sin(global_backtime*2)/3, 10*deltatime);
            global_rotsel2 = lerp(global_rotsel2, 0, 10*deltatime);
            global_cursory = lerp(global_cursory, bdef_button_freeplay->y - 12, 10*deltatime);
        }
        else
        {
            global_rotsel1 = lerp(global_rotsel1, 0, 10*deltatime);
            global_rotsel2 = lerp(global_rotsel2, sin(global_backtime*2)/3, 10*deltatime);
            global_cursory = lerp(global_cursory, bdef_button_compete->y - 12, 10*deltatime);
        }
    }
    if (is_menuvisible(MENU_PLAYERS))
    {
        if (global_curmenu == MENU_PLAYERS && global_transition == TRANS_FORWARD)
            bdef_backbox_plycount->x = lerp(bdef_backbox_plycount->x, 320/2, 7*deltatime);
        else if (global_curmenu == MENU_MODE && global_transition == TRANS_BACKWARD)
            bdef_backbox_plycount->x = lerp(bdef_backbox_plycount->x, -bdef_backbox_mode->w, 7*deltatime);
    }

    // Draw the scene
    setup_draw(deltatime);
}

void setup_draw(float deltatime)
{
    const float backspeed = 0.3f;
    const color_t backcolors[] = {
        {255, 0,   0,   255},
        {255, 255, 0,   255},
        {0,   255, 0,   255},
        {0,   255, 255, 255},
        {0,   0,   255, 255},
        {255, 0,   255, 255},
    };
    rdpq_blitparms_t bp_freeplay = {.cx=16,.cy=16};
    rdpq_blitparms_t bp_compete = {.cx=16,.cy=16};
    int ccurr, cnext;

    // Increase background animation time
    global_backtime += deltatime;
    ccurr = ((int)(global_backtime*backspeed)) % (sizeof(backcolors)/sizeof(backcolors[0]));
    cnext = (ccurr + 1) % (sizeof(backcolors)/sizeof(backcolors[0]));

    // Begin drawing
    surface_t* disp = display_get();
    rdpq_attach(disp, &global_zbuff);

    // Draw the background
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(lerpcolor(backcolors[ccurr], backcolors[cnext], global_backtime*backspeed - floor(global_backtime*backspeed)));
    rdpq_fill_rectangle(0, 0, 320, 240);
    rdpq_clear_z(ZBUF_VAL(0));

    // Draw menu sprites
    if (is_menuvisible(MENU_MODE))
    {
        // Draw the container box
        drawbox(bdef_backbox_mode, (color_t){255, 255, 255, 255}, false);
        culledges(bdef_backbox_mode);

        // Draw option buttons
        drawbox(bdef_button_freeplay, (color_t){200, 255, 200, 255}, true);
        drawbox(bdef_button_compete, (color_t){255, 255, 200, 255}, true);
        rdpq_set_prim_color((color_t){255, 255, 255, 255});

        // Draw button sprites
        if (global_selection == 0)
            bp_freeplay.theta = global_rotsel1;
        else
            bp_compete.theta = global_rotsel2;
        drawculledsprite(spr_toybox, bdef_button_freeplay->x + 40, bdef_button_freeplay->y, &bp_freeplay);
        drawculledsprite(spr_trophy, bdef_button_compete->x + 40, bdef_button_compete->y, &bp_compete);
    }
    if (is_menuvisible(MENU_PLAYERS))
    {
        const int sprsize = 32;
        const int padding = 16;
        drawbox(bdef_backbox_plycount, (color_t){255, 255, 255, 255}, false);
        for (int i=0; i<MAXPLAYERS; i++)
            rdpq_sprite_blit(spr_robot, bdef_backbox_plycount->x - (sprsize+padding)*2 + padding/2 + (sprsize+padding)*i, bdef_backbox_plycount->y-24, NULL);

        rdpq_text_print(&(rdpq_textparms_t){.width=320, .align=ALIGN_CENTER}, FONTDEF_XLARGE, bdef_backbox_plycount->x - 320/2, bdef_backbox_plycount->y-64, "Press START to join");
        for (int i=0; i<MAXPLAYERS; i++)
            rdpq_text_printf(&(rdpq_textparms_t){.width=34, .align=ALIGN_CENTER, .style_id=i+2}, FONTDEF_LARGE, bdef_backbox_plycount->x - (sprsize+padding)*2 + padding/2 + (sprsize+padding)*i, bdef_backbox_plycount->y-30, "P%d", i+1);
    }

    // Pointer
    if (global_cursoractive)
    {
        global_rotcursor = lerp(global_rotcursor, cos(global_backtime*4)*8, 10*deltatime);
        drawculledsprite(spr_pointer, bdef_button_freeplay->x - bdef_button_freeplay->w + 28 + global_rotcursor, global_cursory, NULL);
    }

    // Draw z-buffered menu text
    if (is_menuvisible(MENU_MODE))
    {
        rdpq_text_print(&(rdpq_textparms_t){.char_spacing=1, .style_id=1}, FONTDEF_XLARGE, bdef_backbox_mode->x-76, bdef_backbox_mode->y-64, "I want to play:");
        rdpq_text_print(&(rdpq_textparms_t){.char_spacing=1, .style_id=1}, FONTDEF_LARGE, bdef_button_freeplay->x - 54, bdef_button_freeplay->y+4, "For fun!");
        rdpq_text_print(&(rdpq_textparms_t){.char_spacing=1, .style_id=1}, FONTDEF_LARGE, bdef_button_compete->x - 54, bdef_button_compete->y+4, "For glory!");
    }

    // Draw the screen wipe effect
    drawfade(global_backtime);

    // Done
    rdpq_detach_show();
}

void setup_cleanup()
{
    display_close();
    sprite_free(sprdef_backbox.boxcorner);
    sprite_free(sprdef_backbox.boxedge);
    sprite_free(sprdef_backbox.boxback);
    sprite_free(sprdef_button.boxcorner);
    sprite_free(sprdef_button.boxedge);
    sprite_free(sprdef_button.boxback);
    sprite_free(spr_toybox);
    sprite_free(spr_trophy);
    sprite_free(spr_pointer);
    sprite_free(spr_player);
    sprite_free(spr_robot);
    rdpq_text_unregister_font(FONTDEF_LARGE);
    rdpq_text_unregister_font(FONTDEF_XLARGE);
    rdpq_font_free(global_font1);
    rdpq_font_free(global_font2);
    free(bdef_backbox_mode);
    free(bdef_button_compete);
    free(bdef_button_freeplay);
    surface_free(&global_zbuff);
}



/*=============================================================

=============================================================*/

static void drawbox(BoxDef* bd, color_t col, bool cullable)
{
    int w2, h2;
    if (bd->w < 32)
        bd->w = 32;
    if (bd->h < 32)
        bd->h = 32;
    w2 = bd->w/2;
    h2 = bd->h/2;

    // Initialize the drawing mode
    rdpq_set_mode_standard();
    rdpq_set_prim_color(col);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    if (cullable)
    {
        rdpq_mode_zbuf(true, true);
        rdpq_mode_zoverride(true, 0.7, 0);
    }

    // Background
    if (bd->spr.boxback != NULL)
    {
        int cornersizepad = bd->spr.cornersize - 6;
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (TEX0,0,PRIM,0)));
        rdpq_tex_upload(TILE0, &bd->spr.boxbacksurface, &((rdpq_texparms_t){.s={.repeats=REPEAT_INFINITE},.t={.repeats=REPEAT_INFINITE}}));
        rdpq_texture_rectangle(TILE0, bd->x-w2+cornersizepad, bd->y-h2+cornersizepad, bd->x+w2-cornersizepad, bd->y+h2-cornersizepad, 0, 0);
    }
    else
    {
        int cornersizepad = bd->spr.cornersize - 6;
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_fill_rectangle(bd->x-w2+cornersizepad, bd->y-h2+cornersizepad, bd->x+w2-cornersizepad, bd->y+h2-cornersizepad);
    }
    if (cullable)
        rdpq_mode_zoverride(true, 0.6, 0);

    // Corners
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (TEX0,0,PRIM,0)));
    rdpq_sprite_blit(bd->spr.boxcorner, bd->x-w2, bd->y-h2, NULL);
    rdpq_sprite_blit(bd->spr.boxcorner, bd->x+w2-bd->spr.cornersize, bd->y-h2, &((rdpq_blitparms_t){.flip_x=true}));
    rdpq_sprite_blit(bd->spr.boxcorner, bd->x-w2, bd->y+h2-bd->spr.cornersize, &((rdpq_blitparms_t){.flip_y=true}));
    rdpq_sprite_blit(bd->spr.boxcorner, bd->x+w2-bd->spr.cornersize, bd->y+h2-bd->spr.cornersize, &((rdpq_blitparms_t){.flip_x=true, .flip_y=true}));

    // Edges
    if (bd->w > bd->spr.cornersize*2)
    {
        rdpq_tex_upload_sub(TILE0, &bd->spr.boxedgesurface, NULL, bd->spr.cornersize, 0, bd->spr.cornersize*2, bd->spr.cornersize);
        rdpq_texture_rectangle(TILE0, bd->x-w2+bd->spr.cornersize, bd->y-h2, bd->x+w2-bd->spr.cornersize, bd->y-h2+bd->spr.cornersize, 0, 0);
        rdpq_tex_upload_sub(TILE0, &bd->spr.boxedgesurface, NULL, 0, bd->spr.cornersize, bd->spr.cornersize, bd->spr.cornersize*2);
        rdpq_set_tile_size(TILE0, 0, 0, bd->spr.cornersize, bd->spr.cornersize);
        rdpq_texture_rectangle(TILE0, bd->x-w2+bd->spr.cornersize, bd->y+h2-bd->spr.cornersize, bd->x+w2-bd->spr.cornersize, bd->y+h2, 0, 0);
    }
    if (bd->h > bd->spr.cornersize*2)
    {
        rdpq_tex_upload_sub(TILE0, &bd->spr.boxedgesurface, NULL, 0, 0, bd->spr.cornersize, bd->spr.cornersize);
        rdpq_texture_rectangle(TILE0, bd->x-w2, bd->y-h2+bd->spr.cornersize, bd->x-w2+bd->spr.cornersize, bd->y+h2-bd->spr.cornersize, 0, 0);
        rdpq_tex_upload_sub(TILE0, &bd->spr.boxedgesurface, NULL, bd->spr.cornersize, bd->spr.cornersize, bd->spr.cornersize*2, bd->spr.cornersize*2);
        rdpq_set_tile_size(TILE0, 0, 0, bd->spr.cornersize, bd->spr.cornersize);
        rdpq_texture_rectangle(TILE0, bd->x+w2-bd->spr.cornersize, bd->y-h2+bd->spr.cornersize, bd->x+w2, bd->y+h2-bd->spr.cornersize, 0, 0);
    }
}

static void culledges(BoxDef* back)
{
    int boxleft =  back->x - back->w/2 + back->spr.cornersize - 6;
    int boxtop =  back->y - back->h/2 + back->spr.cornersize - 6;
    int boxbottom =  back->y + back->h/2 - back->spr.cornersize - 6;
    int boxright =  back->x + back->w/2 - back->spr.cornersize - 6;
    uint32_t old_cfg = rdpq_config_disable(RDPQ_CFG_AUTOSCISSOR);
    rdpq_attach(&global_zbuff, NULL);
        rdpq_mode_push();
            rdpq_set_mode_fill(color_from_packed16(ZBUF_VAL(1)));
            rdpq_fill_rectangle(boxleft, boxtop, boxright, boxbottom);
        rdpq_mode_pop();
    rdpq_detach();
    rdpq_config_set(old_cfg);
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (TEX0,0,PRIM,0)));
}

static void drawculledsprite(sprite_t* spr, int x, int y, rdpq_blitparms_t* bp)
{
    rdpq_mode_zbuf(true, true);
    rdpq_mode_zoverride(true, 0.5, 0);
    rdpq_sprite_blit(spr, x, y, bp);
}

static void drawfade(float time)
{
    if (time > 1)
        return;

    const float cornerdist = RECTCORNERDIST*3; // The bigger this value, the longer the trail that is left behind

    // Calculate the x and y intercepts of the perpendicular line to the diagonal of the frame
    // Since the travel is constant (and the line is perfectly diagonal), we can calculate it easily without needing to do trig
    const float perpendicularx1 = clamp(time-0.5, 0, 0.5)*2*320;
    const float perpendicularx2 = clamp(time,     0, 0.5)*2*320;
    const float perpendiculary1 = clamp(time,     0, 0.5)*2*240;
    const float perpendiculary2 = clamp(time-0.5, 0, 0.5)*2*240;

    // Prepare to draw a black fill rectangle
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(color_from_packed32(0x000000FF));

    // Edge case, avoid all the calculations below
    if (time < 0)
    {
        rdpq_fill_rectangle(0, 0, 320, 240);
        return;
    }

    // Draw each block
    for (int y=0; y<BLOCKSH; y++)
    {
        for (int x=0; x<BLOCKSW; x++)
        {
            const float blockx = x*BLOCKSIZE + BLOCKSIZE/2;
            const float blocky = y*BLOCKSIZE + BLOCKSIZE/2;
            const float distpointraw = ((blockx-perpendicularx1)*(-perpendiculary2+perpendiculary1) + (blocky-perpendiculary1)*(perpendicularx2-perpendicularx1));
            const float distpoint = distpointraw/sqrt((-perpendiculary2+perpendiculary1)*(-perpendiculary2+perpendiculary1) + (perpendicularx2-perpendicularx1)*(perpendicularx2-perpendicularx1));
            if (distpoint > -cornerdist)
            {
                float blocksize = clamp(-distpoint, -cornerdist, cornerdist)/cornerdist;
                blocksize = ((1-blocksize)/2)*BLOCKSIZE;
                rdpq_fill_rectangle(blockx - blocksize/2, blocky - blocksize/2, blockx + blocksize/2, blocky + blocksize/2);
            }
        }
    }
}

static void drawcustomtext(rdpq_font_t* font, int fontid, rdpq_textparms_t* params, int x, int y, char* text)
{
    int tsize = strlen(text);
    rdpq_paragraph_t* pg = rdpq_paragraph_build(params, fontid, text, &tsize);
    libdragon_render_text(font, pg->chars, x, y);
    rdpq_paragraph_free(pg);
}

static bool is_menuvisible(CurrentMenu menu)
{
    switch (menu)
    {
        case MENU_MODE:
            return global_curmenu == MENU_START || global_curmenu == MENU_MODE || (global_curmenu == MENU_PLAYERS && global_transition == TRANS_FORWARD); 
        case MENU_PLAYERS:
            return global_curmenu == MENU_PLAYERS || (global_curmenu == MENU_MODE && global_transition == TRANS_BACKWARD) || (global_curmenu == MENU_GAMESETUP && global_transition == TRANS_FORWARD); 
        default:
            return false;
    }
}

static void font_callback(void* arg)
{
    rdpq_mode_zbuf(true, true);
    rdpq_mode_zoverride(true, 0.5, 0);
}


/*=============================================================

=============================================================*/

static int libdragon_render_text(const rdpq_font_t *fnt, const rdpq_paragraph_char_t *chars, float x0, float y0)
{
    uint8_t font_id = chars[0].font_id;
    int cur_atlas = -1;
    int cur_style = -1;
    int rdram_loading = 0;
    int tile_offset = 0;

    const rdpq_paragraph_char_t *ch = chars;
    while (ch->font_id == font_id) {
        const glyph_t *g = &fnt->glyphs[ch->glyph];
        if (UNLIKELY(ch->style_id != cur_style)) {
            assertf(ch->style_id < fnt->num_styles,
                 "style %d not defined in this font", ch->style_id);
            switch (fnt->flags & FONT_FLAG_TYPE_MASK) {
                case FONT_TYPE_MONO_OUTLINE:
                case FONT_TYPE_ALIASED_OUTLINE:
                    rdpq_set_env_color(fnt->styles[ch->style_id].outline_color);
                    // fallthrough
                case FONT_TYPE_ALIASED:
                case FONT_TYPE_MONO:
                    rdpq_set_prim_color(fnt->styles[ch->style_id].color);
                    break;
                case FONT_TYPE_BITMAP:
                    break;
                default:
                    assert(0);
            }
            cur_style = ch->style_id;
        }
        if (UNLIKELY(g->natlas != cur_atlas)) {
            atlas_t *a = &fnt->atlases[g->natlas];
            rspq_block_run(a->up);
            // Buu edit start
            rdpq_mode_zbuf(true, true);
            rdpq_mode_zoverride(true, 0.5, 0);
            // Buu edit end
            if (a->sprite->hslices == 0) { // check if the atlas is in RDRAM instead of TMEM
                switch (fnt->flags & FONT_FLAG_TYPE_MASK) {
                case FONT_TYPE_MONO:            rdram_loading = 1; tile_offset = 0; break;
                case FONT_TYPE_MONO_OUTLINE:    rdram_loading = 1; tile_offset = 1; break;
                case FONT_TYPE_ALIASED:         rdram_loading = 2; tile_offset = 0; break;
                case FONT_TYPE_ALIASED_OUTLINE: rdram_loading = 2; tile_offset = 1; break;
                case FONT_TYPE_BITMAP: switch (TEX_FORMAT_BITDEPTH(sprite_get_format(a->sprite))) {
                    case 4:     rdram_loading = 1; tile_offset = 0; break;
                    default:    rdram_loading = 2; tile_offset = 0; break;
                    } break;
                default: assert(0);
                }
            } else {
                rdram_loading = 0;
            }
            cur_atlas = g->natlas;
        }

        // Draw the glyph
        float x = x0 + (ch->x + g->xoff);
        float y = y0 + (ch->y + g->yoff);
        int width = g->xoff2 - g->xoff;
        int height = g->yoff2 - g->yoff;
        int ntile = g->ntile;

        // Check if the atlas is in RDRAM (rather than TMEM). If so, we need
        // to load each glyph into TMEM before drawing.
        if (UNLIKELY(rdram_loading)) {
            switch (rdram_loading) {
            case 1:
                // If the atlas is 4bpp, we need to load the glyph as CI8 (usual trick)
                // TILE4 is the CI8 tile configured for loading
                rdpq_load_tile(TILE4, g->s/2, g->t, (g->s+width+1)/2, g->t+height);
                rdpq_set_tile_size(ntile+tile_offset, g->s & ~1, g->t, (g->s+width+1) & ~1, g->t+height);
                break;
            case 2:
                ntile += tile_offset;
                tile_offset ^= 4;
                rdpq_load_tile(ntile, g->s, g->t, g->s+width, g->t+height);
                ntile ^= (ntile & 1);
                break;
            default:
                assertf(0, "invalid rdram_loading value %d", rdram_loading);
            }
        }

        rdpq_texture_rectangle(ntile,
            x, y, x+width, y+height,
            g->s, g->t);

        ch++;
    }

    return ch - chars;
}