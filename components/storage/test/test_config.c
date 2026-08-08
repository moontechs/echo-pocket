/** @file test_config.c
 * @brief Unity tests for config INI parser and serializer.
 *
 * Tests config_set_defaults, config_parse, config_serialize as pure
 * functions with in-memory strings — no filesystem needed.
 * File I/O (config_load / config_save) is verified on-device per the plan.
 */

#include <string.h>
#include "unity.h"
#include "config.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** Verify that a freshly-defaulted config has every field set safely. */
static void assert_default_config(const RecorderConfig *c)
{
    TEST_ASSERT_NOT_NULL(c);

    /* [device] */
    TEST_ASSERT_EQUAL_STRING("VoiceRecorder", c->device_name);
    TEST_ASSERT_EQUAL_STRING("UTC", c->timezone);

    /* [wifi_N] — empty */
    TEST_ASSERT_EQUAL_INT(0, c->wifi_count);

    /* [telegram] — disabled */
    TEST_ASSERT_EQUAL_STRING("", c->bot_token);
    TEST_ASSERT_FALSE(c->send_to_all);
    TEST_ASSERT_EQUAL_INT(1, c->active_channel);
    TEST_ASSERT_EQUAL_INT(0, c->channel_count);

    /* [recorder] */
    TEST_ASSERT_TRUE(c->auto_upload);
    TEST_ASSERT_FALSE(c->delete_after_upload);
    TEST_ASSERT_EQUAL_INT(16000, c->sample_rate);
    TEST_ASSERT_TRUE(c->noise_suppression);
    TEST_ASSERT_TRUE(c->voice_detection);

    /* [face] */
    TEST_ASSERT_EQUAL_STRING("vector", c->theme);
    TEST_ASSERT_TRUE(c->react_to_voice);
    TEST_ASSERT_EQUAL_INT(5, c->eye_min_size);
    TEST_ASSERT_EQUAL_INT(22, c->eye_max_size);
    TEST_ASSERT_TRUE(c->blink);
    TEST_ASSERT_EQUAL_INT(20, c->animation_fps);
}

/* ── Test: defaults ──────────────────────────────────────────────────── */

void test_config_defaults(void)
{
    RecorderConfig c;
    config_set_defaults(&c);
    assert_default_config(&c);
}

/* ── Test: empty input ───────────────────────────────────────────────── */

void test_config_parse_empty(void)
{
    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, "");
    TEST_ASSERT_EQUAL_INT(0, errs);
    assert_default_config(&c);  /* defaults untouched */
}

void test_config_parse_null(void)
{
    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, NULL);
    TEST_ASSERT_EQUAL_INT(0, errs);
    assert_default_config(&c);
}

/* ── Test: valid full config ─────────────────────────────────────────── */

