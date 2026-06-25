/*
 * Copyright (C) 2026 Thiago Negri
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <curses.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

// Defines
#define RC_OK 0
#define RC_ERR_SETLOCALE 1
#define RC_ERR_CURSES_INITSCR 2
#define RC_ERR_CURSES_CBREAK 3
#define RC_ERR_CURSES_NOECHO 4
#define RC_ERR_CURSES_KEYPAD 5
#define RC_ERR_CURSES_ENDWIN 6
#define RC_ERR_CURSES_START_COLOR 7
#define RC_ERR_CURSES_INIT_PAIR 8
#define RC_ERR_CURSES_USE_DEFAULT_COLORS 9
#define RC_ERR_CURSES_CURS_SET 10
#define RC_ERR_CURSES_CLEAR 11
#define RC_ERR_CURSES_MVPRINTW 12
#define RC_ERR_CURSES_ATTRON 13
#define RC_ERR_CURSES_ATTROFF 14
#define RC_ERR_CURSES_REFRESH 15
#define RC_ERR_FILE_READ_FOPEN 16
#define RC_ERR_OOM 17
#define RC_ERR_FILE_READ_FREAD 18

#define CURSES_PAIR_HIGHLIGHT 1

#define FLAG_CURSES_INIT_WHITE_TERMINAL (1 >> 0)

#define PNR_OPTION_TYPE_MENU 1
#define PNR_OPTION_TYPE_ACTION 2

// Globals
int g_curses_colors = 0;
int g_curses_w = 0;
int g_curses_h = 0;

// Data types
struct slice {
  const char *start;
  int size;
};

struct pnr_menu {
  struct pnr_menu *parent;
  struct slice name;
  struct pnr_option *options;
  int options_size;
  int options_selected;
};

struct pnr_option {
  struct slice name;
  int type;
  union {
    struct pnr_menu *menu;
    struct pnr_action *action;
  };
};

struct pnr_action {
  struct slice command;
};

// Signatures
int curses_init(int options);
int curses_end();
int file_read(const char *path, char **ret_content, size_t *ret_size);
int pnr_parse(int depth, const char *buffer, size_t size,
              struct pnr_menu **ret_menu);
void pnr_print(int depth, struct pnr_menu *menu);
void pnr_free(struct pnr_menu *menu);
int slice_is_blank(const char *start, const char *end);
int char_is_blank(char c);
const char *ltrim(const char *s);
const char *rtrim(const char *s);
int main(int argc, char **argv);

// Implementation
int main(int argc, char **argv) {
  int rc = 0;
  int curses_flags = 0;
  int selected_option = 0;
  int input = 0;
  char *options_raw = NULL;
  char *title = NULL;
  size_t options_raw_size = 0;
  struct pnr_menu *menu_root = NULL;
  struct pnr_menu *menu_current = NULL;

  // Curses depends on locale
  if (setlocale(LC_ALL, "") == NULL) {
    rc = RC_ERR_SETLOCALE;
    goto _err;
  }

  // Parse command line options
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--white") == 0 || strcmp(argv[i], "-w") == 0) {
      curses_flags = curses_flags | FLAG_CURSES_INIT_WHITE_TERMINAL;
    }
  }

  // Read and parse menu options
  rc = file_read("options.pnr", &options_raw, &options_raw_size);
  if (rc != RC_OK)
    goto _err;

  rc = pnr_parse(0, options_raw, options_raw_size, &menu_root);
  if (rc != RC_OK)
    goto _err;

  rc = curses_init(curses_flags);
  if (rc != RC_OK)
    return rc;

  menu_current = menu_root;

  // Main loop
  while (input != 'q') {
    if (clear() != OK) {
      rc = RC_ERR_CURSES_CLEAR;
      goto _err;
    }

    // Breadcrumb
    if (title != NULL)
      free(title);
    title = NULL;
    int title_size = 0;
    struct pnr_menu *parent = menu_current;
    while (parent != NULL) {
      struct slice *name = &parent->name;
      if (name->start != NULL && name->size != 0) {
        if (title_size > 0)
          title_size += 3;
        char *title_new = malloc(name->size + title_size + 1);
        if (title_new == NULL) {
          rc = RC_ERR_OOM;
          goto _err;
        }
        strncpy(title_new, name->start, name->size);
        if (title_size > 0) {
          strcpy(&title_new[name->size], " > ");
          strcpy(&title_new[name->size + 3], title);
        }
        title_size += name->size;
        title_new[title_size] = '\0';
        if (title != NULL)
          free(title);
        title = title_new;
      }
      parent = parent->parent;
    }
    if (title != NULL) {
      if (mvprintw(0, 0, "%s", title) != OK) {
        rc = RC_ERR_CURSES_MVPRINTW;
        goto _done;
      }
    }

    // Options
    int options_offset_y = 2;
    for (int i = 0; i < menu_current->options_size; i++) {
      struct pnr_option *option = &menu_current->options[i];
      if (selected_option == i) {
        if (mvprintw(i + options_offset_y, 0, ">") != OK) {
          rc = RC_ERR_CURSES_MVPRINTW;
          goto _done;
        }
        if (g_curses_colors)
          if (attron(COLOR_PAIR(CURSES_PAIR_HIGHLIGHT)) != OK) {
            rc = RC_ERR_CURSES_ATTRON;
            goto _done;
          }
      }
      if (mvprintw(i + options_offset_y, 2, "%.*s", option->name.size,
                   option->name.start) != OK) {
        rc = RC_ERR_CURSES_MVPRINTW;
        goto _done;
      }
      if (selected_option == i)
        if (g_curses_colors)
          if (attroff(COLOR_PAIR(CURSES_PAIR_HIGHLIGHT)) != OK) {
            rc = RC_ERR_CURSES_ATTROFF;
            goto _done;
          }
    }

    // Footer
    if (mvprintw(menu_current->options_size + 1 + options_offset_y, 0,
                 "Press 'q' to quit\n") != OK) {
      rc = RC_ERR_CURSES_MVPRINTW;
      goto _done;
    }

    if (refresh() != OK) {
      rc = RC_ERR_CURSES_REFRESH;
      goto _done;
    }

    // Input
    // TODO Add config for key binds
    input = getch();
    switch (input) {
    case 'j':
    case KEY_DOWN:
      selected_option += 1;
      if (selected_option >= menu_current->options_size)
        selected_option = 0;
      menu_current->options_selected = selected_option;
      break;

    case 'k':
    case KEY_UP:
      selected_option -= 1;
      if (selected_option < 0)
        selected_option = menu_current->options_size - 1;
      menu_current->options_selected = selected_option;
      break;

    case 'h':
    case KEY_LEFT:
      if (menu_current->parent != NULL) {
        menu_current = menu_current->parent;
        selected_option = menu_current->options_selected;
      }
      break;

    case '\n':
    case 'l':
    case KEY_RIGHT:
      if (menu_current->options[selected_option].type == PNR_OPTION_TYPE_MENU) {
        menu_current = menu_current->options[selected_option].menu;
        selected_option = menu_current->options_selected;
      } else if (menu_current->options[selected_option].type ==
                 PNR_OPTION_TYPE_ACTION)
        // TODO Run the selected option
        ;
      break;

    case KEY_RESIZE:
      getmaxyx(stdscr, g_curses_h, g_curses_w);
      break;
    }
  }

  rc = RC_OK;

_err:
_done:
  if (title != NULL)
    free(title);

  if (menu_root != NULL)
    pnr_free(menu_root);

  if (options_raw != NULL)
    free(options_raw);

  if (rc != RC_OK)
    curses_end();
  else
    rc = curses_end();

  return rc;
}

int curses_init(int flags) {
  // Init window and screen
  if (initscr() == 0)
    return RC_ERR_CURSES_INITSCR;

  // Disable line buffering
  if (cbreak() != OK)
    return RC_ERR_CURSES_CBREAK;

  // Do not echo back input
  if (noecho() != OK)
    return RC_ERR_CURSES_NOECHO;

  // Handle keypad keys
  if (keypad(stdscr, TRUE) != OK)
    return RC_ERR_CURSES_KEYPAD;

  // Make cursor invisible
  if (curs_set(0) == ERR)
    return RC_ERR_CURSES_CURS_SET;

  // Init colors, if available
  g_curses_colors = has_colors() == TRUE;
  if (g_curses_colors) {
    if (start_color() != OK)
      return RC_ERR_CURSES_START_COLOR;

    if (use_default_colors() != OK)
      return RC_ERR_CURSES_USE_DEFAULT_COLORS;

    if ((flags & FLAG_CURSES_INIT_WHITE_TERMINAL) != 0) {
      if (init_pair(CURSES_PAIR_HIGHLIGHT, COLOR_WHITE, COLOR_BLACK) != OK)
        return RC_ERR_CURSES_INIT_PAIR;
    } else {
      if (init_pair(CURSES_PAIR_HIGHLIGHT, COLOR_BLACK, COLOR_WHITE) != OK)
        return RC_ERR_CURSES_INIT_PAIR;
    }
  }

  getmaxyx(stdscr, g_curses_h, g_curses_w);

  return RC_OK;
}

int curses_end() {
  // End window and screen
  if (endwin() != OK)
    return RC_ERR_CURSES_ENDWIN;

  return RC_OK;
}

int file_read(const char *path, char **ret_content, size_t *ret_size) {
  int rc = RC_OK;
  char *content = NULL;
  FILE *fd = NULL;

  fd = fopen(path, "r");
  if (fd == NULL) {
    rc = RC_ERR_FILE_READ_FOPEN;
    goto _done;
  }

  fseek(fd, 0, SEEK_END);
  long size = ftell(fd);

  content = malloc(size + 1);
  if (content == NULL) {
    rc = RC_ERR_OOM;
    goto _done;
  }

  fseek(fd, 0, SEEK_SET);
  if (fread(content, 1, size, fd) != size) {
    rc = RC_ERR_FILE_READ_FREAD;
    goto _done;
  }

  content[size] = '\0';

  *ret_content = content;
  *ret_size = size;
  rc = RC_OK;

_done:
  if (fd != NULL)
    fclose(fd);

  return rc;
}

int pnr_parse(int depth, const char *buffer, size_t size,
              struct pnr_menu **ret_menu) {
  int rc = RC_OK;
  struct pnr_menu *menu = NULL;
  const char *end = &buffer[size];

  menu = malloc(sizeof(struct pnr_menu));
  if (menu == NULL) {
    rc = RC_ERR_OOM;
    goto _err;
  }
  menu->parent = NULL;
  menu->name.start = NULL;
  menu->name.size = 0;
  menu->options = NULL;
  menu->options_size = 0;
  menu->options_selected = 0;

  // Count how many options we have
  int options_size = 0;
  const char *current = buffer;
  while (current != end) {
    int skip = 0;
    for (int i = 0; i < depth; i++) {
      // We're ascending a depth level, ignore the rest of the buffer
      if (strncmp(current, "- ", 2) != 0) {
        skip = 1;
        current = end;
        break;
      }
      current += 2;
    }
    if (current == end)
      break;
    // This line descends a depth, ignore it
    if (strncmp(current, "- ", 2) == 0) {
      skip = 1;
    }
    const char *start = current;
    while (*current != '\n' && current != end)
      current++;
    skip = skip || slice_is_blank(start, current); // Skip blank lines
    if (!skip)
      options_size++;
    if (*current == '\n')
      current++;
  }

  // Malloc and assign options
  menu->options_size = options_size;
  menu->options = malloc(sizeof(struct pnr_option) * options_size);
  if (menu->options == NULL) {
    rc = RC_ERR_OOM;
    goto _err;
  }

  int option_index = 0;
  current = buffer;
  while (current != end) {
    int skip = 0;
    for (int i = 0; i < depth; i++) {
      // We're ascending a depth level, ignore the rest of the buffer
      if (strncmp(current, "- ", 2) != 0) {
        skip = 1;
        current = end;
        break;
      }
      current += 2;
    }
    if (current == end)
      break;
    // This line descends a depth, ignore it
    if (strncmp(current, "- ", 2) == 0) {
      skip = 1;
    }
    const char *start = current;
    while (*current != '\n' && (skip || *current != '$') && current != end)
      current++;
    skip = skip || slice_is_blank(start, current); // Skip blank lines
    if (!skip) {
      menu->options[option_index].name.start = ltrim(start);
      menu->options[option_index].name.size =
          (int)(rtrim(current - 1) - start) + 1;
      if (*current == '$') {
        // Parse action
        menu->options[option_index].type = PNR_OPTION_TYPE_ACTION;
        menu->options[option_index].action = malloc(sizeof(struct pnr_action));
        if (menu->options[option_index].action == NULL) {
          rc = RC_ERR_OOM;
          goto _err;
        }
        const char *command_start = ltrim(current + 1);
        menu->options[option_index].action->command.start = command_start;
        while (*current != '\n' && current != end)
          current++;
        menu->options[option_index].action->command.size =
            (int)(rtrim(current - 1) - command_start) + 1;
      } else {
        // Parse sub menu
        current++;
        menu->options[option_index].type = PNR_OPTION_TYPE_MENU;
        rc = pnr_parse(depth + 1, current, (int)(end - current),
                       &menu->options[option_index].menu);
        if (rc != RC_OK)
          goto _err;
        menu->options[option_index].menu->parent = menu;
        menu->options[option_index].menu->name =
            menu->options[option_index].name;
      }
      option_index++;
    }
    if (*current == '\n')
      current++;
  }

  *ret_menu = menu;
  rc = RC_OK;
  goto _done;

_err:
  if (menu != NULL)
    pnr_free(menu);

_done:
  return rc;
}

void pnr_print(int depth, struct pnr_menu *menu) {
  for (int i = 0; i < menu->options_size; i++) {
    for (int j = 0; j < depth; j++)
      printf("- ");
    printf("\"%.*s\" ", menu->options[i].name.size,
           menu->options[i].name.start);
    if (menu->options[i].type == PNR_OPTION_TYPE_MENU) {
      printf("(MENU)\n");
      pnr_print(depth + 1, menu->options[i].menu);
    } else {
      printf("(ACTION): $ \"%.*s\"\n", menu->options[i].action->command.size,
             menu->options[i].action->command.start);
    }
  }
}

void pnr_free(struct pnr_menu *menu) {
  for (int i = 0; i < menu->options_size; i++) {
    if (menu->options[i].type == PNR_OPTION_TYPE_ACTION) {
      free(menu->options[i].action);
    } else if (menu->options[i].type == PNR_OPTION_TYPE_MENU) {
      pnr_free(menu->options[i].menu);
    }
  }
  free(menu);
}

int slice_is_blank(const char *start, const char *end) {
  const char *current = start;
  while (current != end) {
    if (!char_is_blank(*current)) {
      return 0;
    }
    current++;
  }
  return char_is_blank(*end);
}

int char_is_blank(char c) {
  switch (c) {
  case ' ':
  case '\t':
  case '\n':
    return 1;
  default:
    return 0;
  }
}

const char *ltrim(const char *s) {
  const char *current = s;
  while (char_is_blank(*current))
    current++;
  return current;
}

const char *rtrim(const char *s) {
  const char *current = s;
  while (char_is_blank(*current))
    current--;
  return current;
}
