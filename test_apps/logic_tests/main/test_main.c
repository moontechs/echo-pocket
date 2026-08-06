#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "board.h"

/* ── Pin uniqueness within each function group ───────────────────────────
 *
 * Rationale (from plan Task 1):
 *   - LCD and SD sharing an SPI bus is legitimate on combined boards,
 *     so we only assert distinctness WITHIN each function group.
 *   - We do NOT assert non-zero because GPIO0 is a legitimate (if strapping) pin.
 */

static void test_button_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_BTN_LEFT_PIN, BOARD_BTN_CENTER_PIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_BTN_LEFT_PIN, BOARD_BTN_RIGHT_PIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_BTN_CENTER_PIN, BOARD_BTN_RIGHT_PIN);
}

static void test_lcd_spi_pins_distinct(void)
{
    /* MOSI, SCLK, CS, DC, RST, BL must all differ */
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_SCLK);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_CS);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_DC);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_MOSI, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_CS);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_DC);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_DC);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_DC, BOARD_LCD_PIN_RST);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_DC, BOARD_LCD_PIN_BL);
    TEST_ASSERT_NOT_EQUAL(BOARD_LCD_PIN_RST, BOARD_LCD_PIN_BL);
}

static void test_sd_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_CMD);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D0);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D1);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CLK, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D0);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D1);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_CMD, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D0, BOARD_SD_PIN_D1);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D0, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D0, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D1, BOARD_SD_PIN_D2);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D1, BOARD_SD_PIN_D3);
    TEST_ASSERT_NOT_EQUAL(BOARD_SD_PIN_D2, BOARD_SD_PIN_D3);
}

static void test_i2c_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_I2C_PIN_SDA, BOARD_I2C_PIN_SCL);
}

static void test_i2s_pins_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_BCK);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_WS);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_DIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_MCK, BOARD_I2S_PIN_DOUT);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_BCK, BOARD_I2S_PIN_WS);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_BCK, BOARD_I2S_PIN_DIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_BCK, BOARD_I2S_PIN_DOUT);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_WS, BOARD_I2S_PIN_DIN);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_WS, BOARD_I2S_PIN_DOUT);
    TEST_ASSERT_NOT_EQUAL(BOARD_I2S_PIN_DIN, BOARD_I2S_PIN_DOUT);
}

static void test_battery_pins_defined(void)
{
    /* Battery pins are optional but must be valid GPIO numbers (or NC) */
    TEST_ASSERT_TRUE(BOARD_BAT_ADC_PIN >= 0 || BOARD_BAT_ADC_PIN == GPIO_NUM_NC);
    TEST_ASSERT_TRUE(BOARD_BAT_CHARGING_PIN >= 0 || BOARD_BAT_CHARGING_PIN == GPIO_NUM_NC);
    TEST_ASSERT_TRUE(BOARD_BAT_POWER_PIN >= 0 || BOARD_BAT_POWER_PIN == GPIO_NUM_NC);
}

/* ── Button debounce tests (Task 3) ─────────────────────────────────── */
/* Defined in test_buttons.c (compiled alongside this file) */
extern void test_debounce_init_state(void);
extern void test_single_press_release(void);
extern void test_press_bounce_no_false_event(void);
extern void test_release_bounce_single_event(void);
extern void test_long_press_one_event(void);
extern void test_consecutive_presses(void);
extern void test_idle_noise_no_event(void);
extern void test_independent_instances(void);

/* ── SD storage tests (Task 4) ──────────────────────────────────────── */
/* Defined in test_sd_storage.c (compiled alongside this file) */
extern void test_mount_point_not_empty(void);
extern void test_app_root_nested_under_mount(void);
extern void test_subdirs_under_app_root(void);
extern void test_subdirs_are_distinct(void);
extern void test_subdir_names_are_expected(void);
extern void test_error_strings_are_distinct(void);
extern void test_mounted_flag_initially_false(void);

/* ── Ring buffer tests (Task 6) ─────────────────────────────────────── */
/* Defined in test_ring_buffer.c (compiled alongside this file) */
extern void test_rb_alloc_free(void);
extern void test_rb_alloc_zero_returns_null(void);
extern void test_rb_free_null_is_safe(void);
extern void test_rb_capacity_null_safe(void);
extern void test_rb_available_null_safe(void);
extern void test_rb_overflow_null_safe(void);
extern void test_rb_write_read_roundtrip(void);
extern void test_rb_read_empty_returns_zero(void);
extern void test_rb_read_less_than_available(void);
extern void test_rb_write_null_params(void);
extern void test_rb_read_null_params(void);
extern void test_rb_overflow_drops_oldest(void);
extern void test_rb_overflow_reset_counter(void);
extern void test_rb_wrap_write_across_boundary(void);
extern void test_rb_wrap_read_across_boundary(void);
extern void test_rb_wrap_write_exactly_to_end(void);
extern void test_rb_producer_consumer_interleaved(void);
extern void test_rb_producer_faster_than_consumer(void);
extern void test_downmix_silent(void);
extern void test_downmix_identical_channels(void);
extern void test_downmix_opposite_channels(void);
extern void test_downmix_rounds_toward_zero(void);
extern void test_downmix_null_safety(void);
extern void test_downmix_large_values(void);