void test_config_parse_valid(void)
{
    const char *ini =
        "[device]\n"
        "name=MyRecorder\n"
        "timezone=Europe/Berlin\n"
        "\n"
        "[wifi_1]\n"
        "ssid=HomeWiFi\n"
        "password=secret123\n"
        "\n"
        "[wifi_2]\n"
        "ssid=OfficeWiFi\n"
        "password=office456\n"
        "\n"
        "[telegram]\n"
        "bot_token=1234567890:AAExampleToken\n"
        "send_to_all=false\n"
        "active_channel=2\n"
        "channel_1_id=-1001234567890\n"
        "channel_1_name=Voice Notes\n"
        "channel_2_id=@myChannel\n"
        "channel_2_name=Second Channel\n"
        "\n"
        "[recorder]\n"
        "auto_upload=false\n"
        "delete_after_upload=true\n"
        "sample_rate=16000\n"
        "noise_suppression=false\n"
        "voice_detection=true\n"
        "\n"
        "[face]\n"
        "theme=owl\n"
        "react_to_voice=true\n"
        "eye_min_size=8\n"
        "eye_max_size=30\n"
        "blink=false\n"
        "animation_fps=15\n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);
    TEST_ASSERT_EQUAL_INT(0, errs);

    /* [device] */
    TEST_ASSERT_EQUAL_STRING("MyRecorder", c.device_name);
    TEST_ASSERT_EQUAL_STRING("Europe/Berlin", c.timezone);

    /* [wifi] */
    TEST_ASSERT_EQUAL_INT(2, c.wifi_count);
    TEST_ASSERT_EQUAL_STRING("HomeWiFi", c.wifi_networks[0].ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", c.wifi_networks[0].password);
    TEST_ASSERT_EQUAL_STRING("OfficeWiFi", c.wifi_networks[1].ssid);
    TEST_ASSERT_EQUAL_STRING("office456", c.wifi_networks[1].password);

    /* [telegram] */
    TEST_ASSERT_EQUAL_STRING("1234567890:AAExampleToken", c.bot_token);
    TEST_ASSERT_FALSE(c.send_to_all);
    TEST_ASSERT_EQUAL_INT(2, c.active_channel);
    TEST_ASSERT_EQUAL_INT(2, c.channel_count);
    TEST_ASSERT_EQUAL_STRING("-1001234567890", c.channels[0].id);
    TEST_ASSERT_EQUAL_STRING("Voice Notes", c.channels[0].name);
    TEST_ASSERT_EQUAL_STRING("@myChannel", c.channels[1].id);
    TEST_ASSERT_EQUAL_STRING("Second Channel", c.channels[1].name);

    /* [recorder] */
    TEST_ASSERT_FALSE(c.auto_upload);
    TEST_ASSERT_TRUE(c.delete_after_upload);
    TEST_ASSERT_EQUAL_INT(16000, c.sample_rate);
    TEST_ASSERT_FALSE(c.noise_suppression);
    TEST_ASSERT_TRUE(c.voice_detection);

    /* [face] */
    TEST_ASSERT_EQUAL_STRING("owl", c.theme);
    TEST_ASSERT_TRUE(c.react_to_voice);
    TEST_ASSERT_EQUAL_INT(8, c.eye_min_size);
    TEST_ASSERT_EQUAL_INT(30, c.eye_max_size);
    TEST_ASSERT_FALSE(c.blink);
    TEST_ASSERT_EQUAL_INT(15, c.animation_fps);
}

/* ── Test: multiple wifi_N entries ──────────────────────────────────── */

void test_config_multiple_wifi(void)
{
    const char *ini =
        "[wifi_3]\n"
        "ssid=ThirdNet\n"
        "password=thirdpass\n"
        "[wifi_1]\n"
        "ssid=FirstNet\n"
        "password=firstpass\n"
        "[wifi_5]\n"
        "ssid=FifthNet\n"
        "password=fifthpass\n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);
    TEST_ASSERT_EQUAL_INT(0, errs);

    /* wifi_count is the max index seen */
    TEST_ASSERT_EQUAL_INT(5, c.wifi_count);

    /* wifi_1 (index 0) */
    TEST_ASSERT_EQUAL_STRING("FirstNet", c.wifi_networks[0].ssid);
    TEST_ASSERT_EQUAL_STRING("firstpass", c.wifi_networks[0].password);

    /* wifi_2 (index 1) — not in INI, should be empty strings */
    TEST_ASSERT_EQUAL_STRING("", c.wifi_networks[1].ssid);
    TEST_ASSERT_EQUAL_STRING("", c.wifi_networks[1].password);

    /* wifi_3 (index 2) */
    TEST_ASSERT_EQUAL_STRING("ThirdNet", c.wifi_networks[2].ssid);
    TEST_ASSERT_EQUAL_STRING("thirdpass", c.wifi_networks[2].password);

    /* wifi_4 (index 3) — not in INI */
    TEST_ASSERT_EQUAL_STRING("", c.wifi_networks[3].ssid);

    /* wifi_5 (index 4) */
    TEST_ASSERT_EQUAL_STRING("FifthNet", c.wifi_networks[4].ssid);
    TEST_ASSERT_EQUAL_STRING("fifthpass", c.wifi_networks[4].password);
}

/* ── Test: malformed lines skipped ──────────────────────────────────── */

void test_config_malformed_skipped(void)
{
    const char *ini =
        "not a valid ini line at all\n"
        "[device]\n"
        "also not valid\n"
        "name = GoodRecorder\n"
        "[unknown_section]\n"
        "some_key=some_value\n"
        "=value_without_key\n"
        "key_without_value=\n"
        "[recorder\n"          /* missing ']' — malformed section */
        "auto_upload=false\n"; /* under NONE section — should NOT apply */

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);

    /* We expect malformed line errors (but struct is still usable) */
    TEST_ASSERT_TRUE(errs > 0);

    /* name was parsed correctly under [device] */
    TEST_ASSERT_EQUAL_STRING("GoodRecorder", c.device_name);

    /* auto_upload was inside a malformed section header — should NOT
     * have been applied, so it stays at default (true) */
    TEST_ASSERT_TRUE(c.auto_upload);
}

/* ── Test: unknown section/keys don't crash ─────────────────────────── */

void test_config_unknown_section(void)
{
    const char *ini =
        "[bogus]\n"
        "whatever=ignored\n"
        "[device]\n"
        "name=Survivor\n"
        "unknown_key=should_be_ignored\n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);
    TEST_ASSERT_EQUAL_INT(0, errs); /* unknown != malformed */

    /* The device.name was still parsed */
    TEST_ASSERT_EQUAL_STRING("Survivor", c.device_name);

    /* Other defaults untouched */
    TEST_ASSERT_EQUAL_STRING("UTC", c.timezone);
}

/* ── Test: comments ──────────────────────────────────────────────────── */

