/* 
 * File:   everdrive.h
 * Author: igor
 *
 * Created on March 28, 2019, 5:57 PM
 */

#ifndef EVERDRIVE_H
#define	EVERDRIVE_H

//---------------------------------------------------------------------------------------------------------------------
/*----------------------------------------------------------------
 * cpu memory map:
 * 0x40FF 1B    mapper status register
 * 0x4100 256B  control registers
 * 0x4200 512B  save state data
 * ^^^^^^
 * 0x4200 128B  game mapper registers state
 * 0x4280 32B   apu registers mirror
 * 0x42A0 32B   ppu palette mirror
 * 0x42C0 4B    ppu registers mirror (ctrl, mask, scrl_hi, scrl_lo)
 * 0x4300 256   OAM memory
 * 
 * 0x5000 4K    OS RAM. first 16 bytes reserved for save state stuff
 * 0x6000 8K    app module. switchable
 * 0x8000 32K   main system ROM
 * ----------------------------------------------------------------
 * system memory map: (128K)
 * 0:0x00000 4K   OS RAM
 * 0:0x01000 4K   reserved
 * 1:0x02000 16K  save state buffer
 * 3:0x06000 8K   reserved
 * 4:0x08000 64K  app memory
 * C:0x18000 32K  main  system ROM
 * ---------------------------------------------------------------- SST structure
 * 1:0x0000 2K  WRAM
 * 1:0x0800 4K  VRAM
 * 1:0x1800 128 mapper regs. 2K begins from this address is various data from fpga. 
 * 1:0x1880 32  apu regs
 * 1:0x18A0 32  ppu pal
 * 1:0x18C0 04  ppu regs. (ctrl, mask, scrl_hi, scrl_lo)
 * 1:0x19C8 04  cpu regs: a, x, y, sp
 * 1:0x18CF 01  'S' constant. used for save state data detection
 * 1:0x18FF 01  SS_HIT. Shows who call in-game menu.
 * 1:0x1900 256 OAM memory
 * 1:0x1A00 512 mapper memory. 
 * 1:0x1C00 1K  mapper memory. 
 * 2:0x2000 8K  CHR
 * X:0x4000 32K EXRAM or FDS RAM
 */
//****************************************************************************** edio
#define PAL_B1          0x00   //black bgr, gray txt
#define PAL_B2          0x01   //black bgr, white txt
#define PAL_B3          0x02   //black bgr, yellow txt
#define PAL_BG          0x03   //black bgr, green txt
#define PAL_G1          0x10   //gray bgr, gray txt
#define PAL_G2          0x11   //gray bgr, white txt
#define PAL_G3          0x12   //gray bgr, yellow txt

//base io regs
#define REG_FIFO_DATA   *((u8 *)0x40F0) //fifo data register
#define REG_FIFO_STAT   *((u8 *)0x40F1) //fifo status register. shows if fifo can be readed.
#define REG_SST_ADDR    *((u8 *)0x40F2)
#define REG_SST_DATA    *((u8 *)0x40F3)
#define REG_MSTAT       *((u8 *)0x40FF)//mapper status register. used during fpga reboot and dma

//mapper regs
#define REG_VRM_ADDR    *((u16 *)0x4100) //system mapper internal nametable address
#define REG_VRM_DATA    *((u8  *)0x4102) //system mapper internal nametable data
#define REG_VRM_ATTR    *((u8  *)0x4103) //when load new bytes to nametable, value of this register will be applied as attribut.
#define REG_TIMER       *((u16 *)0x4104)
#define REG_APP_BANK    *((u8  *)0x4106) //switches 8K memory banks at 0x6000.16 banks. they all located to OS prg ROM (128K)

#define REG_FDS_SWAP    *((u8 *)0x402D) //register for software change of disk side

#define BNK_OS_BASE     0x00 //map os area to app bank
#define BNK_SRM_BASE    0x10 //map sram to app bank 
#define BNK_SRM_HI      0x20 //upper 128K of sram (not implemented yet)

#define FIFO_MOS_RXF    0x80 //fifo flags. system cpu can read
#define FIFO_ARM_RXF    0x40 //fifo flags. mcu can read

#define STATUS_UNLOCK   0x01 //mcu completed system configuration and. System ready for cpu execution
#define STATUS_CMD_OK   0x02 //mcu finished command execution
#define STATUS_FPG_OK   0x04 //fpga reboot complete

//#define SS_SRC_JOY_SAVE 0x01 //ss joy save combo
//#define SS_SRC_JOY_LOAD 0x02 //ss joy load combo
//#define SS_SRC_BTN      0x04 //external button

#define VRM_UPD_MODE    0x80
#define VRM_MODE_STD    0xA0
#define VRM_MODE_SAF    0xA1
#define VRM_MODE_TST    0xA2

#define SUB_STATUS_DISK 0x00
#define SUB_STATUS_USB  0x01
//#define SUB_STATUS_CUE  0x02
#define SUB_STATUS_BOOT 0x03
//******************************************************************************
//PI bus addresses
#define ADDR_PRG        0x0000000       //PRG ROM
#define ADDR_CHR        0x0800000       //CHR ROM
#define ADDR_SRM        0x1000000       //Batery RAM (256K wrapped)
#define ADDR_CFG        0x1800000       //various system configs
#define ADDR_SSR        0x1802000       //save state. sniffer data
#define ADDR_FIFO       0x1810000       //fifo buffer

#define ADDR_OS_PRG     (ADDR_PRG + SIZE_PRG - SIZE_OS_PRG)
#define ADDR_OS_CHR     (ADDR_CHR + SIZE_CHR - SIZE_OS_PRG)
#define ADDR_SST_HW     (ADDR_OS_PRG + 8192)
#define ADDR_SST_FDS    (ADDR_PRG)
#define ADDR_FDS        ADDR_SRM
#define ADDR_FDS_SIG    (ADDR_FDS + SIZE_FDS_DISK)
#define ADDR_FDS_BIOS   (ADDR_PRG + 0x8000)
#define ADDR_FBUFF      (ADDR_OS_PRG - SIZE_FBUFF)

#define ADDR_FLA_MENU   0x00000 //boot fails mos6502 code
#define ADDR_FLA_FPGA   0x40000 //boot fails fpga code
#define ADDR_FLA_RECO   0x80000 //mcu recovery
#define ADDR_FLA_ICOR   0xC0000 //mcu firmware update

#define SIZE_SRM        0x40000         //whole battery ram size
#define SIZE_PRG        0x800000        //PRG chip size
#define SIZE_CHR        0x800000        //CHR chip size
#define SIZE_OS_PRG     0x20000         //OS PRG size 
#define SIZE_OS_CHR     0x4000          //OS CHR size
#define SIZE_SST_HW     0x4000          //size of save state block with hardware state
#define SIZE_SST_ERAM   0x8000          //size of save state block with extended ram
#define SIZE_SRM_GAME   0x10000         //max size of battery ram which will be copird on sd
#define SIZE_FDS_DISK   65500L          
#define SIZE_FIFO       2048            //fifo buffer size between cpu and mcu
#define SIZE_FBUFF      0x20000
#define SIZE_MAX_PRG    (SIZE_PRG - SIZE_OS_PRG - SIZE_FBUFF)
#define SIZE_MAX_CHR    (SIZE_PRG - SIZE_OS_CHR)

#define APP_ADDR        0x6000
#define APP_SIZE        0x2000

#define GG_SLOTS        8
#define SS_COMBO_OFF    0x00 //turn off ss combo val
//****************************************************************************** file mode
#define	FA_READ			0x01
#define	FA_WRITE		0x02
#define	FA_OPEN_EXISTING	0x00
#define	FA_CREATE_NEW		0x04
#define	FA_CREATE_ALWAYS	0x08
#define	FA_OPEN_ALWAYS		0x10
#define	FA_OPEN_APPEND		0x30

#define	AT_RDO	0x01	/* Read only */
#define	AT_HID	0x02	/* Hidden */
#define	AT_SYS	0x04	/* System */
#define AT_DIR	0x10	/* Directory */
#define AT_ARC	0x20	/* Archive */

#define DIR_OPT_SORTED  0x01
#define DIR_OPT_HIDESYS 0x02
#define DIR_OPT_SEEKCUE 0x04
#define DIR_OPT_FILTCUE 0x08
#define DIR_OPT_FILTROM 0x10
#define DIR_OPT_FILTRBF 0x20
//****************************************************************************** system control
#define MAP_CTRL_RDELAY 0x01    //with this option quick reset wil reset the game but will not return to menu
#define MAP_CTRL_SS_ON  0x02    //vblank hook for in-game menu
#define MAP_CTRL_GG_ON  0x04    //cheats engine
#define MAP_CTRL_SS_BTN 0x08    //use external button for save state
#define MAP_CTRL_FAMI   0x10    //cartridge form-factor (0-nes, 1-famicom)
#define MAP_CTRL_UNLOCK 0x80    //mcu sets this bit when fpga configuration complete.
//****************************************************************************** game mapper control
#define MCFG_MIR_MSK    0x03
#define MCFG_MIR_H      0x00
#define MCFG_MIR_V      0x01
#define MCFG_MIR_4      0x02
#define MCFG_MIR_1      0x03
#define MCFG_CHR_RAM    0x04
#define MCFG_SRM_OFF    0x08

#define MCFG_FDS_ASW    0x10    //disk auto swap for fds
#define MCFG_FDS_EBI    0x20    //use external bios
//****************************************************************************** 
extern u8 zp_dat[16];
extern u8 zp_app[128];
extern void *zp_src;
extern void *zp_dst;
extern u16 zp_len;
extern u16 zp_arg;
extern u16 zp_ret;
extern u8 zp_cmd[64];


#pragma zpsym("zp_dat")
#pragma zpsym("zp_app")
#pragma zpsym("zp_src")
#pragma zpsym("zp_dst")
#pragma zpsym("zp_len")
#pragma zpsym("zp_arg")
#pragma zpsym("zp_ret")
#pragma zpsym("zp_cmd")

