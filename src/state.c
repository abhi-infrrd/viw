#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include "screen.h"
#include "buffer.h"
#include "state.h"

static void update_top_row(state_t *st) {
  size_t current_row = st->buf->current_row;
  size_t num_windows = st->scr->num_windows;

  // scroll down
  if (current_row >= st->top_row + num_windows) {
    st->top_row = current_row - num_windows + 1;
    st->to_refresh = true;
  }

  // scroll up
  if (current_row < st->top_row) {
    st->top_row = current_row;
    st->to_refresh = true;
  }
}

/*
 * Cursor position can be computed from:
 * buf->current_row, buf->current_char, scr->top_row
 */
static void update_cursor_position(state_t *st) {
  size_t line_size = st->buf->current->line_size;
  size_t current_row = st->buf->current_row;
  size_t current_char = st->buf->current_char;
  size_t top_row = st->top_row;

  st->cy = current_row - top_row;

  if (line_size == 0) {
    st->cx = 0;
  } else if (st->cx >= line_size) {
    st->cx = line_size - 1;
  } else {
    st->cx = current_char;
  }

  if (st->mode == INSERT_BACK && st->buf->current->line_size != 0) {
    st->cx ++;
  }

  if (st->mode == EX) {
    st->cy = st->scr->num_windows;
    st->cx = st->buf->status_row->line_size;
  }
}

/*
 * Determine current rows to be displayed and update windows <-> rows links
 * Can be computed from:
 * st->cy, buf->current_row, scr->top_window, scr->num_windows
 *
 * We want to update the display when:
 * - scroll up/down (update_top_row() -> true)
 * - insert/delete row(s)
 * - insert at the bottom which triggers a "scroll"
 */
static void update_scr_windows(state_t *st) {
  // link status window and its buffer
  if (!st->scr->status_window->r) {
    st->scr->status_window->r = st->buf->status_row;
  }

  size_t current_row = st->buf->current_row;
  size_t top_row = st->top_row;
  size_t num_windows = st->scr->num_windows;

  window_t **windows = st->scr->windows;

  row_t *r = st->buf->current;

  for (size_t i = top_row; i <= current_row; i ++) {
    windows[current_row - i]->r = r;
    r->is_dirty = true;
    r = r->prev;
  }

  r = st->buf->current;

  for (size_t i = current_row; i < top_row + num_windows; i ++) {
    if (r) {
      windows[i - top_row]->r = r;
      r->is_dirty = true;
      r = r->next;
    } else {
      windows[i - top_row]->r = NULL;
    }
  }
}

state_t *init_state(const char *filename) {
  state_t *st = malloc(sizeof(state_t));

  st->mode = NORMAL;
  st->cx = 0;
  st->cy = 0;
  st->top_row = 0;

  st->to_refresh = true;

  st->buf = init_buffer(filename);
  st->scr = init_screen(LINES);

  st->prev_key = '\0';

  update_state(st);

  return st;
}

void destroy_state(state_t *st) {
  destroy_buffer(st->buf);
  destroy_screen(st->scr);
  free(st);
}

void update_state(state_t *st) {
  update_top_row(st);

  if (st->to_refresh) {
    update_scr_windows(st);
    st->to_refresh = false;
  }

  update_cursor_position(st);
}

void move_cursor(state_t *st, DIRECTION d) {
  move_current(st->buf, d);
}

void handle_enter(state_t *st) {
  // Edge case: enter at the end of the line in insert_back mode
  if (st->mode == INSERT_BACK && st->buf->current_char == st->buf->current->line_size - 1) {
    append_char(st->buf, '0');
    split_row(st->buf);
    delete_char(st->buf);
    st->to_refresh = true;
    return;
  }

  // in insert_back, cursor one char to the right of "current"
  // we always want to in insert_back mode when line is empty
  if (st->mode == INSERT_BACK && st->buf->current->line_size != 0) {
    move_current(st->buf, RIGHT);
    st->mode = INSERT_FRONT;
  }

  split_row(st->buf);
  st->to_refresh = true;
}

void handle_backspace(state_t *st) {
  buffer_t *buf = st->buf;
  row_t *r = buf->current;

  if (st->mode == EX) {
    move_current(st->buf, LEFT);
    drop_char(buf->status_row);
  }

  if (st->mode == INSERT_FRONT) {
    if (!r->current->prev) {
      join_row(st->buf);
      st->to_refresh = true;
      return;
    }

    move_current(st->buf, LEFT);
    delete_char(buf);
  }

  if (st->mode == INSERT_BACK) {
    if (!r->current) {
      join_row(st->buf);
      st->to_refresh = true;
      return;
    }

    if (buf->current_char == 0) {
      if (r->line_size == 1) {
        delete_char(st->buf);
        return;
      }

      // back insert mode cant handle this situation
      st->mode = INSERT_FRONT;
      move_current(st->buf, RIGHT);
      handle_backspace(st);
      return;
    }

    delete_char(buf);

    if (r->current->next) {
      move_current(st->buf, LEFT);
    }
  }
}

void set_prev_key(state_t *st, char c) {
  st->prev_key = c;
}

void reset_prev_key(state_t *st) {
  st->prev_key = '\0';
}
