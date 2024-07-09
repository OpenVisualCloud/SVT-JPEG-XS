/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/
#include <stdint.h>
#include <limits.h>

#include "SvtMalloc.h"
#include "SvtThreads.h"
#define LOG_TAG "SvtMalloc"
#include "SvtLog.h"

void svt_jxs_print_alloc_fail_impl(const char* file, int line) {
    SVT_FATAL("allocate memory failed, at %s:%d\n", file, line);
}

#ifdef DEBUG_MEMORY_USAGE

static Handle_t g_malloc_mutex;

static void malloc_mutex_cleanup(void) {
    if (g_malloc_mutex) {
        svt_jxs_destroy_mutex(g_malloc_mutex);
    }
}
static void create_malloc_mutex(void) {
    g_malloc_mutex = svt_jxs_create_mutex();
    atexit(malloc_mutex_cleanup);
}

#ifdef _WIN32
#include <windows.h>

static INIT_ONCE g_malloc_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK create_malloc_mutex_wrapper(PINIT_ONCE InitOnce, PVOID Parameter, PVOID* lpContext) {
    (void)InitOnce;
    (void)Parameter;
    (void)lpContext;
    create_malloc_mutex();
    return 1;
}

static Handle_t get_malloc_mutex() {
    InitOnceExecuteOnce(&g_malloc_once, create_malloc_mutex_wrapper, NULL, NULL);
    return g_malloc_mutex;
}
#else
#include <pthread.h>

static pthread_once_t g_malloc_once = PTHREAD_ONCE_INIT;

static Handle_t get_malloc_mutex() {
    pthread_once(&g_malloc_once, create_malloc_mutex);
    return g_malloc_mutex;
}
#endif // _WIN32

// Simple hash function to speed up entry search
// Takes the top half and bottom half of the pointer and adds them together.
static INLINE uint32_t hash(const void* const p) {
    const uintptr_t bit_mask = ((uintptr_t)1 << sizeof(bit_mask) / 2 * CHAR_BIT) - 1, v = (uintptr_t)p;
    return (uint32_t)((v >> (sizeof(v) / 2 * CHAR_BIT)) + (v & bit_mask));
}

typedef struct MemoryEntry {
    void* ptr;
    size_t count;
    const char* file;
    PointerType_t type;
    uint32_t line;
} MemoryEntry;

//+1 to get a better hash result
#define MEM_ENTRY_SIZE (4 * 1024 * 1024 + 1)

static MemoryEntry g_mem_entry[MEM_ENTRY_SIZE];

#define TO_INDEX(v) ((v) % MEM_ENTRY_SIZE)
static uint8_t g_add_mem_entry_warning = 1;
static uint8_t g_remove_mem_entry_warning = 1;

/*********************************************************************************
*
* @brief
*  compare and update current memory entry.
*
* @param[in] e
*  current memory entry.
*
* @param[in] param
*  param you set to for_each_mem_entry
*
*
* @returns  return 1 if you want get early exit in for_each_mem_entry
*
s*
********************************************************************************/

typedef uint8_t (*Predicate)(MemoryEntry* e, void* param);

/*********************************************************************************
*
* @brief
*  Loop through mem entries.
*
* @param[in] bucket
*  the hash bucket
*
* @param[in] start
*  loop start position
*
* @param[in] pred
*  return 1 if you want early exit
*
* @param[out] param
*  param send to pred.
*
* @returns  return 1 if we got early exit.
*
*
********************************************************************************/
static uint8_t for_each_hash_entry(MemoryEntry* bucket, uint32_t start, Predicate pred, void* param) {
    const uint32_t s = TO_INDEX(start);
    uint32_t i = s;

    do {
        MemoryEntry* e = bucket + i;
        if (pred(e, param))
            return 1;
        i++;
        i = TO_INDEX(i);
    } while (i != s);
    return 0;
}

static uint8_t for_each_mem_entry(uint32_t start, Predicate pred, void* param) {
    uint8_t ret;
    Handle_t m = get_malloc_mutex();
    if (m == NULL) {
        return 1;
    }
    svt_jxs_block_on_mutex(m);
    ret = for_each_hash_entry(g_mem_entry, start, pred, param);
    svt_jxs_release_mutex(m);
    return ret;
}

