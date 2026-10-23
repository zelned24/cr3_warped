/* Generated stub: embeds game_data/levels.bin into the executable. */
	.section .rodata.levels_bin, "a"
	.balign 4
	.global levels_bin
levels_bin:
	.incbin "game_data/levels.bin"
	.global levels_bin_end
levels_bin_end:
