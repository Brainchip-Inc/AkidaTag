SECTION_PROLOGUE(.sram_upload_buf (NOLOAD), , SUBALIGN(4))
{
    __sram_upload_buf_start = .;
    KEEP(*(.sram_upload_buf))
    __sram_upload_buf_end = .;
} GROUP_LINK_IN(RAM)
