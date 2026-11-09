/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/
/* $NHDT-Header: NetHack 5.0 cursdial.h 2026/04/26_13:25:42 NetHack-5.0:1.13 $ */
/* Copyright (c) Karl Garrison, 2010. */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef CURSDIAL_H
# define CURSDIAL_H

/* Global declarations */

void curses_line_input_dialog(const char *prompt, char *answer,
                                                      int buffer) NONNULLPTRS;
int curses_character_input_dialog(const char *prompt, const char *choices,
                                                        char def) NONNULLARG1;
int curses_ext_cmd(void);
void curses_create_nhmenu(winid wid, unsigned long);
void curses_add_nhmenu_item(winid wid, const glyph_info *glyphinfo,
                            const ANY_P *identifier, char accelerator,
                            char group_accel, int attr, int clr,
                            const char *str, unsigned itemflags);
void curs_menu_set_bottom_heavy(winid);
void curses_finalize_nhmenu(winid wid, const char *prompt);
int curses_display_nhmenu(winid wid, int how, MENU_ITEM_P **_selected);
boolean curses_menu_exists(winid wid);
void curses_del_menu(winid, boolean);
boolean curs_nonselect_menu_action(WINDOW *win, void *menu, int how,
                                   int curletter, int *curpage_p,
                                   char selectors[256], int *num_selected_p)
                                                                  NONNULLPTRS;

#ifdef PS5_CONTROLLER
/* What the controller is driving, for sys/ps5's button hints (the numbers
   are shared with sys/ps5/ps5main.c).  The PS5_DIALOG_s are set while one
   is open; the others follow from program_state.input_state. */
#define PS5_CONTEXT_MAP 0
#define PS5_DIALOG_MENU 1      /* a menu to pick several from */
#define PS5_DIALOG_BUTTONS 2   /* a question as a row of buttons */
#define PS5_CONTEXT_GETPOS 3   /* picking a spot on the map */
#define PS5_CONTEXT_PROMPT 4   /* another single-key prompt */
#define PS5_CONTEXT_GETDIR 5   /* "In what direction?" */
#define PS5_DIALOG_MORE 6      /* >> (--More--) */
#define PS5_DIALOG_TEXT 7      /* text to read */
#define PS5_DIALOG_PICK 8      /* a menu to pick one from */
#define PS5_DIALOG_LIST 9      /* a prompt that offers a list: [abc or ?*] */
#define PS5_DIALOG_KEYBOARD 10 /* typing on the keyboard drawn in-game */
extern int ps5_curses_dialog;
/* sys/ps5/ps5main.c: the system's on-screen keyboard; 1 typed, 0
   cancelled, -1 unavailable */
extern int ps5_text_input(const char *prompt, char *answer, int size);
#endif

#endif /* CURSDIAL_H */
