/*** =========== includes ===========***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>


/*** =========== defines ===========***/

// Mirrors what Ctrl does to a key: strips bits 5 and 6, mapping
// e.g. Ctrl-Q (0x71) -> 0x11. Used to detect Ctrl-key combos.
#define CTRL_KEY(k) ((k) & 0x1f)

#define RUNE_VERSION "0.0.1"


enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};


/*** =========== data ===========***/

// Stores line of text as a pointer to char data and a length
typedef struct erow {
    int size;
    char *chars;
} erow;


// Stores the terminal's original attributes
struct editorConfig {
    int cx, cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
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


int editorReadKey() {
    int nread;  // number of bytes read
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // EAGAIN is returned on timeout (no input) by some systems; only a real error should kill us
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }

        } else if (seq[0] == '0') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else {
        return c;
    }
    
}

// Queries the terminal for the current cursor position, and parses the response into rows and cols
int getCursorPosition(int *rows, int *cols) {
    char buff[32];   // buffer to store the terminal's response string
    unsigned int i = 0;


    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;     // sends escape sequence for the cursor's pos

    while (i < sizeof(buff) -1) {
        if (read(STDIN_FILENO, &buff[i], 1) != 1) break;        // read 1 char into buff[i]
        if (buff[i] == 'R') break;                              // if hit 'R', sotp!
        i++;                                                    // move to the next slot
    }

    buff[i] = '\0';

    if (buff[0] != '\x1b' || buff[1] != '[') return -1;         // check for escape sequence
    // Slices off the '\x1b[' wrapper, reads the numbers, and saves them into rows and cols variables
    if (sscanf(&buff[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}


// Gets the size of the terminal window in rows and columns, and saves them into the provided pointers
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
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


/*** =========== row operations =========== ***/


void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}



/*** =========== file i/o =========== ***/


void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");        // opening a file in read only
    if (!fp) die("fopen");                  // if failed to open file

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);

    }
    
    free(line);
    fclose(fp);
}




/*** =========== append buffer =========== ***/


struct abuff {
    char *b;
    int len;
};

#define ABUFF_INIT {NULL, 0}    // constructor for abuff


// Appends a string of given length to the end of the abuff
void abAppend(struct abuff *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    // 
    if (new == NULL) return;
    // copy the new string to the end of the existing buffer
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;

}

// Deallocates the abuff
void abFree(struct abuff *ab) {
    free(ab->b);
}





/*** =========== output =========== ***/


void editorScroll() {
    // check if the cursor is above the visible window, if so scrolls up to where the cursor is
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    // checks if the cursor is past the bottom of the visible window
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;

    }
}



void editorDrawRows(struct abuff *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        // if the current row is beyond the number of rows in the file, draw a tilde
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welconelen = snprintf(welcome, sizeof(welcome),
            "Rune editor -- version %s", RUNE_VERSION);
            
                if (welconelen > E.screencols) welconelen = E.screencols;

                // centering the welcome message
                int padding = (E.screencols - welconelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welconelen);

            } else {
                // tilde for each row, like vim does for empty lines
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;     // truncate the line if it's longer than the screen width
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);                 // append the line of text to the buffer
        }
        

        // K command erases part of the current line
        abAppend(ab, "\x1b[K", 3);     // clear the rest of the line after the tilde
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);   // move to the next line
        }
    }
}


void editorRefreshScreen() {
    editorScroll();

    struct abuff ab = ABUFF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);      // hides the cursor while we draw the screen
    // abAppend(&ab, "\x1b[2J", 4);        // clears the entire screen
    abAppend(&ab, "\x1b[H", 3);         // reposition cursor to top-left

    editorDrawRows(&ab);                // draw the rows of tildes

    char buff[32];
    snprintf(buff, sizeof(buff), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, E.cx + 1);
    abAppend(&ab, buff, strlen(buff));

    abAppend(&ab, "\x1b[?25h", 6);      // shows the cursor again

    write(STDOUT_FILENO, ab.b, ab.len); // write the entire contents of the abuff to the terminal at once 
    abFree(&ab);
}  


/*** =========== input =========== ***/


void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            E.cx++;    
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            
            break;

    }
}




void editorProcessKeypress() {
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            // clear screen before quitting so we don't leave garbage behind
            write(STDOUT_FILENO, "\x1b[2J", 4);   
            write(STDOUT_FILENO, "\x1b[H", 3);

            exit(0);
            break;


        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;


        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}





/*** =========== init =========== ***/


void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.numrows = 0;
    E.coloff = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}



int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