/* ── WAV writer tests (Task 7) ──────────────────────────────────────── */
/* Defined in test_wav_writer.c (compiled alongside this file) */
extern void test_wav_header_mono_16k_zero_data(void);
extern void test_wav_header_mono_16k_small_data(void);
extern void test_wav_header_mono_16k_10min_data(void);
extern void test_wav_header_mono_16k_max_data(void);
extern void test_wav_header_stereo_44k(void);
extern void test_wav_header_stereo_48k_24bit(void);
extern void test_wav_header_size(void);
extern void test_wav_header_offsets(void);
extern void test_wav_header_fill_null(void);

/* ── Recorder split threshold tests (Task 7) ────────────────────────── */
extern void test_recorder_split_below_threshold(void);
extern void test_recorder_split_at_threshold(void);
extern void test_recorder_split_above_threshold(void);
extern void test_recorder_split_threshold_matches_19_minutes(void);

/* ── Rec ID tests (Task 7) ──────────────────────────────────────────── */
/* Defined in test_rec_id.c (compiled alongside this file) */
extern void test_rec_id_synced_basic(void);
extern void test_rec_id_synced_counter_advanced(void);
extern void test_rec_id_synced_midnight(void);
extern void test_rec_id_synced_leap_year(void);
extern void test_rec_id_synced_max_counter(void);
extern void test_rec_id_offline_zero_uptime(void);
extern void test_rec_id_offline_with_uptime(void);
extern void test_rec_id_offline_max_uptime(void);
extern void test_rec_id_offline_uptime_wraps_at_output_size(void);
extern void test_rec_id_counter_overflow(void);
extern void test_rec_id_counter_at_boundary(void);
extern void test_rec_id_buffer_too_small(void);
extern void test_rec_id_buffer_exact_size(void);
extern void test_rec_id_buffer_one_byte_too_small(void);
extern void test_rec_id_null_buffer(void);
extern void test_rec_id_zero_buf_size(void);
extern void test_rec_id_consecutive_are_unique(void);
extern void test_rec_id_offline_consecutive_are_unique(void);
extern void test_rec_id_error_strings_distinct(void);

/* ── Voice level tests (Task 8) ─────────────────────────────────────── */
/* Defined in test_process_pipeline.c (compiled alongside this file) */
extern void test_voice_rms_silence(void);
extern void test_voice_rms_full_scale_dc(void);
extern void test_voice_rms_half_scale(void);
extern void test_voice_rms_single_sample(void);
extern void test_voice_rms_single_zero(void);
extern void test_voice_rms_zero_count(void);
extern void test_voice_rms_null_samples(void);
extern void test_voice_rms_sine_wave(void);
extern void test_voice_rms_large_chunk(void);
extern void test_voice_smooth_alpha_one(void);
extern void test_voice_smooth_alpha_zero(void);
extern void test_voice_smooth_typical(void);
extern void test_voice_smooth_convergence(void);
extern void test_voice_smooth_rising(void);
extern void test_voice_smooth_falling(void);
extern void test_voice_db_full_scale(void);
extern void test_voice_db_half_amplitude(void);
extern void test_voice_db_silence(void);
extern void test_voice_db_quiet(void);
extern void test_voice_db_above_full_scale(void);
extern void test_voice_db_negative(void);
extern void test_voice_pipeline_speech_chunk(void);
extern void test_voice_pipeline_silence_to_speech(void);

/* ── Face registry tests (Task 9) ──────────────────────────────────── */
/* Defined in test_face_registry.cpp (compiled alongside this file) */
extern void test_face_registry_empty_count(void);
extern void test_face_registry_empty_find(void);
extern void test_face_registry_empty_active_is_null(void);
extern void test_face_registry_register_increases_count(void);
extern void test_face_registry_register_null_noop(void);
extern void test_face_registry_find_by_id(void);
extern void test_face_registry_find_missing_returns_null(void);
extern void test_face_registry_fallback_to_minimal(void);
extern void test_face_registry_fallback_calls_begin_on_fallback(void);
extern void test_face_registry_begin_known(void);
extern void test_face_registry_begin_empty_registry(void);
extern void test_face_registry_all_events_dispatch(void);
extern void test_face_registry_fps_initial_call(void);
extern void test_face_registry_fps_within_interval(void);
extern void test_face_registry_fps_zero_uncapped(void);
extern void test_face_registry_fps_wraparound(void);
extern void test_face_registry_active_update_routing(void);
extern void test_face_registry_active_draw_routing(void);

