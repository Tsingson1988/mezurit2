/*
 *  Copyright (C) 2012 California Institute of Technology
 *
 *  This file is part of Mezurit2, written by Brian Standley <brian@brianstandley.com>.
 *
 *  Mezurit2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 *
 *  Mezurit2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE. See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with this
 *  program. If not, see <http://www.gnu.org/licenses/>.
*/

static void set_page_cb (GtkWidget *widget, Page *page, Config *config, ThreadVars *tv, int pid);
static gboolean exit_cb (GtkWidget *widget, GdkEvent *event, Page *page, ThreadVars *tv, Terminal *terminal);
static char * set_panel_csf (gchar **argv, Page *page, Config *config, ThreadVars *tv);
static char * get_panel_csf (gchar **argv, ThreadVars *tv);
static char * hello_csf (gchar **argv);

void set_page_cb (GtkWidget *widget, Page *page, Config *config, ThreadVars *tv, int pid)
{
	f_start(F_CALLBACK);

	cleanup_dirty_entry();  // Emit focus-out-event on dirty entry, if necessary.

	if (tv->pid >= 0) stop_threads(tv);

	tv->pid = pid;
	set_page(page, config, pid);

	if (pid >= 0) page->panel_array[pid].plot.exposure_blocked = 1;
}

gboolean exit_cb (GtkWidget *widget, GdkEvent *event, Page *page, ThreadVars *tv, Terminal *terminal)
{
	f_start(F_CALLBACK);

	if (tv->pid >= 0)
	{
		if (run_yes_no_dialog("Exit now?")) stop_threads(tv);
		else return 1;
	}

	quit_terminal(terminal);
	tv->pid = -2;

	return 1;
}

char * set_panel_csf (gchar **argv, Page *page, Config *config, ThreadVars *tv)
{
	f_start(F_CONTROL);

	int pid;
	if (scan_arg_int(argv[1], "pid", &pid) && pid >= -1 && pid < M2_NUM_PANEL)
	{
		cleanup_dirty_entry();  // Emit focus-out-event on dirty entry, if necessary.

		if (tv->pid >= 0) stop_threads(tv);

		tv->pid = pid;
		set_page(page, config, pid);

		if (pid >= 0) page->panel_array[pid].plot.exposure_blocked = 1;

		return cat1(argv[0]);
	}

	return cat1("argument_error");
}

char * get_panel_csf (gchar **argv, ThreadVars *tv)
{
	f_start(F_CONTROL);

	return supercat("%s;pid|%d", argv[0], tv->pid);
}

char * hello_csf (gchar **argv)  // used by terminal to test connection
{
	f_start(F_CONTROL);

	char *str _strfree_ = NULL;
	if (scan_arg_string(argv[1], "msg_out", &str)) return supercat("%s;msg_back|%s", argv[0], str);
	else                                           return cat1("argument_error");
}
