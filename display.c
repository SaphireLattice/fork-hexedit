/* hexedit -- Hexadecimal Editor for Binary Files
   Copyright (C) 1998 Pixel (Pascal Rigaux)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.*/
#include "hexedit.h"

int move_cursor(INT delta)
{
  return set_cursor(base + cursor + delta);
}

int set_cursor(INT loc)
{
  if (loc < 0 && base % lineLength)
    loc = 0;

  if (!tryloc(loc))
    return FALSE;

  if (loc < base) {
    if (loc - base % lineLength < 0)
      set_base(0);
    else if (!move_base(myfloor(loc - base % lineLength, lineLength) + base % lineLength - base))
      return FALSE;
    cursor = loc - base;
  } else if (loc >= base + page) {
    if (!move_base(myfloor(loc - base % lineLength, lineLength) + base % lineLength - page + lineLength - base))
      return FALSE;
    cursor = loc - base;
  } else if (loc > base + nbBytes) {
    return FALSE;
  } else
    cursor = loc - base;

  if (mark_set)
    updateMarked();

  return TRUE;
}

int move_base(INT delta)
{
  if (mode == bySector) {
    if (delta > 0 && delta < page)
      delta = page;
    else if (delta < 0 && delta > -page)
      delta = -page;
  }
  return set_base(base + delta);
}

int set_base(INT loc)
{
  if (loc < 0) loc = 0;

  if (!tryloc(loc)) return FALSE;
  base = loc;
  readFile();

  if (mode != bySector && nbBytes < page - lineLength && base != 0) {
    base -= myfloor(page - nbBytes - lineLength, lineLength);
    if (base < 0) base = 0;
    readFile();
  }

  if (cursor > nbBytes) cursor = nbBytes;
  return TRUE;
}


int computeLineSize(void) { return computeCursorXPos(lineLength - 1, 0) + 1; }
int computeCursorXCurrentPos(void) { return computeCursorXPos(cursor, hexOrAscii); }
int computeCursorXPos(int cursor, int hexOrAscii)
{
  int r = nAddrDigits + 3;
  int x = cursor % lineLength;
  int h = (hexOrAscii ? x : lineLength - 1);

  r += normalSpaces * (h % blocSize) + (h / blocSize) * (normalSpaces * blocSize + 1) + (hexOrAscii && cursorOffset);

  if (!hexOrAscii) r += x + normalSpaces + 1;

  return r;
}



/*******************************************************************************/
/* Curses functions */
/*******************************************************************************/

void initCurses(void)
{
  struct sigaction sa;
  initscr();

#ifdef HAVE_COLORS
  if (colored) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED, -1);   /* null zeros */
    init_pair(2, COLOR_GREEN, -1); /* control chars */
    init_pair(3, COLOR_BLUE, -1);  /* extended chars */
  }
  init_pair(4, COLOR_BLUE, COLOR_YELLOW); /* current cursor position*/
#endif

  initDisplay();
}

void initDisplay(void)
{
  refresh();
  raw();
  noecho();
  keypad(stdscr, TRUE);

  if (mode == bySector) {
    lineLength = modes[bySector].lineLength;
    page = modes[bySector].page;
    page = myfloor((LINES - 1) * lineLength, page);
    blocSize = modes[bySector].blocSize;
    if (computeLineSize() > COLS) DIE("%s: term is too small for sectored view (width)\n");
    if (page == 0) DIE("%s: term is too small for sectored view (height)\n");
  } else { /* mode == maximized */
    if (LINES <= 4) DIE("%s: term is too small (height)\n");

    blocSize = modes[maximized].blocSize;
    if (lineLength == 0) {
      /* put as many "blocSize" as can fit on a line */
      for (lineLength = blocSize; computeLineSize() <= COLS; lineLength += blocSize);
      lineLength -= blocSize;
      if (lineLength == 0) DIE("%s: term is too small (width)\n");
    } else {
      if (computeLineSize() > COLS)
        DIE("%s: term is too small (width) for selected line length\n");
    }
    page = lineLength * (LINES - 1);
  }
  colsUsed = computeLineSize();
  buffer = realloc(buffer,page);
  bufferAttr = realloc(bufferAttr,page * sizeof(*bufferAttr));
}

void exitCurses(void)
{
  close(fd);
  clear();
  refresh();
  endwin();
}

static void printaddr(uint64_t addr)
{
  char templ[7]; // maximum string is "%016lX", which is 6 chars + 1 null byte
  sprintf(templ,"%%0%dlX", nAddrDigits);
  PRINTW((templ, addr));
}

void display(void)
{
  long long fsize;
  int i;

  for (i = 0; i < nbBytes; i += lineLength) {
    move(i / lineLength, 0);
    displayLine(i, nbBytes);
  }
  for (; i < page; i += lineLength) {
    int j;
    move(i / lineLength, 0);
    for (j = 0; j < colsUsed; j++) printw(" "); /* cleanup the line */
    move(i / lineLength, 0);
    printaddr(base+i);
  }

  attrset(NORMAL);
  move(LINES - 1, 0);
  for (i = 0; i < colsUsed; i++) printw("-");
  move(LINES - 1, 0);
  if (isReadOnly) i = '%';
  else if (edited) i = '*';
  else i = '-';
  printw("-%c%c  %s       --0x%llX", i, i, baseName, (long long) base + cursor);
  fsize = getfilesize();
  if (MAX(fileSize, lastEditedLoc)) printw("/0x%llX", fsize);
  printw("--%i%%", fsize == 0 ? 0 : 100 * (base + cursor + fsize/200) / fsize );
  if (mode == bySector) printw("--sector %lld", (long long) ((base + cursor) / SECTOR_SIZE));

  printw("---- %s ", selectedEncoding->name);

  move(cursor / lineLength, computeCursorXCurrentPos());
}

void displayLine(int offset, int max)
{
  int i,mark_color=0;
#ifdef HAVE_COLORS
  mark_color = COLOR_PAIR(4) | A_BOLD;
#endif
  printaddr(base + offset);
  PRINTW(("   "));
  for (i = offset; i < offset + lineLength; i++) {
    if (i > offset)
      MAXATTRPRINTW(bufferAttr[i] & MARKED, (((i - offset) % blocSize) ? " " : "  "));
    if (i < max) {
      ATTRPRINTW(
#ifdef HAVE_COLORS
        (
          !colored
          ? (cursor == i && hexOrAscii == 0 ? mark_color : 0)
          : (
            cursor == i && hexOrAscii == 0 ? mark_color :
            buffer[i] == 0 ? (int) COLOR_PAIR(1) :
            buffer[i] < ' ' ? (int) COLOR_PAIR(2) :
            buffer[i] >= 127 ? (int) COLOR_PAIR(3) : 0
          )
        ) |
#else
        (cursor == i && hexOrAscii == 0 ? mark_color : 0)
#endif
        bufferAttr[i], ("%02X", buffer[i])
      );
    }
    else if (i == max) {
      PRINTW((".."));
    }
    else PRINTW(("  "));
  }
  PRINTW(("  "));
  for (i = offset; i < offset + lineLength; i++) {
    if (i >= max)
      PRINTW((" "));
    else
      ATTRPRINTW((cursor == i && hexOrAscii == 1 ? mark_color : 0) | bufferAttr[i], ("%s", selectedEncoding->displayCharacters[buffer[i]]));
  }
}

void clr_line(int line) { move(line, 0); clrtoeol(); }

void displayCentered(const char *msg, int line)
{
  clr_line(line);
  move(line, (COLS - strlen(msg)) / 2);
  PRINTW(("%s", msg));
}

void displayOneLineMessage(const char *msg)
{
  int center = page / lineLength / 2;
  clr_line(center - 1);
  clr_line(center + 1);
  displayCentered(msg, center);
}

void displayTwoLineMessage(const char *msg1, const char *msg2)
{
  int center = page / lineLength / 2;
  clr_line(center - 2);
  clr_line(center + 1);
  displayCentered(msg1, center - 1);
  displayCentered(msg2, center);
}

void displayMessageAndWaitForKey(const char *msg)
{
  displayTwoLineMessage(msg, pressAnyKey);
  getch();
}

int displayMessageAndGetString(const char *msg, char **last, char *p, int p_size)
{
  int ret = TRUE;

  displayOneLineMessage(msg);
  ungetstr(*last);
  echo();
  getnstr(p, p_size - 1);
  noecho();
  if (*p == '\0') {
    if (*last) strcpy(p, *last); else ret = FALSE;
  } else {
    FREE(*last);
    *last = strdup(p);
  }
  return ret;
}

void ungetstr(char *s)
{
  char *p;
  if (s)
    for (p = s + strlen(s) - 1; p >= s; p--) ungetch(*p);
}

int get_number(INT *i)
{
  unsigned long long n;
  int err;
  char tmp[BLOCK_SEARCH_SIZE];
  echo();
  getnstr(tmp, BLOCK_SEARCH_SIZE - 1);
  noecho();
  if (strbeginswith(tmp, "0x"))
    err = sscanf(tmp + strlen("0x"), "%llx", &n);
  else
    err = sscanf(tmp, "%llu", &n);
  *i = (off_t)n;
  if (*i < 0 || n != (unsigned long long) *i)
    err = 0;
  return err == 1;
}
