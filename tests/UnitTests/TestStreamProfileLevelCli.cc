/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <cstring>
#include <initializer_list>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxsEnc.h"
extern "C" {
#include "EncAppConfig.h"
}

namespace {

/* Runs the App's CLI parser (read_command_line + verify_settings) over the given extra tokens, with a
 * minimal valid set of mandatory options (input file / resolution / format / depth / bpp) already
 * filled in, and returns the resulting config. `read_command_line_ret` and `verify_settings_ret`
 * capture each stage's return code so tests can assert on parsing vs. semantic-validation failures
 * separately. */
struct CliResult {
    EncoderConfig_t cfg;
    SvtJxsErrorType_t read_command_line_ret;
    SvtJxsErrorType_t verify_settings_ret;
};

CliResult run_cli(std::initializer_list<const char *> extra_tokens) {
    CliResult result;
    memset(&result.cfg, 0, sizeof(result.cfg));
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &result.cfg.encoder),
              SvtJxsErrorNone);

    std::vector<const char *> argv = {
        "SvtJpegxsEncApp", "-i", "dummy_input.yuv", "-w", "256", "-h", "256", "--colour-format", "yuv422",
        "--input-depth", "8", "--bpp", "8"};
    for (const char *token : extra_tokens) {
        argv.push_back(token);
    }

    result.read_command_line_ret = read_command_line(
        (int32_t)argv.size(), const_cast<char *const *>(argv.data()), &result.cfg);
    result.verify_settings_ret = (result.read_command_line_ret == SvtJxsErrorNone) ? verify_settings(&result.cfg)
                                                                                    : SvtJxsErrorBadParameter;
    return result;
}

} // namespace

TEST(StreamProfileLevelCli, DefaultsToAutoWhenFlagsNotProvided) {
    CliResult result = run_cli({});
    EXPECT_EQ(result.read_command_line_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.profile_ppih_override, 0);
    EXPECT_EQ(result.cfg.encoder.level_plev_override, 0xFFFF);
}

TEST(StreamProfileLevelCli, ProfileNameIsParsedToPpihValue) {
    CliResult result = run_cli({"--stream-profile", "main444"});
    EXPECT_EQ(result.read_command_line_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.profile_ppih_override, 0x3A40);
}

TEST(StreamProfileLevelCli, ProfileNameHigh4444) {
    CliResult result = run_cli({"--stream-profile", "high4444"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.profile_ppih_override, 0x4E40);
}

TEST(StreamProfileLevelCli, ProfileAutoKeywordExplicitlySelectsAuto) {
    CliResult result = run_cli({"--stream-profile", "auto"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.profile_ppih_override, 0);
}

TEST(StreamProfileLevelCli, ProfileRawHexValueIsAccepted) {
    CliResult result = run_cli({"--stream-profile", "0x3540"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.profile_ppih_override, 0x3540);
}

TEST(StreamProfileLevelCli, UnrecognizedProfileNameFailsVerification) {
    CliResult result = run_cli({"--stream-profile", "not-a-real-profile"});
    EXPECT_EQ(result.read_command_line_ret, SvtJxsErrorNone); // parses (sets an internal invalid sentinel)...
    EXPECT_NE(result.verify_settings_ret, SvtJxsErrorNone); // ...but must fail validation.
}

TEST(StreamProfileLevelCli, LevelNameIsParsedToPlevValue) {
    CliResult result = run_cli({"--stream-level", "4k-1"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.level_plev_override, (uint16_t)(0x0008 << 10));
}

TEST(StreamProfileLevelCli, LevelAutoKeywordExplicitlySelectsAuto) {
    CliResult result = run_cli({"--stream-level", "auto"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.level_plev_override, 0xFFFF);
}

TEST(StreamProfileLevelCli, LevelUnrestrictedNameIsDistinctFromAutoSentinel) {
    CliResult result = run_cli({"--stream-level", "unrestricted"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.level_plev_override, 0x0000);
}

TEST(StreamProfileLevelCli, LevelRawHexValueWithSublevelIsAccepted) {
    CliResult result = run_cli({"--stream-level", "0x0810"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.level_plev_override, 0x0810);
}

TEST(StreamProfileLevelCli, UnrecognizedLevelNameFailsVerification) {
    CliResult result = run_cli({"--stream-level", "not-a-real-level"});
    EXPECT_EQ(result.read_command_line_ret, SvtJxsErrorNone);
    EXPECT_NE(result.verify_settings_ret, SvtJxsErrorNone);
}

TEST(StreamProfileLevelCli, ProfileAndLevelCanBeOverriddenTogether) {
    CliResult result = run_cli({"--stream-profile", "high444", "--stream-level", "8k-1"});
    EXPECT_EQ(result.verify_settings_ret, SvtJxsErrorNone);
    EXPECT_EQ(result.cfg.encoder.profile_ppih_override, 0x4A40);
    EXPECT_EQ(result.cfg.encoder.level_plev_override, (uint16_t)(0x000C << 10));
}
