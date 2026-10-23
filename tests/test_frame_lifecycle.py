"""Exercise production frame boundaries against an asynchronous GPU model.

The opening gate, title exit, frame submission and complete cleanup sequence
are copied unchanged from main.c. Game logic and drawing between boundaries
are replaced by resource mutation checks. This models queue ownership, not
PICA commands or the emulator's display backend.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SDK = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
enum { C3D_FRAME_SYNCDRAW=1 };
static int pending, opened, reject_next, begins, ends, mutations, releases;
static int initialized, finalized, fail_initialization;
static int stream_phase, stream_polls;
static void *s_charbuf, *s_propbuf, *s_textbuf, *s_bottom_buf;
static void *s_vshader_dvlb;
static int s_program;
/* A display refresh is deliberately allowed while the GPU remains busy. */
static void C3D_FrameSync(void) {}
static int C3D_FrameBegin(int flags) {
    assert(initialized && !opened);
    if (flags & C3D_FRAME_SYNCDRAW) C3D_FrameSync();
    if (reject_next) { reject_next=0; return 0; }
    pending=0; opened=1; begins++; return 1;
}
static void C3D_FrameEnd(int flags) {
    (void)flags; assert(opened); opened=0; pending=1; ends++;
}
static void C3D_Fini(void) {
    assert(initialized && !opened); pending=0; initialized=0; finalized++;
}
/* citro2d owns its own frame hook and is finalized after citro3d. */
static void C2D_Fini(void) { assert(!opened); }
static void boot_trace(const char *s) { (void)s; }
static void mutate_level_or_vertices(void) {
    assert(!pending && opened); mutations++;
}
static void release_resource(void) { assert(!pending && !opened); releases++; }
static void world_texture_exit(void) { release_resource(); }
static void level_unload(void) { release_resource(); }
static void level_sd_close(void) { release_resource(); }
static void linearFree(void *p) { assert(p); release_resource(); }
static int audio_stream_poll_all(void) { assert(stream_phase==0); stream_polls++; return 0; }
static void audio_stream_stop_all(void) { assert(stream_phase==0); stream_phase=1; }
static int close_level_runtime(void) { assert(stream_phase==1); stream_phase=2; return 0; }
static void audio_exit(void) { assert(stream_phase==2); stream_phase=3; release_resource(); }
static void audio_stream_release_all_after_ndsp_exit(void) { assert(stream_phase==3); stream_phase=4; }
static void shaderProgramFree(void *p) { assert(p); release_resource(); }
static void DVLB_Free(void *p) { assert(p); release_resource(); }
static void gfxExit(void) { release_resource(); }
'''


def main():
    source = (ROOT / "source/main.c").read_text(encoding="utf-8")
    loop = source.split("while (aptMainLoop()) {", 1)[1]
    opening = loop.split("u64 update_time", 1)[0]
    title_exit = loop.split("} else if (kDown & KEY_SELECT) {", 1)[1].split("}", 1)[0]
    # Keep the gate's actual location: any gate left at render time is too late
    # for the simulated level/vertex replacement immediately before it.
    render = loop.split("/* Render Dual-Screen Frame", 1)[1]
    render_gate = render.split("/* TOP SCREEN RENDERING */", 1)[0]
    render_gate = render_gate.split("*/", 1)[1].strip()
    submission = "C3D_FrameEnd(0);"
    assert submission in render
    cleanup = source.split("\nshutdown:", 1)[1].split("return exit_code;", 1)[0]
    harness = SDK + r'''
static int run(int quit_on_frame, int reject_first, int initialization_failed) {
    int exit_code=initialization_failed, c3d_ready=!initialization_failed;
    int c2d_ready=c3d_ready;
    initialized=c3d_ready; fail_initialization=initialization_failed;
    reject_next=reject_first; pending=c3d_ready; opened=0;
    begins=ends=mutations=releases=finalized=0;
    stream_phase=stream_polls=0;
    s_charbuf=s_propbuf=s_textbuf=s_bottom_buf=s_vshader_dvlb=(void*)1;
    if (!initialization_failed) {
        for (int frame=0; frame<4; frame++) {
''' + opening + r'''
            mutate_level_or_vertices();
            if (quit_on_frame==frame) {
''' + title_exit + r'''
            }
''' + render_gate + r'''
            mutate_level_or_vertices();
''' + submission + r'''
        }
    }
''' + cleanup + r'''
    assert(!pending && !opened && begins==ends);
    assert(releases==10);
    assert(stream_phase==4);
    assert(stream_polls>=begins);
    assert(finalized==!fail_initialization);
    return mutations;
}
int main(void) {
    assert(run(-1,0,0)==8);  /* normal apt exit with a pending last frame */
    assert(begins==4);
    assert(run(0,0,0)==1);   /* Select exits after acquiring a frame */
    assert(begins==1);
    assert(run(2,0,0)==5);   /* exit after earlier frames were submitted */
    assert(run(-1,1,0)==6);  /* a rejected begin must skip mutations */
    assert(begins==3);
    assert(run(-1,0,1)==0);  /* partial initialization skips C3D_Fini */
    puts("PASS frame lifecycle: asynchronous GPU ownership, skipped frame, title exit and cleanup");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="crash-frame-") as folder:
        source_path = Path(folder) / "test.c"
        executable = Path(folder) / "test.exe"
        # Negative controls recreate both old unsafe orders in memory. The same
        # GPU model must reject them; production files are never modified.
        late_begin = harness.replace(opening, "\nC3D_FrameSync();\n", 1)
        late_begin = late_begin.replace("            mutate_level_or_vertices();",
                                        "            mutate_level_or_vertices();\n" + opening, 1)
        late_fini = cleanup.replace("    if (c3d_ready) C3D_Fini();", "", 1)
        late_fini = late_fini.replace("    gfxExit();",
                                      "    if (c3d_ready) C3D_Fini();\n    gfxExit();", 1)
        early_close = cleanup.replace("    audio_stream_stop_all();\n", "", 1)
        early_close = early_close.replace("    audio_exit();",
                                           "    audio_stream_stop_all();\n    audio_exit();", 1)
        variants = [("production", harness, None),
                    ("late frame acquire", late_begin, "!pending && opened"),
                    ("late GPU shutdown", harness.replace(cleanup, late_fini, 1),
                     "!pending && !opened"),
                    ("early audio pool release", harness.replace(
                        "    audio_exit();\n    audio_stream_release_all_after_ndsp_exit();",
                        "    audio_stream_release_all_after_ndsp_exit();\n    audio_exit();",1),
                     "stream_phase==3"),
                    ("early resource session close", harness.replace(cleanup, early_close, 1),
                     "stream_phase==1")]
        for label, program, expected_failure in variants:
            source_path.write_text(program, encoding="utf-8")
            subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11",
                            "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                            str(source_path), "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            if expected_failure:
                assert result.returncode != 0, label + " unexpectedly passed"
                assert expected_failure in result.stderr, result.stderr
                print("PASS negative control: " + label)
            else:
                assert result.returncode == 0, result.stdout + result.stderr
                print(result.stdout, end="")


if __name__ == "__main__":
    main()
