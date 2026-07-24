#pragma once

#define NCCS 32

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;

struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t c_line;
  cc_t c_cc[NCCS];
};

#define BRKINT  0x0002
#define ICRNL   0x0100
#define INPCK   0x0010
#define ISTRIP  0x0020
#define IXON    0x0400
#define OPOST   0x0001
#define CS8     0x0030
#define ECHO    0x0008
#define ICANON  0x0002
#define IEXTEN  0x8000
#define ISIG    0x0001

#define VMIN    6
#define VTIME   5

#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
