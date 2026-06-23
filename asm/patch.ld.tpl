/* 1. Define the entry point of the application */
ENTRY(Reset_Handler)

/* 2. Define the memory map of the target device */
MEMORY
{
    RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 192K
    CCMRAM (xrw)      : ORIGIN = 0x10000000, LENGTH = 64K
    FLASH (rx)      : ORIGIN = 0x8020000, LENGTH = 1024K
}

/* 3. Define output sections and how input sections map to them */
SECTIONS
{
    /* Code and read-only data go into FLASH */
    .text :
    {
        start_text = ABSOLUTE(.);

${INSERTS}


        /* Set new functions with offset for asm code */
        . = ${OEM_FW_END} - start_text;
        . = ALIGN(4);

${WRAPPERS}

${NEW_FN}

    } > FLASH

}
