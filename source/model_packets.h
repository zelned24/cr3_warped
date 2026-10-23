#ifndef MODEL_PACKETS_H
#define MODEL_PACKETS_H
#include "gool_domain.h"
#include "model_triangles.h"

#define MODEL_PACKET_WORDS 255u
enum {MODEL_PACKETS_DONE=0,MODEL_PACKETS_PENDING=1,MODEL_PACKETS_PACKET=2,
      MODEL_PACKETS_INVALID=-174,MODEL_PACKETS_CYCLE=-175};
typedef struct {uint32_t address,bytes;gool_domain_lease lease;} model_packet_span;
typedef struct {uint32_t packet,word,texture_eid;} model_packet_binding;
typedef struct {
    gool_domain *domain;
    model_packet_span spans[2];
    uint32_t words[MODEL_PACKET_WORDS];
    uint32_t cursor,address,next,count,copied,nodes,node_limit;
    int opened,status,loading,retained[2];
} model_packets;
/* Read the actual PS1 linked DMA chain after native emission completes.
 * Two explicit retained spans cover the real OT and primitive storage. Resolve
 * low24 tags only within those spans; reject aliases, wrap, unaligned links,
 * payload overruns and unmapped data. FFFFFF terminates; zero-length OT nodes
 * advance without producing a packet. Every step budget unit reads one tag or
 * payload word. A packet stays copied and pending until explicit consume.
 * A finite node limit derived from both spans rejects cyclic chains without
 * a heap/visited bitmap. It bounds damage, rather than accepting a cycle.
 * Caller excludes concurrent writes and overlays for both spans through close.
 * Begin failures after retention require close. Never copy the live owner.
 */
int model_packets_begin(model_packets *owner,gool_domain *domain,
    const model_packet_span spans[2],uint32_t first_low24);
int model_packets_step(model_packets *owner,uint32_t budget);
int model_packets_consume(model_packets *owner);
int model_packets_close(model_packets *owner);
enum {MODEL_PACKET_NOP=0,MODEL_PACKET_DRAW_MODE=1,MODEL_PACKET_TRIANGLE=2};
typedef struct {
    uint32_t colors[3],sxy[3],uv[3],draw_mode,next_word;
    uint8_t opcode,kind,textured,gouraud,semi_transparent,raw_texture;
} model_packet_command;
/* Decode one command from a fully copied pending DMA packet. A DMA packet
 * can contain multiple state/triangle commands (notably mode7); advance using
 * next_word only after the backend accepts it. Support emitted F3/G3/FT3/GT3,
 * E1 draw mode and00 padding, retaining raw coordinate/UV/state bits. Unknown
 * or truncated commands reject without changing output or consuming a packet.
 * No PS1 blend, texel-mask or viewport semantics are guessed here.
 */
int model_packets_decode(const model_packets *owner,uint32_t word,model_packet_command *output);
/* Bind the packet's actual texture index to its owning TGEO texture-name
 * table snapshot. The table must belong to this command's material context;
 * it cannot be replaced with the last actor's table during OT traversal.
 * Preserve modified page/blend bits and the fourth UV packed into UV2 high16.
 * Untextured commands reject rather than manufacturing a texture key. */
int model_packets_material(const model_packet_command *command,const uint32_t *texture_eids,
    uint32_t texture_count,model_material_uv *output);
/* Resolve immutable per-primitive provenance captured at native emission.
 * Bindings are strictly sorted by(packet,word), retained for this actual DMA
 * frame through presentation. Never use a later actor's texture-name table.
 */
int model_packets_bound_material(const model_packet_command *command,uint32_t packet,uint32_t word,
    const model_packet_binding *bindings,uint32_t count,model_material_uv *output);
#endif
