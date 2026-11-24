@ The Forge plugin's start (as forge-rs's forge-rt, MIT): the NRO header's room and the MOD0 header the loader reads.
    .section ".text.crt0","ax"
    .global plugin_start
plugin_start:
    .word 0
    .word plugin_mod0 - plugin_start
    @ bytes 0x08-0x7F are overwritten with the NRO header (tools/elf_to_nro.py): code starts at 0x80
    .space 0x78

    .section ".rodata.mod0","a"
    .global plugin_mod0
    .hidden plugin_mod0
plugin_mod0:
    .ascii "MOD0"
    .word __dynamic_start__ - plugin_mod0
    .word __bss_start__ - plugin_mod0
    .word __bss_end__ - plugin_mod0
    .word 0
    .word 0
    .word plugin_module_runtime - plugin_mod0

    .bss
    .global plugin_module_runtime
    .hidden plugin_module_runtime
    .balign 8
plugin_module_runtime:
    .space 0xD0
