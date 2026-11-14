/* Linker script to configure memory regions.
 * Copy of src/platform/nrf52/nrf52840_s140_v7.ld for the Bitchat bridge builds. Only the RAM origin
 * differs: the SoftDevice gets 48 KB instead of 24 KB, because the bridge enables a second
 * peripheral link, a deeper write queue and a larger attribute table. Unused SoftDevice RAM is only
 * wasted; too little would stop Bluetooth from starting.
 */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)     : ORIGIN = 0x27000, LENGTH = 0xED000 - 0x27000

  /* SRAM required by Softdevice depend on
   * - Attribute Table Size (Number of Services and Characteristics)
   * - Vendor UUID count
   * - Max ATT MTU
   * - Concurrent connection peripheral + central + secure links
   * - Event Len, HVN queue, Write CMD queue
   */ 
  RAM (rwx) :  ORIGIN = 0x2000C000, LENGTH = 0x20040000 - 0x2000C000
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM
  
  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf52_common.ld"