void test_config_comments(void)
{
    const char *ini =
        "# This is a comment\n"
        "; So is this\n"
        "[device]\n"
        "   # indented comment (not a valid key, should be skipped)\n"
        "name=EchoDevice\n"
        "; mid-section comment\n"
        "timezone=Asia/Tokyo\n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);

    TEST_ASSERT_EQUAL_STRING("EchoDevice", c.device_name);
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", c.timezone);
}

/* ── Test: whitespace tolerance ──────────────────────────────────────── */

void test_config_whitespace(void)
{
    const char *ini =
        "  [device]  \n"
        "  name  =  Spaced Recorder  \n"
        "timezone =    America/New_York    \n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);
    TEST_ASSERT_EQUAL_INT(0, errs);

    TEST_ASSERT_EQUAL_STRING("Spaced Recorder", c.device_name);
    TEST_ASSERT_EQUAL_STRING("America/New_York", c.timezone);
}

/* ── Test: boolean formats ───────────────────────────────────────────── */

void test_config_bool_formats(void)
{
    const char *ini =
        "[recorder]\n"
        "auto_upload=TRUE\n"
        "delete_after_upload=1\n"
        "noise_suppression=yes\n"
        "voice_detection=FALSE\n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);
    TEST_ASSERT_EQUAL_INT(0, errs);

    TEST_ASSERT_TRUE(c.auto_upload);
    TEST_ASSERT_TRUE(c.delete_after_upload);
    TEST_ASSERT_TRUE(c.noise_suppression);
    TEST_ASSERT_FALSE(c.voice_detection);
}

/* ── Test: serialization round-trip ──────────────────────────────────── */

void test_config_roundtrip(void)
{
    const char *ini =
        "[device]\n"
        "name=RoundTrip\n"
        "timezone=Pacific/Auckland\n"
        "[wifi_1]\n"
        "ssid=RoundNet\n"
        "password=roundpass\n"
        "[telegram]\n"
        "bot_token=999:abc\n"
        "send_to_all=true\n"
        "active_channel=1\n"
        "channel_1_id=-123\n"
        "channel_1_name=Chan One\n"
        "[recorder]\n"
        "auto_upload=false\n"
        "delete_after_upload=true\n"
        "sample_rate=16000\n"
        "noise_suppression=false\n"
        "voice_detection=false\n"
        "[face]\n"
        "theme=robot\n"
        "react_to_voice=false\n"
        "eye_min_size=10\n"
        "eye_max_size=25\n"
        "blink=false\n"
        "animation_fps=30\n";

    /* Parse original */
    RecorderConfig c1;
    config_set_defaults(&c1);
    int errs = config_parse(&c1, ini);
    TEST_ASSERT_EQUAL_INT(0, errs);

    /* Serialize */
    char buf[2048];
    size_t len = config_serialize(&c1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len < sizeof(buf));

    /* Parse the serialized output */
    RecorderConfig c2;
    config_set_defaults(&c2);
    errs = config_parse(&c2, buf);
    TEST_ASSERT_EQUAL_INT(0, errs);

    /* Compare key fields */
    TEST_ASSERT_EQUAL_STRING(c1.device_name, c2.device_name);
    TEST_ASSERT_EQUAL_STRING(c1.timezone, c2.timezone);
    TEST_ASSERT_EQUAL_INT(c1.wifi_count, c2.wifi_count);
    TEST_ASSERT_EQUAL_STRING(c1.wifi_networks[0].ssid, c2.wifi_networks[0].ssid);
    TEST_ASSERT_EQUAL_STRING(c1.wifi_networks[0].password, c2.wifi_networks[0].password);
    TEST_ASSERT_EQUAL_STRING(c1.bot_token, c2.bot_token);
    TEST_ASSERT_EQUAL_INT(c1.send_to_all, c2.send_to_all);
    TEST_ASSERT_EQUAL_INT(c1.active_channel, c2.active_channel);
    TEST_ASSERT_EQUAL_INT(c1.channel_count, c2.channel_count);
    TEST_ASSERT_EQUAL_STRING(c1.channels[0].id, c2.channels[0].id);
    TEST_ASSERT_EQUAL_STRING(c1.channels[0].name, c2.channels[0].name);
    TEST_ASSERT_EQUAL_INT(c1.auto_upload, c2.auto_upload);
    TEST_ASSERT_EQUAL_INT(c1.delete_after_upload, c2.delete_after_upload);
    TEST_ASSERT_EQUAL_INT(c1.noise_suppression, c2.noise_suppression);
    TEST_ASSERT_EQUAL_INT(c1.voice_detection, c2.voice_detection);
    TEST_ASSERT_EQUAL_STRING(c1.theme, c2.theme);
    TEST_ASSERT_EQUAL_INT(c1.react_to_voice, c2.react_to_voice);
    TEST_ASSERT_EQUAL_INT(c1.eye_min_size, c2.eye_min_size);
    TEST_ASSERT_EQUAL_INT(c1.eye_max_size, c2.eye_max_size);
    TEST_ASSERT_EQUAL_INT(c1.blink, c2.blink);
    TEST_ASSERT_EQUAL_INT(c1.animation_fps, c2.animation_fps);
}

