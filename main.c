#include <stdio.h>
#include "mps.h"
#include "mpsavm.h"
#include <assert.h>
#include <stdlib.h>

static mps_arena_t arena;
static mps_ap_t obj_ap; 

#define ALIGNMENT sizeof(mps_word_t)

/* Align size upwards to the next multiple of the word size. */
#define ALIGN_WORD(size) \
(((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

#define LENGTH(array) (sizeof(array) / sizeof(array[0]))

#define TYPE(obj) ((obj)->type.type)

#define FIX(ref) \
    do { \
        mps_addr_t _addr = (ref); /* copy to local to avoid type pun */ \
        mps_res_t res = MPS_FIX12(ss, &_addr); \
        if (res != MPS_RES_OK) return res; \
        (ref) = _addr; \
    } while(0)


typedef union obj_u *obj_t;

typedef obj_t (*entry_t)(obj_t env, obj_t op_env, obj_t operator, obj_t rands);

typedef int type_t;

enum {
    TYPE_INTEGER,
    TYPE_FWD2,
    TYPE_PAD1, 
    TYPE_PAD 
};

typedef struct type_s {
    type_t type;
} type_s;

typedef struct pair_s {
    type_t type;
    obj_t car, cdr;
} pair_s;

typedef struct integer_s {
    type_t type; 
    long integer;
} integer_s;

typedef struct fwd2_s {
    type_t type;
    obj_t fwd;
} fwd2_s;

typedef struct pad_s {
    type_t type;
    size_t size;
} pad_s;

typedef struct pad1_s {
    type_t type;
} pad1_s;

typedef union obj_u {
    type_s type;
    integer_s integer;
    fwd2_s fwd2;
    pad_s pad;
    pad1_s pad1;
} obj_s;


static mps_res_t obj_scan(mps_ss_t ss, mps_addr_t base, mps_addr_t limit)
{
    MPS_SCAN_BEGIN(ss) {
        while (base < limit) {
            obj_t obj = base;
            switch ( TYPE(obj) ) {
            case TYPE_INTEGER:
                base = (char *)base + ALIGN_WORD(sizeof(integer_s));
                break;
            case TYPE_FWD2:
                base = (char *)base + ALIGN_WORD(sizeof(fwd2_s));
                break;
            case TYPE_PAD:
                base = (char *)base + ALIGN_WORD(obj->pad.size);
                break;
            case TYPE_PAD1:
                base = (char *)base + ALIGN_WORD(sizeof(pad1_s));
                break;
            default:
                assert(0);
                fprintf(stderr, "Unexpected object on the heap\n");
                abort();
            }
        }
    } MPS_SCAN_END(ss);
    return MPS_RES_OK;
}

static mps_addr_t obj_skip(mps_addr_t base)
{
    obj_t obj = base;
    switch (TYPE(obj)) {
    case TYPE_INTEGER:
        base = (char *)base + ALIGN_WORD(sizeof(integer_s));
        break;
    case TYPE_FWD2:
        base = (char *)base + ALIGN_WORD(sizeof(fwd2_s));
        break;
    case TYPE_PAD:
        base = (char *)base + ALIGN_WORD(obj->pad.size);
        break;
    case TYPE_PAD1:
        base = (char *)base + ALIGN_WORD(sizeof(pad1_s));
        break;
    default:
        assert(0);
        fprintf(stderr, "Unexpected object on the heap\n");
        abort();
    }
    return base;
}

static void obj_fwd(mps_addr_t old, mps_addr_t new)
{
    obj_t obj = old;
    mps_addr_t limit = obj_skip(old);
    size_t size = (char *)limit - (char *)old;
    assert(size >= ALIGN_WORD(sizeof(fwd2_s)));
    if (size == ALIGN_WORD(sizeof(fwd2_s))) {
        TYPE(obj) = TYPE_FWD2;
        obj->fwd2.fwd = new;
    }
}

static mps_addr_t obj_isfwd(mps_addr_t addr)
{
    obj_t obj = addr;
    switch (TYPE(obj)) {
    case TYPE_FWD2:
        return obj->fwd2.fwd;
    }
    return NULL;
}

static void obj_pad(mps_addr_t addr, size_t size)
{
    obj_t obj = addr;
    assert(size >= ALIGN_WORD(sizeof(pad1_s)));
    if (size == ALIGN_WORD(sizeof(pad1_s))) {
        TYPE(obj) = TYPE_PAD1;
    } else {
        TYPE(obj) = TYPE_PAD;
        obj->pad.size = size;
    }
}

static obj_t random_int_list[50];

static mps_res_t globals_scan(mps_ss_t ss, void *p, size_t s)
{
    MPS_SCAN_BEGIN(ss) {

        for (int i = 0; i < LENGTH(random_int_list); ++i)
            FIX(random_int_list[i]);

    } MPS_SCAN_END(ss);
    return MPS_RES_OK;
}


static obj_t make_integer(long integer)
{
    obj_t obj;
    mps_addr_t addr;
    size_t size = ALIGN_WORD(sizeof(integer_s));
    do {
        mps_res_t res = mps_reserve(&addr, obj_ap, size);
        if (res != MPS_RES_OK) {
            printf("out of memory in make_integer");
        }
        obj = addr;
        obj->integer.type = TYPE_INTEGER;
        obj->integer.integer = integer;
    } while(!mps_commit(obj_ap, addr, size));

    return obj;
}


int main () {
    
    mps_res_t res;
    mps_fmt_t obj_fmt;
    mps_pool_t obj_pool;
    void *marker = &marker;
    mps_root_t reg_root;


    MPS_ARGS_BEGIN(args) {
        MPS_ARGS_ADD(args, MPS_KEY_ARENA_SIZE, 32 * 1024 * 1024);
        res = mps_arena_create_k(&arena, mps_arena_class_vm(), args);
    } MPS_ARGS_END(args);
    if (res != MPS_RES_OK) {
        printf("Couldn't create arena\n");
    } else {
        printf("Arena created\n");    
    }
     
    MPS_ARGS_BEGIN(args) {
        MPS_ARGS_ADD(args, MPS_KEY_FMT_ALIGN, ALIGNMENT);
        MPS_ARGS_ADD(args, MPS_KEY_FMT_SCAN, obj_scan);
        MPS_ARGS_ADD(args, MPS_KEY_FMT_SKIP, obj_skip);
        MPS_ARGS_ADD(args, MPS_KEY_FMT_FWD, obj_fwd);
        MPS_ARGS_ADD(args, MPS_KEY_FMT_ISFWD, obj_isfwd);
        MPS_ARGS_ADD(args, MPS_KEY_FMT_PAD, obj_pad);
        res = mps_fmt_create_k(&obj_fmt, arena, args);
    } MPS_ARGS_END(args);
    if (res != MPS_RES_OK) {
        printf("Couldn't create obj format\n");
    } else {
        printf("Create obj format\n");
    }

    
    MPS_ARGS_BEGIN(args) {
        MPS_ARGS_ADD(args, MPS_KEY_FORMAT, obj_fmt);
        res = mps_pool_create_k(&obj_pool, arena, mps_class_amc(), args);
    } MPS_ARGS_END(args);
    if (res != MPS_RES_OK) {
        printf("Couldn't create obj pool\n");
    } else {
        printf("Create obj pool\n");
    }

    mps_root_t globals_root;
    res = mps_root_create(&globals_root, arena, mps_rank_exact(), 0,
                        globals_scan, NULL, 0);
    if (res != MPS_RES_OK) {
        printf("Couldn't register globals root\n");
    } else {
        printf("register globals root\n");
    }

    mps_thr_t thread;
    res = mps_thread_reg(&thread, arena);
    if (res != MPS_RES_OK) {
        printf("Couldn't register thread\n");
    } else {
        printf("Register thread\n");
    }

    res = mps_root_create_reg(&reg_root,
                              arena,
                              mps_rank_ambig(),
                              0,
                              thread,
                              mps_stack_scan_ambig,
                              marker,
                              0);
    if (res != MPS_RES_OK) {
        printf("Couldn't create root\n");
    } else {
        printf("Create root\n");
    }

    
    res = mps_ap_create_k(&obj_ap, obj_pool, mps_args_none);
    if (res != MPS_RES_OK) {
        printf("Couldn't create obj allocation point\n");
    } else {
        printf("Create obj allocation point\n");
    }


    int random_int_index = 0;
    for (int i = 0; i != 1000; ++i) {
        long r = rand() % 100;

        obj_t int_obj = make_integer(r);
        
        if (r > 96) {
            printf("%ld ", r);
            if ( random_int_index != 49 ) {
                random_int_list[random_int_index] = int_obj;
                random_int_index++;
            }
        }
    }
    printf("\n");

    obj_t fja;
    for (int i = 0; i != 100000000; ++i) {
        
        fja = make_integer(i);

    }
    
    for (int i = 0; i != 50; ++i) {
        if ( random_int_list[i] != NULL ) {
            printf("%ld ", random_int_list[i]->integer.integer);
        } else {    
            printf("empty ");
        }
    }
    

    mps_arena_park(arena);      /* ensure no collection is running */
    mps_ap_destroy(obj_ap);     /* destroy ap before pool */
    mps_pool_destroy(obj_pool); /* destroy pool before fmt */
    mps_root_destroy(reg_root); /* destroy root before thread */
    mps_root_destroy(globals_root);
    mps_thread_dereg(thread);   /* deregister thread before arena */
    mps_fmt_destroy(obj_fmt);   /* destroy fmt before arena */

    mps_arena_destroy(arena);   /* last of all */

    return 0;
}

