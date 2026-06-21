/*** =========== includes ===========***/
#include <asm-generic/errno-base.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>


/*** =========== defines ===========***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** =========== data ===========***/

struct termios orig_termios;

/*** =========== terminal =========== ***/

void die(const char *message) {
    // clears the screen and reposition the cursor on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);   
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(message);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}


void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");  // read current attributes into struct raw
    atexit(disableRawMode);     // disable raw mode at exit

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);     // disable echo and canonical mode (read input byte-by-byte)
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");   // apply changes, discard unread inputs
}


char editorReadKey() {
    int nread;  // number of bytes read
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        //
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}



/*** =========== output =========== ***/

void editorDrawRows() {
    int y;
    for (y = 0; y < 24; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}


void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);   // clears the entire screen
    write(STDOUT_FILENO, "\x1b[H", 3);    // reposition cursor to top-left

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}  


/*** =========== input =========== ***/

void editorProcessKeypress() {
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);   
            write(STDOUT_FILENO, "\x1b[H", 3);

            exit(0);
            break;
    }
}





/*** =========== init =========== ***/

int main() {
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}