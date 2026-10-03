	@ VER_64BIT is passed with --defsym on native builds. GBA builds leave it
	@ undefined, which .if reads as 0, so every macro here reduces to the
	@ 32-bit form there.

	.if VER_64BIT == 1
		.set PTR_SIZE, 8
	.else
		.set PTR_SIZE, 4
	.endif

	.macro space64 x:req
		.if VER_64BIT == 1
			.space \x
		.endif
	.endm

	.macro ptrvalue x:vararg
		.if VER_64BIT == 1
			.quad \x
		.else
			.int \x
		.endif
	.endm

	@ Aligns data read as pointers or as structs that contain pointers.
	@ 4 bytes (.align 2 on ARM) on 32-bit targets, 8 on 64-bit.
	@ .balign counts bytes on every host, unlike .align.
	.macro ptr_align
		.if VER_64BIT == 1
			.balign 8
		.else
			.balign 4
		.endif
	.endm
