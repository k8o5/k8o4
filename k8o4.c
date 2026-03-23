// cc k8o4.c -o k8o4 -Wall -Wextra -pedantic -std=c99
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* --- DEFINES & COLORS --- */
#define TAB_STOP 4
#define QUIT_TIMES 2
#define MAX_UNDO 1000
#define PL_RIGHT_ARROW "\uE0B0"

#define COLOR_BG               "\x1b[48;2;30;30;30m"
#define COLOR_FG               "\x1b[38;2;212;212;212m"
#define COLOR_TITLE_BG         "\x1b[48;2;24;24;24m"
#define COLOR_TITLE_FG         "\x1b[38;2;200;200;200m"
#define COLOR_LINENO           "\x1b[38;2;133;133;133m"
#define COLOR_LINENO_CURRENT   "\x1b[38;2;198;198;198m"
#define COLOR_STATUS_FG        "\x1b[38;2;255;255;255m"
#define COLOR_STATUS_ALT_BG    "\x1b[48;2;60;60;60m"
#define COLOR_SELECTION_BG     "\x1b[48;2;38;79;120m" 
#define COLOR_KEYWORD1         "\x1b[38;2;86;156;214m"
#define COLOR_KEYWORD2         "\x1b[38;2;197;134;192m"
#define COLOR_STRING           "\x1b[38;2;206;145;120m"
#define COLOR_COMMENT          "\x1b[38;2;106;153;85m"
#define COLOR_NUMBER           "\x1b[38;2;181;206;168m"
#define COLOR_MATCH            "\x1b[48;2;80;80;0m"
#define COLOR_RESET            "\x1b[0m"

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/* --- ENUMS --- */
enum editorKey {
    BACKSPACE = 127, ARROW_LEFT = 1000, ARROW_RIGHT, ARROW_UP, ARROW_DOWN,
    DEL_KEY, HOME_KEY, END_KEY, PAGE_UP, PAGE_DOWN,
    CTRL_ARROW_LEFT, CTRL_ARROW_RIGHT, CTRL_ARROW_UP, CTRL_ARROW_DOWN,
    SHIFT_ARROW_LEFT, SHIFT_ARROW_RIGHT, SHIFT_ARROW_UP, SHIFT_ARROW_DOWN,
    SHIFT_HOME_KEY, SHIFT_END_KEY, CTRL_SHIFT_ARROW_LEFT, CTRL_SHIFT_ARROW_RIGHT,
    BRACKETED_PASTE_START, BRACKETED_PASTE_END
};

enum editorHighlight { 
    HL_NORMAL = 0, HL_COMMENT, HL_MLCOMMENT, HL_KEYWORD1, HL_KEYWORD2, 
    HL_STRING, HL_NUMBER, HL_MATCH 
};

enum undoType { 
    UNDO_INSERT_CHAR, UNDO_DELETE_CHAR, UNDO_INSERT_NEWLINE, 
    UNDO_DELETE_NEWLINE, UNDO_DELETE_SELECTION, UNDO_PASTE 
};

/* --- STRUCTURES --- */
struct editorSyntax {
    char *filetype; char **filematch; char **keywords;
    char *singleline_comment_start; char *multiline_comment_start;
    char *multiline_comment_end; int flags;
};

typedef struct erow {
    int idx, size, rsize;
    char *chars, *render;
    unsigned char *hl;
    int hl_open_comment;
} erow;

typedef struct undoState {
    enum undoType type;
    int cy, cx, prev_cy, prev_cx;
    char c, *text;
    int text_len, sel_start_cy, sel_start_cx, sel_end_cy, sel_end_cx;
    char **lines; int num_lines;
    struct undoState *prev, *next;
} undoState;

struct editorConfig {
    int cx, cy, rx, rowoff, coloff, screenrows, screencols, numrows;
    erow *row; int dirty; char *filename; char statusmsg[80]; time_t statusmsg_time;
    struct editorSyntax *syntax; struct termios orig_termios;
    int sidebar_visible, selection_active;
    int sel_start_cy, sel_start_cx, sel_end_cy, sel_end_cx;
    undoState *undo_head, *undo_current;
    int undo_count, in_undo;
};

struct editorConfig E;

/* --- EXHAUSTIVE PROTOTYPES --- */
int editorReadKey(void);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorClearSelection(void);
void editorNormalizeSelection(void);
void editorDeleteSelection(void);
void editorDeleteRegion(int scy, int scx, int ecy, int ecx);
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);
void editorUpdateRow(erow *row);
void editorUpdateSyntax(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorDelRow(int at);
void editorOpen(char *filename);
void editorSave(void);
void editorFind(void);
void editorSelectSyntaxHighlight(void);
void editorPasteText(const char *text);
int editorRowRxToCx(erow *row, int rx);
int editorRowCxToRx(erow *row, int cx);
int getWindowSize(int *rows, int *cols);
char *getClipboardText(void);
void undoPush(enum undoType type, int cy, int cx, char c, char *text, int text_len);
void undoPushSelectionDeletion(void);
void undoFreeState(undoState *state);
void editorUndo(void);
void editorRedo(void);
int is_separator(int c);
const char *editorSyntaxToAnsiColor(int hl);

/* --- SYNTAX DATABASE --- */
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "short|", "auto|", "const|", "extern|", "register|", "volatile|",
    NULL
};
struct editorSyntax HLDB[] = {
    { "c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS }
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* --- APPEND BUFFER --- */
struct abuf { char *b; int len; };
#define ABUF_INIT {NULL, 0}
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) {
        return;
    }
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
#define AB_STR(ab, s) abAppend(ab, s, (int)strlen(s))
void abFree(struct abuf *ab) { 
    free(ab->b); 
}

/* --- TERMINAL --- */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[?2004l", 11);
    perror(s); 
    exit(1);
}

void disableRawMode(void) {
    write(STDOUT_FILENO, "\x1b[?2004l", 8);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
}

void enableRawMode(void) {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0; 
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, "\x1b[?2004h", 8); 
}

int editorReadKey(void) {
    int nread;
    char c;
    while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1) { 
        if (nread == -1 && errno != EAGAIN) {
            die("read"); 
        }
    }
    if (c == '\x1b') {
        char seq[5];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': case '7': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': case '8': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                } else if (seq[2] == ';') {
                    if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
                    if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
                    if (seq[1] == '1' && seq[3] == '2') {
                        switch (seq[4]) {
                            case 'H': return SHIFT_HOME_KEY; case 'F': return SHIFT_END_KEY;
                            case 'A': return SHIFT_ARROW_UP; case 'B': return SHIFT_ARROW_DOWN;
                            case 'C': return SHIFT_ARROW_RIGHT; case 'D': return SHIFT_ARROW_LEFT;
                        }
                    } else if (seq[1] == '1' && seq[3] == '5') {
                        switch (seq[4]) {
                            case 'A': return CTRL_ARROW_UP; case 'B': return CTRL_ARROW_DOWN;
                            case 'C': return CTRL_ARROW_RIGHT; case 'D': return CTRL_ARROW_LEFT;
                        }
                    } else if (seq[1] == '1' && seq[3] == '6') {
                        switch (seq[4]) {
                            case 'C': return CTRL_SHIFT_ARROW_RIGHT;
                            case 'D': return CTRL_SHIFT_ARROW_LEFT;
                        }
                    }
                } else if (seq[1] == '2' && seq[2] == '0') {
                    if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
                    if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
                    if (seq[3] == '0' && seq[4] == '~') return BRACKETED_PASTE_START;
                    if (seq[3] == '1' && seq[4] == '~') return BRACKETED_PASTE_END;
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP; case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT; case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY; case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY; case 'F': return END_KEY;
                case 'c': return CTRL_ARROW_RIGHT; case 'd': return CTRL_ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return c;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    }
    *cols = ws.ws_col; *rows = ws.ws_row;
    return 0;
}

/* --- LOGIC & HELPERS --- */
int is_separator(int c) { 
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL; 
}

const char *editorSyntaxToAnsiColor(int hl) {
    switch (hl) {
        case HL_COMMENT: case HL_MLCOMMENT: return COLOR_COMMENT;
        case HL_KEYWORD1: return COLOR_KEYWORD1;
        case HL_KEYWORD2: return COLOR_KEYWORD2;
        case HL_STRING: return COLOR_STRING;
        case HL_NUMBER: return COLOR_NUMBER;
        case HL_MATCH: return COLOR_MATCH;
        default: return COLOR_FG;
    }
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);
    if (E.syntax == NULL) {
        return;
    }
    char **kw = E.syntax->keywords;
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;
    int scs_len = scs ? (int)strlen(scs) : 0;
    int mcs_len = mcs ? (int)strlen(mcs) : 0;
    int mce_len = mce ? (int)strlen(mce) : 0;
    int prev_sep = 1, in_string = 0, in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment), i = 0;
    
    while (i < row->rsize) {
        char c = row->render[i];
        if (scs_len && !in_string && !in_comment && !strncmp(&row->render[i], scs, scs_len)) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i); 
            break;
        }
        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len; 
                    in_comment = 0; 
                    prev_sep = 1; 
                    continue;
                } else { 
                    i++; 
                    continue; 
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len; 
                in_comment = 1; 
                continue;
            }
        }
        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) { 
                    row->hl[i + 1] = HL_STRING; 
                    i += 2; 
                    continue; 
                }
                if (c == in_string) {
                    in_string = 0;
                }
                i++; 
                prev_sep = 1; 
                continue;
            } else if (c == '"' || c == '\'') {
                in_string = (int)c; 
                row->hl[i] = HL_STRING; 
                i++; 
                continue;
            }
        }
        if ((E.syntax->flags & HL_HIGHLIGHT_NUMBERS) && ((isdigit(c) && (prev_sep || (i > 0 && row->hl[i - 1] == HL_NUMBER))) || (c == '.' && i > 0 && row->hl[i - 1] == HL_NUMBER))) {
            row->hl[i] = HL_NUMBER; 
            i++; 
            prev_sep = 0; 
            continue;
        }
        if (prev_sep) {
            int j;
            for (j = 0; kw[j]; j++) {
                int klen = (int)strlen(kw[j]);
                int kw2 = kw[j][klen - 1] == '|'; 
                if (kw2) {
                    klen--;
                }
                if (!strncmp(&row->render[i], kw[j], klen) && is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen; 
                    break;
                }
            }
            if (kw[j] != NULL) { 
                prev_sep = 0; 
                continue; 
            }
        }
        prev_sep = is_separator(c); 
        i++;
    }
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numrows) {
        editorUpdateSyntax(&E.row[row->idx + 1]);
    }
}

void editorSelectSyntaxHighlight(void) {
    E.syntax = NULL; 
    if (E.filename == NULL) return; 
    char *ext = strrchr(E.filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        for (unsigned int i = 0; s->filematch[i]; i++) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;
                for (int filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }
                return;
            }
        }
    }
}

/* --- ROW OPERATIONS --- */
void editorUpdateRow(erow *row) {
    int tabs = 0;
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') tabs++;
    }
    free(row->render);
    row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1);
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' '; 
            while (idx % TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0'; 
    row->rsize = idx; 
    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    if (at < E.numrows) {
        memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    }
    for (int j = at + 1; j <= E.numrows; j++) {
        E.row[j].idx++;
    }
    E.row[at].idx = at; 
    E.row[at].size = (int)len; 
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len); 
    E.row[at].chars[len] = '\0';
    E.row[at].render = NULL; 
    E.row[at].hl = NULL; 
    E.row[at].rsize = 0; 
    E.row[at].hl_open_comment = 0;
    editorUpdateRow(&E.row[at]); 
    E.numrows++; 
    if (!E.in_undo) {
        E.dirty++;
    }
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    free(E.row[at].render); 
    free(E.row[at].chars); 
    free(E.row[at].hl);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++) {
        E.row[j].idx--;
    }
    E.numrows--; 
    if (!E.in_undo) {
        E.dirty++;
    }
}

void editorDeleteRegion(int scy, int scx, int ecy, int ecx) {
    if (scy == ecy) {
        int len = ecx - scx;
        memmove(&E.row[scy].chars[scx], &E.row[scy].chars[ecx], E.row[scy].size - ecx + 1);
        E.row[scy].size -= len; 
        editorUpdateRow(&E.row[scy]);
    } else {
        int end_len = E.row[ecy].size - ecx;
        E.row[scy].chars = realloc(E.row[scy].chars, scx + end_len + 1);
        memcpy(&E.row[scy].chars[scx], &E.row[ecy].chars[ecx], end_len);
        E.row[scy].size = scx + end_len; 
        E.row[scy].chars[E.row[scy].size] = '\0';
        for (int i = ecy; i > scy; i--) {
            editorDelRow(i);
        }
        editorUpdateRow(&E.row[scy]);
    }
    if (!E.in_undo) {
        E.dirty++;
    }
}

/* --- UNDO SYSTEM --- */
void undoFreeState(undoState *state) {
    if (state == NULL) {
        return;
    }
    free(state->text);
    if (state->lines != NULL) {
        for (int i = 0; i < state->num_lines; i++) {
            free(state->lines[i]);
        }
        free(state->lines);
    }
    free(state);
}

void undoAppendState(undoState *state) {
    if (E.undo_current != NULL) {
        undoState *next = E.undo_current->next;
        while (next != NULL) {
            undoState *tmp = next->next; 
            undoFreeState(next); 
            E.undo_count--; 
            next = tmp;
        }
        E.undo_current->next = state; 
        state->prev = E.undo_current;
    } else {
        E.undo_head = state;
    }
    E.undo_current = state; 
    E.undo_count++;
    while (E.undo_count > MAX_UNDO) {
        undoState *old = E.undo_head; 
        E.undo_head = old->next;
        if (E.undo_head) {
            E.undo_head->prev = NULL;
        }
        undoFreeState(old); 
        E.undo_count--;
    }
}

void undoPush(enum undoType type, int cy, int cx, char c, char *text, int text_len) {
    if (E.in_undo) {
        return;
    }
    undoState *st = calloc(1, sizeof(undoState));
    st->type = type; 
    st->cy = cy; 
    st->cx = cx; 
    st->prev_cy = E.cy; 
    st->prev_cx = E.cx; 
    st->c = c;
    if (text) {
        st->text = strndup(text, (size_t)text_len);
    }
    undoAppendState(st);
}

void undoPushSelectionDeletion(void) {
    if (E.in_undo || !E.selection_active) {
        return;
    }
    undoState *st = calloc(1, sizeof(undoState));
    st->type = UNDO_DELETE_SELECTION; 
    st->cy = st->sel_start_cy = E.sel_start_cy;
    st->cx = st->sel_start_cx = E.sel_start_cx; 
    st->sel_end_cy = E.sel_end_cy;
    st->sel_end_cx = E.sel_end_cx; 
    st->prev_cy = E.cy; 
    st->prev_cx = E.cx;
    int n = st->sel_end_cy - st->sel_start_cy + 1; 
    st->num_lines = n;
    st->lines = malloc(sizeof(char*) * n);
    if (n == 1) {
        st->lines[0] = strndup(&E.row[E.sel_start_cy].chars[E.sel_start_cx], (size_t)(E.sel_end_cx - E.sel_start_cx));
    } else {
        st->lines[0] = strdup(&E.row[E.sel_start_cy].chars[E.sel_start_cx]);
        for (int i = 1; i < n - 1; i++) {
            st->lines[i] = strdup(E.row[E.sel_start_cy + i].chars);
        }
        st->lines[n - 1] = strndup(E.row[E.sel_end_cy].chars, (size_t)E.sel_end_cx);
    }
    undoAppendState(st);
}

void editorUndo(void) {
    if (E.undo_current == NULL) {
        return;
    }
    E.in_undo = 1; 
    undoState *st = E.undo_current;
    if (st->type == UNDO_PASTE) {
        editorDeleteRegion(st->sel_start_cy, st->sel_start_cx, st->sel_end_cy, st->sel_end_cx);
        E.cy = st->sel_start_cy; 
        E.cx = st->sel_start_cx;
    } else if (st->type == UNDO_INSERT_CHAR) {
        E.cy = st->cy; 
        E.cx = st->cx;
        if (E.cy < E.numrows) {
            memmove(&E.row[E.cy].chars[E.cx], &E.row[E.cy].chars[E.cx+1], (size_t)(E.row[E.cy].size - E.cx));
            E.row[E.cy].size--; 
            editorUpdateRow(&E.row[E.cy]);
        }
    }
    E.undo_current = st->prev; 
    E.in_undo = 0; 
    editorSetStatusMessage("Undo");
}

void editorRedo(void) {
    if (E.undo_current == NULL && E.undo_head != NULL) {
        E.undo_current = E.undo_head;
    } else if (E.undo_current != NULL && E.undo_current->next != NULL) {
        E.undo_current = E.undo_current->next;
    } else {
        return;
    }
    E.in_undo = 1; 
    undoState *st = E.undo_current;
    if (st->type == UNDO_PASTE) { 
        E.cy = st->sel_start_cy; 
        E.cx = st->sel_start_cx; 
        editorPasteText(st->text); 
    }
    E.in_undo = 0; 
    editorSetStatusMessage("Redo");
}

/* --- TURBO PASTE ENGINE --- */
void editorPasteText(const char *text) {
    if (text == NULL || *text == '\0') return;
    if (E.selection_active) {
        editorDeleteSelection();
    }
    int scy = E.cy;
    int scx = E.cx;
    undoPush(UNDO_PASTE, scy, scx, 0, (char*)text, (int)strlen(text));

    struct editorSyntax *saved_syntax = E.syntax; 
    E.syntax = NULL; 
    E.in_undo = 1;
    const char *p = text;
    while (*p) {
        const char *nl = strpbrk(p, "\n\r"); 
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (E.cy >= E.numrows) {
            editorInsertRow(E.numrows, "", 0);
        }
        erow *row = &E.row[E.cy];
        row->chars = realloc(row->chars, row->size + len + 1);
        memmove(&row->chars[E.cx + len], &row->chars[E.cx], row->size - E.cx + 1);
        memcpy(&row->chars[E.cx], p, len); 
        row->size += (int)len; 
        E.cx += (int)len;
        if (nl == NULL) {
            break;
        }
        if (*nl == '\r' && *(nl+1) == '\n') {
            p = nl + 2; 
        } else {
            p = nl + 1;
        }
        editorInsertRow(E.cy + 1, &E.row[E.cy].chars[E.cx], (size_t)(E.row[E.cy].size - E.cx));
        E.row[E.cy].size = E.cx; 
        E.row[E.cy].chars[E.cx] = '\0'; 
        E.cy++; 
        E.cx = 0;
    }
    E.syntax = saved_syntax; 
    E.in_undo = 0;
    for (int i = scy; i <= E.cy && i < E.numrows; i++) {
        editorUpdateRow(&E.row[i]);
    }
    E.dirty++; 
    E.selection_active = 0;
}

char *getClipboardText(void) {
    FILE *fp = popen("pbpaste 2>/dev/null || xclip -o -sel clip 2>/dev/null || xsel -ob 2>/dev/null", "r");
    if (fp == NULL) {
        return NULL;
    }
    size_t cap = 16384;
    size_t len = 0;
    size_t tlen; 
    char *buf = malloc(cap);
    char tmp[4096];
    while (fgets(tmp, sizeof(tmp), fp)) {
        tlen = strlen(tmp);
        if (len + tlen + 1 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, tmp, tlen); 
        len += tlen;
    }
    buf[len] = '\0'; 
    pclose(fp);
    if (len == 0) { 
        free(buf); 
        return NULL; 
    }
    return buf;
}

/* --- EDITING --- */
void editorNormalizeSelection(void) {
    if (!E.selection_active) return;
    if (E.sel_end_cy < E.sel_start_cy || (E.sel_end_cy == E.sel_start_cy && E.sel_end_cx < E.sel_start_cx)) {
        int tcy = E.sel_start_cy;
        int tcx = E.sel_start_cx;
        E.sel_start_cy = E.sel_end_cy; 
        E.sel_start_cx = E.sel_end_cx;
        E.sel_end_cy = tcy; 
        E.sel_end_cx = tcx;
    }
}

void editorClearSelection(void) { 
    E.selection_active = 0; 
}

void editorDeleteSelection(void) {
    if (E.selection_active) {
        editorNormalizeSelection(); 
        undoPushSelectionDeletion();
        editorDeleteRegion(E.sel_start_cy, E.sel_start_cx, E.sel_end_cy, E.sel_end_cx);
        E.cy = E.sel_start_cy; 
        E.cx = E.sel_start_cx; 
        E.selection_active = 0;
    }
}

void editorInsertChar(int c) {
    if (E.selection_active) {
        editorDeleteSelection();
    }
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    undoPush(UNDO_INSERT_CHAR, E.cy, E.cx, (char)c, NULL, 0);
    erow *row = &E.row[E.cy]; 
    row->chars = realloc(row->chars, (size_t)(row->size + 2));
    memmove(&row->chars[E.cx + 1], &row->chars[E.cx], (size_t)(row->size - E.cx + 1));
    row->chars[E.cx] = (char)c; 
    row->size++; 
    editorUpdateRow(row);
    if (!E.in_undo) {
        E.dirty++;
    }
    E.cx++;
}

void editorInsertNewline(void) {
    if (E.selection_active) {
        editorDeleteSelection();
    }
    undoPush(UNDO_INSERT_NEWLINE, E.cy, E.cx, 0, NULL, 0);
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], (size_t)(row->size - E.cx));
        E.row[E.cy].size = E.cx; 
        E.row[E.cy].chars[E.cx] = '\0'; 
        editorUpdateRow(&E.row[E.cy]);
    }
    E.cy++; 
    E.cx = 0;
}

void editorDelChar(void) {
    if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
        return;
    }
    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        undoPush(UNDO_DELETE_CHAR, E.cy, E.cx - 1, row->chars[E.cx - 1], NULL, 0);
        memmove(&row->chars[E.cx - 1], &row->chars[E.cx], (size_t)(row->size - E.cx + 1));
        row->size--; 
        editorUpdateRow(row); 
        E.cx--;
    } else {
        undoPush(UNDO_DELETE_NEWLINE, E.cy - 1, E.row[E.cy - 1].size, 0, NULL, 0);
        E.cx = E.row[E.cy - 1].size; 
        erow *prev = &E.row[E.cy - 1];
        prev->chars = realloc(prev->chars, (size_t)(prev->size + row->size + 1));
        memcpy(&prev->chars[prev->size], row->chars, (size_t)(row->size + 1));
        prev->size += row->size; 
        editorUpdateRow(prev);
        editorDelRow(E.cy); 
        E.cy--;
    }
    if (!E.in_undo) {
        E.dirty++;
    }
}

/* --- RENDER --- */
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0; 
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') {
            rx += (TAB_STOP - 1) - (rx % TAB_STOP); 
        }
        rx++; 
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur = 0, cx; 
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') {
            cur += (TAB_STOP - 1) - (cur % TAB_STOP); 
        }
        cur++; 
        if (cur > rx) return cx; 
    }
    return cx;
}

void editorRefreshScreen(void) {
    int text_w = E.screencols - 6; 
    if (E.sidebar_visible) {
        text_w -= 25;
    }
    if (text_w < 10) {
        text_w = 10;
    }
    
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx); 
    } else {
        E.rx = 0;
    }
    
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + text_w) {
        E.coloff = E.rx - text_w + 1;
    }

    struct abuf ab = ABUF_INIT; 
    AB_STR(&ab, "\x1b[?25l\x1b[H");
    AB_STR(&ab, COLOR_TITLE_BG COLOR_TITLE_FG);
    
    char title[256]; 
    int tl = snprintf(title, sizeof(title), " k8o4 — %s ", E.filename ? E.filename : "Untitled");
    int pad = (E.screencols - tl) / 2; 
    for (int i = 0; i < pad; i++) {
        AB_STR(&ab, " ");
    }
    abAppend(&ab, title, tl); 
    AB_STR(&ab, "\x1b[K\r\n");
    
    editorNormalizeSelection();
    
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff; 
        AB_STR(&ab, COLOR_BG);
        if (filerow >= E.numrows) { 
            AB_STR(&ab, "\x1b[38;2;90;90;90m~"); 
        } else {
            char b[16]; 
            if (filerow == E.cy) { 
                AB_STR(&ab, COLOR_LINENO_CURRENT); 
            } else { 
                AB_STR(&ab, COLOR_LINENO); 
            }
            snprintf(b, sizeof(b), " %4d ", filerow + 1); 
            AB_STR(&ab, b);
            
            erow *r = &E.row[filerow]; 
            int len = r->rsize - E.coloff;
            if (len < 0) {
                len = 0;
            }
            if (len > text_w) {
                len = text_w;
            }
            
            const char* cur = COLOR_FG; 
            AB_STR(&ab, cur); 
            int isel = 0;
            
            for (int j = 0; j < len; j++) {
                int s = 0; 
                if (E.selection_active) {
                    int cx = editorRowRxToCx(r, E.coloff + j);
                    s = (filerow > E.sel_start_cy && filerow < E.sel_end_cy) || (filerow == E.sel_start_cy && filerow == E.sel_end_cy && cx >= E.sel_start_cx && cx < E.sel_end_cx) || (filerow == E.sel_start_cy && filerow < E.sel_end_cy && cx >= E.sel_start_cx) || (filerow == E.sel_end_cy && filerow > E.sel_start_cy && cx < E.sel_end_cx);
                }
                const char *col = editorSyntaxToAnsiColor(r->hl[E.coloff + j]);
                if (s && !isel) { 
                    AB_STR(&ab, COLOR_SELECTION_BG); 
                    AB_STR(&ab, col); 
                    isel = 1; 
                } else if (!s && isel) { 
                    AB_STR(&ab, COLOR_BG); 
                    AB_STR(&ab, col); 
                    isel = 0; 
                }
                if (strcmp(col, cur)) { 
                    cur = col; 
                    if (!isel) {
                        AB_STR(&ab, col); 
                    }
                }
                abAppend(&ab, &r->render[E.coloff + j], 1);
            }
        }
        AB_STR(&ab, COLOR_BG "\x1b[K\r\n");
    }
    
    if (E.sidebar_visible) {
        char sbuf[512]; 
        int sstart = 6 + text_w + 1;
        for (int y = 0; y < E.screenrows; y++) { 
            snprintf(sbuf, sizeof(sbuf), "\x1b[%d;%dH\x1b[48;2;25;25;25m\x1b[38;2;80;80;80m│\x1b[K", y + 2, sstart); 
            AB_STR(&ab, sbuf); 
        }
        DIR *d = opendir("."); 
        if (d) { 
            struct dirent *dir; 
            int sy = 1; 
            while ((dir = readdir(d)) != NULL && sy < E.screenrows - 1) { 
                if (dir->d_name[0] == '.') continue; 
                snprintf(sbuf, sizeof(sbuf), "\x1b[%d;%dH\x1b[38;2;180;180;180m %s", sy + 3, sstart + 1, dir->d_name); 
                AB_STR(&ab, sbuf); 
                sy++; 
            } 
            closedir(d); 
        }
    }
    
    AB_STR(&ab, "\x1b[48;2;0;122;204m\x1b[38;2;255;255;255m NORMAL \x1b[48;2;45;45;45m\x1b[38;2;0;122;204m" PL_RIGHT_ARROW "\x1b[38;2;212;212;212m ");
    char stat[128]; 
    snprintf(stat, sizeof(stat), "%s | %d:%d", E.filename ? E.filename : "Untitled", E.cy + 1, E.cx + 1);
    abAppend(&ab, stat, (int)strlen(stat)); 
    AB_STR(&ab, "\x1b[K\x1b[0m\x1b[H");
    
    if (E.statusmsg[0] && time(NULL) - E.statusmsg_time < 5) { 
        AB_STR(&ab, "\x1b[24;1H\x1b[K"); 
        abAppend(&ab, E.statusmsg, (int)strlen(E.statusmsg)); 
    }
    
    char cpos[32]; 
    snprintf(cpos, sizeof(cpos), "\x1b[%d;%dH", (E.cy - E.rowoff) + 2, (E.rx - E.coloff) + 7);
    AB_STR(&ab, cpos); 
    AB_STR(&ab, "\x1b[?25h"); 
    write(STDOUT_FILENO, ab.b, ab.len); 
    abFree(&ab);
}

/* --- PROMPT & FILE OPS --- */
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128, buflen = 0; 
    char *buf = malloc(bufsize); 
    buf[0] = '\0';
    while (1) {
        editorSetStatusMessage(prompt, buf); 
        editorRefreshScreen();
        int c = editorReadKey();
        if (c == DEL_KEY || c == 127 || c == BACKSPACE) {
            if (buflen != 0) {
                buf[--buflen] = '\0';
            }
        } else if (c == '\x1b') {
            editorSetStatusMessage(""); 
            if (callback) callback(buf, c); 
            free(buf); 
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) { 
                editorSetStatusMessage(""); 
                if (callback) callback(buf, c); 
                return buf; 
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = (char)c; 
            buf[buflen] = '\0';
        }
        if (callback) {
            callback(buf, c);
        }
    }
}

void editorOpen(char *filename) {
    free(E.filename); 
    E.filename = strdup(filename); 
    editorSelectSyntaxHighlight();
    FILE *fp = fopen(filename, "r"); 
    if (fp == NULL) return;
    char *line = NULL; 
    size_t linecap = 0; 
    ssize_t linelen; 
    E.in_undo = 1;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        editorInsertRow(E.numrows, line, (size_t)linelen);
    }
    free(line); 
    fclose(fp); 
    E.dirty = 0; 
    E.in_undo = 0;
}

void editorSave(void) {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save As: %s", NULL);
        if (E.filename == NULL) {
            return;
        }
        editorSelectSyntaxHighlight();
    }
    int len = 0; 
    for (int j = 0; j < E.numrows; j++) { 
        len += E.row[j].size + 1; 
    }
    char *buf = malloc((size_t)len);
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) { 
        memcpy(p, E.row[j].chars, (size_t)E.row[j].size); 
        p += E.row[j].size; 
        *p++ = '\n'; 
    }
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) { 
        if (ftruncate(fd, len) != -1) { 
            write(fd, buf, (size_t)len); 
        } 
        close(fd); 
    }
    free(buf); 
    E.dirty = 0; 
    editorSetStatusMessage("%d bytes written", len);
}

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;
    static int saved_hl_line; 
    static char *saved_hl = NULL;
    
    if (saved_hl) { 
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize); 
        free(saved_hl); 
        saved_hl = NULL; 
    }
    
    if (key == '\r' || key == '\x1b') { 
        last_match = -1; 
        direction = 1; 
        return; 
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    }
    
    if (last_match == -1) {
        direction = 1;
    }
    
    int current = last_match;
    for (int i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1) {
            current = E.numrows - 1; 
        } else if (current == E.numrows) {
            current = 0;
        }
        
        erow *row = &E.row[current]; 
        char *match = strstr(row->render, query);
        if (match) {
            last_match = current; 
            E.cy = current; 
            E.cx = editorRowRxToCx(row, (int)(match - row->render)); 
            E.rowoff = E.numrows;
            saved_hl_line = current; 
            saved_hl = malloc(row->rsize); 
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query)); 
            break;
        }
    }
}

void editorFind(void) {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;
    char *query = editorPrompt("Search: %s (ESC/Arrows/Enter)", editorFindCallback);
    if (query) {
        free(query);
    } else { 
        E.cx = saved_cx; 
        E.cy = saved_cy; 
        E.coloff = saved_coloff; 
        E.rowoff = saved_rowoff; 
    }
}

void editorSetStatusMessage(const char *fmt, ...) { 
    va_list ap; 
    va_start(ap, fmt); 
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap); 
    va_end(ap); 
    E.statusmsg_time = time(NULL); 
}

/* --- MAIN CONTROLLER --- */
void editorMoveCursor(int k) {
    erow *r = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    if (k == ARROW_LEFT) { 
        if (E.cx > 0) {
            E.cx--; 
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size; 
        }
    } else if (k == ARROW_RIGHT) { 
        if (r && E.cx < r->size) {
            E.cx++; 
        } else if (r && E.cx == r->size) { 
            E.cy++; 
            E.cx = 0; 
        } 
    } else if (k == ARROW_UP) { 
        if (E.cy > 0) E.cy--; 
    } else if (k == ARROW_DOWN) { 
        if (E.cy < E.numrows - 1) E.cy++; 
    }
    r = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; 
    if (r && E.cx > r->size) {
        E.cx = r->size;
    }
}

void editorProcessKey(void) {
    int c = editorReadKey();
    switch (c) {
        case '\r': 
            editorInsertNewline(); 
            break;
        case 24: 
            if (E.dirty) {
                editorSetStatusMessage("Unsaved changes!"); 
            } 
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); 
            exit(0); 
            break;
        case 1: 
            if (E.numrows > 0) { 
                E.selection_active = 1; 
                E.sel_start_cy = 0; 
                E.sel_start_cx = 0; 
                E.cy = E.numrows - 1; 
                E.cx = E.row[E.cy].size; 
                E.sel_end_cy = E.cy; 
                E.sel_end_cx = E.cx; 
            } 
            break;
        case 22: { 
            char *cl = getClipboardText(); 
            if (cl) {
                editorPasteText(cl); 
                free(cl); 
            }
            break; 
        }
        case BRACKETED_PASTE_START: {
            size_t cp = 16384, l = 0; 
            char *pb = malloc(cp), ch[4096]; 
            int nr;
            while ((nr = (int)read(STDIN_FILENO, ch, sizeof(ch))) > 0) {
                while (l + (size_t)nr + 1 >= cp) {
                    cp *= 2;
                    pb = realloc(pb, cp);
                }
                memcpy(pb + l, ch, (size_t)nr); 
                l += (size_t)nr; 
                pb[l] = '\0';
                if (l >= 6 && strstr(pb + (l < 1024 ? 0 : l - 1024), "\x1b[201~")) {
                    break;
                }
            }
            char *end = strstr(pb, "\x1b[201~"); 
            if (end) *end = '\0'; 
            editorPasteText(pb); 
            free(pb); 
            break;
        }
        case 26: 
            editorUndo(); 
            break; 
        case 25: 
            editorRedo(); 
            break; 
        case 19: 
            editorSave(); 
            break; 
        case 6: 
            editorFind(); 
            break;
        case 5: 
            E.sidebar_visible = !E.sidebar_visible; 
            break;
        case BACKSPACE: 
            if (E.selection_active) {
                editorDeleteSelection(); 
            } else {
                editorDelChar(); 
            }
            break;
        case ARROW_UP: case ARROW_DOWN: case ARROW_LEFT: case ARROW_RIGHT: 
            E.selection_active = 0; 
            editorMoveCursor(c); 
            break;
        case '\x1b': 
            E.selection_active = 0; 
            break;
        default: 
            if (!iscntrl(c)) {
                editorInsertChar(c); 
            }
            break;
    }
}

int main(int argc, char **argv) {
    enableRawMode(); 
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("win");
    }
    E.screenrows -= 3; 
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    while (1) { 
        editorRefreshScreen(); 
        editorProcessKey(); 
    }
    return 0;
}
