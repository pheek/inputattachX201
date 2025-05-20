/**
 * date  : 2025 - 05 - 19
 * author: ph@freimann.eu (the pheek) + chatGPT (openai.com)
 * license: gnu public license: https://www.gnu.org/licenses/#GPL
 *
 * inputattachX201
 * ---------------
 *
 * src: https://github.com/pheek/inputattachX201
 *
 * Minimal inputattach clone with uinput support for internal Wacom
 * tablet on the lenovo X201 Tablet.
 * Adapted specifically for Lenovo ThinkPad X201 Tablet from the year 2005
 * [WACf004(?) on ttyS4].
 */


/* INCLUDES */
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h> // to handle interrupt (sigint)

#define DEVICE_IN   "/dev/ttyS4"
#define DEVICE_OUT  "/dev/uinput"
#define DEVICE_NAME "Wacom Serial Tablet"
#define BAUDRATE B19200


/**
 * Opens the input device; this is where to get the "bits" from the stylus.
 * 
 */
int setup_serial(int fd) {
  struct termios options;
  if (tcgetattr(fd, &options) != 0) {
    perror("tcgetattr");
    return -1;
  }
  cfsetispeed(&options, BAUDRATE);
  cfsetospeed(&options, BAUDRATE);
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_cflag &= ~CSIZE          ;
  options.c_cflag |=  CS8            ; // 8 data bits
  options.c_cflag &= ~PARENB         ; // No parity
  options.c_cflag &= ~CSTOPB         ; // 1 stop bit
  options.c_iflag  =  IGNPAR         ;
  options.c_oflag  =  0              ;
  options.c_lflag  =  0              ; // Non-canonical mode
  tcflush(fd, TCIFLUSH);
  if (tcsetattr(fd, TCSANOW, &options) != 0) {
    perror("tcsetattr");
    return -1;
  }
  return 0;
}


/**
 * Setup the uinput, which is the input for X11.
 * From the perspective of this programm, the uinput is the output device.
 */
int setup_uinput(int *uifd) {
  struct uinput_user_dev uidev;
  struct uinput_abs_setup abs = {0};
  
  *uifd = open(DEVICE_OUT, O_WRONLY | O_NONBLOCK);
  if (*uifd < 0) {
    perror("open DEVICE_OUT (default /dev/uinput)");
    return -1;
  }
  
  // Basic device and version information
  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, DEVICE_NAME);
  uidev.id.bustype = BUS_RS232;
  uidev.id.vendor  = 0x056a   ;
  uidev.id.product = 0x00f4   ;
  uidev.id.version = 0x0100   ;
  
  // Enable event types
  ioctl(*uifd, UI_SET_EVBIT , EV_KEY);
  ioctl(*uifd, UI_SET_EVBIT , EV_ABS);
  ioctl(*uifd, UI_SET_EVBIT , EV_SYN);
  
  // Enable key codes
  ioctl(*uifd, UI_SET_KEYBIT, BTN_TOOL_PEN   );
  ioctl(*uifd, UI_SET_KEYBIT, BTN_TOOL_RUBBER);
  ioctl(*uifd, UI_SET_KEYBIT, BTN_TOUCH      );
  ioctl(*uifd, UI_SET_KEYBIT, BTN_STYLUS     );
  
  // Enable abs axes
  ioctl(*uifd, UI_SET_ABSBIT, ABS_X       );
  ioctl(*uifd, UI_SET_ABSBIT, ABS_Y       );
  ioctl(*uifd, UI_SET_ABSBIT, ABS_PRESSURE);
  
  // set bounds
  uidev.absmin [ABS_X] =    0;
  uidev.absmax [ABS_X] = 6578;
  uidev.absfuzz[ABS_X] =    0;
  uidev.absmin [ABS_Y] =    0;
  uidev.absmax [ABS_Y] = 4095;
  uidev.absfuzz[ABS_Y] =    0;
  uidev.absmin[ABS_PRESSURE] =   0;
  uidev.absmax[ABS_PRESSURE] = 255;
  
  // Write the created device info to uinput
  write(*uifd, &uidev, sizeof(uidev));
  ioctl(*uifd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
  ioctl(*uifd, UI_DEV_CREATE);
  
  return 0;
}

/**
 * Emit an event to the output device.
 */
void emit(int fd, int type, int code, int val) {
  struct input_event ie;
  ie.type  = type;
  ie.code  = code;
  ie.value = val;
  gettimeofday(&ie.time, NULL);
  write(fd, &ie, sizeof(ie));
}


/** Nr of bytes to read per chunk */
#define NR_OF_BYTES  8 // byte Nr 7 und Nr 8 (8. and 9. byte are always 0)

