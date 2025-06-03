/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

// Command line argument parsing

/***************************************
 * Includes
 ***************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "DecParamParser.h"

/**********************************
   * CLI options
   **********************************/
#define HELP_TOKEN                   "--help"
#define COMMAND_LINE_MAX_SIZE        2048
#define INPUT_FILE_TOKEN             "-i"
#define OUTPUT_FILE_TOKEN            "-o"
#define THREADS_TOKEN                "--lp"
#define FRAMES_TOKEN                 "-n"
#define VERBOSE_TOKEN                "-v"
#define ASM_TYPE_TOKEN               "--asm"
#define FORCE_BITSTREAM_HEADER_TOKEN "--find-bitstream-header"
#define FORCE_DECODE_TOKEN           "--force-decode"
#define LIMIT_FPS_TOKEN              "--limit-fps"
#define PACKETIZATION_MODE           "--packetization-mode"
#define PROXY_MODE                   "--proxy-mode"
#define MAX_NUM_TOKENS               200

static void strncpy_local(char* dest, const char* src, size_t count) {
#if defined(__clang__)
    strncpy(dest, src, count);
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(dest, src, count);
#pragma GCC diagnostic pop
#else
    strcpy_s(dest, count, src);
#endif
    if (count) {
        dest[count - 1] = '\0';
    }
}

static void set_cfg_input_file(const char* value, DecoderConfig_t* cfg) {
    strncpy_local(cfg->in_filename, value, sizeof(cfg->in_filename));
};

static void set_cfg_output_file(const char* value, DecoderConfig_t* cfg) {
    strncpy_local(cfg->out_filename, value, sizeof(cfg->out_filename));
};

static void set_find_header(const char* value, DecoderConfig_t* cfg) {
    cfg->autodetect_bitstream_header = 1;
    UNUSED(value);
};

static void set_num_thread(const char* value, DecoderConfig_t* cfg) {
    cfg->decoder.threads_num = strtoul(value, NULL, 0);
};

static void set_frame_num(const char* value, DecoderConfig_t* cfg) {
    cfg->frames_count = strtoul(value, NULL, 0);
};

static void set_verbose(const char* value, DecoderConfig_t* cfg) {
    cfg->decoder.verbose = strtoul(value, NULL, 0);
};

static void set_force_decode(const char* value, DecoderConfig_t* cfg) {
    cfg->force_decode = strtoul(value, NULL, 0);
};

static void set_limit_fps(const char* value, DecoderConfig_t* cfg) {
    cfg->limit_fps = strtoul(value, NULL, 0);
}

static void set_asm_type(const char* value, DecoderConfig_t* cfg) {
    const struct {
        const char* name;
        CPU_FLAGS flags;
    } param_maps[] = {
        {"c", CPU_FLAGS_C},
        {"0", CPU_FLAGS_C},
        {"mmx", (CPU_FLAGS_MMX << 1) - 1},
        {"1", (CPU_FLAGS_MMX << 1) - 1},
        {"sse", (CPU_FLAGS_SSE << 1) - 1},
        {"2", (CPU_FLAGS_SSE << 1) - 1},
        {"sse2", (CPU_FLAGS_SSE2 << 1) - 1},
        {"3", (CPU_FLAGS_SSE2 << 1) - 1},
        {"sse3", (CPU_FLAGS_SSE3 << 1) - 1},
        {"4", (CPU_FLAGS_SSE3 << 1) - 1},
        {"ssse3", (CPU_FLAGS_SSSE3 << 1) - 1},
        {"5", (CPU_FLAGS_SSSE3 << 1) - 1},
        {"sse4_1", (CPU_FLAGS_SSE4_1 << 1) - 1},
        {"6", (CPU_FLAGS_SSE4_1 << 1) - 1},
        {"sse4_2", (CPU_FLAGS_SSE4_2 << 1) - 1},
        {"7", (CPU_FLAGS_SSE4_2 << 1) - 1},
        {"avx", (CPU_FLAGS_AVX << 1) - 1},
        {"8", (CPU_FLAGS_AVX << 1) - 1},
        {"avx2", (CPU_FLAGS_AVX2 << 1) - 1},
        {"9", (CPU_FLAGS_AVX2 << 1) - 1},
        {"avx512", (CPU_FLAGS_AVX512VL << 1) - 1},
        {"10", (CPU_FLAGS_AVX512VL << 1) - 1},
        {"max", CPU_FLAGS_ALL},
        {"11", CPU_FLAGS_ALL},
    };
    const uint32_t para_map_size = sizeof(param_maps) / sizeof(param_maps[0]);
    uint32_t i;

    for (i = 0; i < para_map_size; ++i) {
        if (strcmp(value, param_maps[i].name) == 0) {
            cfg->decoder.use_cpu_flags = param_maps[i].flags;
            return;
        }
    }

    cfg->decoder.use_cpu_flags = CPU_FLAGS_INVALID;
}

