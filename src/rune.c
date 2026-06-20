#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);  // read current attributes into struct raw
    atexit(disableRawMode);     // disable raw mode at exit

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);     // disable echo and canonical mode (read input byte-by-byte)

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);   // apply changes, discard unread inputs
}


int main() {
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        // check for control characters (ASCII code: 0-31)
        if (iscntrl(c)) {
            printf("%d\n", c);
        } 
        else {
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}