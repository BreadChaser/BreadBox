/* c5acf64 GBA core section page offsets: bitmap effects may grow,
 * unrelated functions retain their established instruction-cache sets. */
        . = . + ((0xf50 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text)
        . = . + ((0x700 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_profile_read)
        . = . + ((0x710 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rand_gen)
        . = . + ((0x740 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rand_seed)
        . = . + ((0x778 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_main)
        . = . + ((0x868 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_run_guard)
        . = . + ((0x888 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.update_gba)
        . = . + ((0x014 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.reset_gba)
        . = . + ((0x030 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.main_check_savestate)
        . = . + ((0x1e8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.main_read_savestate)
        . = . + ((0x43c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.main_write_savestate)
        . = . + ((0x7e8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.trigger_timer)
        . = . + ((0x954 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.evict_gamepak_page)
        . = . + ((0xa44 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rom_is_pokemon_family)
        . = . + ((0xb20 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_frequency_changed)
        . = . + ((0xc24 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_battery_generation)
        . = . + ((0xc34 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.reload_timing_info)
        . = . + ((0xd9c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_backup)
        . = . + ((0xeb8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_eeprom)
        . = . + ((0x09c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_eeprom)
        . = . + ((0x150 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_backup)
        . = . + ((0x49c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_solar_level)
        . = . + ((0x4b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_has_solar_sensor)
        . = . + ((0x4c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_get_rtc)
        . = . + ((0x508 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_rtc)
        . = . + ((0x54c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rtc_set_time_source)
        . = . + ((0x568 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.update_gpio_romregs)
        . = . + ((0x5e8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_rumble)
        . = . + ((0x64c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rumble_frame_reset)
        . = . + ((0x680 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rumble_active_pct)
        . = . + ((0x6c8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_gpio)
        . = . + ((0x1b8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.load_gamepak_page)
        . = . + ((0x3b8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_memory16)
        . = . + ((0x658 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_io_register16)
        . = . + ((0x9c8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_memory8)
        . = . + ((0xbcc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_io_register8)
        . = . + ((0xd08 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_memory16_signed)
        . = . + ((0xf2c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_memory32)
        . = . + ((0x1ac - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_io_register32)
        . = . + ((0x318 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.dma_tf_loop32)
        . = . + ((0x0a8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.dma_transfer)
        . = . + ((0x4e8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.trigger_dma)
        . = . + ((0x714 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_io_register16)
        . = . + ((0x204 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_io_register8)
        . = . + ((0x284 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_io_register32)
        . = . + ((0x304 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_memory16)
        . = . + ((0x5dc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.dma_tf_loop16)
        . = . + ((0xbe4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_memory8)
        . = . + ((0xe7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_memory32)
        . = . + ((0x24c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_memory16s)
        . = . + ((0x454 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.read_memory8s)
        . = . + ((0x64c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_rom_budget)
        . = . + ((0x664 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_gamepak_buffer)
        . = . + ((0x71c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gamepak_must_swap)
        . = . + ((0x760 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_memory)
        . = . + ((0xabc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.memory_term)
        . = . + ((0xb4c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.memory_check_savestate)
        . = . + ((0xd38 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.memory_read_savestate)
        . = . + ((0x370 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.memory_write_savestate)
        . = . + ((0xf4c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.load_gamepak)
        . = . + ((0xfec - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.load_bios)
        . = . + ((0x030 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.bson_contains_key)
        . = . + ((0x120 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.bson_find_key)
        . = . + ((0x204 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.bson_read_int32)
        . = . + ((0x240 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.bson_read_int32_array)
        . = . + ((0x2c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.bson_read_bytes)
        . = . + ((0x314 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_load_state)
        . = . + ((0x4dc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_save_state)
        . = . + ((0x5f0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_input_state)
        . = . + ((0x600 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.update_input)
        . = . + ((0x988 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.input_check_savestate)
        . = . + ((0x9b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.input_read_savestate)
        . = . + ((0xab8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.input_write_savestate)
        . = . + ((0xbf0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_timer_queue32)
        . = . + ((0xc48 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_timer)
        . = . + ((0xe7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_reset_fifo)
        . = . + ((0xe98 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.render_gbc_sound)
        . = . + ((0x6a0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.reset_sound)
        . = . + ((0x7a4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_sound)
        . = . + ((0x858 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_check_savestate)
        . = . + ((0x984 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_read_savestate)
        . = . + ((0xf10 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_write_savestate)
        . = . + ((0x900 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_read_samples)
        . = . + ((0x9b0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.sound_flush_ring)
        . = . + ((0x9f4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.process_cheats)
        . = . + ((0xdc4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serial_get_irq_cycles)
        . = . + ((0xdd4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serial_set_irq_cycles)
        . = . + ((0xde4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_rcnt)
        . = . + ((0xec0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.write_siocnt)
        . = . + ((0x144 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serial_next_event)
        . = . + ((0x15c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.update_serial)
        . = . + ((0x244 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gbp_get_state)
        . = . + ((0x27c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gbp_set_state)
        . = . + ((0x2bc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gbp_reset)
        . = . + ((0x2f8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gbp_transfer)
        . = . + ((0x3f8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rfu_process_command)
        . = . + ((0x2e0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rfu_reset)
        . = . + ((0x388 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rfu_transfer)
        . = . + ((0x670 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rfu_frame_update)
        . = . + ((0x89c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rfu_net_receive)
        . = . + ((0xde0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.rfu_update)
        . = . + ((0x080 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.process_awpeer_val)
        . = . + ((0x158 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialaw_senddata)
        . = . + ((0x1fc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialproto_reset)
        . = . + ((0x214 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialpoke_master_send)
        . = . + ((0x6d0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialpoke_frame_update)
        . = . + ((0x878 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialpoke_update)
        . = . + ((0x054 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialpoke_net_receive)
        . = . + ((0x104 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialaw_master_send)
        . = . + ((0x730 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialaw_update)
        . = . + ((0x1c0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.serialaw_net_receive)
        . = . + ((0x394 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.audio_buff_status_cb)
        . = . + ((0x3c0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.video_post_process_cc)
        . = . + ((0x428 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.video_post_process_mix)
        . = . + ((0x4a4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.video_post_process_cc_mix)
        . = . + ((0x540 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_start)
        . = . + ((0x57c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_stop)
        . = . + ((0x59c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_connected)
        . = . + ((0x5fc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_disconnected)
        . = . + ((0x614 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_perf_dummy_get_time_usec)
        . = . + ((0x620 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_perf_dummy_get_counter)
        . = . + ((0x62c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_perf_dummy_log)
        . = . + ((0x630 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_perf_dummy_counter)
        . = . + ((0x634 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_receive)
        . = . + ((0x66c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.check_variables)
        . = . + ((0x204 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.show_warning_message.constprop.0)
        . = . + ((0x2a8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_perf_dummy_get_cpu_features)
        . = . + ((0x2b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.set_fastforward_override)
        . = . + ((0x30c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_poll_receive)
        . = . + ((0x324 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.netpacket_send)
        . = . + ((0x350 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_get_system_av_info)
        . = . + ((0x394 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.ctr_jit_available)
        . = . + ((0x3ac - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_init)
        . = . + ((0x628 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_deinit)
        . = . + ((0x760 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_environment)
        . = . + ((0xd5c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_video_refresh)
        . = . + ((0xd6c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_audio_sample)
        . = . + ((0xd70 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_audio_sample_batch)
        . = . + ((0xd80 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_set_input_poll)
        . = . + ((0xd90 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_reset)
        . = . + ((0xd94 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_serialize_size)
        . = . + ((0xd9c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_serialize)
        . = . + ((0xdd0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_unserialize)
        . = . + ((0xde4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_load_game)
        . = . + ((0x3b0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_unload_game)
        . = . + ((0x47c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_get_memory_data)
        . = . + ((0x4a8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_get_memory_size)
        . = . + ((0x4c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_run)
        . = . + ((0x8b8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.strlcpy_retro__)
        . = . + ((0x930 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.filestream_vfs_init)
        . = . + ((0xa3c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.filestream_get_size)
        . = . + ((0xa7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.filestream_open)
        . = . + ((0xadc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.filestream_seek)
        . = . + ((0xb34 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.filestream_read)
        . = . + ((0xb74 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.filestream_close)
        . = . + ((0xbdc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_vfs_file_open_impl)
        . = . + ((0xe08 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_vfs_file_close_impl)
        . = . + ((0xe7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_vfs_file_size_impl)
        . = . + ((0xe90 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_vfs_file_seek_impl)
        . = . + ((0xef4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.retro_vfs_file_read_impl)
        . = . + ((0xf50 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.trace_instruction)
        . = . + ((0xf74 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.arm_disect_imm_32bit.part.0)
        . = . + ((0x03c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.arm_prepare_load_reg_pc.part.0)
        . = . + ((0x140 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.arm_block_ram)
        . = . + ((0x5bc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.execute_spsr_restore_body)
        . = . + ((0x650 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.execute_store_cpsr_body)
        . = . + ((0x708 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.execute_arm_translate)
        . = . + ((0x714 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.term_dynarec_caches)
        . = . + ((0x758 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.flush_translation_cache_ram)
        . = . + ((0x8bc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.translate_block_arm)
        . = . + ((0xa04 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.translate_block_thumb)
        . = . + ((0xbec - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.block_lookup_translate_thumb)
        . = . + ((0xe50 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.process_deferred_exits)
        . = . + ((0xef8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.block_lookup_translate_arm)
        . = . + ((0x164 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_emitter)
        . = . + ((0x530 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.block_lookup_address_arm)
        . = . + ((0xaec - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.block_lookup_address_thumb)
        . = . + ((0x208 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_dynarec_caches)
        . = . + ((0x2e4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.flush_dynarec_caches)
        . = . + ((0x380 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj3EtLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x440 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj3EtLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0x508 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj3EtLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x5e4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj3EtLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x6c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj3EtLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x7ac - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj3EtLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x8ac - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj4EhLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x988 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj4EhLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xa58 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj4EhLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0xb48 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj4EhLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0xc7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj4EhLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0xd74 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj4EhLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0xe98 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj5EtLj160ELj128EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0xf64 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj5EtLj160ELj128EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0x038 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj5EtLj160ELj128EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x120 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj5EtLj160ELj128EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x20c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj5EtLj160ELj128EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x300 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EjLj5EtLj160ELj128EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x41c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineItL8rendtype0ELb1EEvjjjPvPKt)
        . = . + ((0x4c0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineItL8rendtype0ELb0EEvjjjPvPKt)
        . = . + ((0x564 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineItL8rendtype1ELb1EEvjjjPvPKt)
        . = . + ((0x608 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineItL8rendtype1ELb0EEvjjjPvPKt)
        . = . + ((0x6ac - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineItL8rendtype2ELb1EEvjjjPvPKt)
        . = . + ((0x750 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineItL8rendtype2ELb0EEvjjjPvPKt)
        . = . + ((0x7f4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineIjL8rendtype2ELb1EEvjjjPvPKt)
        . = . + ((0x898 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineIjL8rendtype2ELb0EEvjjjPvPKt)
        . = . + ((0x93c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineIjL8rendtype3ELb1EEvjjjPvPKt)
        . = . + ((0x9e0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_affineIjL8rendtype3ELb0EEvjjjPvPKt)
        . = . + ((0xa84 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj3EtLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xb44 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj3EtLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0xc18 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj3EtLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0xcf0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj3EtLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0xdd0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj3EtLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0xec8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj4EhLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0xf94 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj4EhLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0x058 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj4EhLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x138 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj4EhLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x264 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj4EhLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x350 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj4EhLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x460 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj5EtLj160ELj128EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0x52c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj5EtLj160ELj128EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x60c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj5EtLj160ELj128EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x6f0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj5EtLj160ELj128EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x7dc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj5EtLj160ELj128EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x8f0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj4EhLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x9cc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj4EhLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xa9c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj4EhLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0xb88 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj4EhLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0xcbc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj4EhLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0xdb4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj4EhLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0xed0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x024 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x198 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x308 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x4b0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x57c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x668 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x738 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb1ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x83c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x920 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xa2c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xb40 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xc84 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xd3c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xe10 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xec8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype1ELb0ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xfb0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x124 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x2d0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x470 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x648 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x738 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x848 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x94c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb1ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xa80 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xb78 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xc9c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xdcc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xf30 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xff8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x0e4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x1b0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype2ELb0ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x2b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x430 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x5e0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x784 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x960 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xa50 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xb60 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xc64 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb1ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xd98 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xe90 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xfb4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x0e4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x248 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x310 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x3fc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x4c8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype2ELb0ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x5cc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x750 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x908 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xab4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xc98 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xd90 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xeb0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xfbc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb1ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x0f8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z18stereo_render_baseIjEvPFvjjjPvPKtEjjjPT_j)
        . = . + ((0x348 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z18stereo_render_baseItEvPFvjjjPvPKtEjjjPT_j)
        . = . + ((0x598 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj5EtLj160ELj128EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x6f8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype1EtLj3EtLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x84c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj4EhLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x930 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xb44 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xc48 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype1ELb1ELb0EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0x03c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype1ELb1ELb1EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0x454 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype4ELb1ELb0EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0x804 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype4ELb1ELb1EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0xbd8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype2ELb1ELb0EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0xf7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype2ELb1ELb1EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0x350 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype2ELb1ELb0EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0x6f4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype2ELb1ELb1EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0xabc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype2ELb0ELb0EEvijPT_jjtPKt.isra.0)
        . = . + ((0xe40 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype2ELb0ELb1EEvijPT_jjtPKt.isra.0)
        . = . + ((0x228 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype2ELb0ELb0EEvijPT_jjtPKt.isra.0)
        . = . + ((0x5a4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype2ELb0ELb1EEvijPT_jjtPKt.isra.0)
        . = . + ((0x98c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype1ELb0ELb0EEvijPT_jjtPKt.isra.0)
        . = . + ((0xdb0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype1ELb0ELb1EEvijPT_jjtPKt.isra.0)
        . = . + ((0x240 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype4ELb0ELb0EEvijPT_jjtPKt.isra.0)
        . = . + ((0x5c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectItL8rendtype4ELb0ELb1EEvijPT_jjtPKt.isra.0)
        . = . + ((0x9b8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_objsIjL8rendtype2EEvjjjPvPKt.isra.0)
        . = . + ((0x104 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_objsItL8rendtype2EEvjjjPvPKt.isra.0)
        . = . + ((0xafc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20bitmap_render_layersIL8rendtype2ELS0_2EtEvjjPT1_j)
        . = . + ((0xd4c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj4EhLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0xe70 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj4EhLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0xf70 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj4EhLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x0ac - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj4EhLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x1a0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj4EhLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0x278 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x388 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x460 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x558 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x62c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x79c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x8d8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundIjL8rendtype3ELb0ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xa04 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xacc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xb98 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb1ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xc80 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb1ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xd7c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xe78 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xf78 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x0a8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb1ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x1b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb1ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x2fc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x45c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb0ELb1ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x64c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb0ELb1ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0x87c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype3ELb1ELb0EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0xcbc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0xdb4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb0ELb0ELb0ELb1EEvjjjPKhjS2_PvPKt)
        . = . + ((0xed4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype3ELb1ELb1EEvijPT_jjtPKt.constprop.0.isra.0)
        . = . + ((0x304 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z24render_affine_backgroundItL8rendtype0ELb1ELb0ELb0ELb0EEvjjjPKhjS2_PvPKt)
        . = . + ((0x4c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textIjL8rendtype2ELb0EEvjjjPvPKt)
        . = . + ((0x768 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textItL8rendtype2ELb0EEvjjjPvPKt)
        . = . + ((0xa24 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textIjL8rendtype2ELb1EEvjjjPvPKt)
        . = . + ((0x0d4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textItL8rendtype2ELb1EEvjjjPvPKt)
        . = . + ((0x760 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype3ELb0ELb0EEvijPT_jjtPKt.isra.0)
        . = . + ((0xb84 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textItL8rendtype1ELb0EEvjjjPvPKt)
        . = . + ((0x128 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textIjL8rendtype3ELb1EEvjjjPvPKt)
        . = . + ((0x9f4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textIjL8rendtype3ELb0EEvjjjPvPKt)
        . = . + ((0x0a8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textItL8rendtype1ELb1EEvjjjPvPKt)
        . = . + ((0x824 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z13render_objectIjL8rendtype3ELb0ELb1EEvijPT_jjtPKt.isra.0)
        . = . + ((0xc50 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_objsIjL8rendtype3EEvjjjPvPKt.isra.0)
        . = . + ((0x884 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20bitmap_render_layersIL8rendtype3ELS0_3EjEvjjPT1_j)
        . = . + ((0xd98 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20bitmap_render_layersIL8rendtype2ELS0_3EjEvjjPT1_j)
        . = . + ((0x04c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textItL8rendtype0ELb0EEvjjjPvPKt)
        . = . + ((0x9a4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj3EtLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0xa64 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj3EtLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xb24 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj3EtLj240ELj160EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xbec - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj5EtLj160ELj128EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0xcb8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj5EtLj160ELj128EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xd84 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj5EtLj160ELj128EL11bm_rendmode1ELb0EEvjjPvPKt)
        . = . + ((0xe58 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj3EtLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0xf30 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj3EtLj240ELj160EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x010 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj3EtLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x0e4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj3EtLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x1c4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj3EtLj240ELj160EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x2a0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj3EtLj240ELj160EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x388 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj5EtLj160ELj128EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x46c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj5EtLj160ELj128EL11bm_rendmode0ELb1EEvjjPvPKt)
        . = . + ((0x558 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj5EtLj160ELj128EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x638 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj5EtLj160ELj128EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x724 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj5EtLj160ELj128EL11bm_rendmode2ELb0EEvjjPvPKt)
        . = . + ((0x80c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj5EtLj160ELj128EL11bm_rendmode1ELb1EEvjjPvPKt)
        . = . + ((0x900 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj3EtLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x9f8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj3EtLj240ELj160EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0xaf8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_textItL8rendtype0ELb1EEvjjjPvPKt)
        . = . + ((0x5f4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj5EtLj160ELj128EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x708 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype3EjLj5EtLj160ELj128EL11bm_rendmode2ELb1EEvjjPvPKt)
        . = . + ((0x824 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj3EtLj240ELj160EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0x978 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z22render_scanline_bitmapIL8rendtype2EtLj5EtLj160ELj128EL11bm_rendmode0ELb0EEvjjPvPKt)
        . = . + ((0xad8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._ZL16render_w_effectsjjPtjPK19layer_render_struct)
        . = . + ((0xf44 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_cpu_config)
        . = . + ((0xf84 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_stereo_admit)
        . = . + ((0xf9c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_stereo_config)
        . = . + ((0x0a4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_stereo_flags)
        . = . + ((0x0b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_stereo_valid)
        . = . + ((0x108 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.video_reload_counters)
        . = . + ((0x85c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20bitmap_render_layersIL8rendtype1ELS0_1EtEvjjPT1_j)
        . = . + ((0xac0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._ZL27render_scanline_conditionaljjPtj)
        . = . + ((0xc58 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_window_n_passIXadL_ZL21render_windowout_passPtjjEELj1EEvS0_jj)
        . = . + ((0xe04 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_scanline_objsItL8rendtype4EEvjjjPvPKt.isra.0)
        . = . + ((0x3c0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20render_window_n_passIXadL_ZL21render_windowobj_passPtjjEELj1EEvS0_jj)
        . = . + ((0x5b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._ZL21render_windowobj_passPtjj)
        . = . + ((0x770 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._ZL22render_scanline_windowPt)
        . = . + ((0xf2c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.update_scanline)
        . = . + ((0xa14 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.io_read_requires_handler)
        . = . + ((0xba8 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.set_cpu_mode.constprop.0)
        . = . + ((0xc24 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._Z20exec_thumb_block_memIL7AccMode0EL8AddrMode2EEhjjRi.isra.0)
        . = . + ((0xfc4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.set_cpu_mode)
        . = . + ((0x098 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text._ZL20execute_arm_internaljb)
        . = . + ((0x534 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.check_interrupt)
        . = . + ((0x580 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.check_and_raise_interrupts)
        . = . + ((0x678 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.flag_interrupt)
        . = . + ((0x6cc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.execute_arm)
        . = . + ((0x6d4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.execute_arm_vram)
        . = . + ((0x794 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.init_cpu)
        . = . + ((0x890 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.cpu_check_savestate)
        . = . + ((0x91c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.cpu_read_savestate)
        . = . + ((0x9bc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.cpu_write_savestate)
        . = . + ((0xc3c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.ctr_flush_invalidate_cache)
        . = . + ((0xc40 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.unsupported)
        . = . + ((0xf14 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.tile)
        . = . + ((0x2fc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_reset)
        . = . + ((0x388 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_config)
        . = . + ((0x44c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_frame)
        . = . + ((0x46c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_updates)
        . = . + ((0x498 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_stats)
        . = . + ((0x4b0 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_damage)
        . = . + ((0xc54 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_row)
        . = . + ((0xd80 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_index_raster)
        . = . + ((0x238 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_journal_vram_range)
        . = . + ((0x2b4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_journal_reset)
        . = . + ((0x4cc - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_journal_config)
        . = . + ((0x53c - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_journal_read)
        . = . + ((0x5e4 - .) & 0xfff);
        *gba_core_blob.o(.sys.gba.text.gba_journal_row)
        *gba_core_blob.o(.sys.gba.text .sys.gba.text.*)
