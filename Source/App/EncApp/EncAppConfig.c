/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "EncAppConfig.h"
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

static void strncpy_local(char *dest, const char *src, size_t count) {
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

/**********************************
 * Defines
 **********************************/
#define COMMAND_LINE_MAX_SIZE 2048

#define HELP_TOKEN              "--help"
#define INPUT_FILE_TOKEN        "-i"
#define WIDTH_TOKEN             "-w"
#define HEIGHT_TOKEN            "-h"
#define COMPRESS_BPP_LONG_TOKEN "--bpp"
#define DECOMP_V_LONG_TOKEN     "--decomp_v"
#define DECOMP_H_LONG_TOKEN     "--decomp_h"
#define QUANTIZATION_TOKEN      "--quantization"
#define SLICE_HEIGHT_TOKEN      "--slice-height"
#define ENCODER_COLOUR_FORMAT   "--colour-format"
#define INPUT_DEPTH_TOKEN       "--input-depth"

#define OUTPUT_BITSTREAM_TOKEN "-b"
#define NO_PROGRESS_TOKEN      "--no-progress" // tbd if it should be removed
#define STATS_REPORT_TOKEN     "--stat-report"

#define ASM_TYPE_TOKEN     "--asm"
#define PROFILE_TYPE_TOKEN "--profile"
#define THREAD_MGMNT       "--lp"
#define FRAMES_COUNT_TOKEN "-n"
// double dash
#define PRESET_TOKEN "--preset"

#define CODING_SIGNS_TOKEN  "--coding-signs"
#define CODING_SIGF_TOKEN   "--coding-sigf"
#define CODING_PRED_TOKEN   "--coding-vpred"
#define CODING_RATE_CONTROL "--rc"
#define SHOW_BANDS          "--show-bands"

#define LIMIT_FPS_TOKEN "--limit-fps"
#define VERBOSE_TOKEN   "-v"

// latency configuration
#define PACKETIZATION_MODE "--packetization-mode"

/**********************************
 * Set Cfg Functions
 **********************************/
static void set_cfg_input_file(const char *value, EncoderConfig_t *cfg) {
    strncpy_local(cfg->in_filename, value, sizeof(cfg->in_filename));
}

static void set_cfg_stream_file(const char *value, EncoderConfig_t *cfg) {
    strncpy_local(cfg->out_filename, value, sizeof(cfg->out_filename));
}

static void set_cfg_source_width(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.source_width = strtoul(value, NULL, 0);
}
static void set_cfg_source_height(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.source_height = strtoul(value, NULL, 0);
}
static void set_cfg_frames_count(const char *value, EncoderConfig_t *cfg) {
    cfg->frames_count = strtoul(value, NULL, 0);
}

static void set_limit_fps(const char *value, EncoderConfig_t *cfg) {
    cfg->limit_fps = strtoul(value, NULL, 0);
}

static void set_no_progress(const char *value, EncoderConfig_t *cfg) {
    switch (value ? *value : '1') {
    case '0':
        cfg->progress = 1;
        break; // equal to --progress 1
    default:
        cfg->progress = 0;
        break; // equal to --progress 0
    }
}

static void set_input_bit_depth(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.input_bit_depth = (uint8_t)strtoul(value, NULL, 0);
}

static void coding_signs_handling(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.coding_signs_handling = strtoul(value, NULL, 0);
}

static void set_coding_significance(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.coding_significance = strtoul(value, NULL, 0);
}

static void set_coding_vpred(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.coding_vertical_prediction_mode = strtoul(value, NULL, 0);
}

static void set_rate_control_mode(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.rate_control_mode = strtoul(value, NULL, 0);
}

static void set_encoder_colour_format(const char *value, EncoderConfig_t *cfg) {
    if (!strcmp(value, "yuv400")) {
        cfg->encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV400;
    }
    else if (!strcmp(value, "yuv420")) {
        cfg->encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    }
    else if (!strcmp(value, "yuv422")) {
        cfg->encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    }
    else if (!strcmp(value, "yuv444")) {
        cfg->encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    }
    else if (!strcmp(value, "rgb")) {
        cfg->encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    }
    else if (!strcmp(value, "rgbp")) {
        cfg->encoder.colour_format = COLOUR_FORMAT_PACKED_YUV444_OR_RGB;
    }
    else {
        cfg->encoder.colour_format = COLOUR_FORMAT_INVALID;
    }
}

static void set_encoder_bpp(const char *value, EncoderConfig_t *cfg) {
    char *end;
    cfg->encoder.bpp_numerator = strtoul(value, &end, 0);
    cfg->encoder.bpp_denominator = 1;
    if (*end != '\0') {
        while (*value) {
            if (*value == '.' || *value == ',') {
                value++;
                break;
            }
            value++;
        }
        if (*value) {
            uint32_t fraction = strtoul(value, &end, 0);
            uint32_t chars = (uint32_t)(end - value);
            for (uint32_t i = 0; i < chars; ++i) {
                cfg->encoder.bpp_denominator *= 10;
            }
            cfg->encoder.bpp_numerator = cfg->encoder.bpp_numerator * cfg->encoder.bpp_denominator + fraction;
        }
    }
}

static void set_encoder_decomp_v(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.ndecomp_v = strtoul(value, NULL, 0);
}

static void set_encoder_decomp_h(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.ndecomp_h = strtoul(value, NULL, 0);
}

static void set_quantization(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.quantization = strtoul(value, NULL, 0);
}

static void set_slice_height(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.slice_height = strtoul(value, NULL, 0);
}

static void set_asm_type(const char *value, EncoderConfig_t *cfg) {
    const struct {
        const char *name;
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
            cfg->encoder.use_cpu_flags = param_maps[i].flags;
            return;
        }
    }

    cfg->encoder.use_cpu_flags = CPU_FLAGS_INVALID;
}