static const char* mem_type_name(PointerType_t type) {
    static const char* name[POINTER_TYPE_NUM] = {
        "malloced memory", "calloced memory", "aligned memory", "mutex", "semaphore", "thread"};
    return name[type];
}

static uint8_t add_mem_entry(MemoryEntry* e, void* param) {
    if (!e->ptr) {
        memcpy(e, param, sizeof(*e));
        return 1;
    }
    return 0;
}

static uint8_t remove_mem_entry(MemoryEntry* e, void* param) {
    MemoryEntry* item = param;
    if (e->ptr == item->ptr) {
        // The second case is a special case, we use SVT_FREE to free calloced memory
        if (e->type == item->type || (e->type == POINTER_TYPE_C_PTR && item->type == POINTER_TYPE_N_PTR)) {
            e->ptr = NULL;
            return 1;
        }
    }
    return 0;
}

typedef struct MemSummary {
    size_t amount[POINTER_TYPE_NUM];
    uint32_t occupied;
} MemSummary;

static uint8_t count_mem_entry(MemoryEntry* e, void* param) {
    if (e->ptr) {
        MemSummary* sum = param;
        sum->amount[e->type] += e->count;
        sum->occupied++;
    }
    return 0;
}

static INLINE void get_memory_usage_and_scale(size_t amount, double* const usage, char* const scale) {
    static const char scales[] = {' ', 'K', 'M', 'G', 'T'};
    size_t i = 1;
    for (; i < sizeof(scales) && amount >= (size_t)1 << (++i * 10);)
        ;
    *scale = scales[--i];
    *usage = (double)amount / (double)((size_t)1 << (i * 10));
}

//this need more memory and cpu
#define PROFILE_MEMORY_USAGE
#ifdef PROFILE_MEMORY_USAGE

//if we use a static array here, this size + sizeof(g_mem_entry) will exceed max size allowed on windows.
static MemoryEntry* g_profile_entry;

static uint8_t add_location(MemoryEntry* e, void* param) {
    MemoryEntry* new_item = param;
    if (!e->ptr) {
        *e = *new_item;
        return 1;
    }
    if (e->file == new_item->file && e->line == new_item->line) {
        e->count += new_item->count;
        return 1;
    }
    // to next position.
    return 0;
}

static uint8_t collect_mem(MemoryEntry* e, void* param) {
    PointerType_t* type = param;
    if (e->ptr && e->type == *type)
        for_each_hash_entry(g_profile_entry, 0, add_location, e);
    //Loop entire bucket.
    return 0;
}

static int compare_count(const void* a, const void* b) {
    const MemoryEntry* pa = a;
    const MemoryEntry* pb = b;
    return pb->count < pa->count ? -1 : pb->count != pa->count;
}

#define MIN_PRINT_SIZE (64 * 1024)

static void print_top_10_locations_type(PointerType_t type) {
    Handle_t m = get_malloc_mutex();
    if (m == NULL) {
        return;
    }
    svt_jxs_block_on_mutex(m);
    g_profile_entry = calloc(MEM_ENTRY_SIZE, sizeof(*g_profile_entry));
    if (!g_profile_entry) {
        SVT_ERROR("not enough memory for memory profile");
        svt_jxs_release_mutex(m);
        return;
    }

    for_each_hash_entry(g_mem_entry, 0, collect_mem, &type);
    qsort(g_profile_entry, MEM_ENTRY_SIZE, sizeof(*g_profile_entry), compare_count);

    int num_max = 10;
    while (num_max > 0 && ((MemoryEntry*)(g_profile_entry + num_max - 1))->count < MIN_PRINT_SIZE) {
        --num_max;
    }

    if (num_max > 0) {
        SVT_INFO("top %i %s locations:\n", num_max, mem_type_name(type));
        for (int i = 0; i < num_max; i++) {
            double usage;
            char scale;
            MemoryEntry* e = g_profile_entry + i;
            get_memory_usage_and_scale(e->count, &usage, &scale);
            SVT_INFO("(%.2lf %cB): %s:%d\n", usage, scale, e->file, e->line);
        }
    }
    free(g_profile_entry);
    svt_jxs_release_mutex(m);
}

static void print_top_10_locations() {
    print_top_10_locations_type(POINTER_TYPE_N_PTR);
    print_top_10_locations_type(POINTER_TYPE_C_PTR);
    print_top_10_locations_type(POINTER_TYPE_A_PTR);
}
#endif //PROFILE_MEMORY_USAGE

static int g_component_count;

static uint8_t print_leak(MemoryEntry* e, void* param) {
    if (e->ptr) {
        uint8_t* leaked = param;
        *leaked = 1;
        SVT_ERROR("%s leaked at %s:%d\n", mem_type_name(e->type), e->file, e->line);
    }
    //loop through all items
    return 0;
}

void svt_jxs_print_memory_usage() {
    MemSummary sum;
    double usage;
    char scale;
    memset(&sum, 0, sizeof(sum));

    for_each_mem_entry(0, count_mem_entry, &sum);
    SVT_INFO("SVT Memory Usage:\n");
    get_memory_usage_and_scale(
        sum.amount[POINTER_TYPE_N_PTR] + sum.amount[POINTER_TYPE_C_PTR] + sum.amount[POINTER_TYPE_A_PTR], &usage, &scale);
    SVT_INFO("    total allocated memory:       %.2lf %cB\n", usage, scale);
    get_memory_usage_and_scale(sum.amount[POINTER_TYPE_N_PTR], &usage, &scale);
    SVT_INFO("        malloced memory:          %.2lf %cB\n", usage, scale);
    get_memory_usage_and_scale(sum.amount[POINTER_TYPE_C_PTR], &usage, &scale);
    SVT_INFO("        callocated memory:        %.2lf %cB\n", usage, scale);
    get_memory_usage_and_scale(sum.amount[POINTER_TYPE_A_PTR], &usage, &scale);
    SVT_INFO("        allocated aligned memory: %.2lf %cB\n", usage, scale);

    SVT_INFO("    mutex count: %zu\n", sum.amount[POINTER_TYPE_MUTEX]);
    SVT_INFO("    semaphore count: %zu\n", sum.amount[POINTER_TYPE_SEMAPHORE]);
    SVT_INFO("    thread count: %zu\n", sum.amount[POINTER_TYPE_THREAD]);
    SVT_INFO("    hash table fulless: %f, hash bucket is %s\n",
             (double)sum.occupied / MEM_ENTRY_SIZE,
             (double)sum.occupied / MEM_ENTRY_SIZE < .3 ? "healthy" : "too full");
#ifdef PROFILE_MEMORY_USAGE
    print_top_10_locations();
#endif
}

void svt_jxs_increase_component_count() {
    Handle_t m = get_malloc_mutex();
    if (m == NULL) {
        return;
    }
    svt_jxs_block_on_mutex(m);
    g_component_count++;
    svt_jxs_release_mutex(m);
}

void svt_jxs_decrease_component_count() {
    Handle_t m = get_malloc_mutex();
    if (m == NULL) {
        return;
    }
    svt_jxs_block_on_mutex(m);
    g_component_count--;
    if (!g_component_count) {
        uint8_t leaked = 0;
        for_each_hash_entry(g_mem_entry, 0, print_leak, &leaked);
        if (!leaked)
            SVT_INFO("you have no memory leak\n");
    }
    svt_jxs_release_mutex(m);
}

void svt_jxs_add_mem_entry_impl(void* ptr, PointerType_t type, size_t count, const char* file, uint32_t line) {
    if (for_each_mem_entry(
            hash(ptr), add_mem_entry, &(MemoryEntry){.ptr = ptr, .type = type, .count = count, .file = file, .line = line}))
        return;
    if (g_add_mem_entry_warning) {
        SVT_ERROR(
            "can't add memory entry.\n"
            "You have memory leak or you need increase MEM_ENTRY_SIZE\n");
        g_add_mem_entry_warning = 0;
    }
}

void svt_jxs_remove_mem_entry(void* ptr, PointerType_t type) {
    if (!ptr)
        return;
    if (for_each_mem_entry(hash(ptr), remove_mem_entry, &(MemoryEntry){.ptr = ptr, .type = type}))
        return;
    if (g_remove_mem_entry_warning) {
        SVT_ERROR("something wrong. you freed a unallocated memory %p, type = %s\n", ptr, mem_type_name(type));
        g_remove_mem_entry_warning = 0;
    }
}
#endif