/* ── Face theme tests (Task 10) ──────────────────────────────────────── */
/* Defined in test_face_themes.cpp (compiled alongside this file) */
extern void test_face_theme_owl_all_events(void);
extern void test_face_theme_minimal_all_events(void);
extern void test_face_theme_minimal_is_fallback(void);
extern void test_face_theme_robot_all_events(void);
extern void test_face_theme_pixel_all_events(void);
extern void test_face_theme_vector_all_events(void);
extern void test_face_theme_config_eye_range(void);
extern void test_face_theme_config_no_react(void);
extern void test_face_theme_config_no_blink(void);
extern void test_face_theme_begin_resets_state(void);
extern void test_face_theme_unique_ids(void);

/* ── UI button routing tests (Task 11) ────────────────────────────────── */
/* Defined in test_ui_button_routing.c (compiled alongside this file) */
extern void test_home_center_starts_recording(void);
extern void test_home_left_noop(void);
extern void test_home_right_opens_menu(void);
extern void test_recording_center_stops(void);
extern void test_recording_left_noop(void);
extern void test_recording_right_noop(void);
extern void test_saved_any_button_returns_home(void);
extern void test_screen_next_null_action(void);
extern void test_full_record_cycle(void);
extern void test_screen_name_known(void);
extern void test_screen_name_unknown(void);

/* ── Menu navigation tests (Task 12) ────────────────────────────────── */
/* Defined in test_menu_nav.c (compiled alongside this file) */
extern void test_menu_init_cursor(void);
extern void test_menu_init_null_safe(void);
extern void test_menu_label_known_items(void);
extern void test_menu_label_unknown(void);
extern void test_menu_right_moves_cursor(void);
extern void test_menu_right_wraps_around(void);
extern void test_menu_left_returns_home(void);
extern void test_menu_center_new_recording(void);
extern void test_menu_center_face(void);
extern void test_menu_center_send_all(void);
extern void test_menu_center_stub_items(void);
extern void test_menu_navigate_null_safety(void);
extern void test_menu_item_count_is_eight(void);
extern void test_face_submenu_init(void);
extern void test_face_submenu_init_zero_count(void);
extern void test_face_submenu_init_null(void);
extern void test_face_submenu_right_moves_cursor(void);
extern void test_face_submenu_right_wraps(void);
extern void test_face_submenu_left_exits(void);
extern void test_face_submenu_center_selects(void);
extern void test_face_submenu_center_first(void);
extern void test_face_submenu_navigate_null_safety(void);
extern void test_menu_to_face_and_back(void);

/* ── Net selection tests (Task 14) ──────────────────────────────────── */
/* Defined in test_net_selection.c (compiled alongside this file) */
extern void test_ns_first_attempt_with_networks(void);
extern void test_ns_first_attempt_single_network(void);
extern void test_ns_first_attempt_no_networks(void);
extern void test_ns_first_attempt_negative_count(void);
extern void test_ns_already_connected_ignores_last_result(void);
extern void test_ns_already_connected_on_first_attempt(void);
extern void test_ns_success_stays_on_same_network(void);
extern void test_ns_success_on_last_network(void);
extern void test_ns_failure_advances_to_next(void);
extern void test_ns_failure_advances_multiple_steps(void);
extern void test_ns_failure_last_network_returns_no_more(void);
extern void test_ns_failure_single_network_returns_no_more(void);
extern void test_ns_full_walkthrough_all_fail(void);
extern void test_ns_full_walkthrough_success_on_second(void);
extern void test_ns_null_out_next_on_try_index(void);
extern void test_ns_null_out_next_on_no_more(void);
extern void test_ns_disconnect_then_retry_same_network(void);
extern void test_ns_large_network_count(void);
extern void test_ns_large_network_count_all_fail(void);

/* ── List pagination tests (Task 13) ─────────────────────────────────── */
/* Defined in test_list_pagination.c (compiled alongside this file) */
extern void test_pagination_init_normal(void);
extern void test_pagination_init_zero_items(void);
extern void test_pagination_init_negative_items(void);
extern void test_pagination_init_zero_page_size(void);
extern void test_pagination_init_negative_page_size(void);
extern void test_pagination_init_null(void);
extern void test_pagination_scroll_items_fewer_than_page(void);
extern void test_pagination_scroll_items_equal_to_page(void);
extern void test_pagination_scroll_cursor_in_first_page(void);
extern void test_pagination_scroll_cursor_middle(void);
extern void test_pagination_scroll_cursor_near_end(void);
extern void test_pagination_scroll_one_item(void);
extern void test_pagination_scroll_zero_items(void);
extern void test_pagination_scroll_null(void);
extern void test_pagination_cursor_down_basic(void);
extern void test_pagination_cursor_down_wraps(void);
extern void test_pagination_cursor_down_single_item(void);
extern void test_pagination_cursor_down_zero_items(void);
extern void test_pagination_cursor_down_null(void);
extern void test_pagination_cursor_up_basic(void);
extern void test_pagination_cursor_up_wraps(void);
extern void test_pagination_cursor_up_single_item(void);
extern void test_pagination_cursor_up_zero_items(void);
extern void test_pagination_cursor_up_null(void);
extern void test_pagination_full_traverse_down_and_back(void);
extern void test_pagination_scroll_two_pages_exact(void);

