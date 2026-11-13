/* Save-service extraction: retain every surviving adapter entry's page offset.
 * Removed persistence/menu functions now live in the shell. Core/compositor and
 * shared-host sections retain the established block offsets. */
        __bb_gba_host_frame_start = .;
        . = . + ((0x02c - .) & 0xfff);
        gba.o(.text.gba_cost_median)
        . = . + ((0x0e4 - .) & 0xfff);
        gba.o(.text.environment)
        . = . + ((0x45c - .) & 0xfff);
        gba.o(.text.frame_guard)
        . = . + ((0x758 - .) & 0xfff);
        gba.o(.text.draw_prepare)
        . = . + ((0xb34 - .) & 0xfff);
        gba.o(.text.prepare_progress)
        . = . + ((0xba4 - .) & 0xfff);
        gba.o(.text.draw_top.part.0)
        . = . + ((0xd38 - .) & 0xfff);
        gba.o(.text.draw_settings_help)
        . = . + ((0xe8c - .) & 0xfff);
        gba.o(.text.draw_gba_settings)
        . = . + ((0x124 - .) & 0xfff);
        gba.o(.text.video_callback)
        . = . + ((0xc90 - .) & 0xfff);
        gba.o(.text.apt_event)
        . = . + ((0x1a0 - .) & 0xfff);
        gba.o(.text.userAppExit)
        . = . + ((0x1a4 - .) & 0xfff);
        gba.o(.text.gba_error)
        . = . + ((0x1c8 - .) & 0xfff);
        gba.o(.text.gba_debug_key)
        . = . + ((0x264 - .) & 0xfff);
        gba.o(.text.gba_debug_state)
        . = . + ((0x344 - .) & 0xfff);
        gba.o(.text.gba_continue_available)
        . = . + ((0x5c0 - .) & 0xfff);
        gba.o(.text.gba_run)
        . = . + ((0xaf4 - .) & 0xfff);
        gba.o(.text.gba_session_state_size)
        . = . + ((0xb44 - .) & 0xfff);
        gba.o(.text.gba_session_state_save)
        . = . + ((0xbe8 - .) & 0xfff);
        gba.o(.text.gba_session_state_load)
        . = . + ((0xd58 - .) & 0xfff);
        gba.o(.text.gba_session_battery)
        . = . + ((0xe1c - .) & 0xfff);
        gba.o(.text.gba_session_generation)
        . = . + ((0xe50 - .) & 0xfff);
        gba.o(.text.gba_switch)
        . = . + ((0xf10 - .) & 0xfff);
        gba.o(.text.gba_autostart)
        . = . + ((0xf2c - .) & 0xfff);
        gba.o(.text.host_rtc_size)
        . = . + ((0xf34 - .) & 0xfff);
        gba.o(.text.host_log)
        . = . + ((0xf44 - .) & 0xfff);
        gba.o(.text.host_ticks)
        . = . + ((0xf48 - .) & 0xfff);
        gba.o(.text.audio_sink)
        . = . + ((0xf60 - .) & 0xfff);
        gba.o(.text.host_generation)
        . = . + ((0xf94 - .) & 0xfff);
        gba.o(.text.host_rtc_set)
        . = . + ((0x004 - .) & 0xfff);
        gba.o(.text.host_rtc_get)
        /* Mono VBlank latch wait: uses padding before draw_bottom. */
        gba.o(.text.wait_top_latch)
        gba.o(.text.gba_track_alternation)
        . = . + ((0xa24 - .) & 0xfff);
        gba.o(.text.draw_bottom)
        . = . + ((0x234 - .) & 0xfff);
        gba.o(.text.draw_top)
        gba.o(.text .text.*)
        . = . + ((0x0b0 - .) & 0xfff);
        index_gpu.o(.text .text.*)
        . = . + ((0xe90 - .) & 0xfff);
        __bb_gba_core_frame_start = .;
        /* Preserve surviving core cache sets when a hot function is slimmed. */
        sys_gba.o(.sys.gba.text .sys.gba.text.*)
        INCLUDE ../../../systems/gba/core-text.ld
        /* Keep unchanged shared callbacks in their established cache sets. */
        . = . + ((0x4d4 - .) & 0xfff);
        retro_host.o(.text.rh_video)
        . = . + ((0x5e8 - .) & 0xfff);
        retro_host.o(.text.rh_audio_batch)
        . = . + ((0x6b4 - .) & 0xfff);
        retro_host.o(.text.rh_input_poll)
        . = . + ((0x6b8 - .) & 0xfff);
        retro_host.o(.text.rh_input_state)
        . = . + ((0x748 - .) & 0xfff);
        retro_host.o(.text.rh_environment)
        . = . + ((0x93c - .) & 0xfff);
        retro_host.o(.text.rh_core_log)
        . = . + ((0x9a0 - .) & 0xfff);
        retro_host.o(.text.rh_audio_sample)
        . = . + ((0xa70 - .) & 0xfff);
        retro_host.o(.text.bb_retro_rebase)
        . = . + ((0xb5c - .) & 0xfff);
        retro_host.o(.text.bb_retro_bind)
        . = . + ((0xcec - .) & 0xfff);
        retro_host.o(.text.bb_retro_unbind)
        . = . + ((0xd40 - .) & 0xfff);
        retro_host.o(.text.bb_retro_run_frame)
        . = . + ((0xdac - .) & 0xfff);
        retro_host.o(.text.bb_retro_battery)
        . = . + ((0xe60 - .) & 0xfff);
        retro_host.o(.text.bb_retro_state_save)
        . = . + ((0xf28 - .) & 0xfff);
        retro_host.o(.text.bb_retro_state_load)
        retro_host.o(.text .text.*)
        . = . + ((0x0b8 - .) & 0xfff);
        retro_bindings_gba.o(.text.activate)
        . = . + ((0x134 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_unload_game)
        . = . + ((0x160 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_reset)
        . = . + ((0x18c - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_run)
        . = . + ((0x1b8 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_deinit)
        . = . + ((0x1e4 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_init)
        . = . + ((0x210 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_serialize_size)
        . = . + ((0x244 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_get_system_av_info)
        . = . + ((0x278 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_set_input_state)
        . = . + ((0x2ac - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_set_input_poll)
        . = . + ((0x2e0 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_set_audio_sample_batch)
        . = . + ((0x314 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_set_audio_sample)
        . = . + ((0x348 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_set_video_refresh)
        . = . + ((0x37c - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_set_environment)
        . = . + ((0x3b0 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_get_memory_size)
        . = . + ((0x3ec - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_get_memory_data)
        . = . + ((0x428 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_load_game)
        . = . + ((0x464 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_unserialize)
        . = . + ((0x4a8 - .) & 0xfff);
        retro_bindings_gba.o(.text.guarded_serialize)
        . = . + ((0x4ec - .) & 0xfff);
        retro_bindings_gba.o(.text.deactivate)
        retro_bindings_gba.o(.text .text.*)
        __bb_gba_frame_end = .;
        . = . + ((0xdcc - .) & 0xfff);
