/*
 * microkeyer
 *
 * Copyright 2011 Norvald H. Ryeng
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdarg.h>
#include <getopt.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "microkeyer.h"

struct ports {
  int keyer;
  int control;
  int radio1;
  int radio2;
  int fsk1;
  int fsk2;
  int winkey;
  int keyboard;
};

int verbosity = 0;
int keyer_model = MODEL_UNSUPPORTED;

int debugprintf(int level, const char *format, ...)
{
  int res = 0;

  if (level > verbosity)
    return 0;

  va_list args;
  va_start(args, format);
  res = vprintf(format, args);
  va_end(args);

  return res;
}

/*
 * Open a new pseudo TTY, set it to raw mode, grant rights and unlock
 *
 * Print error message and exit on failure
 */
int newpty()
{
  int fd;
  struct termios tio;

  if ((fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1) {
    perror("Can't open new pty");
    exit(1);
  }

  if (tcgetattr(fd, &tio)) {
    perror("Can't get PTY communication parameters");
    exit(1);
  }
  cfmakeraw(&tio);
  if (tcsetattr(fd, TCSADRAIN, &tio)) {
    // NOTE: This does NOT guarantee that ALL parameters are set
    perror("Can't set PTY communication parameters");
    exit(1);
  }
  if (grantpt(fd)) {
    perror("Can't grant PTY access");
    exit(1);
  }
  if (unlockpt(fd)) {
    perror("Can't unlock PTY slave");
    exit(1);
  }

  return fd;
}

/*
 * Initialize a new sequence of (up to) 5 frames
 *
 * No flags set, no channels valid
 */
void sequence_init(sequence_t seq)
{
  int i;

  for (i = 0; i < 20; i++)
    seq[i] = 0x80;
  for (i = 4; i < 20; i += 4)
    seq[i] = 0x40;
  seq[0] = 0x08; // Valid flags in first frame
  seq[20] = 0;
}

/*
 * Set RTS flags in a sequence
 */
void sequence_set_rts(sequence_t seq, int radio)
{
  if (radio == 1)
    seq[3] |= FLAGS_R1_RTS;
  else if (radio == 2)
    seq[3] |= FLAGS_R2_RTS;
  if (seq[20] < 1)
    seq[20] = 1;
}

/*
 * Set PTT flags in a sequence
 */
void sequence_set_ptt(sequence_t seq, int radio)
{
  if (radio == 1)
    seq[3] |= FLAGS_R1_PTT;
  else if (radio == 2)
    seq[3] |= FLAGS_R2_PTT;
  if (seq[20] < 1)
    seq[20] = 1;
}

/*
 * Set FSK EXT flags in a sequence
 */
void sequence_set_fsk_ext(sequence_t seq, int radio)
{
  if (radio == 1)
    seq[3] |= FLAGS_R1_FSK_EXT;
  else if (radio == 2)
    seq[3] |= FLAGS_R2_FSK_EXT;
  if (seq[20] < 1)
    seq[20] = 1;
}

/*
 * Set CW flags in a sequence
 */
void sequence_set_cw(sequence_t seq, int radio)
{
  if (radio == 1)
    seq[3] |= FLAGS_R1_CW;
  else if (radio == 2)
    seq[3] |= FLAGS_R2_CW;
  if (seq[20] < 1)
    seq[20] = 1;
}

/*
 * Set radio channel byte in a given frame (0-4)
 */
void sequence_set_radio(sequence_t seq, int radio, int frame, unsigned char data)
{
  seq[radio + 4*frame] = 0x80 | data;
  if (radio == 1)
    seq[4*frame] |= ((data & 0x080) ? SYNCHRO_MSB_R1 : 0x00) | SYNCHRO_VALID_R1;
  else if (radio == 2)
    seq[4*frame] |= ((data & 0x080) ? SYNCHRO_MSB_R2 : 0x00) | SYNCHRO_VALID_R2;
  if (seq[20] < frame + 1)
    seq[20] = frame + 1;
}

/*
 * Set control channel byte
 */
void sequence_set_control(sequence_t seq, unsigned char data, int valid)
{
  seq[7] = 0x80 | data;
  seq[4] |= ((data & 0x080) ? SYNCHRO_MSB_SHARED : 0x00);
  if (valid) // First and last byte in a command are marked as invalid
    seq[4] |= SYNCHRO_VALID_SHARED;
  if (seq[20] < 2)
    seq[20] = 2;
}

/*
 * Set Winkey channel byte
 */
void sequence_set_winkey(sequence_t seq, unsigned char data)
{
  seq[11] = 0x80 | data;
  seq[8] |= ((data & 0x080) ? SYNCHRO_MSB_SHARED : 0x00) | SYNCHRO_VALID_SHARED;
  if (seq[20] < 3)
    seq[20] = 3;
}

/*
 * Set FSK channel byte for given radio
 */
void sequence_set_fsk(sequence_t seq, int radio, unsigned char data)
{
  seq[11 + radio*4] = 0x80 | data;
  seq[8 + radio*4] |= ((data & 0x080) ? SYNCHRO_MSB_SHARED : 0x00) | SYNCHRO_VALID_SHARED;
  if (seq[20] < 3 + radio)
    seq[20] = 3 + radio;
}

/*
 * How many frames in the sequence are used?
 */
int frames_in_sequence(sequence_t seq)
{
  return seq[20];
}

/*
 * Send a sequence
 *
 * Only the minimum number of frames necessary from a sequence are actually sent
 */
void send_sequence(int fd, sequence_t seq)
{
  int numframes = frames_in_sequence(seq);

  debugprintf(5, "Sending %i frames:\n%02x %02x %02x %02x\n%02x %02x %02x %02x\n%02x %02x %02x %02x \n%02x %02x %02x %02x \n%02x %02x %02x %02x\n", numframes, seq[0], seq[1], seq[2], seq[3], seq[4], seq[5], seq[6], seq[7], seq[8], seq[9], seq[10], seq[11], seq[12], seq[13], seq[14], seq[15], seq[16], seq[17], seq[18], seq[19]);

  // TODO: Better error handling
  if (write(fd, seq, numframes*4) != numframes*4) {
    perror("Error sending sequence to device");
  }
  else
    debugprintf(7, "Send successful.\n");
}

/*
 * Decode a 4 octet frame
 *
 * Keeps a static sequence position
 */
void decode_frame(frame_t frame, struct ports *ports)
{
  static int sequencepos = 0;
  int radio_index;

  // Synchronize on SYNCHRO_SEQUENCE
  if (!(frame[0] & 0xC0))
    sequencepos = 0;

  // Decode R1 data channel
  if (frame[0] & SYNCHRO_VALID_R1) {
    if (!(frame[0] & SYNCHRO_MSB_R1))
      frame[1] &= 0x7F;
    debugprintf(3, "R1: %02x ('%c')\n", frame[1], frame[1]);
    if (ports->radio1 >= 0 && write(ports->radio1, &frame[1], 1) != 1)
      perror("Error writing to radio1");
  }

  // Decode R2 data channel
  if (frame[0] & SYNCHRO_VALID_R2) {
    if (!(frame[0] & SYNCHRO_MSB_R2))
      frame[2] &= 0x7F;
    debugprintf(3, "R2: %02x ('%c')\n", frame[2], frame[2]);
    if (ports->radio2 >= 0 && write(ports->radio2, &frame[2], 1) != 1)
      perror("Error writing to radio2");
  }

  // Decode shared channel
  if (frame[0] & SYNCHRO_VALID_SHARED || sequencepos == 1) {
    if (!(frame[0] & SYNCHRO_MSB_SHARED))
      frame[3] &= 0x7F;
    switch (sequencepos) {
    case 0: // FLAGS
      radio_index = (frame[3] & FLAGS_IS_R2) ? 1 : 0;
      if (frame[3] & FLAGS_CTS) {
	debugprintf(4, "R%i flags: CTS\n", radio_index+1);
	// TODO: Set radio no. radio_index CTS = (frame[3] & FLAGS_CTS)
      }
      if (frame[3] & FLAGS_SQUELCH) {
	debugprintf(4, "R%i flags: SQUELCH\n", radio_index+1);
	// TODO: Set radio no. radio_index squelch = (frame[3] & FLAGS_SQUELCH)
      }
      if (frame[3] & FLAGS_FSK_BUSY) {
	debugprintf(4, "R%i flags: FSK BUSY\n", radio_index+1);
	// TODO: Set radio no. radio_index fsk_busy = (frame[3] & FLAGS_FSK_BUSY)
      }
      if (frame[3] & FLAGS_ANY_PTT_ON) {
	debugprintf(4, "R%i flags: ANY PTT ON\n", radio_index+1);
	// TODO: Set radio no. radio_index any_ptt_on = (frame[3] & FLAGS_ANY_PTT_ON)
      }
      if (frame[3] & FLAGS_FOOTSWITCH) {
	debugprintf(4, "R%i flags: FOOTSWITCH\n", radio_index+1);
	// TODO: Set radio no. radio_index footswitch = (frame[3] & FLAGS_FOOTSWITCH)
      }
      break;
    case 1: // CONTROL
      if (frame[3]) { // Ignore NOPs from device
	debugprintf(3, "CONTROL: %02x ('%c')\n", frame[3], frame[3]);
	if (ports->control >= 0 && write(ports->control, &frame[3], 1) != 1)
	  perror("Error writing to control");
      }
      break;
    case 2: // WINKEY
      debugprintf(3, "WINKEY: %02x ('%c')\n", frame[3], frame[3]);
      if (ports->winkey >= 0 && write(ports->winkey, &frame[3], 1) != 1)
	perror("Error writing to winkey");
      break;
    case 3: // KEYBOARD
      debugprintf(3, "KEYBOARD: %02x ('%c')\n", frame[3], frame[3]);
      if (ports->keyboard >= 0 && write(ports->keyboard, &frame[3], 1) != 1)
	perror("Error writing to keyboard");
      break;
    default: // Should not happen. Each input sequence consists of max 4 frames.
      debugprintf(2, "Received frame %i in sequence of 4. Offending frame: 0x%02x 0x%02x 0x%02x 0x%02x\n", sequencepos, frame[0], frame[1], frame[2], frame[3]);
      break;
    }
  }

  sequencepos++;
}

void show_version()
{
  printf("microkeyer 0.1\n");
  exit(0);
}

void show_help()
{
  printf("Usage: microkeyer [OPTIONS] -m MODEL DEVICE\n");
  printf("\n Help and debug:\n");
  printf("  -m, --model=MODEL       Set keyer model (MK, MK2, MK2R, MK2R+, CK, DK, DK2, U2R, SM)\n");
  printf("  -h, --help              Display this help text\n");
  printf("  -v, --verbose           Show debug output (repeat for more verbosity)\n");
  printf("  -V, --version           Show version information\n");
  printf("\n");
  exit(0);
}

/*
 * Parse command line arguments
 */
char *parseargs(int argc, char *argv[])
{
  static struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"model", required_argument, NULL, 'm'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'}
  };
  int c;
  int option_index;
  char *devicename = NULL;

  while ((c = getopt_long(argc, argv, "-hm:vV", long_options, &option_index)) != -1) {
    switch (c) {
    case 1:
      if (!devicename)
        devicename = optarg;
      else
        show_help();
      break;
    case 'm':
      if (!strcasecmp(optarg, "MK"))
	keyer_model = MODEL_MK;
      else if (!strcasecmp(optarg, "DK"))
	keyer_model = MODEL_DK;
      else if (!strcasecmp(optarg, "CK"))
	keyer_model = MODEL_CK;
      else if (!strcasecmp(optarg, "MK2R"))
	keyer_model = MODEL_MK2R;
      else if (!strcasecmp(optarg, "MK2R+"))
	keyer_model = MODEL_MK2RPLUS;
      else if (!strcasecmp(optarg, "MK2"))
	keyer_model = MODEL_MK2;
      else if (!strcasecmp(optarg, "DK2"))
	keyer_model = MODEL_DK2;
      else if (!strcasecmp(optarg, "U2R"))
	keyer_model = MODEL_U2R;
      else if (!strcasecmp(optarg, "SM"))
	keyer_model = MODEL_SM;
      // TODO: Add MODEL_SMD
      break;
    case 'v':
      verbosity++;
      break;
    case 'V':
      show_version();
      break;
    case 'h':
    case '?':
    default:
      show_help();
      break;
    }
  }

  return devicename;
}

