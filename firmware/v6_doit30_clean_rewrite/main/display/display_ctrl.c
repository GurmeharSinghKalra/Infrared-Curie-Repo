#include "display_ctrl.h"
#include "board/board_config.h"
#include "state/robot_state.h"
#include "drivers/max7219_bsp.h"
#include "display/mouth_data.h"
#include "u8g2.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/i2c.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DISPLAY";

/* ── geometry constants (sized for 0.96″ 128×64 OLED) ── */
#define CX 64
#define CY 32
#define I2C_BUF 1024

static u8g2_t u8g2_L, u8g2_R;

/* ── I2C plumbing (unchanged) ── */
static void init_i2c_ports(void) {
    i2c_config_t c0={.mode=I2C_MODE_MASTER,.sda_io_num=CURIE_LEFT_OLED_SDA_GPIO,.scl_io_num=CURIE_LEFT_OLED_SCL_GPIO,.sda_pullup_en=GPIO_PULLUP_ENABLE,.scl_pullup_en=GPIO_PULLUP_ENABLE,.master.clk_speed=CURIE_OLED_I2C_SPEED_HZ};
    ESP_ERROR_CHECK(i2c_param_config(CURIE_LEFT_OLED_I2C_PORT,&c0));
    ESP_ERROR_CHECK(i2c_driver_install(CURIE_LEFT_OLED_I2C_PORT,c0.mode,0,0,0));
    i2c_config_t c1={.mode=I2C_MODE_MASTER,.sda_io_num=CURIE_RIGHT_OLED_SDA_GPIO,.scl_io_num=CURIE_RIGHT_OLED_SCL_GPIO,.sda_pullup_en=GPIO_PULLUP_ENABLE,.scl_pullup_en=GPIO_PULLUP_ENABLE,.master.clk_speed=CURIE_OLED_I2C_SPEED_HZ};
    ESP_ERROR_CHECK(i2c_param_config(CURIE_RIGHT_OLED_I2C_PORT,&c1));
    ESP_ERROR_CHECK(i2c_driver_install(CURIE_RIGHT_OLED_I2C_PORT,c1.mode,0,0,0));
}
static uint8_t i2c_left(u8x8_t*u,uint8_t msg,uint8_t a,void*p){
    static uint8_t buf[I2C_BUF]; static uint16_t idx=0;
    switch(msg){case U8X8_MSG_BYTE_SEND:if(idx+a<=I2C_BUF){memcpy(&buf[idx],p,a);idx+=a;}break;
    case U8X8_MSG_BYTE_START_TRANSFER:idx=0;break;
    case U8X8_MSG_BYTE_END_TRANSFER:i2c_master_write_to_device(CURIE_LEFT_OLED_I2C_PORT,u8x8_GetI2CAddress(u)>>1,buf,idx,pdMS_TO_TICKS(100));break;}return 1;
}
static uint8_t i2c_right(u8x8_t*u,uint8_t msg,uint8_t a,void*p){
    static uint8_t buf[I2C_BUF]; static uint16_t idx=0;
    switch(msg){case U8X8_MSG_BYTE_SEND:if(idx+a<=I2C_BUF){memcpy(&buf[idx],p,a);idx+=a;}break;
    case U8X8_MSG_BYTE_START_TRANSFER:idx=0;break;
    case U8X8_MSG_BYTE_END_TRANSFER:i2c_master_write_to_device(CURIE_RIGHT_OLED_I2C_PORT,u8x8_GetI2CAddress(u)>>1,buf,idx,pdMS_TO_TICKS(100));break;}return 1;
}
static uint8_t gpio_delay(u8x8_t*u,uint8_t msg,uint8_t a,void*p){if(msg==U8X8_MSG_DELAY_MILLI)vTaskDelay(pdMS_TO_TICKS(a));return 1;}

/* ── helpers ── */
static uint8_t rev8(uint8_t v){v=((v&0xF0)>>4)|((v&0x0F)<<4);v=((v&0xCC)>>2)|((v&0x33)<<2);v=((v&0xAA)>>1)|((v&0x55)<<1);return v;}
static int clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static int ease_sin(int phase,int period,int amp){
    /* integer sine approximation: triangle wave smoothed */
    int p=phase%period; int half=period/2;
    int t=p<half?p:period-p; /* 0..half..0 */
    return (t*amp*2)/period - amp/2;
}

