	.section .rodata
	.balign 16
	.globl fsca_table_data
fsca_table_data:
	.incbin "misc/fsca-table.bin"
	.globl fsca_table_data_end
fsca_table_data_end:
