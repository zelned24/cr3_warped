#include "bottom_ui.h"
#include "font_data.h"
#include <stdio.h>
#include <string.h>

typedef struct { level_vertex *out; int cap, count; } canvas;
typedef struct { float r, g, b; } color;
#define RGB(r,g,b) ((color){(r)/255.0f,(g)/255.0f,(b)/255.0f})
static const color ink = {0.82f, 0.91f, 0.96f};
static const color muted = {0.35f, 0.48f, 0.56f};
static const color cyan = {0.16f, 0.76f, 0.88f};
static const color orange = {1.0f, 0.55f, 0.17f};

static void tri(canvas *c, float ax,float ay,float bx,float by,float dx,float dy,color col)
{
    if (c->count + 3 > c->cap) return;
    c->out[c->count++] = (level_vertex){ax,240-ay,0,col.r,col.g,col.b};
    c->out[c->count++] = (level_vertex){bx,240-by,0,col.r,col.g,col.b};
    c->out[c->count++] = (level_vertex){dx,240-dy,0,col.r,col.g,col.b};
}

static void rect(canvas *c,int x,int y,int w,int h,color col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x+w > 320) w=320-x;
    if (y+h > 240) h=240-y;
    if (w <= 0 || h <= 0 || c->count+6 > c->cap) return;
    tri(c,x,y,x+w,y,x+w,y+h,col); tri(c,x,y,x+w,y+h,x,y+h,col);
}

static void frame(canvas *c,int x,int y,int w,int h,color edge)
{
    rect(c,x,y,w,1,edge);rect(c,x,y+h-1,w,1,RGB(10,24,35));
    rect(c,x,y,1,h,edge);rect(c,x+w-1,y,1,h,RGB(14,37,48));
}

static void panel(canvas *c,int x,int y,int w,int h)
{
    for(int row=0;row<h;row+=2) {
        int glow = (h-row)*14/h;
        rect(c,x,y+row,w,row+2<=h?2:1,RGB(12+glow,26+glow,38+glow));
    }
    frame(c,x,y,w,h,RGB(52,92,111));
    rect(c,x+1,y+1,w-2,1,RGB(37,61,77));
}

static void text(canvas *c,int x,int y,int scale,const char *s,color col,int right)
{
    if(!s) return;
    for(;*s;s++,x+=6*scale) {
        if(x+5*scale>right) break;
        unsigned ch=(unsigned char)*s;
        int glyph=ch>=32 && ch<128?font_index[ch-32]:0;
        for(int column=0;column<FONT_W;column++)
            for(int row=0;row<FONT_H;row++)
                if(font_glyphs[glyph][column] & (1u<<row))
                    rect(c,x+column*scale,y+row*scale,scale,scale,col);
    }
}

static void gem(canvas *c,int x,int y,color col)
{
    tri(c,x,y-9,x+7,y,x,y+10,col);
    tri(c,x,y-9,x,y+10,x-7,y,RGB(col.r*155,col.g*155,col.b*155));
    tri(c,x,y-7,x+4,y-1,x,y+5,RGB(195,235,255));
}

static void fruit(canvas *c,int x,int y)
{
    rect(c,x-5,y-5,10,3,RGB(244,164,41));
    rect(c,x-7,y-2,14,7,orange);rect(c,x-5,y+5,10,3,RGB(195,69,22));
    rect(c,x-4,y-3,3,4,RGB(255,207,89));
    tri(c,x,y-5,x-6,y-11,x+1,y-9,RGB(61,154,67));
    tri(c,x,y-5,x+7,y-10,x+3,y-5,RGB(101,201,78));
}

static void crate(canvas *c,int x,int y)
{
    rect(c,x-7,y-7,15,15,RGB(163,88,37));frame(c,x-7,y-7,15,15,RGB(235,162,71));
    rect(c,x-4,y-4,9,9,RGB(92,48,28));
    for(int i=0;i<11;i++) rect(c,x-5+i,y-5+i,2,2,RGB(224,146,59));
}

