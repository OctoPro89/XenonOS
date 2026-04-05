global trigger_gp_fault
trigger_gp_fault:
    mov ax, 0x9999   ; an index likely beyond GDT limit
    mov ds, ax       ; triggers #GP(selector index)