static void set_packetization_mode(const char* value, DecoderConfig_t* cfg) {
    cfg->decoder.packetization_mode = (uint8_t)strtoul(value, NULL, 0);
};

static void set_proxy_mode(const char* value, DecoderConfig_t* cfg) {
    uint8_t proxy_mode = (uint8_t)strtoul(value, NULL, 0);
    if (proxy_mode == 0) {
        cfg->decoder.proxy_mode = proxy_mode_full;
    }
    else if (proxy_mode == 1) {
        cfg->decoder.proxy_mode = proxy_mode_half;
    }
    else if (proxy_mode == 2) {
        cfg->decoder.proxy_mode = proxy_mode_quarter;
    }
    else {
        cfg->decoder.proxy_mode = proxy_mode_max;
    }
}

/**********************************
 * Config Entry Struct
 **********************************/
enum CfgType {
    NULL_OPTIONS,
    OPTIONS,
    INPUT_OPTIONS,
    OUTPUT_OPTIONS,
    THREAD_PERF_OPTIONS,
};

typedef struct ConfigEntry {
    enum CfgType type;
    const char* token;
    const char* name;
    uint8_t param_required;
    uint8_t value_required;
    void (*scf)(const char*, DecoderConfig_t*);
} ConfigEntry;

// clang-format off
ConfigEntry config_entry[] = {
    {OPTIONS, VERBOSE_TOKEN,                    "Verbose decoder internal level (disabled:0, errors:1, system info:2,system info full:3  warnings:4, ... all:6, default: 2)", 0, 1, set_verbose},
    {OPTIONS, FORCE_DECODE_TOKEN,               "Force decode first frame", 0, 1, set_force_decode},
    {INPUT_OPTIONS, INPUT_FILE_TOKEN,           "Input Filename", 1, 1, set_cfg_input_file},
    {INPUT_OPTIONS, FRAMES_TOKEN,               "Number of frames to decode", 0, 1, set_frame_num},
    {INPUT_OPTIONS, FORCE_BITSTREAM_HEADER_TOKEN,"Find bitstream header", 0, 0, set_find_header},
    {INPUT_OPTIONS, LIMIT_FPS_TOKEN,             "Limit number of frames per second (disabled: 0, enabled [1-240])", 0, 1, set_limit_fps},
    {INPUT_OPTIONS, PACKETIZATION_MODE,          "Specify how bitstream is passed to decoder(multiple packets per frame:1, single packet per frame:0, default:0)", 0, 1, set_packetization_mode},
    {OUTPUT_OPTIONS, OUTPUT_FILE_TOKEN,         "Output Filename", 0, 1, set_cfg_output_file},
    {OUTPUT_OPTIONS, PROXY_MODE,                "Resolution scaling mode(disabled: 0, scale 1/2: 1, scale 1/4: 2, default: 0)", 0, 1, set_proxy_mode},
    {THREAD_PERF_OPTIONS, ASM_TYPE_TOKEN,       "Limit assembly instruction set [0 - 11] or [c, mmx, sse, sse2, sse3, "
                                                "ssse3, sse4_1, sse4_2,"
                                                " avx, avx2, avx512, max], by default highest level supported by CPU", 0, 1,
                                                set_asm_type},
    {THREAD_PERF_OPTIONS, THREADS_TOKEN,        "Thread Scaling parameter, the higher the value the more threads are created and thus lower latency and/or higher FPS can be achieved (default: 0, which means lowest possible number of threads is created)", 0, 1, set_num_thread},
    // Termination
    {NULL_OPTIONS, NULL, NULL, 0, 0, NULL}
};
// clang-format on

void show_help() {
    enum CfgType type = OPTIONS;
    printf(
        "Usage: SvtJpegxsDecApp <options> -o dst_filename -i src_filename\n"
        "\nOptions:\n");
    printf("      [--help]                    Show usage options and exit\n");
    for (ConfigEntry* token_index = config_entry; token_index->token; ++token_index) {
        if (token_index->type != type) {
            type = token_index->type;
            switch (type) {
            case INPUT_OPTIONS:
                printf("\nInput Options:\n");
                break;
            case OUTPUT_OPTIONS:
                printf("\nOutput Options:\n");
                break;
            case THREAD_PERF_OPTIONS:
                printf("\nThreading, performance:\n");
                break;
            default:
                break;
            }
        }

        if (token_index->param_required) {
            printf("      %-25s   %-25s\n", token_index->token, token_index->name);
        }
        else {
            int offset = (int)strnlen(token_index->token, 500);
            if (offset < 25) {
                offset = 25 - offset;
            }
            else {
                offset = 0;
            }
            printf("      [%s]%*c %-25s\n", token_index->token, offset, ' ', token_index->name);
        }
    }
}

