#ifndef GOOL_VM_H
#define GOOL_VM_H
#include "gool_operand.h"
#include "gool_stack.h"
#include "gool_memory.h"

enum { GOOL_VM_RUNNING=0,GOOL_VM_DONE=1,GOOL_VM_WAIT=2,GOOL_VM_NEW_STATE=4,GOOL_VM_SIGNAL_RETURN=5,
       GOOL_VM_FAILED_SIGNAL=6,GOOL_VM_SEND=7,GOOL_VM_SEND_REPLACED=8,GOOL_VM_RESUME_RETURN=10,GOOL_VM_STATE_REQUEST=11,GOOL_VM_GEOMETRY=12,GOOL_VM_SPAWN=13,GOOL_VM_RESOURCE=14,GOOL_VM_INVALID=-10,
       GOOL_VM_UNSUPPORTED=-11,GOOL_VM_OPERAND=-12,GOOL_VM_STACK=-13,
       GOOL_VM_ARITHMETIC=-14,GOOL_VM_BUDGET=-15 };
/* Borrowed native handler result context. It is supplied by the engine,
 * not decoded from a script parameter or an implicit receiver-index pool.
 * Result selection does not apply the final state transition/delivery. */
typedef struct {
    int was_b,has_ack;
    int32_t new_state; /* -1=no matching receive, 255=handled/no state */
    uint32_t argument_count;
    int32_t arguments[15];
} gool_signal_receiver;
typedef struct {
    int32_t target,raw_signal,arguments[7];
    uint32_t argument_count,caller_mode;
} gool_send_request;
typedef int (*gool_vm_address_read)(void *context,uint32_t address,int32_t *value);
typedef int (*gool_vm_address_write)(void *context,uint32_t address,int32_t value);
typedef struct {
    gool_data code;
    gool_operand_context external;
    gool_stack stack;
    uint32_t pc,fault_pc,instruction;
    uint32_t pending_pc;
    int has_pending_pc; /* raw engine-return PC, formed without instruction fetch */
    uint32_t mode; /* Native call flags; engine scheduling/modes still partial. */
    uint64_t tick,next_update; /* next_update is diagnostic only; token governs scheduling */
    uint32_t frame_token_index;
    int frame_yield; /* SET_FRAME/GEO mode 4 yielded with token on stack. */
    int respect_delay;
    int32_t *globals;
    uint32_t global_count;
    gool_memory *memory; /* borrowed shared engine spans; no implicit mapping */
    void *address_context;
    gool_vm_address_read read_address;
    gool_vm_address_write write_address;
    uint32_t *random_state; /* two borrowed words, shared engine RNG; no default seed */
    const int32_t *phase_source; /* borrowed native scratchpad+50, not inferred tick units */
    const int32_t *frame_factor; /* borrowed native scratchpad +0x54; no inferred units/default */
    int status,detail;
    /* Engine owns state tables, compatibility and root context replacement.
     * Return RUNNING if skipped (arguments discarded), NEW_STATE if replaced,
     * STATE_REQUEST if native engine entry is queued with retained arguments,
     * or a negative error. Native opcode wrapper consumes arguments before
     * compatibility; a later provider/target error can retain that stack effect.
     * False condition skips target tables/providers entirely. */
    int (*change_state)(void *context,uint32_t state,uint32_t argc,int meets_condition);
    void *state_context;
    /* Native39378 wrapper. RUNNING0 completes35D08 effects, raw SP adoption
     * and constant-owner refresh. GOOL_VM_GEOMETRY consumes operands and queues
     * geometry_instruction/geometry_operand for the stopped native engine;
     * step advances PC once and preserves geometry_pending until engine resume. */
    int (*geometry)(void *context,uint32_t instruction);
    void *geometry_context;
    int geometry_pending;
    uint32_t geometry_operand,geometry_instruction;
    /* Original38DD8 sound wrapper for41/42. Synchronous native provider owns
     * operand-address resolution, stack effects, native33864/33C54 result stores
     * and reloading raw PC/SP/FP. On success it must install the NEXT PC itself
     * (including has_pending_pc for a raw non-code pointer). No hidden increment.
     * Missing provider stops unsupported before any operand is consumed. */
    int (*sound)(void *context,uint32_t instruction);
    void *sound_context;
    gool_signal_receiver *receiver;
    int spawn_enabled,resource_enabled;
    int send_enabled,send_pending; /* engine must own/consume deferred requests */
    gool_send_request send;
} gool_vm;

/* Immutable code/data must outlive VM. External callbacks service engine
 * members/linked objects. STACK and FRAME are serviced by VM itself.
 * This initial dispatcher is intentionally incomplete: unsupported opcodes
 * stop with PC/word recorded, never NOP or fabricated successful events. */