/* ── eye drawing primitives ── */
static void draw_ellipse_eye(u8g2_t*g,int rx,int ry){
    u8g2_DrawFilledEllipse(g,CX,CY,rx,ry,U8G2_DRAW_ALL);
}
static void draw_pupil(u8g2_t*g,int dx,int dy,int r){
    int px=CX+dx, py=CY+dy;
    u8g2_SetDrawColor(g,0);
    u8g2_DrawDisc(g,px,py,r,U8G2_DRAW_ALL);
    u8g2_SetDrawColor(g,1);
    /* highlight */
    u8g2_DrawDisc(g,px-r/2-1,py-r/2-1,clamp(r/3,1,3),U8G2_DRAW_ALL);
}
static void draw_eyelid_top(u8g2_t*g,int drop){
    /* black rectangle covering top of eye */
    if(drop>0){u8g2_SetDrawColor(g,0);u8g2_DrawBox(g,0,0,128,CY-25+drop);u8g2_SetDrawColor(g,1);}
}
static void draw_brow(u8g2_t*g,bool left,int oy,int iy,int thick){
    int ox=left?18:110, ix=left?110:18;
    for(int t=0;t<thick;t++) u8g2_DrawLine(g,ox,oy+t,ix,iy+t);
}

/* ── per-expression eye renderers ── */
static void eye_happy(u8g2_t*g,bool left,int ph){
    /* crescent: draw full ellipse then erase top portion */
    draw_ellipse_eye(g,24,18);
    u8g2_SetDrawColor(g,0);
    u8g2_DrawBox(g,CX-26,CY-20,52,16+ease_sin(ph,8,3));
    u8g2_SetDrawColor(g,1);
    int bounce=ease_sin(ph,6,2);
    draw_brow(g,left,8+bounce,12+bounce,3);
}
static void eye_sad(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,22,16);
    draw_eyelid_top(g,8);
    draw_pupil(g,left?-2:2,4,5);
    draw_brow(g,left,left?10:16,left?16:10,3);
    /* tear */
    int tear_y=(ph*6)%40;
    u8g2_DrawDisc(g,CX+(left?-12:12),CY+10+tear_y,2,U8G2_DRAW_ALL);
    if(tear_y>5)u8g2_DrawDisc(g,CX+(left?-12:12),CY+10+tear_y-5,1,U8G2_DRAW_ALL);
}
static void eye_angry(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,22,10);
    draw_pupil(g,left?2:-2,0,4);
    /* heavy V-brow */
    int twitch=(ph%4==0)?1:0;
    draw_brow(g,left,left?18:8+twitch,left?8+twitch:18,4);
}
static void eye_fear(u8g2_t*g,bool left,int ph){
    int shake=ease_sin(ph,4,2);
    u8g2_DrawFilledEllipse(g,CX+shake,CY,24,22,U8G2_DRAW_ALL);
    draw_pupil(g,shake,0,3); /* tiny pupil */
    draw_brow(g,left,left?12:8,left?8:12,2);
}
static void eye_disgust(u8g2_t*g,bool left,int ph){
    int ry=left?10:16;
    draw_ellipse_eye(g,20,ry);
    if(left) draw_eyelid_top(g,6);
    draw_pupil(g,left?4:-4,left?0:2,4);
    draw_brow(g,left,left?14:18,left?18:14,3);
}
static void eye_confused(u8g2_t*g,bool left,int ph){
    int r=left?16:22;
    draw_ellipse_eye(g,20,r);
    int pdx=ease_sin(ph+(left?0:4),8,5);
    draw_pupil(g,pdx,ease_sin(ph+2,6,2),5);
    draw_brow(g,left,left?14:20,left?20:14,2);
}
static void eye_contempt(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,22,14);
    draw_eyelid_top(g,6);
    int side=ease_sin(ph,12,3);
    draw_pupil(g,left?3+side:-3+side,1,5);
    draw_brow(g,left,left?16:12,left?12:16,3);
}
static void eye_think(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,20,16);
    int drift=ease_sin(ph,10,4);
    draw_pupil(g,3+drift,-3,5);
    draw_brow(g,left,14,16,2);
}
static void eye_shy(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,18,12);
    draw_eyelid_top(g,8);
    int glance=(ph%8<2)?-4:2;
    draw_pupil(g,left?-3+glance:3-glance,3,4);
    draw_brow(g,left,14,16,2);
}
static void eye_needy(u8g2_t*g,bool left,int ph){
    /* big sparkle eyes */
    int bounce=ease_sin(ph,8,2);
    u8g2_DrawFilledEllipse(g,CX,CY+bounce,26,22,U8G2_DRAW_ALL);
    draw_pupil(g,0,bounce,8);
    /* extra sparkle highlights */
    u8g2_DrawDisc(g,CX-8,CY-8+bounce,3,U8G2_DRAW_ALL);
    u8g2_DrawDisc(g,CX+5,CY-12+bounce,2,U8G2_DRAW_ALL);
    draw_brow(g,left,10+bounce,14+bounce,2);
}
static void eye_surprised(u8g2_t*g,bool left,int ph){
    u8g2_DrawFilledEllipse(g,CX,CY,26,24,U8G2_DRAW_ALL);
    draw_pupil(g,0,ease_sin(ph,6,1),6);
    draw_brow(g,left,6,6,2);
}
static void eye_excited(u8g2_t*g,bool left,int ph){
    int bounce=ease_sin(ph,4,3);
    u8g2_DrawFilledEllipse(g,CX,CY+bounce,24,20,U8G2_DRAW_ALL);
    draw_pupil(g,ease_sin(ph,3,3),bounce,5);
    draw_brow(g,left,8+bounce,12+bounce,3);
}
static void eye_neutral(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,22,16);
    draw_pupil(g,0,0,5);
    draw_brow(g,left,12,12,2);
}
static void eye_wink(u8g2_t*g,bool left,int ph){
    if(!left){
        /* right eye closed */
        u8g2_DrawLine(g,CX-22,CY,CX+22,CY);
        u8g2_DrawLine(g,CX-22,CY+1,CX+22,CY+1);
        u8g2_DrawLine(g,CX-22,CY+2,CX+22,CY+2);
        draw_brow(g,left,16,12,3);
    } else {
        draw_ellipse_eye(g,22,18);
        draw_pupil(g,-2,-1,5);
        draw_brow(g,left,10,14,3);
    }
}
static void eye_love(u8g2_t*g,bool left,int ph){
    int b=ease_sin(ph,8,2);
    /* heart shape */
    u8g2_DrawDisc(g,CX-8,CY-4+b,10,U8G2_DRAW_ALL);
    u8g2_DrawDisc(g,CX+8,CY-4+b,10,U8G2_DRAW_ALL);
    u8g2_DrawTriangle(g,CX-18,CY+b,CX+18,CY+b,CX,CY+18+b);
}
static void eye_sleep(u8g2_t*g,bool left,int ph){
    u8g2_DrawLine(g,CX-22,CY,CX+22,CY);
    u8g2_DrawLine(g,CX-22,CY+1,CX+22,CY+1);
    u8g2_DrawLine(g,CX-20,CY+2,CX+20,CY+2);
    draw_brow(g,left,14+(ph&1),16+(ph&1),2);
    /* Z */
    int zx=CX+16, zy=CY-14-(ph%3)*2;
    u8g2_DrawLine(g,zx,zy,zx+8,zy);
    u8g2_DrawLine(g,zx+8,zy,zx,zy+8);
    u8g2_DrawLine(g,zx,zy+8,zx+8,zy+8);
}
static void eye_scan(u8g2_t*g,bool left,int ph){
    draw_ellipse_eye(g,22,16);
    int scan_x=(ph-4)*4;
    draw_pupil(g,scan_x,0,4);
    draw_brow(g,left,12,12,2);
}
static void eye_lost(u8g2_t*g,bool left,int ph){
    (void)left;(void)ph;
    u8g2_DrawLine(g,CX-14,CY-14,CX+14,CY+14);
    u8g2_DrawLine(g,CX-14,CY+14,CX+14,CY-14);
    u8g2_DrawLine(g,CX-15,CY-14,CX+13,CY+14);
    u8g2_DrawLine(g,CX-13,CY+14,CX+15,CY-14);
}