static void face(canvas *c,int x,int y)
{
    tri(c,x-7,y-5,x-9,y-11,x,y-6,orange);
    tri(c,x+7,y-5,x+9,y-11,x,y-6,orange);
    rect(c,x-6,y-6,13,11,orange);rect(c,x-4,y+5,9,3,RGB(231,179,100));
    rect(c,x-4,y-4,3,4,ink);rect(c,x+2,y-4,3,4,ink);
    rect(c,x-3,y-2,2,2,RGB(11,28,35));rect(c,x+2,y-2,2,2,RGB(11,28,35));
    rect(c,x-2,y+2,5,3,RGB(39,27,24));
}

static void counter(canvas *c,int x,const char *label,int value,int total,int icon)
{
    panel(c,x,46,98,33);
    if(icon==0) fruit(c,x+15,63);
    else if(icon==1) crate(c,x+15,63);
    else face(c,x+15,63);
    text(c,x+30,50,1,label,muted,x+96);
    char count[32];
    if(total>=0) snprintf(count,sizeof(count),"%d/%d",value,total);
    else snprintf(count,sizeof(count),"%02d",value);
    text(c,x+30,61,total>=0?1:2,count,ink,x+96);
}

typedef struct { int x, width; } button_bounds;
static const button_bounds buttons[4]={{10,68},{84,68},{158,76},{240,70}};
static const button_bounds tabs[2]={{10,142},{160,150}};

bottom_ui_action bottom_ui_hit_test(int x,int y,int paused)
{
    static const bottom_ui_action normal[4]={BOTTOM_UI_SAVE,BOTTOM_UI_LOAD,BOTTOM_UI_HUB,BOTTOM_UI_CAMERA};
    static const bottom_ui_action pause[4]={BOTTOM_UI_RESUME,BOTTOM_UI_RESTART,BOTTOM_UI_HUB,BOTTOM_UI_TITLE};
    if(y>=218 && y<236) {
        for(int i=0;i<2;i++)
            if(x>=tabs[i].x && x<tabs[i].x+tabs[i].width)
                return i==0?BOTTOM_UI_STATUS_PAGE:BOTTOM_UI_TRIAL_PAGE;
    }
    if(y<168 || y>=196) return BOTTOM_UI_NONE;
    for(int i=0;i<4;i++)
        if(x>=buttons[i].x && x<buttons[i].x+buttons[i].width)
            return paused?pause[i]:normal[i];
    return BOTTOM_UI_NONE;
}

static void mask(canvas *c,int x,int y,int tier)
{
    color wood=tier>1?RGB(237,190,52):RGB(148,83,41);
    rect(c,x,y,8,10,tier?wood:muted);
    rect(c,x+1,y+2,2,2,RGB(42,210,97));
    rect(c,x+5,y+2,2,2,RGB(42,210,97));
    rect(c,x+2,y+7,4,2,RGB(36,23,28));
    for(int i=0;i<3;i++) rect(c,x+i*3,y-4-i%2,2,5,tier?RGB(181-i*40,69+i*51,180-i*55):muted);
}

/* Small silhouettes stay legible at the native touch-screen resolution. */
static void power_icon(canvas *c,int x,int y,int kind,color col)
{
    if(kind==0) { /* downward slam */
        rect(c,x-2,y-5,4,6,col);tri(c,x-7,y,x+7,y,x,y+6,col);
    } else if(kind==1) { /* second jump */
        rect(c,x-2,y-1,4,6,col);tri(c,x-7,y,x,y-6,x+7,y,col);
    } else if(kind==2) { /* tornado */
        rect(c,x-7,y-5,14,2,col);rect(c,x-5,y-1,10,2,col);rect(c,x-2,y+3,4,2,col);
    } else if(kind==3) { /* bazooka */
        rect(c,x-7,y-3,13,5,col);rect(c,x-3,y+2,3,4,col);rect(c,x+5,y-5,3,9,col);
    } else { /* speed dash */
        rect(c,x-8,y-4,6,1,col);rect(c,x-10,y-1,6,1,col);
        tri(c,x-2,y-5,x+7,y,x-2,y+5,col);
    }
}