int gool_vm_init(gool_vm *vm,const gool_data *code,uint32_t pc,
                 const gool_operand_context *context,const int32_t *args,uint32_t argc);
int gool_vm_step(gool_vm *vm);
/* Opcode 0x18 forms a base/alternate code pointer and writes a six-bit direct
 * destination. Index 0x3FFF forms null before inspecting owner; STACK reserves
 * its slot first. Formation does not fetch target code. The member provider
 * owns raw SCRIPT_PTR stores; writing object+C0 does not branch cached native
 * dispatcher S5. Unsupported providers reject that store explicitly. */
/* JALR is native opcode 0x47 (0x3A is a different, unsupported service).
 * Requires a bound code-owner pointer. Non-null uses signed count>>8; null
 * uses raw wrapped stack adjustment and preserves reserved old words.
 * Native operand-address providers resolve remote SP and literal scratch
 * storage. Address1 dereferences reject in the domain. Legacy providers retain
 * their explicit ABI restrictions. High count operand is evaluated first. */
/* NEW_STATE is a resumable engine event, not vm.status: the state service
 * has replaced code/frames. The owning instance must enter the new pre_run
 * before fetching the next main instruction, within its update budget. */
int gool_vm_run(gool_vm *vm,uint32_t instruction_budget);
/* RESUME_RETURN is a resumable engine event: mode 0x13 RETURN restored its
 * parent and seeded zero. Owner must advance state-entry phase before main.
 * Engine/signal returns keep current mode rather than restoring frame flags;
 * ordinary nested calls restore them as before. */
/* Integer GOOL logic ticks, not render frames. UPDATE obeys delays; handlers
 * may bypass them without clearing a delay established for the update script.
 * Tick cannot go backwards; reset/reinitialize on a new execution timeline. */
int gool_vm_set_tick(gool_vm *vm,uint64_t tick,int respect_delay);
/* Native update gate: consume the yielded top token when ready. Tokens use
 * raw uint32 tick OR duration<<24; readiness mirrors 0x8001d870..0x8001d8b8.
 * No automatic consumption inside step: a yielded frame ends that UPDATE. */
int gool_vm_resume_frame(gool_vm *vm,uint64_t tick);
/* Initial state without RESUME pushes a zero token before its first update. */
int gool_vm_seed_frame(gool_vm *vm);
/* Borrowed shared engine table, initialized by the caller with actual game
 * state. No viewer/debug "all levels complete" defaults are inserted. */
int gool_vm_bind_globals(gool_vm *vm,int32_t *globals,uint32_t count);
/* Bind a stable shared word-span domain; NULL unbinds. Engines own lifetime,
 * registration/release of incoming argument spans and other memory providers.
 * Only mapped words may be dereferenced by array read/write 0x2A/0x4E. */
int gool_vm_bind_memory(gool_vm *vm,gool_memory *memory);
/* Explicit native word-address provider for ARRAY_READ/WRITE. Both callbacks
 * must be present, or bothNULL to restore the existing tagged span domain.
 * No fallback from a native provider failure to an unrelated address domain. */
int gool_vm_bind_address_space(gool_vm *vm,void *context,gool_vm_address_read read,gool_vm_address_write write);
/* Native opcode 0x10 mutates two engine-wide words at 0x80060A2C/30.
 * Caller owns initialization, lifetime and sharing across objects/handlers.
 * NULL unbinds; no per-object PRNG or host rand() substitution. */
int gool_vm_bind_random(gool_vm *vm,uint32_t state[2]);
/* Opcode 0x25 reads this engine-owned factor after evaluating its operands.
 * Storage must remain valid throughout UPDATE and nested signal delivery. */
int gool_vm_bind_phase_source(gool_vm *vm,const int32_t *source);
int gool_vm_bind_frame_factor(gool_vm *vm,const int32_t *factor);
/* Bind engine result storage; caller enters handler in mode 8 and initializes
 * new_state=-1. No mode/arguments/record reset is hidden in this binder.
 * SIGNAL_RETURN/FAILED_SIGNAL are handler completion events, not vm.status. */
int gool_vm_bind_receiver(gool_vm *vm,gool_signal_receiver *receiver);
/* After a completion event, the engine must consume the result and restore
 * handler context/mode/delay policy before another step of the interrupted VM.
 * No implicit final state application or nested SEND delivery is performed. */
int gool_vm_address(const gool_data *code,uint32_t pc,int32_t *pointer);
/* Resolve a live handler pointer against this level's behavior registry. */
int gool_vm_resolve(gool_vm *vm,int32_t pointer,gool_data *owner,uint32_t *pc);
/* Engine handler entry: saves current PC (not PC+1 as JAL does). */
int gool_vm_invoke(gool_vm *vm,const gool_data *code,uint32_t pc,uint32_t argc);
#endif