/* ── expression dispatch ── */
typedef void (*eye_fn_t)(u8g2_t*,bool,int);
static const eye_fn_t EYE_FN[EXP_COUNT] = {
    [EXP_NEUTRAL]=eye_neutral, [EXP_HAPPY]=eye_happy, [EXP_SAD]=eye_sad,
    [EXP_ANGRY]=eye_angry, [EXP_FEAR]=eye_fear, [EXP_DISGUST]=eye_disgust,
    [EXP_CONFUSED]=eye_confused, [EXP_CONTEMPT]=eye_contempt,
    [EXP_THOUGHTFUL]=eye_think, [EXP_SHY]=eye_shy, [EXP_FUNNY]=eye_needy,
    [EXP_SURPRISED]=eye_surprised, [EXP_EXCITED]=eye_excited,
    [EXP_WINK]=eye_wink, [EXP_LOVE]=eye_love, [EXP_SLEEP]=eye_sleep,
    [EXP_SCAN]=eye_scan, [EXP_LOST]=eye_lost, [EXP_CUSTOM]=eye_neutral,
};

/* mouth frame tables */
typedef struct { const uint16_t(*f)[8]; uint8_t n; uint8_t spd; } mdata_t;
static const mdata_t MDATA[EXP_COUNT] = {
    [EXP_NEUTRAL]={M_NEUTRAL,1,1}, [EXP_HAPPY]={M_HAPPY,3,10},
    [EXP_SAD]={M_SAD,3,14}, [EXP_ANGRY]={M_ANGRY,3,8},
    [EXP_FEAR]={M_FEAR,2,6}, [EXP_DISGUST]={M_DISGUST,2,12},
    [EXP_CONFUSED]={M_CONFUSED,3,10}, [EXP_CONTEMPT]={M_CONTEMPT,2,16},
    [EXP_THOUGHTFUL]={M_THINK,2,18}, [EXP_SHY]={M_SHY,2,14},
    [EXP_FUNNY]={M_NEEDY,2,12}, [EXP_SURPRISED]={M_SURPRISED,2,8},
    [EXP_EXCITED]={M_EXCITED,3,6}, [EXP_WINK]={M_WINK,1,1},
    [EXP_LOVE]={M_LOVE,1,1}, [EXP_SLEEP]={M_SLEEP,2,20},
    [EXP_SCAN]={M_SCAN,2,6}, [EXP_LOST]={M_LOST,1,1},
    [EXP_CUSTOM]={M_NEUTRAL,1,1},
};

