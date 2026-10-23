#include "model_draw.h"

/* PSX-SPX, Graphics Processing Unit: GP0(E1h) Draw Mode/TPage and
 * https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
 * Semi-transparency. PICA normalized blend coefficients approximate the
 * native integer operations; rasterization and rounding remain unverified.
 */
static void native_blend(model_draw_state *state,uint32_t mode) {
    switch(mode) {
        case 0:
            state->blend_constant=0x80808080u;
            state->source_color=GPU_CONSTANT_COLOR;
            state->destination_color=GPU_CONSTANT_COLOR;
            break;
        case 1:
            state->source_color=GPU_ONE;
            state->destination_color=GPU_ONE;
            break;
        case 2:
            state->color_equation=GPU_BLEND_REVERSE_SUBTRACT;
            state->source_color=GPU_ONE;
            state->destination_color=GPU_ONE;
            break;
        case 3:
            state->blend_constant=0x40404040u;
            state->source_color=GPU_CONSTANT_COLOR;
            state->destination_color=GPU_ONE;
            break;
    }
}

int model_draw_packet_plan(const model_packet_command *command,uint32_t draw_mode,
    model_draw_packet_passes *output) {
    if(!command||!output||draw_mode>0x3fffu||command->kind>MODEL_PACKET_TRIANGLE||
       command->textured>1u||command->gouraud>1u||command->semi_transparent>1u||
       command->raw_texture>1u)return MODEL_DRAW_INVALID;
    model_draw_packet_passes next={.draw_mode=draw_mode};
    if(command->kind==MODEL_PACKET_DRAW_MODE)next.draw_mode=command->draw_mode&0x3fffu;
    else if(command->kind==MODEL_PACKET_TRIANGLE) {
        if(command->textured) {
            uint32_t page=command->uv[1]>>16;
            if(((page>>7)&3u)==3u)return MODEL_DRAW_INVALID;
            next.draw_mode=(draw_mode&~0x9ffu)|(page&0x9ffu);
        }
        next.count=1;
        next.states[0]=(model_draw_state){
            .cull=GPU_CULL_NONE,.alpha_test=GPU_ALWAYS,.depth_test=GPU_ALWAYS,
            .write_mask=GPU_WRITE_COLOR,.color_equation=GPU_BLEND_ADD,
            .alpha_equation=GPU_BLEND_ADD,.source_color=GPU_ONE,.destination_color=GPU_ZERO,
            .source_alpha=GPU_ONE,.destination_alpha=GPU_ZERO,
            .rgb_scale=command->textured&&!command->raw_texture?2u:1u
        };
        if(command->textured) {
            next.states[0].alpha_enabled=1;
            if(command->semi_transparent) {
                next.states[0].alpha_test=GPU_EQUAL;
                next.states[0].alpha_reference=255u;
                next.states[1]=next.states[0];
                next.states[1].alpha_reference=128u;
                native_blend(&next.states[1],(next.draw_mode>>5)&3u);
                next.count=2;
            } else next.states[0].alpha_test=GPU_GREATER;
        } else if(command->semi_transparent)native_blend(&next.states[0],(next.draw_mode>>5)&3u);
    }
    *output=next;return 0;
}
