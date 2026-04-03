
; ===== Mapper selection via build defines =====
; Define exactly one: USE_VRC6, USE_VRC7, USE_S5B, USE_N163
; Default: no expansion (NROM, mapper 0)

.ifdef USE_VRC6
    NES_MAPPER = 24
.elseif .defined(USE_VRC7)
    NES_MAPPER = 85
.elseif .defined(USE_S5B)
    NES_MAPPER = 69
.elseif .defined(USE_N163)
    NES_MAPPER = 19
.else
    ; Default: NROM (no expansion audio)
    NES_MAPPER = 0
.endif

NES_PRG_BANKS   = 2;number of 16K PRG banks, change to 2 for NROM256
NES_CHR_BANKS   = 1;number of 8K CHR banks
NES_MIRRORING   = 1;0 horizontal, 1 vertical, 8 four screen

PPU_CTRL	=$2000
PPU_MASK	=$2001
PPU_STATUS	=$2002
PPU_OAM_ADDR    =$2003
PPU_OAM_DATA    =$2004
PPU_SCROLL	=$2005
PPU_ADDR	=$2006
PPU_DATA	=$2007
PPU_OAM_DMA	=$4014
PPU_FRAMECNT    =$4017
DMC_FREQ	=$4010
CTRL_PORT1	=$4016
CTRL_PORT2	=$4017

OS_ZRAM = $90
GFX_PTR = OS_ZRAM+2
PRG_PTR = OS_ZRAM+4
CTR1 = OS_ZRAM+6
CTR2 = OS_ZRAM+7


.define cmd_addr1 $4400
.define cmd_data1 $4401
.define cmd_addr2 $4402
.define cmd_data2 $4403

.define reg_spi 0
.define reg_usb 1
.define reg_cfg 2
.define reg_state 3
.define reg_key 4
.define reg_fpga 5
.define reg_fpga_nc 6
.define reg_map 7
.define reg_bios_cfg 9

.define freg_srm_map 129

.define srm_bank_os 7
.define srm_bank_tileset 8

.export start,__STARTUP__:absolute=1
.export _getTVSystem
.import __RAM_START__   ,__RAM_SIZE__
.import __ROM0_START__  ,__ROM0_SIZE__
.import __STARTUP_LOAD__,__STARTUP_RUN__,__STARTUP_SIZE__
.import	__CODE_LOAD__   ,__CODE_RUN__   ,__CODE_SIZE__
.import	__RODATA_LOAD__ ,__RODATA_RUN__ ,__RODATA_SIZE__

FT_BASE_ADR = $0100;page in RAM, should be $xx00
.include "zeropage.inc"
.import initlib, push0, popa, popax, _main, zerobss, copydata
.import _main

.segment "HEADER"

    .byte $4e,$45,$53,$1a
    .byte NES_PRG_BANKS
    .byte NES_CHR_BANKS
    .byte NES_MIRRORING|((NES_MAPPER & 15)<<4)
    .byte NES_MAPPER&$f0
    .res 8,0

.segment "STARTUP"
start:

    sei
    ldx #$ff
    txs

; ----- Mapper-specific bank setup -----

.ifdef USE_VRC6
    ; VRC6 (mapper 24) bank setup
    ; Set up VRC6 PRG banks so $8000-$DFFF is visible
    lda #0
    sta $8000           ; 16KB bank 0 -> $8000-$BFFF
    lda #2
    sta $C000           ;  8KB bank 2 -> $C000-$DFFF

    ; Set up VRC6 CHR banks - map 8KB linearly
    lda #0
    sta $D000           ; 1KB page 0 -> PPU $0000
    lda #1
    sta $D001           ; 1KB page 1 -> PPU $0400
    lda #2
    sta $D002           ; 1KB page 2 -> PPU $0800
    lda #3
    sta $D003           ; 1KB page 3 -> PPU $0C00
    lda #4
    sta $E000           ; 1KB page 4 -> PPU $1000
    lda #5
    sta $E001           ; 1KB page 5 -> PPU $1400
    lda #6
    sta $E002           ; 1KB page 6 -> PPU $1800
    lda #7
    sta $E003           ; 1KB page 7 -> PPU $1C00

    lda #0
    sta $5100

.elseif .defined(USE_VRC7)
    ; VRC7 (mapper 85) bank setup
    ; VRC7 uses A4 for register select, so pairs are $x000/$x010
    ; PRG banks (8KB each, $E000-$FFFF fixed to last bank):
    ;   $8000 = bank at $8000-$9FFF
    ;   $8010 = bank at $A000-$BFFF
    ;   $9000 = bank at $C000-$DFFF
    lda #0
    sta $8000           ; 8KB bank 0 -> $8000-$9FFF
    lda #1
    sta $8010           ; 8KB bank 1 -> $A000-$BFFF
    lda #2
    sta $9000           ; 8KB bank 2 -> $C000-$DFFF

    ; CHR banks - map 8KB linearly (1KB pages)
    lda #0
    sta $A000           ; 1KB page 0 -> PPU $0000
    lda #1
    sta $A010           ; 1KB page 1 -> PPU $0400
    lda #2
    sta $B000           ; 1KB page 2 -> PPU $0800
    lda #3
    sta $B010           ; 1KB page 3 -> PPU $0C00
    lda #4
    sta $C000           ; 1KB page 4 -> PPU $1000
    lda #5
    sta $C010           ; 1KB page 5 -> PPU $1400
    lda #6
    sta $D000           ; 1KB page 6 -> PPU $1800
    lda #7
    sta $D010           ; 1KB page 7 -> PPU $1C00

.elseif .defined(USE_S5B)
    ; Sunsoft 5B / FME-7 (mapper 69) bank setup
    ; Command register at $8000, parameter at $A000
    ; PRG bank registers: command 8-B
    ;   cmd 8: $6000-$7FFF (SRAM/PRG)
    ;   cmd 9: $8000-$9FFF
    ;   cmd A: $A000-$BFFF
    ;   cmd B: $C000-$DFFF

    lda #$09
    sta $8000           ; select PRG bank register for $8000
    lda #0
    sta $A000           ; 8KB bank 0 -> $8000-$9FFF

    lda #$0A
    sta $8000           ; select PRG bank register for $A000
    lda #1
    sta $A000           ; 8KB bank 1 -> $A000-$BFFF

    lda #$0B
    sta $8000           ; select PRG bank register for $C000
    lda #2
    sta $A000           ; 8KB bank 2 -> $C000-$DFFF

    ; CHR banks (command 0-7, 1KB each)
    lda #$00
    sta $8000
    lda #0
    sta $A000           ; 1KB page 0 -> PPU $0000

    lda #$01
    sta $8000
    lda #1
    sta $A000           ; 1KB page 1 -> PPU $0400

    lda #$02
    sta $8000
    lda #2
    sta $A000           ; 1KB page 2 -> PPU $0800

    lda #$03
    sta $8000
    lda #3
    sta $A000           ; 1KB page 3 -> PPU $0C00

    lda #$04
    sta $8000
    lda #4
    sta $A000           ; 1KB page 4 -> PPU $1000

    lda #$05
    sta $8000
    lda #5
    sta $A000           ; 1KB page 5 -> PPU $1400

    lda #$06
    sta $8000
    lda #6
    sta $A000           ; 1KB page 6 -> PPU $1800

    lda #$07
    sta $8000
    lda #7
    sta $A000           ; 1KB page 7 -> PPU $1C00