/* per-expression blink timing (min,max ticks) */
static const uint8_t BLINK_TIMING[EXP_COUNT][2] = {
    [EXP_NEUTRAL]={60,120}, [EXP_HAPPY]={40,80}, [EXP_SAD]={80,160},
    [EXP_ANGRY]={100,200}, [EXP_FEAR]={20,40}, [EXP_DISGUST]={70,140},
    [EXP_CONFUSED]={50,100}, [EXP_CONTEMPT]={80,160},
    [EXP_THOUGHTFUL]={70,140}, [EXP_SHY]={30,60}, [EXP_FUNNY]={60,120},
    [EXP_SURPRISED]={80,160}, [EXP_EXCITED]={25,50},
    [EXP_WINK]={120,200}, [EXP_LOVE]={50,100}, [EXP_SLEEP]={200,255},
    [EXP_SCAN]={80,140}, [EXP_LOST]={100,200}, [EXP_CUSTOM]={60,120},
};

/* ── draw both eyes ── */
static void render_eyes(robot_expression_t exp, int phase){
    eye_fn_t fn = EYE_FN[exp];
    if(!fn) fn = eye_neutral;
    /* left */
    u8g2_ClearBuffer(&u8g2_L);
    u8g2_SetDrawColor(&u8g2_L,1);
    fn(&u8g2_L,true,phase);
    /* right */
    u8g2_ClearBuffer(&u8g2_R);
    u8g2_SetDrawColor(&u8g2_R,1);
    fn(&u8g2_R,false,phase);
    /* send both as close together as possible */
    u8g2_SendBuffer(&u8g2_L);
    u8g2_SendBuffer(&u8g2_R);
}

/* ── blink ── */
static void render_blink(void){
    u8g2_ClearBuffer(&u8g2_L);u8g2_SetDrawColor(&u8g2_L,1);
    u8g2_DrawRBox(&u8g2_L,CX-28,CY-2,56,5,2);
    u8g2_ClearBuffer(&u8g2_R);u8g2_SetDrawColor(&u8g2_R,1);
    u8g2_DrawRBox(&u8g2_R,CX-28,CY-2,56,5,2);
    u8g2_SendBuffer(&u8g2_L);
    u8g2_SendBuffer(&u8g2_R);
}

