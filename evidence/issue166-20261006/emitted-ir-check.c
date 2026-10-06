#include "qemu/osdep.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-temp-internal.h"
#include "exec/translation-block.h"
#include "accel/tcg/internal-common.h"
#include "accel/tcg/tb-hash.h"

__thread TCGContext *tcg_ctx;
TCGv_env tcg_env;
bool one_insn_per_tb;
unsigned qemu_loglevel;
uint64_t xemu_inline_lookup_hits;
const TargetPageBits target_page = { .decided = true, .bits = 12, .mask = ~(uintptr_t)4095 };
TCGHelperInfo helper_info_lookup_tb_ptr_i32;
static unsigned calls;
static TCGTemp *temp(TCGType type, TCGTempKind kind, uint64_t value)
{
    TCGTemp *t = &tcg_ctx->temps[tcg_ctx->nb_temps++];
    assert(tcg_ctx->nb_temps < TCG_MAX_TEMPS);
    *t = (TCGTemp){ .type=type, .base_type=type, .kind=kind, .val=value };
    return t;
}
TCGv_i32 tcg_temp_new_i32(void) { return temp_tcgv_i32(temp(TCG_TYPE_I32,TEMP_TB,0)); }
TCGv_i64 tcg_temp_new_i64(void) { return temp_tcgv_i64(temp(TCG_TYPE_I64,TEMP_TB,0)); }
TCGv_ptr tcg_temp_new_ptr(void) { return temp_tcgv_ptr(temp(TCG_TYPE_PTR,TEMP_TB,0)); }
TCGv_ptr tcg_temp_ebb_new_ptr(void) { return tcg_temp_new_ptr(); }
TCGv_i32 tcg_constant_i32(int32_t v) { return temp_tcgv_i32(temp(TCG_TYPE_I32,TEMP_CONST,(uint32_t)v)); }
TCGv_i64 tcg_constant_i64(int64_t v) { return temp_tcgv_i64(temp(TCG_TYPE_I64,TEMP_CONST,v)); }
TCGv_ptr tcg_constant_ptr_int(intptr_t v) { return temp_tcgv_ptr(temp(TCG_TYPE_PTR,TEMP_CONST,v)); }
void tcg_temp_free_internal(TCGTemp *t) { }
void tcg_temp_free_ptr(TCGv_ptr t) { }
void *tcg_malloc_internal(TCGContext *s,int n) { return calloc(1,n); }
TCGLabel *gen_new_label(void) {
    TCGLabel *l=calloc(1,sizeof(*l)); QSIMPLEQ_INIT(&l->branches); QSIMPLEQ_INIT(&l->relocs); return l;
}
TCGOp *tcg_emit_op(TCGOpcode code,unsigned n) {
    TCGOp *o=calloc(1,sizeof(*o)+n*sizeof(TCGArg));o->opc=code;o->nargs=n;
    QTAILQ_INSERT_TAIL(&tcg_ctx->ops,o,link);return o;
}
bool tcg_op_supported(TCGOpcode c,TCGType t,unsigned f) { return true; }
bool tcg_op_deposit_valid(TCGType t,unsigned o,unsigned n) { return true; }
void tcg_gen_call4(void *f,TCGHelperInfo *i,TCGTemp *out,TCGTemp *a,TCGTemp *b,TCGTemp *c,TCGTemp *d) {
    TCGOp *o=tcg_emit_op(INDEX_op_call,1);o->args[0]=temp_arg(out);
}
const void *helper_lookup_tb_ptr_i32(CPUArchState *e,uint32_t p,uint64_t b,uint32_t f) { abort(); }
static uint64_t get(TCGArg a) { return arg_temp(a)->val; }
static void put(TCGOp *o,uint64_t v) { arg_temp(o->args[0])->val=TCGOP_TYPE(o)==TCG_TYPE_I32?(uint32_t)v:v; }
static uintptr_t run(void) {
    TCGOp *o;
    QTAILQ_FOREACH(o,&tcg_ctx->ops,link) {
        TCGArg *a=o->args;uint64_t v=0;size_t size;
        switch(o->opc) {
        case INDEX_op_ld: size=TCGOP_TYPE(o)==TCG_TYPE_I32?4:8; memcpy(&v,(void *)(get(a[1])+a[2]),size);put(o,v);break;
        case INDEX_op_ld8u:memcpy(&v,(void *)(get(a[1])+a[2]),1);put(o,v);break;
        case INDEX_op_st8: v=get(a[0]);memcpy((void *)(get(a[1])+a[2]),&v,1);break;
        case INDEX_op_st: v=get(a[0]);size=TCGOP_TYPE(o)==TCG_TYPE_I32?4:8;memcpy((void *)(get(a[1])+a[2]),&v,size);break;
        case INDEX_op_add:put(o,get(a[1])+get(a[2]));break;
        case INDEX_op_and:put(o,get(a[1])&get(a[2]));break;
        case INDEX_op_or:put(o,get(a[1])|get(a[2]));break;
        case INDEX_op_xor:put(o,get(a[1])^get(a[2]));break;
        case INDEX_op_shr:put(o,(TCGOP_TYPE(o)==TCG_TYPE_I32?(uint32_t)get(a[1]):get(a[1]))>>get(a[2]));break;
        case INDEX_op_shl:put(o,get(a[1])<<get(a[2]));break;
        case INDEX_op_extu_i32_i64:put(o,(uint32_t)get(a[1]));break;
        case INDEX_op_mov:put(o,get(a[1]));break;
        case INDEX_op_brcond: {
            bool equal = TCGOP_TYPE(o)==TCG_TYPE_I32 ? (uint32_t)get(a[0])==(uint32_t)get(a[1]) : get(a[0])==get(a[1]);
            assert(a[2]==TCG_COND_EQ||a[2]==TCG_COND_NE);
            if(equal==(a[2]==TCG_COND_EQ)) {
                TCGLabel *l=arg_label(a[3]);
                QTAILQ_FOREACH(o,&tcg_ctx->ops,link) if(o->opc==INDEX_op_set_label&&arg_label(o->args[0])==l) break;
                assert(o);
            } break;
        }
        case INDEX_op_set_label:break;
        case INDEX_op_call:calls++;arg_temp(a[0])->val=0xdead1000;break;
        case INDEX_op_goto_ptr:return get(a[0]);
        case INDEX_op_exit_tb:return 0xdead2000;
        default:fprintf(stderr,"unsupported real IR opcode %d\n",o->opc);abort();
        }
    }
    abort();
}
int main(void) {
    bool disabled = getenv("XEMU_EXPERIMENTAL_INLINE_JUMP_CACHE")==NULL;
    CPUState *cpu=calloc(1,sizeof(*cpu)+64);CPUJumpCache *jc=calloc(1,sizeof(*jc));
    TranslationBlock key={.cs_base=0xffff0000,.flags=0x1234,.cflags=0},gen={0};
    const uint32_t eip=0x23400,pc=(uint32_t)(key.cs_base+eip);unsigned h=tb_jmp_cache_hash_func(pc);
    for(unsigned test=0;test<17;test++) {
        TCGContext ctx={0};tcg_ctx=&ctx;ctx.gen_tb=&gen;QTAILQ_INIT(&ctx.ops);
        calls=0;xemu_inline_lookup_hits=0;qemu_loglevel=0;one_insn_per_tb=false;
        memset(cpu,0,sizeof(*cpu));cpu->tb_jmp_cache=jc;QTAILQ_INIT(&cpu->breakpoints);
        key.tc.ptr=(void *)0xabcdef00;key.cflags=0;key.flags=0x1234;key.cs_base=0xffff0000;
        jc->array[h].tb=&key;jc->array[h].pc=pc;gen.cflags=0;
        tcg_env=temp_tcgv_ptr(temp(TCG_TYPE_PTR,TEMP_GLOBAL,(uintptr_t)cpu_env(cpu)));
        TCGv_i32 input=tcg_temp_new_i32();tcgv_i32_temp(input)->val=eip;
        CPUBreakpoint bp={0};
        switch(test) {
        case 1:jc->array[h].tb=NULL;break;
        case 2:jc->array[h].pc++;break;
        case 3:key.cs_base++;break;
        case 4:key.flags++;break;
        case 5:key.cflags=CF_INVALID;break;
        case 6:cpu->tcg_cflags=CF_NO_GOTO_TB;break;
        case 7:cpu->singlestep_enabled=1;break;
        case 8:QTAILQ_INSERT_HEAD(&cpu->breakpoints,&bp,entry);break;
        case 9:one_insn_per_tb=true;break;
        case 10:qemu_loglevel=CPU_LOG_TB_NOCHAIN;break;
        case 11:qemu_loglevel=CPU_LOG_TB_CPU;break;
        case 12:qemu_loglevel=CPU_LOG_EXEC;break;
        case 13:cpu->tcg_cflags=CF_USE_ICOUNT;break;
        case 14:cpu->tcg_cflags=CF_BP_PAGE;break;
        case 15:cpu->tb_jmp_cache=NULL;break;
        case 16:gen.cflags=CF_NO_GOTO_PTR;break;
        }
        tcg_gen_lookup_and_goto_ptr_i32(input,0xffff0000,0x1234);
        uintptr_t target=run();
        if(test==0 && !disabled) {
            assert(target==(uintptr_t)key.tc.ptr&&calls==0&&cpu->neg.can_do_io&&xemu_inline_lookup_hits==1);
        } else if(test==16) assert(target==0xdead2000&&calls==0&&xemu_inline_lookup_hits==0);
        else assert(target==0xdead1000&&calls==1&&xemu_inline_lookup_hits==0);
    }
    puts("17 production TCG-emission IR path checks pass (interpreter; not native JIT proof)");
}