static void button(canvas *c,int x,int w,const char *label,int accent)
{
    panel(c,x,168,w,28);
    rect(c,x+1,169,w-2,1,accent?orange:cyan);
    rect(c,x+1,194,w-2,1,RGB(26,58,73));
    text(c,x+(w-(int)strlen(label)*6)/2,179,1,label,accent?orange:ink,x+w-3);
}

void bottom_ui_format_time(char *out,size_t capacity,uint32_t ms,int available)
{
    if(!out || !capacity) return;
    if(!available) snprintf(out,capacity,"--:--.--");
    else snprintf(out,capacity,"%02u:%02u.%02u",(unsigned)(ms/60000),
                  (unsigned)((ms/1000)%60),(unsigned)((ms/10)%100));
}

static void time_value(canvas *c,int right,int y,uint32_t ms,int available,color col)
{
    char value[24];
    bottom_ui_format_time(value,sizeof(value),ms,available);
    int scale=strlen(value)>8?1:2;
    text(c,right-(int)strlen(value)*6*scale,y,scale,value,col,right);
}

static void trial_page(canvas *c,const bottom_ui_state *s)
{
    const char *names[3]={"SAPPHIRE","GOLD","PLATINUM"};
    color medals[3]={RGB(61,149,245),RGB(248,185,51),RGB(203,231,239)};
    for(int i=0;i<3;i++) {
        int y=46+i*27;
        panel(c,8,y,304,25);
        gem(c,25,y+12,medals[i]);
        text(c,43,y+4,1,names[i],medals[i],160);
        text(c,43,y+15,1,s->saved_relic_tier==i+1?"SAVED RELIC":"TARGET",muted,160);
        time_value(c,305,y+6,s->trial_targets_ms[i],s->trial_targets_available,ink);
    }
    panel(c,8,132,304,29);
    text(c,16,136,1,"BEST SAVED",cyan,120);
    const char *tier=s->saved_relic_tier>=1 && s->saved_relic_tier<=3?
        names[s->saved_relic_tier-1]:"NONE";
    text(c,16,150,1,tier,muted,120);
    if(s->trial_targets_available) time_value(c,305,140,s->best_time_ms,s->best_time_ms!=0,ink);
    else text(c,131,143,1,"NO TARGETS FOR THIS AREA",muted,306);
}

static void page_tabs(canvas *c,bottom_ui_page page)
{
    const char *labels[2]={"STATUS","TIME TRIAL"};
    for(int i=0;i<2;i++) {
        panel(c,tabs[i].x,218,tabs[i].width,18);
        rect(c,tabs[i].x+1,219,tabs[i].width-2,1,(int)page==i?orange:muted);
        text(c,tabs[i].x+(tabs[i].width-(int)strlen(labels[i])*6)/2,224,1,
             labels[i],(int)page==i?orange:muted,tabs[i].x+tabs[i].width-2);
    }
}