/* ── mouth rendering with mask ── */
static void render_mouth(robot_expression_t exp, int phase){
    if(exp==EXP_CUSTOM){
        uint8_t custom[16]; robot_get_custom_mouth(custom);
        int ld=CURIE_MOUTH_SWAP_HALVES?1:0, rd=CURIE_MOUTH_SWAP_HALVES?0:1;
        for(int r=0;r<8;r++){
            uint8_t lb=custom[r], rb=custom[8+r];
            if(CURIE_MOUTH_LEFT_FLIP_COLS) lb=rev8(lb);
            if(CURIE_MOUTH_RIGHT_FLIP_COLS) rb=rev8(rb);
            lb &= MOUTH_MASK_LEFT[r]; rb &= MOUTH_MASK_RIGHT[r];
            max7219_set_row(ld,r,lb); max7219_set_row(rd,r,rb);
        }
        return;
    }
    if(exp<0||exp>=EXP_COUNT) exp=EXP_HAPPY;
    const mdata_t *md = &MDATA[exp];
    int fi=0;
    if(md->n>1){ int p=md->spd>0?md->spd:1; fi=(phase/p)%md->n; }
    const uint16_t *rows = md->f[fi];

    int ld=CURIE_MOUTH_SWAP_HALVES?1:0, rd=CURIE_MOUTH_SWAP_HALVES?0:1;
    for(int r=0;r<8;r++){
        int slr=CURIE_MOUTH_LEFT_FLIP_ROWS?(7-r):r;
        int srr=CURIE_MOUTH_RIGHT_FLIP_ROWS?(7-r):r;
        uint8_t lb=(rows[slr]>>8)&0xFF, rb=rows[srr]&0xFF;
        if(CURIE_MOUTH_LEFT_FLIP_COLS) lb=rev8(lb);
        if(CURIE_MOUTH_RIGHT_FLIP_COLS) rb=rev8(rb);
        lb &= MOUTH_MASK_LEFT[r]; rb &= MOUTH_MASK_RIGHT[r];
        max7219_set_row(ld,r,lb); max7219_set_row(rd,r,rb);
    }
}

/* ── main display task ── */
void task_display(void *arg){
    ESP_LOGI(TAG,"Initializing Displays...");
    init_i2c_ports();
    max7219_init();

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_L,U8G2_R0,i2c_left,gpio_delay);
    u8g2_SetI2CAddress(&u8g2_L,CURIE_LEFT_OLED_I2C_ADDR);
    u8g2_InitDisplay(&u8g2_L); u8g2_SetPowerSave(&u8g2_L,0);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2_R,U8G2_R0,i2c_right,gpio_delay);
    u8g2_SetI2CAddress(&u8g2_R,CURIE_RIGHT_OLED_I2C_ADDR);
    u8g2_InitDisplay(&u8g2_R); u8g2_SetPowerSave(&u8g2_R,0);

    robot_expression_t last_exp=-1;
    int last_bright=-1; bool was_on=true;
    int blink_ctr=0, next_blink=60+(esp_random()%60);
    int anim_phase=0;

    while(1){
        bool is_on=robot_get_power();
        robot_state_t st=robot_state_get();

        if(!is_on){
            if(was_on){
                u8g2_ClearBuffer(&u8g2_L);u8g2_SendBuffer(&u8g2_L);
                u8g2_ClearBuffer(&u8g2_R);u8g2_SendBuffer(&u8g2_R);
                max7219_clear();max7219_shutdown(true);was_on=false;
            }
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        } else if(!was_on){
            max7219_shutdown(false);last_exp=-1;last_bright=-1;was_on=true;
        }

        robot_expression_t cur=robot_get_expression();
        int bright=robot_get_brightness();
        if(st==ROBOT_STATE_ERROR) cur=EXP_SAD;
        if(st==ROBOT_STATE_LOW_POWER) bright=1;

        /* expression changed — reset blink timer */
        if(cur!=last_exp){
            blink_ctr=0;
            uint8_t bmin=BLINK_TIMING[cur][0], bmax=BLINK_TIMING[cur][1];
            next_blink=bmin+(esp_random()%(bmax-bmin+1));
        }

        bool force_blink=robot_get_and_clear_blink();
        blink_ctr++;

        /* blink (skip for sleep/lost/love) */
        if((blink_ctr>=next_blink||force_blink) && cur!=EXP_SLEEP && cur!=EXP_LOST && cur!=EXP_LOVE){
            render_blink();
            vTaskDelay(pdMS_TO_TICKS(100));
            blink_ctr=0;
            uint8_t bmin=BLINK_TIMING[cur][0], bmax=BLINK_TIMING[cur][1];
            next_blink=bmin+(esp_random()%(bmax-bmin+1));
            last_exp=-1; /* force redraw after blink */
        }

        anim_phase++;
        /* always redraw for animated expressions, or on change */
        render_eyes(cur, anim_phase);
        render_mouth(cur, anim_phase);
        last_exp=cur;

        if(bright!=last_bright){
            max7219_set_intensity(bright);last_bright=bright;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
