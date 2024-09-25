	.section script_data, "aw"

	.align 2
gMysteryEventScriptCmdTable::
	.quad MEScrCmd_nop                 /* 0x00*/
	.quad MEScrCmd_checkcompat         /* 0x01*/
	.quad MEScrCmd_end                 /* 0x02*/
	.quad MEScrCmd_setmsg              /* 0x03*/
	.quad MEScrCmd_setstatus           /* 0x04*/
	.quad MEScrCmd_runscript           /* 0x05*/
	.quad MEScrCmd_initramscript       /* 0x06*/
	.quad MEScrCmd_setenigmaberry      /* 0x07*/
	.quad MEScrCmd_giveribbon          /* 0x08*/
	.quad MEScrCmd_givenationaldex     /* 0x09*/
	.quad MEScrCmd_addrareword         /* 0x0a*/
	.quad MEScrCmd_setrecordmixinggift /* 0x0b*/
	.quad MEScrCmd_givepokemon         /* 0x0c*/
	.quad MEScrCmd_addtrainer          /* 0x0d*/
	.quad MEScrCmd_enableresetrtc      /* 0x0e*/
	.quad MEScrCmd_checksum            /* 0x0f*/
	.quad MEScrCmd_crc                 /* 0x10*/
gMysteryEventScriptCmdTableEnd::