int bottom_ui_build(level_vertex *vertices,int capacity,const bottom_ui_state *s)
{
    if(!vertices || capacity<3 || !s) return 0;
    canvas c={vertices,capacity,0};
    rect(&c,0,0,320,240,RGB(6,13,21));
    for(int row=3;row<238;row+=4) rect(&c,3,row,314,1,RGB(9,20,29));
    frame(&c,2,2,316,236,RGB(37,75,94));
    rect(&c,2,2,34,2,cyan);rect(&c,284,236,34,2,orange);
    panel(&c,8,7,304,33);
    text(&c,15,11,1,s->page==BOTTOM_PAGE_TRIAL?"TIME TRIAL / PORT TARGETS":s->aiming?"PAD AIM / B Y R FIRE / RELEASE L X":"PAD MOVE / A JUMP / B Y SPIN / R SLIDE",muted,260);
    text(&c,15,23,2,s->level_name?s->level_name:"WARPED",ink,260);
    rect(&c,264,12,1,23,RGB(41,70,86));
    char buffer[32];
    snprintf(buffer,sizeof(buffer),"%u%%",(unsigned)s->completion);
    text(&c,274,22,1,buffer,orange,307);
    if(s->page==BOTTOM_PAGE_TRIAL && !s->paused) trial_page(&c,s);
    else {
    counter(&c,8,"WUMPA",s->wumpa,-1,0);
    counter(&c,111,"BOXES",s->boxes,s->boxes_total,1);
    counter(&c,214,"LIVES",s->lives,-1,2);
    panel(&c,8,85,304,42);
    const char *labels[3]={"CRYSTALS","GEMS","RELICS"};
    uint32_t values[3]={s->crystals,s->gems,s->relics};
    int totals[3]={25,45,30};
    color colors[3]={RGB(194,71,232),RGB(44,200,234),RGB(249,185,59)};
    for(int i=0;i<3;i++) {
        int x=10+i*101;
        if(i) rect(&c,x-1,94,1,25,RGB(32,56,69));
        gem(&c,x+13,107,colors[i]);
        text(&c,x+27,92,1,labels[i],muted,x+98);
        snprintf(buffer,sizeof(buffer),"%02u/%d",(unsigned)values[i],totals[i]);
        text(&c,x+27,106,1,buffer,ink,x+98);
    }
    panel(&c,8,132,304,29);
    if(s->paused) {
        text(&c,16,136,1,"PAUSED",orange,110);
        const char *options[4]={"CONTINUE","RESTART LEVEL","WARP ROOM","TITLE SCREEN"};
        text(&c,112,136,1,options[s->pause_index>=0 && s->pause_index<4?s->pause_index:0],ink,306);
        text(&c,16,150,1,"UP/DOWN SELECT   A OK   B RESUME",muted,306);
    } else if(s->context) {
        text(&c,16,136,1,s->context,cyan,306);
        text(&c,16,150,1,s->context_detail,ink,306);
    } else {
        const char *powers[5]={"SLAM","JUMP2","SPIN","AIM","DASH"};
        for(int i=0;i<5;i++) {
            int x=16+i*60;
            int active=(s->powers & (1u<<i))!=0;
            power_icon(&c,x+22,141,i,active?RGB(73,236,114):muted);
            text(&c,x+7,152,1,powers[i],active?ink:muted,x+57);
        }
    }
    }
    const char *normal[4]={"SAVE SD","LOAD SD","WARP HUB","CAMERA"};
    const char *pause[4]={"RESUME","RESTART","WARP HUB","TITLE"};
    for(int i=0;i<4;i++) button(&c,buttons[i].x,buttons[i].width,s->paused?pause[i]:normal[i],i==2);
    mask(&c,13,208,s->aku_aku);
    snprintf(buffer,sizeof(buffer),"AKU %d",s->aku_aku);
    text(&c,26,205,1,buffer,s->aku_aku?cyan:muted,83);
    if(s->time_trial) {
        unsigned ms=s->time_trial_ms;
        snprintf(buffer,sizeof(buffer),"%02u:%02u.%02u",ms/60000,(ms/1000)%60,(ms/10)%100);
        text(&c,88,205,1,buffer,orange,158);
    } else text(&c,88,205,1,"ADVENTURE",muted,158);
    if(s->boss_active) {
        int hp=s->boss_hp<0?0:s->boss_hp;
        int maximum=s->boss_max_hp>0?s->boss_max_hp:1;
        if(hp>maximum) hp=maximum;
        text(&c,166,205,1,"BOSS",orange,195);
        frame(&c,199,204,109,9,muted);
        rect(&c,201,206,105*hp/maximum,5,RGB(224,67,48));
    } else text(&c,166,205,1,s->aiming?"B/Y FIRE WUMPA":"START PAUSE",muted,309);
    if(s->message) {
        rect(&c,8,201,304,14,RGB(9,20,29));
        text(&c,13,205,1,s->message,orange,309);
    }
    page_tabs(&c,s->page);
    return c.count;
}