static void set_profile_type(const char *value, EncoderConfig_t *cfg) {
    const struct {
        const char *name;
        uint8_t value;
    } param_maps[] = {
        {"latency", 0},
        {"cpu", 1},
    };
    const uint32_t para_map_size = sizeof(param_maps) / sizeof(param_maps[0]);
    uint32_t i;

    for (i = 0; i < para_map_size; ++i) {
        if (strcmp(value, param_maps[i].name) == 0) {
            cfg->encoder.cpu_profile = param_maps[i].value;
            return;
        }
    }
    cfg->encoder.cpu_profile = 0xff; //Set Invalid
}

static void set_num_thread(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.threads_num = (uint32_t)strtoul(value, NULL, 0);
}

static void set_print_bands(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.print_bands_info = (int32_t)strtol(value, NULL, 0);
}

static void set_verbose(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.verbose = strtoul(value, NULL, 0);
};

static void set_packetization_mode(const char *value, EncoderConfig_t *cfg) {
    cfg->encoder.slice_packetization_mode = (uint8_t)strtoul(value, NULL, 0);
};

/**********************************
  * Config Entry Array
  **********************************/
enum CfgType {
    NULL_OPTIONS,
    OPTIONS,
    INPUT_OPTIONS,
    OUTPUT_OPTIONS,
    THREAD_PERF_OPTIONS,
    CODING_OPTIONS,
};

typedef struct config_entry_s {
    enum CfgType type;
    const char *token;
    const char *name;
    uint8_t param_required;
    uint8_t value_required;
    void (*scf)(const char *, EncoderConfig_t *);
} ConfigEntry;