typedef struct {
    u32 size;
    u16 date;
    u16 time;
    u8 is_dir;
    u8 *file_name;
} FileInfo;

typedef struct {
    u16 addr;
    u8 cmp_val;
    u8 new_val;
} CheatSlot;

typedef struct {
    CheatSlot slot[GG_SLOTS];
} CheatList;

typedef struct {
    CheatList gg;
    u8 map_idx;
    u8 prg_msk;
    u8 chr_msk;
    u8 master_vol;
    u8 map_cfg;
    u8 ss_key_save;
    u8 ss_key_load;
    u8 map_ctrl;
    u8 ss_key_menu;
    u8 jmp_val;
} MapConfig;

typedef struct {
    u16 v50;
    u16 v25;
    u16 v12;
    u16 vbt;
} Vdc;

typedef struct {
    u8 yar;
    u8 mon;
    u8 dom;
    u8 hur;
    u8 min;
    u8 sec;
} RtcTime;

typedef struct {
    u8 fla_id[8];
    u8 cpu_id[12];
    u32 serial_g;
    u32 serial_l;
    u32 boot_ctr;
    u32 game_ctr;
    u16 asm_date;
    u16 asm_time;
    u16 sw_ver;
    u16 hv_ver;
    u16 boot_ver;
    u8 device_id;
    u8 manufac_id;
    u8 rst_src;
    u8 boot_status;
    u8 ram_rst;
    u8 disk_status;
    u8 cart_form; //cartridge form factor
    u8 pwr_sys;
    u8 pwr_usb;
    u8 reserved[9];
} SysInfoIO;

u8 ed_init();
void ed_cmd_status(u16 *status);
u8 ed_check_status();
u8 ed_cmd_disk_init();
u8 ed_cmd_dir_load(u8 *path, u8 sorted);
void ed_cmd_dir_get_size(u16 *size);
void ed_cmd_dir_get_recs(u16 start_idx, u16 amount, u16 max_name_len);
u8 ed_cmd_file_open(u8 *path, u8 mode);
u8 ed_cmd_file_close();
u8 ed_cmd_file_read_mem(u32 addr, u32 len);
u8 ed_cmd_file_read(void *dst, u32 len);
u8 ed_cmd_file_write(void *src, u32 len);
u8 ed_cmd_file_write_mem(u32 addr, u32 len);
u8 ed_cmd_file_info(u8 *path, FileInfo *inf);
u8 ed_cmd_file_set_ptr(u32 addr);
u8 ed_file_get_size(u8 *path, u32 *size);
u8 ed_cmd_file_del(u8 *path);
u8 ed_cmd_dir_make(u8 *path);



void ed_cmd_uart_wr(void *data, u16 len);
void ed_cmd_usb_wr(void *data, u16 len);
void ed_cmd_fifo_wr(void *data, u16 len);
u8 ed_cmd_fpg_init_sdc(u8 *path);
void ed_cmd_fpg_init_usb();
u8 ed_cmd_file_crc(u32 len, u32 *crc_base);
u32 ed_cmd_file_available();
u8 ed_cmd_file_copy(u8 *src, u8 *dst, u8 dst_mode);
u8 ed_cmd_file_move(u8 *src, u8 *dst, u8 dst_mode);
void ed_cmd_mem_set(u8 val, u32 addr, u32 len);
u8 ed_cmd_mem_test(u8 val, u32 addr, u32 len);
void ed_cmd_mem_rd(u32 addr, void *dst, u32 len);
void ed_cmd_mem_wr(u32 addr, void *src, u32 len);
void ed_cmd_mem_crc(u32 addr, u32 len, u32 *crc_base);
void ed_cmd_upd_exec(u32 addr, u32 crc);
void ed_cmd_get_vdc(Vdc *vdc);
void ed_cmd_rtc_get(RtcTime *time);
void ed_cmd_rtc_set(RtcTime *time);
void ed_cmd_sys_inf(SysInfoIO *inf);
void ed_cmd_reboot();
void ed_cmd_game_ctr();
void ed_cmd_fla_rd(void *dst, u32 addr, u32 len);
u8 ed_cmd_fla_wr_sdc(u32 addr, u32 len);
u8 ed_cmd_efu_unpack_file(u8 *path);
u8 ed_cmd_sub_status(u8 stat_req);

void ed_tx_string(u8 *string);
void ed_rx_string(u8 *string);
u8 ed_rx_next_rec(FileInfo *inf);
void ed_fifo_wr(void *data, u16 len);
void ed_fifo_rd(void *data, u16 len);
u8 ed_fifo_busy();
void ed_halt_usb();

void ed_put_str();
void ed_clean_screen();
void ed_copy_screen();
void ed_copy_screen_safe();
void ed_vram_fill();
u8 ed_get_rom_mask(u32 size);
u8 ed_get_srm_mask(u32 size);
void ed_exit_game();
void ed_cfg_set(MapConfig *cfg);
void ed_sleep(u16 ms);
u16 ed_get_ticks();
void ed_reboot_usb();
void ed_start_app(MapConfig *cfg);



#endif	/* EVERDRIVE_H */

