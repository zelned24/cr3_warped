/* Isolated CPU framebuffer probe: no Citro3D, shaders, textures or audio. */
#include <3ds.h>
#include <string.h>
int main(void) {
    gfxInitDefault();
    while(aptMainLoop()) {
        hidScanInput();
        if(hidKeysDown()&KEY_START)break;
        u8 *top=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
        u8 *bottom=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
        for(unsigned i=0;i<240*400;i++) {
            top[i*3]=0;top[i*3+1]=32;top[i*3+2]=200;
        }
        for(unsigned i=0;i<240*320;i++) {
            bottom[i*3]=200;bottom[i*3+1]=80;bottom[i*3+2]=0;
        }
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    gfxExit();return 0;
}
