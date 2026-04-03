/* 
 * File:   errors.h
 * Author: igor
 *
 * Created on April 4, 2019, 12:37 AM
 */

#ifndef ERRORS_H
#define	ERRORS_H

//****************************************************************************** 
#define ERR_UNXP_STAT           0x40
#define ERR_UNK_ROM_FORMAT      0x41
#define ERR_PATH_SIZE           0x42
#define ERR_NAME_SIZE           0x43
#define ERR_OUT_OF_MEMORY       0x44
#define ERR_GAME_NOT_SEL        0x45
#define ERR_REGI_CRC            0x46
#define ERR_MAP_NOT_SUPP        0x47
#define ERR_MAP_NOT_FOUND       0x48
#define ERR_INCORRECT_GG        0x49
#define ERR_NULL_PATH           0x4A
#define ERR_USB_GAME            0x4B
#define ERR_FDS_SIZE            0x4C
#define ERR_BAT_RDY             0x4D
#define ERR_BAD_NSF             0x4E
#define ERR_BAD_FILE            0x4F
#define ERR_ROM_SIZE            0x50
#define ERR_FBUFF_SIZE          0x51
#define ERR_NO_REST_POINT       0x52


//mcu errors
#define ERR_EFU_BAD_FILE        0x70
#define ERR_EFU_NO_FILE         0x71

#define ERR_STR_SIZE    0x80
#define ERR_OUT_OF_DIR  0x81
#define ERR_OUT_OF_PAGE 0x82
#define ERR_BOOT_FAULT  0x83
#define ERR_FPGA_INIT   0x84
#define ERR_SPI_STATE   0x85

#define ERR_LINK_TOUT   0x86
#define ERR_UPD_SIZE    0x87
#define ERR_UPD_SAME    0x88
#define ERR_UPD_CORUPT  0x89
#define ERR_UPD_BT_CRC  0x8A
#define ERR_UPD_VERIFY  0x8B
#define ERR_WDG_TOUT    0x8C
#define ERR_SIG_NBLA    0x8D

#define DISK_ERR_INIT   0xC0 //0xCx disk init errors
#define DISK_ERR_IO     0xD0 //0xDx disk io errors
#define DISK_ERR_RD1    0xD0 
#define DISK_ERR_RD2    0xD1 
#define DISK_ERR_RD3    0xD2
#define DISK_ERR_RD4    0xD3

#define DISK_ERR_WR1    0xD4
#define DISK_ERR_WR2    0xD5
#define DISK_ERR_WR3    0xD6
#define DISK_ERR_WR4    0xD7
#define DISK_ERR_WR5    0xD8

#define DISK_ERR_CTO    0xD9//cmd timeout
#define DISK_ERR_CCR    0xDA//cmd crc error

#define FAT_DISK_ERR    0x01
#define FAT_NOT_READY   0x03
#define FAT_NO_FILE     0x04
#define FAT_NO_PATH     0x05
#define FAT_NO_FS       0x0D


#endif	/* ERRORS_H */
