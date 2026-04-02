global trigger_gp_fault
trigger_gp_fault:
    mov ax, 0x9999   ; An index likely beyond your GDT limit
    mov ds, ax       ; Triggers #GP(selector index)