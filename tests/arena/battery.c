/* Production gpSP battery writes, without a ROM or a copied implementation. */
#include <assert.h>
#include <stdio.h>
#include "common.h"
extern u32 gba_battery_generation(void);
extern u32 flash_mode,flash_command_position,flash_bank_num,eeprom_mode,eeprom_address,eeprom_counter;
int main(void)
{
 u32 before=gba_battery_generation();
 backup_type=BACKUP_SRAM;write_backup(4,0x73);
 assert(gamepak_backup[4]==0x73 && gba_battery_generation()!=before);
 before=gba_battery_generation();
 backup_type=BACKUP_FLASH;flash_mode=FLASH_WRITE_MODE;flash_command_position=0;flash_bank_num=1;
 write_backup(8,0x27);assert(gamepak_backup[65536+8]==0x27 && gba_battery_generation()!=before);
 before=gba_battery_generation();flash_mode=FLASH_ERASE_MODE;flash_command_position=2;
 write_backup(8,0x30);assert(gamepak_backup[65536+8]==0xff && gba_battery_generation()!=before);
 before=gba_battery_generation();eeprom_mode=EEPROM_WRITE_MODE;eeprom_address=0;eeprom_counter=0;
 write_eeprom(0,1);assert(gamepak_backup[0]&0x80);assert(gba_battery_generation()!=before);
 before=gba_battery_generation();
 flash_command_position=0;flash_mode=FLASH_BASE_MODE;backup_type=BACKUP_FLASH;
 write_backup(0x5555,0xaa);write_backup(0x2aaa,0x55);write_backup(0x5555,0x90);
 assert(gba_battery_generation()==before);
 eeprom_mode=EEPROM_BASE_MODE;eeprom_address=eeprom_counter=0;
 write_eeprom(0,1);write_eeprom(0,1);assert(gba_battery_generation()==before);
 puts("Production SRAM/flash/EEPROM writes advance battery generation: PASS");
}