/**
 * check if the byte is the start of a pen chunk.
 */
int isStartByte(unsigned char startByte){
  if(startByte >= 0xA0 && startByte <= 0xA5) return 1;
  if(startByte == 0x80) return 1;
  return 0;
}// end method isStartByte


/**
 * decode a package from the stylus
 
  Protocol (reverse engineered, missing meaning of parts of byte 6):

  Byte 0: 0x80 : pen connection lost
          0xA0 : stylus hover
	  0xA1 : stylus touch screen
	  0xA2 : button press
	  0xA3 : button press + touch screen
	  0xA4 : eraser
	  0xA5 : eraser touch screen
  Byte 1,2 : x-coordinate (absolute value)
  Byte 3,4 : y-coordinate (absolute value)
  Byte 5   : pressure (only if touch)
  Byte 6   : bit0     : touching, but harder (tip + rubber)
                        both work and have this bit set, when pressing
			harder.
             bit1=bit2: always 0
	     bit3-bit6: "some random (unknown) values
	     bit7     : always 0
  Byte 7,8 : always 0
*/
void decodePackage(unsigned char * buf,
		   int* x       , int* y       ,
		   int* pressure, int* touching,
		   int* button1 , int* hardpress ,
		   int* rubber  , int* stylus   ) {
  
  *x = ((buf[1] & 0x7F) << 7) | (buf[2] & 0x7F);
  *y = ((buf[3] & 0x7F) << 7) | (buf[4] & 0x7F);
  
  *touching = (buf[0] & 0x01) ? 1 : 0;
  *button1  = (buf[0] & 0x02) ? 1 : 0;
  *hardpress  = (buf[6] & 0x01) ? 1 : 0; // button 2 is the hard press to the screen
  *rubber   = (buf[0] & 0x04) ? 1 : 0;
  *stylus   = (1 == *rubber)  ? 0 : 1;

  *pressure = buf[5];
  if(   1 == *hardpress) {*pressure = *pressure + 127;} 
  if(0x80 == buf[0]    ) {*stylus = 0; *rubber = 0;   } // on proximity lost: remove tool
}// end decodePackage


/**
 * Emit events to the X11-Input device
 */
void emitEvents(int uifd, int stylus, int rubber, int button1, int button2, int x, int y, int pressure, int touching) {

  emit(uifd, EV_KEY, BTN_TOOL_PEN   , stylus      );
  emit(uifd, EV_KEY, BTN_TOOL_RUBBER, rubber      );
  
  emit(uifd, EV_KEY, BTN_STYLUS     , button1     );
  emit(uifd, EV_KEY, BTN_STYLUS2    , button2     );
  
  emit(uifd, EV_ABS, ABS_X          , x           );
  emit(uifd, EV_ABS, ABS_Y          , y           );
  emit(uifd, EV_ABS, ABS_PRESSURE   , pressure    );
  emit(uifd, EV_KEY, BTN_TOUCH      , touching    );
  emit(uifd, EV_SYN, SYN_REPORT     , 0           );
}


/**
 * Handle signal interrupt.
 * As long as "running" is true, handle the wacom tablet.
 */
int running = 1; 
void handle_sigint(int sig) {running = 0 /* false */;}

int main() {
  int serial = open(DEVICE_IN, O_RDONLY | O_NOCTTY);
  if (serial < 0) {
    perror("open tty");
    return 1;
  }
  if (setup_serial(serial) != 0) return 1;
  
  int uifd;
  if (setup_uinput(&uifd) != 0) return 1;
  
  unsigned char buf[NR_OF_BYTES + 1];
  
  while (running) {
    // Look for start of packet (0xA0 or similar)
    unsigned char ch = 0;
    do {
      if (read(serial, &ch, 1) != 1) continue;
    } while (! isStartByte(ch)); // omit non start-chunk bytes
    
    buf[0] = ch;
    // Read remaining bytes
    int i = 1;
    while (i < NR_OF_BYTES) {
      int rd = read(serial, &buf[i], NR_OF_BYTES - i);
      if (rd > 0) i += rd;
    }
    
    // debug (all except x and y, because they are good)
    // Decode package
    int x, y, pressure, touching, button1, button2, rubber, stylus;
    decodePackage(buf, &x, &y, &pressure, &touching, &button1, &button2, &rubber, &stylus);

    emitEvents(uifd, stylus, rubber, button1, button2, x, y, pressure, touching);
    
  } // end while "running"
  ioctl(uifd, UI_DEV_DESTROY);
  close(uifd);
  close(serial);
  return 0;
} // end main()
