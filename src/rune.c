/*** =========== includes ===========***/
#include <asm-generic/errno-base.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>


/*** =========== defines ===========***/

// Mirrors what Ctrl does to a key: strips bits 5 and 6, mapping
// e.g. Ctrl-Q (0x71) -> 0x11. Used to detect Ctrl-key combos.
#define CTRL_KEY(k) ((k) & 0x1f)


/*** =========== data ===========***/

// Stores the terminal's original attributes
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;


/*** =========== terminal =========== ***/

void die(const char *message) {
    // clears the screen and reposition the cursor on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);   
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(message);   // prints message + reason from errno
    exit(1);
}

void disableRawMode() {
    // restore the terminal to whatever state it was in before we touched it
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}


void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");  // read current attributes into struct raw
    atexit(disableRawMode);     // disable raw mode at exit

    struct termios raw = E.orig_termios;

    // input flags: turn off break/CR-to-NL translation/parity check/strip 8th bit/sw flow control
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);     // turn off output processing (e.g. \n -> \r\n translation)
    raw.c_cflag |= (CS8);        // set character size to 8 bits per byte
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);     // disable echo and canonical mode (read input byte-by-byte)
    raw.c_cc[VMIN] = 0;    // read() returns as soon as there is any input
    raw.c_cc[VTIME] = 1;   // max wait time for read() before returning, in tenths of a second

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");   // apply changes, discard unread inputs
}


char editorReadKey() {
    int nread;  // number of bytes read
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // EAGAIN is returned on timeout (no input) by some systems; only a real error should kill us
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}


int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;


    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    buf[i] = '\0';

    printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);

    editorReadKey();

    return -1;
}



int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // fallback method: move cursor to bottom-right and query position
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}



/*** =========== output =========== ***/

void editorDrawRows() {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);   // tilde for each row, like vim does for empty lines
    }
}


void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);   // clears the entire screen
    write(STDOUT_FILENO, "\x1b[H", 3);    // reposition cursor to top-left

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);    // move cursor back to top-left after drawing
}  


/*** =========== input =========== ***/

void editorProcessKeypress() {
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            // clear screen before quitting so we don't leave garbage behind
            write(STDOUT_FILENO, "\x1b[2J", 4);   
            write(STDOUT_FILENO, "\x1b[H", 3);

            exit(0);
            break;
    }
}





/*** =========== init =========== ***/


void initEditor() {
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}



int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}