/**
 * @brief Find token and its argument
 * @param argc      Argument count
 * @param argv      Argument array
 * @param token     The token to look for
 * @param configStr Output buffer to write the argument to or NULL
 * @return 0 if found, non-zero otherwise
 *
 * @note The configStr buffer must be at least
 *       COMMAND_LINE_MAX_SIZE bytes big.
 *       The argv must contain an additional NULL
 *       element to terminate it, so that
 *       argv[argc] == NULL.
 */
static int32_t find_token(int32_t argc, char* const argv[], char const* token, char* configStr) {
    // assert(argv[argc] == NULL);

    if (argc == 0)
        return -1;

    for (int32_t i = argc - 1; i >= 0; i--) {
        if (strcmp(argv[i], token) != 0)
            continue;

        // The argument matches the token.
        // If given, try to copy its argument to configStr
        if (configStr && argv[i + 1] != NULL) {
            strncpy_local(configStr, argv[i + 1], COMMAND_LINE_MAX_SIZE);
        }
        else if (configStr) {
            configStr[0] = '\0';
        }

        return 0;
    }
    return -1;
}

uint32_t get_help(int32_t argc, char* const argv[]) {
    char config_string[COMMAND_LINE_MAX_SIZE];
    if (find_token(argc, argv, HELP_TOKEN, config_string))
        return 0;
    show_help();
    return 1;
}

SvtJxsErrorType_t read_command_line(int32_t argc, char* const argv[], DecoderConfig_t* configs) {
    char* cmd_copy[MAX_NUM_TOKENS] = {NULL};
    uint8_t cmd_detected[MAX_NUM_TOKENS] = {0};
    char* config_strings[MAX_NUM_TOKENS] = {NULL};
    int32_t cmd_token_cnt = 0;
    int token_index = 0;

    for (token_index = 1; token_index < argc; token_index++, cmd_token_cnt++) {
        if (argv[token_index][0] == '-') {
            cmd_copy[cmd_token_cnt] = argv[token_index];
            if (argv[token_index + 1] != NULL && (argv[token_index + 1][0] != '-'))
                config_strings[cmd_token_cnt] = argv[++token_index];
        }
        else {
            fprintf(stderr, " Invalid CLI: %s \n", argv[token_index]);
            return SvtJxsErrorBadParameter;
        }
    }

    for (int32_t param_idx1 = 0; param_idx1 < cmd_token_cnt; param_idx1++) {
        for (int32_t param_idx2 = param_idx1 + 1; param_idx2 < cmd_token_cnt; param_idx2++) {
            if (strcmp(cmd_copy[param_idx1], cmd_copy[param_idx2]) == 0) {
                fprintf(stderr, "CLI Parameter appears multiple times: %s \n", cmd_copy[param_idx1]);
                return SvtJxsErrorBadParameter;
            }
        }
    }

    token_index = 0;
    while (config_entry[token_index].name != NULL) {
        uint8_t param_found = 0;
        for (int32_t param_idx = 0; param_idx < cmd_token_cnt; param_idx++) {
            if (strcmp(cmd_copy[param_idx], HELP_TOKEN) == 0) {
                show_help();
            }
            if (strcmp(cmd_copy[param_idx], config_entry[token_index].token) == 0) {
                if (config_strings[param_idx] == NULL && config_entry[token_index].value_required) {
                    fprintf(stderr, "Empty CLI option: %s \n", cmd_copy[param_idx]);
                    return SvtJxsErrorBadParameter;
                }
                (*config_entry[token_index].scf)(config_strings[param_idx], configs);
                param_found = 1;
                cmd_detected[param_idx] = 1;
                break;
            }
        }
        if (config_entry[token_index].param_required && !param_found) {
            fprintf(stderr, "Required CLI option not found: %s \n", config_entry[token_index].token);
            return SvtJxsErrorBadParameter;
        }
        token_index++;
    }
    for (int32_t param_idx = 0; param_idx < cmd_token_cnt; param_idx++) {
        if (cmd_detected[param_idx] == 0) {
            fprintf(stderr, "Provided CLI option unrecognized: %s \n", cmd_copy[param_idx]);
            return SvtJxsErrorBadParameter;
        }
    }

    return SvtJxsErrorNone;
}
