#ifndef _MICROKEYER_H
#define _MICROKEYER_H

#define FTDI_VID            0x0403 /* FTDI USB vendor ID */
#define MHAM_MICROKEYER_PID 0xEEEF /* microHAM micro KEYER family product ID */
                                   /* Also used by incompatible products :-( */

/*
 * Device models supported by the driver
 */
#define MODEL_UNSUPPORTED 0x00 /* Unsupported device */
#define MODEL_MK       0x01          /* microHAM micro KEYER */
#define MODEL_DK       0x02          /* microHAM DIGI KEYER */
#define MODEL_CK       0x03          /* microHAM CW KEYER */
#define MODEL_MK2R     0x04          /* microHAM micro KEYER 2R */
#define MODEL_MK2RPLUS 0x05          /* microHAM micro KEYER 2R+ */
#define MODEL_MK2      0x06          /* microHAM micro KEYER II */
#define MODEL_DK2      0x07          /* microHAM DIGI KEYER II */
#define MODEL_U2R      0x08          /* microHAM micro 2R */
#define MODEL_SM       0x09          /* microHAM Station Master */
#define MODEL_SMD      0x0A          /* microHAM Station Master Deluxe */

/*
 * Bit fields in synchro byte (byte 0) of frame
 */
#define SYNCHRO_MSB_SHARED   0x01
#define SYNCHRO_MSB_R2       0x02
#define SYNCHRO_MSB_R1       0x04
#define SYNCHRO_VALID_SHARED 0x08
#define SYNCHRO_VALID_R2     0x10
#define SYNCHRO_VALID_R1     0x20
#define SYNCHRO_SEQUENCE     0x40
/* bit 7 always cleared */

/*
 * Flags sent from computer to device
 */
#define FLAGS_R1_RTS     0x01 /* 0=active, 1=inactive */
#define FLAGS_R2_RTS     0x02 /* 0=active, 1=inactive */
#define FLAGS_R1_PTT     0x04
#define FLAGS_R2_PTT     0x08
#define FLAGS_R1_FSK_EXT 0x10
#define FLAGS_R2_FSK_EXT 0x20
#define FLAGS_R1_CW      0x40
#define FLAGS_R2_CW      0x80

/*
 * Flags sent from device to computer, FLAGS_IS_R2 used to select R1 or R2
 */
#define FLAGS_CTS        0x01 /* 0=active, 1=inactive */
                              /* bit 2 reserved */
                              /* bit 3 reserved */
#define FLAGS_IS_R2      0x08 /* 0 for R1, 1 for R2 */
#define FLAGS_SQUELCH    0x10 /* 0=inactive, 1=active */
#define FLAGS_FSK_BUSY   0x20 /* 0=no tx & buffer empty, 1=tx active */
#define FLAGS_ANY_PTT_ON 0x40 /* 1=any PTT, serial/winkey/footsw./F10/LPT/vox */
#define FLAGS_FOOTSWITCH 0x80 /* 1=PTT from footswitch or F10 is on */

/*
 * Control channel commands
 */
#define CONTROL_NOP                      0x00
#define CONTROL_SET_R1_RADIO_CHANNEL     0x01
#define CONTROL_SET_R2_RADIO_CHANNEL     0x02
#define CONTROL_SET_R1_FSK_CHANNEL       0x03
#define CONTROL_SET_R2_FSK_CHANNEL       0x04
#define CONTROL_GET_VERSION              0x05
#define CONTROL_START_BOOTLOADER         0x06
#define CONTROL_JUST_RESTARTED           0x07
#define CONTROL_STORE_SETTINGS           0x08
#define CONTROL_SET_SETTINGS             0x09
#define CONTROL_SET_KB_MODE              0x0A
#define CONTROL_STORE_WINKEY_INIT        0x0B
#define CONTROL_RECORD_CW_FSK_MESSAGE    0x0C
#define CONTROL_PLAY_CW_FSK_MESSAGE      0x0D
#define CONTROL_ABORT_CW_FSK_MESSAGE     0x0E
#define CONTROL_WINKEY_DOES_NOT_RESPOND  0x0F
#define CONTROL_STORE_CW_FSK_MESSAGE_1   0x10
#define CONTROL_STORE_CW_FSK_MESSAGE_2   0x11
#define CONTROL_STORE_CW_FSK_MESSAGE_3   0x12
#define CONTROL_STORE_CW_FSK_MESSAGE_4   0x13
#define CONTROL_STORE_CW_FSK_MESSAGE_5   0x14
#define CONTROL_STORE_CW_FSK_MESSAGE_6   0x15
#define CONTROL_STORE_CW_FSK_MESSAGE_7   0x16
#define CONTROL_STORE_CW_FSK_MESSAGE_8   0x17
#define CONTROL_STORE_CW_FSK_MESSAGE_9   0x18
#define CONTROL_STORE_FSK_MESSAGE_1      0x20
#define CONTROL_STORE_FSK_MESSAGE_2      0x21
#define CONTROL_STORE_FSK_MESSAGE_3      0x22
#define CONTROL_STORE_FSK_MESSAGE_4      0x23
#define CONTROL_STORE_FSK_MESSAGE_5      0x24
#define CONTROL_STORE_FSK_MESSAGE_6      0x25
#define CONTROL_STORE_FSK_MESSAGE_7      0x26
#define CONTROL_STORE_FSK_MESSAGE_8      0x27
#define CONTROL_STORE_FSK_MESSAGE_9      0x28
#define CONTROL_BANDS_INFO               0x30
#define CONTROL_HOST_FOCUS_CONTROL       0x31
#define CONTROL_STORE_SCENARIO           0x32
#define CONTROL_GET_SCENARIO             0x33
#define CONTROL_APPLY_SCENARIO           0x34
#define CONTROL_HOST_ACC_OUTPUTS_CONTROL 0x35
#define CONTROL_ACC_STATE                0x79
#define CONTROL_DVK_CONTROL              0x7A
#define CONTROL_MOK_STATE                0x7B
#define CONTROL_CURRENT_KB_MODE          0x7C
#define CONTROL_AUTO_NUMBER              0x7D
#define CONTROL_ARE_YOU_THERE            0x7E
#define CONTROL_COMMAND_NOT_SUPPORTED    0x7F
#define CONTROL_END_COMMAND              0x80 /* OR-ed with start command */

/*
 * For use with SS byte of SET_*_RADIO_CHANNEL control commands
 */
#define CHANNEL_PARITY_NONE      0x00
#define CHANNEL_PARITY_EVEN      0x01
#define CHANNEL_PARITY_ODD       0x02
#define CHANNEL_PARITY_MARKSPACE 0x03
#define CHANNEL_STOP_1BIT        0x00
#define CHANNEL_STOP_2BIT        0x04
#define CHANNEL_STOP_15BIT       0x80
#define CHANNEL_RTSCTS           0x10
#define CHANNEL_DATA_5BIT        0x00
#define CHANNEL_DATA_6BIT        0x20
#define CHANNEL_DATA_7BIT        0x40
#define CHANNEL_DATA_8BIT        0x60
#define CHANNEL_PARITY_SPACE     0x00
#define CHANNEL_PARITY_MARK      0x80

typedef unsigned char frame_t[4];     /* A 4 byte frame */
typedef unsigned char sequence_t[21]; /* Sequence of up to 5 frames + number of frames to send */

#endif
