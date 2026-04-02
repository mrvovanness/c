0000000000000000 <ltmp0>:
       0: a9bf7bfd     	stp	x29, x30, [sp, #-0x10]!
       4: 910003fd     	mov	x29, sp
       8: 90000000     	adrp	x0, 0x0 <ltmp0>
       c: 91000000     	add	x0, x0, #0x0
      10: 94000000     	bl	0x10 <ltmp0+0x10>
      14: a8c17bfd     	ldp	x29, x30, [sp], #0x10
      18: d65f03c0     	ret

000000000000001c <_main>:
      1c: d10083ff     	sub	sp, sp, #0x20
      20: a9017bfd     	stp	x29, x30, [sp, #0x10]
      24: 910043fd     	add	x29, sp, #0x10
      28: 52800008     	mov	w8, #0x0                ; =0
      2c: b9000be8     	str	w8, [sp, #0x8]
      30: b81fc3bf     	stur	wzr, [x29, #-0x4]
      34: 94000000     	bl	0x34 <_main+0x18>
      38: 94000000     	bl	0x38 <_main+0x1c>
      3c: b9400be0     	ldr	w0, [sp, #0x8]
      40: a9417bfd     	ldp	x29, x30, [sp, #0x10]
      44: 910083ff     	add	sp, sp, #0x20
      48: d65f03c0     	ret