/* ── Queue store tests (Task 15) ──────────────────────────────────── */
/* Defined in test_queue_store.c (compiled alongside this file) */
extern void test_queue_state_strings_all(void);
extern void test_queue_state_strings_invalid(void);
extern void test_queue_state_from_str_all(void);
extern void test_queue_state_from_str_unknown(void);
extern void test_serialize_empty_queue(void);
extern void test_serialize_null_params(void);
extern void test_serialize_single_pending(void);
extern void test_serialize_single_sent(void);
extern void test_serialize_single_failed(void);
extern void test_serialize_multiple_entries(void);
extern void test_serialize_buffer_too_small(void);
extern void test_deserialize_empty_array(void);
extern void test_deserialize_null_params(void);
extern void test_deserialize_bad_input(void);
extern void test_deserialize_single(void);
extern void test_deserialize_single_sent(void);
extern void test_deserialize_multiple(void);
extern void test_round_trip_serialize_deserialize(void);
extern void test_recover_no_uploading(void);
extern void test_recover_one_uploading(void);
extern void test_recover_multiple_uploading(void);
extern void test_recover_null_safety(void);
extern void test_deserialize_exceeds_capacity(void);
extern void test_queue_store_error_strings_distinct(void);
extern void test_recover_preserves_non_uploading_fields(void);
extern void test_serialize_escape_quotes(void);

/* ── Telegram caption tests (Task 16) ───────────────────────────────── */
/* Defined in test_caption.c (compiled alongside this file) */
extern void test_caption_basic(void);
extern void test_caption_zero_duration(void);
extern void test_caption_one_second(void);
extern void test_caption_59_seconds(void);
extern void test_caption_one_minute(void);
extern void test_caption_one_hour(void);
extern void test_caption_max_18_min(void);
extern void test_caption_19_min_59_sec(void);
extern void test_caption_null_rec_id(void);
extern void test_caption_null_device_name(void);
extern void test_caption_null_both(void);
extern void test_caption_null_buffer(void);
extern void test_caption_zero_buf_size(void);
extern void test_caption_buffer_exact(void);
extern void test_caption_buffer_one_byte_too_small(void);
extern void test_caption_large_duration(void);
extern void test_caption_empty_device_name(void);
extern void test_caption_empty_rec_id(void);
extern void test_caption_no_trailing_newline(void);
extern void test_caption_subsecond_truncation(void);
extern void test_caption_exact_expected_format(void);
extern void test_caption_error_strings(void);
extern void test_caption_default_device_name(void);

/* ── Upload drain flow tests (Task 17) — NOT currently built ──────────
 * test_upload_flow.c exercises upload_drain_compute_outcome(), which
 * only exists in upload_task.c — never added to this test app's SRCS
 * (unlike recorder_logic.c, which was split out from recorder.c
 * specifically for testability, upload_task.c has no equivalent pure-
 * logic split, so pulling it in means pulling in its full FreeRTOS/
 * Wi-Fi/telegram/recorder dependency chain). test_upload_flow.c's own
 * test functions were also `static` until now, so none of this has
 * ever actually linked or run. Left disabled rather than risk pulling
 * in a chain of new component REQUIRES unrelated to this session's fix
 * — pre-existing gap, flagged for a follow-up. */

/* ── Battery discharge curve tests (Task 18) ──────────────────────────── */
/* Defined in test_battery_curve.c (compiled alongside this file) */
extern void test_voltage_above_max_is_100(void);
extern void test_voltage_below_min_is_0(void);
extern void test_voltage_zero_is_0(void);
extern void test_voltage_negative_is_0(void);
extern void test_voltage_table_points_match(void);
extern void test_voltage_interpolation_mid(void);
extern void test_voltage_interpolation_just_above_low_end(void);
extern void test_voltage_interpolation_just_below_high_end(void);
extern void test_voltage_monotonic_descending(void);
extern void test_voltage_le_100(void);
extern void test_percent_to_threshold_normal(void);
extern void test_percent_to_threshold_warning(void);
extern void test_percent_to_threshold_critical(void);
extern void test_percent_to_threshold_unknown(void);
extern void test_block_upload_critical_large_file(void);
extern void test_block_upload_critical_small_file(void);
extern void test_block_upload_warning_allowed(void);
extern void test_block_upload_normal_allowed(void);
extern void test_block_upload_unknown_allowed(void);
extern void test_block_upload_critical_exact_boundary(void);
extern void test_unknown_constant_is_negative(void);
extern void test_low_upload_max_is_reasonable(void);
extern void test_full_pipeline_high_battery(void);
extern void test_full_pipeline_warning(void);
extern void test_full_pipeline_critical(void);