/* ── Test: serialization buffer too small ────────────────────────────── */

void test_config_serialize_buffer_too_small(void)
{
    RecorderConfig c;
    config_set_defaults(&c);

    char buf[16];
    size_t len = config_serialize(&c, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, len); /* should fail */
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* ── Test: NULL safety ───────────────────────────────────────────────── */

void test_config_null_safety(void)
{
    /* config_set_defaults(NULL) must not crash */
    config_set_defaults(NULL);

    /* config_parse(NULL, …) must not crash */
    TEST_ASSERT_EQUAL_INT(0, config_parse(NULL, NULL));
    TEST_ASSERT_EQUAL_INT(0, config_parse(NULL, "[device]\nname=x\n"));

    /* config_serialize(NULL, …) must not crash */
    char buf[64];
    TEST_ASSERT_EQUAL_INT(0, config_serialize(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, config_serialize(NULL, NULL, 0));
}

/* ── Test: channel parsing with gaps ──────────────────────────────────── */

void test_config_channels_with_gaps(void)
{
    const char *ini =
        "[telegram]\n"
        "channel_1_id=chan1_id\n"
        "channel_1_name=Channel One\n"
        "channel_3_id=chan3_id\n"
        "channel_3_name=Channel Three\n";

    RecorderConfig c;
    config_set_defaults(&c);
    int errs = config_parse(&c, ini);
    TEST_ASSERT_EQUAL_INT(0, errs);

    /* channel_count should be 3 (max index seen) */
    TEST_ASSERT_EQUAL_INT(3, c.channel_count);

    TEST_ASSERT_EQUAL_STRING("chan1_id", c.channels[0].id);
    TEST_ASSERT_EQUAL_STRING("Channel One", c.channels[0].name);

    /* channel 2 (index 1) — not set, should be empty */
    TEST_ASSERT_EQUAL_STRING("", c.channels[1].id);
    TEST_ASSERT_EQUAL_STRING("", c.channels[1].name);

    TEST_ASSERT_EQUAL_STRING("chan3_id", c.channels[2].id);
    TEST_ASSERT_EQUAL_STRING("Channel Three", c.channels[2].name);
}

/* ── Test: config_load defaults on NULL path ─────────────────────────── */

void test_config_load_null_path(void)
{
    config_err_t err = CONFIG_OK;
    RecorderConfig c = config_load(NULL, &err);
    TEST_ASSERT_EQUAL_INT(CONFIG_ERR_FILE_NOT_FOUND, err);
    assert_default_config(&c);
}

/* ── Test: config_load nonexistent file ──────────────────────────────── */

void test_config_load_nonexistent_file(void)
{
    config_err_t err = CONFIG_OK;
    RecorderConfig c = config_load("/nonexistent/path/recorder.ini", &err);
    TEST_ASSERT_EQUAL_INT(CONFIG_ERR_FILE_NOT_FOUND, err);
    assert_default_config(&c);
}

/* ── Test: config_save NULL safety ───────────────────────────────────── */

void test_config_save_null_safety(void)
{
    RecorderConfig c;
    config_set_defaults(&c);

    config_err_t err = config_save(NULL, "/tmp/test.ini");
    TEST_ASSERT_EQUAL_INT(CONFIG_ERR_WRITE_FAILED, err);

    err = config_save(&c, NULL);
    TEST_ASSERT_EQUAL_INT(CONFIG_ERR_WRITE_FAILED, err);
}

/* ── Test: error strings ─────────────────────────────────────────────── */

void test_config_error_strings(void)
{
    /* Every error code maps to a distinct non-empty string */
    const char *s_ok       = config_err_str(CONFIG_OK);
    const char *s_notfound = config_err_str(CONFIG_ERR_FILE_NOT_FOUND);
    const char *s_read     = config_err_str(CONFIG_ERR_READ_FAILED);
    const char *s_write    = config_err_str(CONFIG_ERR_WRITE_FAILED);
    const char *s_rename   = config_err_str(CONFIG_ERR_RENAME_FAILED);
    const char *s_buf      = config_err_str(CONFIG_ERR_BUFFER_TOO_SMALL);

    TEST_ASSERT_NOT_NULL(s_ok);
    TEST_ASSERT_TRUE(strlen(s_ok) > 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_notfound) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_read) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_write) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_rename) != 0);
    TEST_ASSERT_TRUE(strcmp(s_ok, s_buf) != 0);
}