.elseif .defined(USE_N163)
    ; Namco 163 (mapper 19) bank setup
    ; PRG bank registers:
    ;   $E000 = 8KB bank at $8000-$9FFF
    ;   $E800 = 8KB bank at $A000-$BFFF
    ;   $F000 = 8KB bank at $C000-$DFFF
    lda #0
    sta $E000           ; 8KB bank 0 -> $8000-$9FFF
    lda #1
    sta $E800           ; 8KB bank 1 -> $A000-$BFFF
    lda #2
    sta $F000           ; 8KB bank 2 -> $C000-$DFFF

    ; CHR bank registers (1KB pages):
    ;   $8000-$8400-$8800-$8C00 for PPU $0000-$0FFF
    ;   $9000-$9400-$9800-$9C00 for PPU $1000-$1FFF
    lda #0
    sta $8000           ; 1KB page 0 -> PPU $0000
    lda #1
    sta $8800           ; 1KB page 1 -> PPU $0400
    lda #2
    sta $9000           ; 1KB page 2 -> PPU $0800
    lda #3
    sta $9800           ; 1KB page 3 -> PPU $0C00
    lda #4
    sta $A000           ; 1KB page 4 -> PPU $1000
    lda #5
    sta $A800           ; 1KB page 5 -> PPU $1400
    lda #6
    sta $B000           ; 1KB page 6 -> PPU $1800
    lda #7
    sta $B800           ; 1KB page 7 -> PPU $1C00

    ; Disable N163 sound RAM write protect
    lda #0
    sta $F800

.else
    ; NROM (mapper 0) - no bank setup needed, flat 32KB PRG
.endif

    ldx #0
    stx PPU_MASK
    stx PPU_CTRL

    ldy #0
    lda #0
clram:
    sta $0000, y
    sta $0100, y
    sta $0200, y
    sta $0300, y
    sta $0400, y
    sta $0500, y
    sta $0600, y
    sta $0700, y
    iny
    bne clram


    jsr	zerobss
    jsr	copydata

    lda #<(__RAM_START__+__RAM_SIZE__)
    sta	c_sp
    lda	#>(__RAM_START__+__RAM_SIZE__)
    sta	c_sp+1          ; Set argument stack ptr

    jsr	initlib

    jmp _main


nmi:
    rti
irq:
    rti

; ---------------------------------------------------------------
; TV system detection (NTSC / PAL / Dendy) by CPU-cycle counting.
; Based on Damian Yerrick's method: count iterations of a tight
; loop between two vblanks.  No external timer hardware needed.
;
;   NTSC:  29780 cyc/frame -> ~23 iterations of 1284 cycles
;   PAL:   33247 cyc/frame -> ~26 iterations
;   Dendy: 35464 cyc/frame -> ~28 iterations
;
; Returns in A: 0 = NTSC, 1 = PAL NES, 2 = Dendy
; ---------------------------------------------------------------
.segment "CODE"

; Counts Y*1284 + X*5 + 5 cycles (minus 1284 if X!=0),
; then reads $2002.
.proc wait1284y
    dex                 ; 2 cyc
    bne wait1284y       ; 3/2 cyc  (5 per iter when taken)
    dey                 ; 2 cyc
    bne wait1284y       ; 3/2 cyc
    bit $2002
    rts
.endproc

.proc _getTVSystem
    ; Acknowledge any pending NMI (important after Reset during vblank
    ; on top-loader NES, which leaves NMI unacknowledged)
    bit $2002

    ; Wait for start of vblank (first vblank wait for PPU warm-up)
@vwait1:
    bit $2002
    bpl @vwait1

    ; Now time one full frame with a 1284-cycle loop.
    ; 24 * 1284 = 30816 cycles.
    ;   NTSC (29780):  vblank fires BEFORE 24 loops finish -> N flag set
    ;   PAL  (33247):  vblank fires AFTER 24 but before 27 loops
    ;   Dendy(35464):  vblank fires AFTER 27 loops
    ldx #0
    ldy #24
    jsr wait1284y
    bpl @not_ntsc
    ; Vblank happened within 24 loops -> NTSC
    lda #0
    rts
@not_ntsc:
    ; Wait 3 more loops (total 27 * 1284 = 34668 cycles)
    lda #1
    ldy #3
    jsr wait1284y
    ; If vblank happened by now -> PAL NES.  Otherwise -> Dendy.
    bmi @done
    asl a               ; A = 2 (Dendy)
@done:
    rts
.endproc

.segment "STARTUP"

.segment "VECTORS"

.word nmi ;$fffa vblank nmi
.word start ;$fffc reset
.word irq ;$fffe irq / brk

.segment "CHARS"
.incbin "font.chr"