// clang-format off
ConfigEntry config_entry[] = {
    {OPTIONS,       SHOW_BANDS,             "Print detailed informations about bands (enabled:1, disable:0, default:0)", 0, 1, set_print_bands},
    {OPTIONS,       VERBOSE_TOKEN,          "Verbose encoder internal level (disabled:0, errors:1, system info:2,system info full:3  warnings:4, ... all:6, default: 2)", 0, 1, set_verbose},
    {INPUT_OPTIONS, INPUT_FILE_TOKEN,       "Input Filename", 1, 1, set_cfg_input_file},
    {INPUT_OPTIONS, WIDTH_TOKEN,            "Frame width", 1, 1, set_cfg_source_width},
    {INPUT_OPTIONS, HEIGHT_TOKEN,           "Frame height", 1, 1, set_cfg_source_height},
    {INPUT_OPTIONS, ENCODER_COLOUR_FORMAT,  "Set encoder colour format (yuv420, yuv422) (Experimental: yuv400, yuv444, rgb(planar), rgbp(packed))", 1, 1, set_encoder_colour_format},
    {INPUT_OPTIONS, INPUT_DEPTH_TOKEN,      "Input depth", 1, 1, set_input_bit_depth},
    {INPUT_OPTIONS, COMPRESS_BPP_LONG_TOKEN,"Bits Per Pixel, can be passed as integer or float (example: 0.5, 3, 3.75, 5 etc.)", 1, 1, set_encoder_bpp},
    {INPUT_OPTIONS, FRAMES_COUNT_TOKEN,     "Number of frames to encode", 0, 1, set_cfg_frames_count},
    {INPUT_OPTIONS, LIMIT_FPS_TOKEN,        "Limit number of frames per second (disabled: 0, enabled [1-240])", 0, 1, set_limit_fps},
    {OUTPUT_OPTIONS, OUTPUT_BITSTREAM_TOKEN,"Output filename", 0, 1, set_cfg_stream_file},
    {OUTPUT_OPTIONS, NO_PROGRESS_TOKEN,     "Do not print out progress (no print:1, print:0, default:0)", 0, 1, set_no_progress},
    {OUTPUT_OPTIONS, PACKETIZATION_MODE,    "Specify how encoded stream is returned (multiple packets per frame:1, single packet per frame:0, default:0)", 0, 1, set_packetization_mode},
    {CODING_OPTIONS, DECOMP_V_LONG_TOKEN,   "Vertical decomposition (0, 1, 2, default: 2)", 0, 1, set_encoder_decomp_v},
    {CODING_OPTIONS, DECOMP_H_LONG_TOKEN,   "Horizontal decomposition have to be greater or equal to decomp_v (1, 2, 3, 4, 5, default: 5)", 0, 1, set_encoder_decomp_h},
    {CODING_OPTIONS, QUANTIZATION_TOKEN,    "Quantization algorithm (deadzone:0, uniform:1, default:0)", 0, 1, set_quantization},
    {CODING_OPTIONS, SLICE_HEIGHT_TOKEN,    "Slice height(default:16)", 0, 1, set_slice_height},
    {CODING_OPTIONS, CODING_SIGNS_TOKEN,    "Enable Signs handling strategy (full:2, fast:1, disable:0, default:0)", 0, 1, coding_signs_handling},
    {CODING_OPTIONS, CODING_SIGF_TOKEN,     "Enable Significance coding (enabled:1, disable:0, default:1)", 0, 1, set_coding_significance},
    {CODING_OPTIONS, CODING_PRED_TOKEN,     "Enable Vertical Prediction coding (disable:0, zero prediction residuals:1, zero coefficients:2, default: 0)", 0, 1, set_coding_vpred},
    {CODING_OPTIONS, CODING_RATE_CONTROL,   "Rate Control mode (CBR: budget per precinct: 0, CBR: budget per precinct with padding movement: 1, CBR: budget per slice: 2, CBR: budget per slice with max size RATE: 3, default 0)", 0, 1, set_rate_control_mode},
    {THREAD_PERF_OPTIONS, ASM_TYPE_TOKEN,   "Limit assembly instruction set [0 - 11] or [c, mmx, sse, sse2, sse3, "
                                            "ssse3, sse4_1, sse4_2,"
                                            " avx, avx2, avx512, max], by default highest level supported by CPU", 0, 1,
                                            set_asm_type},
    {THREAD_PERF_OPTIONS, PROFILE_TYPE_TOKEN,"Profile of CPU use. 0:latency Low Latency mode, 1:cpu Low CPU use mode [latency:0, cpu:1, default: 0]", 0, 1, set_profile_type},
    {THREAD_PERF_OPTIONS, THREAD_MGMNT,     "Thread Scaling parameter, the higher the value the more threads are created and thus lower latency and/or higher FPS can be achieved (default: 0, which means lowest possible number of threads is created)", 0, 1, set_num_thread},
    // Termination
    {NULL_OPTIONS, NULL, NULL, 0, 0, NULL}
};
// clang-format on

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
static int32_t find_token(int32_t argc, char *const argv[], char const *token, char *configStr) {
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

/******************************************
 * Verify Settings
 ******************************************/
SvtJxsErrorType_t verify_settings(EncoderConfig_t *config) {
    SvtJxsErrorType_t return_error = SvtJxsErrorNone;

    if (config->encoder.colour_format == COLOUR_FORMAT_INVALID) {
        fprintf(stderr, "Error: colour format not set\n");
        return_error = SvtJxsErrorBadParameter;
    }

    if (config->encoder.source_width == 0 || config->encoder.source_height == 0) {
        fprintf(stderr, "Error: Invalid input resolution %dx%d\n", config->encoder.source_width, config->encoder.source_height);
        return_error = SvtJxsErrorBadParameter;
    }

    if (config->limit_fps > 240) {
        fprintf(stderr, "Error The fps limit is 240 fps\n");
        return_error = SvtJxsErrorBadParameter;
    }

    if (config->encoder.cpu_profile != 0 && config->encoder.cpu_profile != 1) {
        fprintf(stderr, "Error: Invalid profile [latency:0, cpu:1], your input: %d\n", config->encoder.cpu_profile);
        return_error = SvtJxsErrorBadParameter;
    }

    return return_error;
}

uint32_t show_help() {
    enum CfgType type = OPTIONS;
    printf(
        "Usage: SvtJpegxsEncApp <options> -b dst_filename -i src_filename\n"
        "\nOptions:\n");
    printf("      [--help]                    Show usage options and exit\n");
    for (ConfigEntry *token_index = config_entry; token_index->token; ++token_index) {
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
            case CODING_OPTIONS:
                printf("\nCoding features used during Rate Calculation (quality/speed trade-off):\n");
                break;
            //case VERBOSE_OPTIONS:
            //  printf("\nVerbose Options:\n");
            //  break;
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
    return 0;
}

uint32_t get_help(int32_t argc, char *const argv[]) {
    char config_string[COMMAND_LINE_MAX_SIZE];
    if (find_token(argc, argv, HELP_TOKEN, config_string))
        return 0;
    show_help();
    return 1;
}

/******************************************
 * Read Command Line
 ******************************************/
SvtJxsErrorType_t read_command_line(int32_t argc, char *const argv[], EncoderConfig_t *configs) {
    char *cmd_copy[MAX_NUM_TOKENS] = {NULL};
    uint8_t cmd_detected[MAX_NUM_TOKENS] = {0};
    char *config_strings[MAX_NUM_TOKENS] = {NULL};
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