/* ── Config INI tests (Task 5) ──────────────────────────────────────── */
/* Defined in test_config.c (compiled alongside this file) */
extern void test_config_defaults(void);
extern void test_config_parse_empty(void);
extern void test_config_parse_null(void);
extern void test_config_parse_valid(void);
extern void test_config_multiple_wifi(void);
extern void test_config_malformed_skipped(void);
extern void test_config_unknown_section(void);
extern void test_config_comments(void);
extern void test_config_whitespace(void);
extern void test_config_bool_formats(void);
extern void test_config_roundtrip(void);
extern void test_config_serialize_buffer_too_small(void);
extern void test_config_null_safety(void);
extern void test_config_channels_with_gaps(void);
extern void test_config_load_null_path(void);
extern void test_config_load_nonexistent_file(void);
extern void test_config_save_null_safety(void);
extern void test_config_error_strings(void);

/* ── Test runner ─────────────────────────────────────────────────────── */

void app_main(void)
{
    UNITY_BEGIN();

    /* Task 1: pin distinctness */
    RUN_TEST(test_button_pins_distinct);
    RUN_TEST(test_lcd_spi_pins_distinct);
    RUN_TEST(test_sd_pins_distinct);
    RUN_TEST(test_i2c_pins_distinct);
    RUN_TEST(test_i2s_pins_distinct);
    RUN_TEST(test_battery_pins_defined);

    /* Task 3: button debounce state machine */
    RUN_TEST(test_debounce_init_state);
    RUN_TEST(test_single_press_release);
    RUN_TEST(test_press_bounce_no_false_event);
    RUN_TEST(test_release_bounce_single_event);
    RUN_TEST(test_long_press_one_event);
    RUN_TEST(test_consecutive_presses);
    RUN_TEST(test_idle_noise_no_event);
    RUN_TEST(test_independent_instances);

    /* Task 4: SD storage paths and error handling */
    RUN_TEST(test_mount_point_not_empty);
    RUN_TEST(test_app_root_nested_under_mount);
    RUN_TEST(test_subdirs_under_app_root);
    RUN_TEST(test_subdirs_are_distinct);
    RUN_TEST(test_subdir_names_are_expected);
    RUN_TEST(test_error_strings_are_distinct);
    RUN_TEST(test_mounted_flag_initially_false);

    /* Task 6: audio ring buffer and downmix helper */
    RUN_TEST(test_rb_alloc_free);
    RUN_TEST(test_rb_alloc_zero_returns_null);
    RUN_TEST(test_rb_free_null_is_safe);
    RUN_TEST(test_rb_capacity_null_safe);
    RUN_TEST(test_rb_available_null_safe);
    RUN_TEST(test_rb_overflow_null_safe);
    RUN_TEST(test_rb_write_read_roundtrip);
    RUN_TEST(test_rb_read_empty_returns_zero);
    RUN_TEST(test_rb_read_less_than_available);
    RUN_TEST(test_rb_write_null_params);
    RUN_TEST(test_rb_read_null_params);
    RUN_TEST(test_rb_overflow_drops_oldest);
    RUN_TEST(test_rb_overflow_reset_counter);
    RUN_TEST(test_rb_wrap_write_across_boundary);
    RUN_TEST(test_rb_wrap_read_across_boundary);
    RUN_TEST(test_rb_wrap_write_exactly_to_end);
    RUN_TEST(test_rb_producer_consumer_interleaved);
    RUN_TEST(test_rb_producer_faster_than_consumer);
    RUN_TEST(test_downmix_silent);
    RUN_TEST(test_downmix_identical_channels);
    RUN_TEST(test_downmix_opposite_channels);
    RUN_TEST(test_downmix_rounds_toward_zero);
    RUN_TEST(test_downmix_null_safety);
    RUN_TEST(test_downmix_large_values);

    /* Task 7: WAV header math */
    RUN_TEST(test_wav_header_mono_16k_zero_data);
    RUN_TEST(test_wav_header_mono_16k_small_data);
    RUN_TEST(test_wav_header_mono_16k_10min_data);
    RUN_TEST(test_wav_header_mono_16k_max_data);
    RUN_TEST(test_wav_header_stereo_44k);
    RUN_TEST(test_wav_header_stereo_48k_24bit);
    RUN_TEST(test_wav_header_size);
    RUN_TEST(test_wav_header_offsets);
    RUN_TEST(test_wav_header_fill_null);

    /* Task 7: recorder split threshold (state machine) */
    RUN_TEST(test_recorder_split_below_threshold);
    RUN_TEST(test_recorder_split_at_threshold);
    RUN_TEST(test_recorder_split_above_threshold);
    RUN_TEST(test_recorder_split_threshold_matches_19_minutes);

    /* Task 7: recording ID generation */
    RUN_TEST(test_rec_id_synced_basic);
    RUN_TEST(test_rec_id_synced_counter_advanced);
    RUN_TEST(test_rec_id_synced_midnight);
    RUN_TEST(test_rec_id_synced_leap_year);
    RUN_TEST(test_rec_id_synced_max_counter);
    RUN_TEST(test_rec_id_offline_zero_uptime);
    RUN_TEST(test_rec_id_offline_with_uptime);
    RUN_TEST(test_rec_id_offline_max_uptime);
    RUN_TEST(test_rec_id_offline_uptime_wraps_at_output_size);
    RUN_TEST(test_rec_id_counter_overflow);
    RUN_TEST(test_rec_id_counter_at_boundary);
    RUN_TEST(test_rec_id_buffer_too_small);
    RUN_TEST(test_rec_id_buffer_exact_size);
    RUN_TEST(test_rec_id_buffer_one_byte_too_small);
    RUN_TEST(test_rec_id_null_buffer);
    RUN_TEST(test_rec_id_zero_buf_size);
    RUN_TEST(test_rec_id_consecutive_are_unique);
    RUN_TEST(test_rec_id_offline_consecutive_are_unique);
    RUN_TEST(test_rec_id_error_strings_distinct);

    /* Task 8: voice level computation and smoothing */
    RUN_TEST(test_voice_rms_silence);
    RUN_TEST(test_voice_rms_full_scale_dc);
    RUN_TEST(test_voice_rms_half_scale);
    RUN_TEST(test_voice_rms_single_sample);
    RUN_TEST(test_voice_rms_single_zero);
    RUN_TEST(test_voice_rms_zero_count);
    RUN_TEST(test_voice_rms_null_samples);
    RUN_TEST(test_voice_rms_sine_wave);
    RUN_TEST(test_voice_rms_large_chunk);
    RUN_TEST(test_voice_smooth_alpha_one);
    RUN_TEST(test_voice_smooth_alpha_zero);
    RUN_TEST(test_voice_smooth_typical);
    RUN_TEST(test_voice_smooth_convergence);
    RUN_TEST(test_voice_smooth_rising);
    RUN_TEST(test_voice_smooth_falling);
    RUN_TEST(test_voice_db_full_scale);
    RUN_TEST(test_voice_db_half_amplitude);
    RUN_TEST(test_voice_db_silence);
    RUN_TEST(test_voice_db_quiet);
    RUN_TEST(test_voice_db_above_full_scale);
    RUN_TEST(test_voice_db_negative);
    RUN_TEST(test_voice_pipeline_speech_chunk);
    RUN_TEST(test_voice_pipeline_silence_to_speech);

    /* Task 9: face registry */
    RUN_TEST(test_face_registry_empty_count);
    RUN_TEST(test_face_registry_empty_find);
    RUN_TEST(test_face_registry_empty_active_is_null);
    RUN_TEST(test_face_registry_register_increases_count);
    RUN_TEST(test_face_registry_register_null_noop);
    RUN_TEST(test_face_registry_find_by_id);
    RUN_TEST(test_face_registry_find_missing_returns_null);
    RUN_TEST(test_face_registry_fallback_to_minimal);
    RUN_TEST(test_face_registry_fallback_calls_begin_on_fallback);
    RUN_TEST(test_face_registry_begin_known);
    RUN_TEST(test_face_registry_begin_empty_registry);
    RUN_TEST(test_face_registry_all_events_dispatch);
    RUN_TEST(test_face_registry_fps_initial_call);
    RUN_TEST(test_face_registry_fps_within_interval);
    RUN_TEST(test_face_registry_fps_zero_uncapped);
    RUN_TEST(test_face_registry_fps_wraparound);
    RUN_TEST(test_face_registry_active_update_routing);
    RUN_TEST(test_face_registry_active_draw_routing);

    /* Task 10: face themes */
    RUN_TEST(test_face_theme_owl_all_events);
    RUN_TEST(test_face_theme_minimal_all_events);
    RUN_TEST(test_face_theme_minimal_is_fallback);
    RUN_TEST(test_face_theme_robot_all_events);
    RUN_TEST(test_face_theme_pixel_all_events);
    RUN_TEST(test_face_theme_vector_all_events);
    RUN_TEST(test_face_theme_config_eye_range);
    RUN_TEST(test_face_theme_config_no_react);
    RUN_TEST(test_face_theme_config_no_blink);
    RUN_TEST(test_face_theme_begin_resets_state);
    RUN_TEST(test_face_theme_unique_ids);

    /* Task 11: UI button routing state machine */
    RUN_TEST(test_home_center_starts_recording);
    RUN_TEST(test_home_left_noop);
    RUN_TEST(test_home_right_opens_menu);
    RUN_TEST(test_recording_center_stops);
    RUN_TEST(test_recording_left_noop);
    RUN_TEST(test_recording_right_noop);
    RUN_TEST(test_saved_any_button_returns_home);
    RUN_TEST(test_screen_next_null_action);
    RUN_TEST(test_full_record_cycle);
    RUN_TEST(test_screen_name_known);
    RUN_TEST(test_screen_name_unknown);

    /* Task 12: menu navigation state machine */
    RUN_TEST(test_menu_init_cursor);
    RUN_TEST(test_menu_init_null_safe);
    RUN_TEST(test_menu_label_known_items);
    RUN_TEST(test_menu_label_unknown);
    RUN_TEST(test_menu_right_moves_cursor);
    RUN_TEST(test_menu_right_wraps_around);
    RUN_TEST(test_menu_left_returns_home);
    RUN_TEST(test_menu_center_new_recording);
    RUN_TEST(test_menu_center_face);
    RUN_TEST(test_menu_center_send_all);
    RUN_TEST(test_menu_center_stub_items);
    RUN_TEST(test_menu_navigate_null_safety);
    RUN_TEST(test_menu_item_count_is_eight);
    RUN_TEST(test_face_submenu_init);
    RUN_TEST(test_face_submenu_init_zero_count);
    RUN_TEST(test_face_submenu_init_null);
    RUN_TEST(test_face_submenu_right_moves_cursor);
    RUN_TEST(test_face_submenu_right_wraps);
    RUN_TEST(test_face_submenu_left_exits);
    RUN_TEST(test_face_submenu_center_selects);
    RUN_TEST(test_face_submenu_center_first);
    RUN_TEST(test_face_submenu_navigate_null_safety);
    RUN_TEST(test_menu_to_face_and_back);

    /* Task 13: list pagination state machine */
    RUN_TEST(test_pagination_init_normal);
    RUN_TEST(test_pagination_init_zero_items);
    RUN_TEST(test_pagination_init_negative_items);
    RUN_TEST(test_pagination_init_zero_page_size);
    RUN_TEST(test_pagination_init_negative_page_size);
    RUN_TEST(test_pagination_init_null);
    RUN_TEST(test_pagination_scroll_items_fewer_than_page);
    RUN_TEST(test_pagination_scroll_items_equal_to_page);
    RUN_TEST(test_pagination_scroll_cursor_in_first_page);
    RUN_TEST(test_pagination_scroll_cursor_middle);
    RUN_TEST(test_pagination_scroll_cursor_near_end);
    RUN_TEST(test_pagination_scroll_one_item);
    RUN_TEST(test_pagination_scroll_zero_items);
    RUN_TEST(test_pagination_scroll_null);
    RUN_TEST(test_pagination_cursor_down_basic);
    RUN_TEST(test_pagination_cursor_down_wraps);
    RUN_TEST(test_pagination_cursor_down_single_item);
    RUN_TEST(test_pagination_cursor_down_zero_items);
    RUN_TEST(test_pagination_cursor_down_null);
    RUN_TEST(test_pagination_cursor_up_basic);
    RUN_TEST(test_pagination_cursor_up_wraps);
    RUN_TEST(test_pagination_cursor_up_single_item);
    RUN_TEST(test_pagination_cursor_up_zero_items);
    RUN_TEST(test_pagination_cursor_up_null);
    RUN_TEST(test_pagination_full_traverse_down_and_back);
    RUN_TEST(test_pagination_scroll_two_pages_exact);

    /* Task 14: net selection next-attempt logic */
    RUN_TEST(test_ns_first_attempt_with_networks);
    RUN_TEST(test_ns_first_attempt_single_network);
    RUN_TEST(test_ns_first_attempt_no_networks);
    RUN_TEST(test_ns_first_attempt_negative_count);
    RUN_TEST(test_ns_already_connected_ignores_last_result);
    RUN_TEST(test_ns_already_connected_on_first_attempt);
    RUN_TEST(test_ns_success_stays_on_same_network);
    RUN_TEST(test_ns_success_on_last_network);
    RUN_TEST(test_ns_failure_advances_to_next);
    RUN_TEST(test_ns_failure_advances_multiple_steps);
    RUN_TEST(test_ns_failure_last_network_returns_no_more);
    RUN_TEST(test_ns_failure_single_network_returns_no_more);
    RUN_TEST(test_ns_full_walkthrough_all_fail);
    RUN_TEST(test_ns_full_walkthrough_success_on_second);
    RUN_TEST(test_ns_null_out_next_on_try_index);
    RUN_TEST(test_ns_null_out_next_on_no_more);
    RUN_TEST(test_ns_disconnect_then_retry_same_network);
    RUN_TEST(test_ns_large_network_count);
    RUN_TEST(test_ns_large_network_count_all_fail);

    /* Task 15: upload queue persistence and crash recovery */
    RUN_TEST(test_queue_state_strings_all);
    RUN_TEST(test_queue_state_strings_invalid);
    RUN_TEST(test_queue_state_from_str_all);
    RUN_TEST(test_queue_state_from_str_unknown);
    RUN_TEST(test_serialize_empty_queue);
    RUN_TEST(test_serialize_null_params);
    RUN_TEST(test_serialize_single_pending);
    RUN_TEST(test_serialize_single_sent);
    RUN_TEST(test_serialize_single_failed);
    RUN_TEST(test_serialize_multiple_entries);
    RUN_TEST(test_serialize_buffer_too_small);
    RUN_TEST(test_deserialize_empty_array);
    RUN_TEST(test_deserialize_null_params);
    RUN_TEST(test_deserialize_bad_input);
    RUN_TEST(test_deserialize_single);
    RUN_TEST(test_deserialize_single_sent);
    RUN_TEST(test_deserialize_multiple);
    RUN_TEST(test_round_trip_serialize_deserialize);
    RUN_TEST(test_recover_no_uploading);
    RUN_TEST(test_recover_one_uploading);
    RUN_TEST(test_recover_multiple_uploading);
    RUN_TEST(test_recover_null_safety);
    RUN_TEST(test_deserialize_exceeds_capacity);
    RUN_TEST(test_queue_store_error_strings_distinct);
    RUN_TEST(test_recover_preserves_non_uploading_fields);
    RUN_TEST(test_serialize_escape_quotes);

    /* Task 16: telegram caption formatting */
    RUN_TEST(test_caption_basic);
    RUN_TEST(test_caption_zero_duration);
    RUN_TEST(test_caption_one_second);
    RUN_TEST(test_caption_59_seconds);
    RUN_TEST(test_caption_one_minute);
    RUN_TEST(test_caption_one_hour);
    RUN_TEST(test_caption_max_18_min);
    RUN_TEST(test_caption_19_min_59_sec);
    RUN_TEST(test_caption_null_rec_id);
    RUN_TEST(test_caption_null_device_name);
    RUN_TEST(test_caption_null_both);
    RUN_TEST(test_caption_null_buffer);
    RUN_TEST(test_caption_zero_buf_size);
    RUN_TEST(test_caption_buffer_exact);
    RUN_TEST(test_caption_buffer_one_byte_too_small);
    RUN_TEST(test_caption_large_duration);
    RUN_TEST(test_caption_empty_device_name);
    RUN_TEST(test_caption_empty_rec_id);
    RUN_TEST(test_caption_no_trailing_newline);
    RUN_TEST(test_caption_subsecond_truncation);
    RUN_TEST(test_caption_exact_expected_format);
    RUN_TEST(test_caption_error_strings);
    RUN_TEST(test_caption_default_device_name);

    /* Task 17: upload drain flow state machine — see the "NOT currently
     * built" comment near the extern block above; disabled. */

    /* Task 5: config INI parser */
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_parse_empty);
    RUN_TEST(test_config_parse_null);
    RUN_TEST(test_config_parse_valid);
    RUN_TEST(test_config_multiple_wifi);
    RUN_TEST(test_config_malformed_skipped);
    RUN_TEST(test_config_unknown_section);
    RUN_TEST(test_config_comments);
    RUN_TEST(test_config_whitespace);
    RUN_TEST(test_config_bool_formats);
    RUN_TEST(test_config_roundtrip);
    RUN_TEST(test_config_serialize_buffer_too_small);
    RUN_TEST(test_config_null_safety);
    RUN_TEST(test_config_channels_with_gaps);
    RUN_TEST(test_config_load_null_path);
    RUN_TEST(test_config_load_nonexistent_file);
    RUN_TEST(test_config_save_null_safety);
    RUN_TEST(test_config_error_strings);

    /* Task 18: battery discharge curve and threshold logic */
    RUN_TEST(test_voltage_above_max_is_100);
    RUN_TEST(test_voltage_below_min_is_0);
    RUN_TEST(test_voltage_zero_is_0);
    RUN_TEST(test_voltage_negative_is_0);
    RUN_TEST(test_voltage_table_points_match);
    RUN_TEST(test_voltage_interpolation_mid);
    RUN_TEST(test_voltage_interpolation_just_above_low_end);
    RUN_TEST(test_voltage_interpolation_just_below_high_end);
    RUN_TEST(test_voltage_monotonic_descending);
    RUN_TEST(test_voltage_le_100);
    RUN_TEST(test_percent_to_threshold_normal);
    RUN_TEST(test_percent_to_threshold_warning);
    RUN_TEST(test_percent_to_threshold_critical);
    RUN_TEST(test_percent_to_threshold_unknown);
    RUN_TEST(test_block_upload_critical_large_file);
    RUN_TEST(test_block_upload_critical_small_file);
    RUN_TEST(test_block_upload_warning_allowed);
    RUN_TEST(test_block_upload_normal_allowed);
    RUN_TEST(test_block_upload_unknown_allowed);
    RUN_TEST(test_block_upload_critical_exact_boundary);
    RUN_TEST(test_unknown_constant_is_negative);
    RUN_TEST(test_low_upload_max_is_reasonable);
    RUN_TEST(test_full_pipeline_high_battery);
    RUN_TEST(test_full_pipeline_warning);
    RUN_TEST(test_full_pipeline_critical);

    UNITY_END();
}