int main(int argc, char *argv[])
{
  char *devicename = NULL;       // Name of microkeyer device
  struct ports ports = {-1, -1, -1, -1, -1, -1, -1, -1}; // File descriptors for device and ptys
  struct termios oldtio, newtio; // For keyer
  fd_set allfds;                 // All available file descriptors
  unsigned char getversion[] = {0x05, 0x85}; // GET VERSION command
  unsigned char deviceversion[256]; // Result of GET VERSION command

  // Parse command line arguments
  devicename = parseargs(argc, argv);
  if (!devicename || !strlen(devicename) || keyer_model == MODEL_UNSUPPORTED)
    show_help();
  debugprintf(1, "Using microkeyer device %s.\n", devicename);

  // Connect to device
  if ((ports.keyer = open(devicename, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1) {
    perror("Can't open microkeyer device");
    exit(1);
  }

  if (tcgetattr(ports.keyer, &oldtio)) {
    perror("Can't get device communication parameters");
    exit(1);
  }
  newtio = oldtio;
  cfmakeraw(&newtio);
  cfsetispeed(&newtio, B230400);
  cfsetospeed(&newtio, B230400);
  if (tcsetattr(ports.keyer, TCSADRAIN, &newtio)) {
    // NOTE: This does NOT guarantee that ALL parameters are set
    perror("Can't set device communication parameters");
    exit(1);
  }
  FD_ZERO(&allfds);
  FD_SET(ports.keyer, &allfds);

  // TODO: Check device type automatically with GET VERSION command and set keyer_model

  // TODO: Add option to create symlinks and/or print pty slaves

  // Open ptys
  ports.control = newpty();
  printf("Control: %s\n", (char *)ptsname(ports.control));
  FD_SET(ports.control, &allfds);

  if (keyer_model != MODEL_U2R) {
    ports.radio1 = newpty();
    printf("Radio 1: %s\n", (char *)ptsname(ports.radio1));
    FD_SET(ports.radio1, &allfds);
  }

  if (keyer_model == MODEL_MK2R || keyer_model == MODEL_MK2RPLUS || keyer_model == MODEL_MK2 || keyer_model == MODEL_SM) {
    // MK2 and SM has AUX, not RADIO2, but it is only the name of the port that changes
    ports.radio2 = newpty();
    printf("Radio 2: %s\n", (char *)ptsname(ports.radio2));
    FD_SET(ports.radio2, &allfds);
  }

  if (keyer_model != MODEL_CK && keyer_model != MODEL_SM) {
    ports.fsk1 = newpty();
    printf("FSK 1: %s\n", (char *)ptsname(ports.fsk1));
    FD_SET(ports.fsk1, &allfds);
  }

  if (keyer_model == MODEL_MK2R || keyer_model == MODEL_MK2RPLUS || keyer_model == MODEL_U2R) {
    ports.fsk2 = newpty();
    printf("FSK 2: %s\n", (char *)ptsname(ports.fsk1));
    FD_SET(ports.fsk2, &allfds);
  }

  if (keyer_model != MODEL_DK && keyer_model != MODEL_SM) {
    ports.winkey = newpty();
    printf("Winkey: %s\n", (char *)ptsname(ports.winkey));
    FD_SET(ports.winkey, &allfds);
  }

  if (keyer_model != MODEL_SM) {
    ports.keyboard = newpty();
    printf("Keyboard: %s\n", (char *)ptsname(ports.keyboard));
    FD_SET(ports.keyboard, &allfds);
  }

  // Mux and demux until exit
  int framepos = 0; // Current position in input frame from device
  unsigned char controlendbyte = 0x00; // Byte that will finish the current control command
  fd_set eoffds; // File descriptors that are not waited for by select
  FD_ZERO(&eoffds);
  while (1) { // TODO: Fix loop condition
    fd_set fds; // File descriptors that are waited for by select
    struct timeval tv;
    int numready = -1; // Number of ready ports
    int frames;
    frame_t frame; // Current frame read from device
    sequence_t sequence; // Next sequence to output to device
    unsigned char data; // Data read from pty

    // Set up fds to all except those that return EOF or other error
    fds = allfds;
    if (FD_ISSET(ports.keyer, &eoffds))
      FD_CLR(ports.keyer, &fds);
    if (FD_ISSET(ports.control, &eoffds))
      FD_CLR(ports.control, &fds);
    if (FD_ISSET(ports.radio1, &eoffds))
      FD_CLR(ports.radio1, &fds);
    if (FD_ISSET(ports.radio2, &eoffds))
      FD_CLR(ports.radio2, &fds);
    if (FD_ISSET(ports.fsk1, &eoffds))
      FD_CLR(ports.fsk1, &fds);
    if (FD_ISSET(ports.fsk2, &eoffds))
      FD_CLR(ports.fsk2, &fds);
    if (FD_ISSET(ports.winkey, &eoffds))
      FD_CLR(ports.winkey, &fds);
    if (FD_ISSET(ports.keyboard, &eoffds))
      FD_CLR(ports.keyboard, &fds);

    // Wait for input from device or ptys
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // If we wait too long, reopened PTYs are not read
    while (numready < 0) {
      numready = select(20, &fds, NULL, NULL, &tv); // NOTE: 20 is more than enough
      if (numready == -1 && errno != EINTR) {
	perror("Error selecting input");
	exit(1);
      }
    }
    debugprintf(12, "Number of ready fds: %i.\n", numready);

    // Check if there is new input from keyer
    if (FD_ISSET(ports.keyer, &fds)) {
      if (read(ports.keyer, &frame[framepos], 1) == 1)
	framepos++;
      debugprintf(6, "Read 1 byte from device: %02x, new framepos=%i.\n", frame[framepos - 1], framepos);
      if (framepos >= 4) {
	framepos = 0;
	debugprintf(4, "Decoding frame.\n");
	decode_frame(frame, &ports);
      }
      numready--;
    }

    // Construct and populate sequence
    sequence_init(sequence);
    if (read(ports.control, &data, 1) == 1) {
      // NOTE: This does NOT implement the protocol correctly. This implementation
      //       doesn't allow the end byte to occur within the command string. This
      //       should be possible, as such bytes are legal and may occur.
      // TODO: Add some out-of-band signalling to indicate start and end of a command
      debugprintf(6, "Input from control: %02x ('%c')\n", data, data);
      if (!controlendbyte) { // If 0x00, this is the start of a new command
	debugprintf(7, "Start of new command\n");
	sequence_set_control(sequence, data, 0);
	if (data) // 0x00 NOP is a single byte command
	  controlendbyte = data | 0x80;
      }
      else if (controlendbyte == data) { // End of a command
	debugprintf(7, "End of command\n");
	sequence_set_control(sequence, data, 0);
	controlendbyte = 0x00;
      }
      else
	sequence_set_control(sequence, data, 1);
      FD_CLR(ports.control, &eoffds);
    }
    else if (FD_ISSET(ports.control, &fds)) {
      debugprintf(7, "EOF or error from control. Removing from select fds.\n");
      FD_SET(ports.control, &eoffds);
    }
    // TODO: Read up to 5 bytes from radio1 and radio2 in order to fill up sequence
    if (read(ports.radio1, &data, 1) == 1) {
      debugprintf(6, "Input from radio1: %02x ('%c')\n", data, data);
      sequence_set_radio(sequence, 1, 0, data);
      FD_CLR(ports.radio1, &eoffds);
    }
    else if (FD_ISSET(ports.radio1, &fds)) {
      debugprintf(7, "EOF or error from radio1. Removing from select fds.\n");
      FD_SET(ports.radio1, &eoffds);
    }
    if (read(ports.radio2, &data, 1) == 1) {
      debugprintf(6, "Input from radio2: %02x ('%c')\n", data, data);
      sequence_set_radio(sequence, 2, 0, data);
      FD_CLR(ports.radio2, &eoffds);
    }
    else if (FD_ISSET(ports.radio2, &fds)) {
      debugprintf(7, "EOF or error from radio2. Removing from select fds.\n");
      FD_SET(ports.radio2, &eoffds);
    }
    if (read(ports.fsk1, &data, 1) == 1) {
      debugprintf(6, "Input from fsk1: %02x ('%c')\n", data, data);
      sequence_set_fsk(sequence, 1, data);
      FD_CLR(ports.fsk1, &eoffds);
    }
    else if (FD_ISSET(ports.fsk1, &fds)) {
      debugprintf(7, "EOF or error from fsk1. Removing from select fds.\n");
      FD_SET(ports.fsk1, &eoffds);
    }
    if (read(ports.fsk2, &data, 1) == 1) {
      debugprintf(6, "Input from fsk2: %02x ('%c')\n", data, data);
      sequence_set_fsk(sequence, 2, data);
      FD_CLR(ports.fsk2, &eoffds);
    }
    else if (FD_ISSET(ports.fsk2, &fds)) {
      debugprintf(7, "EOF or error from fsk2. Removing from select fds.\n");
      FD_SET(ports.fsk2, &eoffds);
    }
    if (read(ports.winkey, &data, 1) == 1) {
      debugprintf(6, "Input from winkey: %02x ('%c')\n", data, data);
      sequence_set_winkey(sequence, data);
      FD_CLR(ports.winkey, &eoffds);
    }
    else if (FD_ISSET(ports.winkey, &fds)) {
      debugprintf(7, "EOF or error from winkey. Removing from select fds.\n");
      FD_SET(ports.winkey, &eoffds);
    }
    
    if (frames_in_sequence(sequence)) {
      // Set flags whenever sending a sequence
      sequence_set_rts(sequence, 1); // TODO: Actually check RTS value on radio1
      sequence_set_rts(sequence, 2); // TODO: Actually check RTS value on radio2
      // TODO: Set ptt1 and ptt2 flags
      // TODO: Set cw1 and cw2 flags
      send_sequence(ports.keyer, sequence);
    }
  }
  
  tcsetattr(ports.keyer,TCSADRAIN,&oldtio);
  return 0;
}
