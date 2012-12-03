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

#define HEADER_SANS_WARNINGS <glib/gstdio.h>
#include <sans_warnings.h>
#ifdef MINGW
#include <windows.h>  // FreeConsole()
#endif

#include <lib/status.h>
#include <lib/mcf2.h>
#include <lib/gui.h>
#include <lib/util/fs.h>
#include <lib/util/str.h>
#include <lib/util/num.h>
#include <lib/hardware/timing.h>
#include <control/server.h>
#include <main/menu/config.h>
#include <main/menu/page.h>
#include <main/menu/terminal.h>
#include <main/menu/bufmenu.h>
#include <main/menu/help.h>

typedef struct
{
	Config   config;
	Page     page;
	Terminal terminal;
	Bufmenu  bufmenu;
	Help     help;

	Setup setup;
	Panel panel[M2_NUM_PANEL];
	ColorScheme colorscheme;
	OldVars oldvars;

	ChanSet chanset;
	ThreadVars tv;

} Mezurit2;

static GtkWidget * run_splash_window (void);

static void mezurit2_init     (Mezurit2 *m2);
static void mezurit2_register (Mezurit2 *m2);
static void mezurit2_final    (Mezurit2 *m2);

int main (int argc, char *argv[])
{
	remember_argv0(argv[0]);

#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 32
	g_thread_init(NULL);
#endif
	gtk_init(&argc, &argv);
	Timer *timer _timerfree_ = timer_new();

	int f_mode = M2_F_MODE_DEFAULT;
	bool darkpanel = 0;

	int i = 1;
	while (i < argc)
	{
		if      (str_equal(argv[i], "--debug") && (i + 1) < argc && scan_hex(argv[i + 1], &f_mode)) i++;
		else if (str_equal(argv[i], "--darkpanel")) darkpanel = 1;
		else 
		{
			printf("\nUsage: %s [--darkpanel] [--debug mode]\n\n", argv[0]);
			printf("  Modes:    0x%X  error      0x%X  run         0x%X  verbose\n", F_ERROR,   F_RUN,      F_VERBOSE);
			printf("            0x%X  warning    0x%X  callback    0x%X  bench\n",   F_WARNING, F_CALLBACK, F_BENCH);
			printf("            0x%X  init       0x%X  mcf         0x%X  profile\n", F_INIT,    F_MCF,      F_PROFILE);
			printf("            0x%X  update     0x%X  control\n\n",                 F_UPDATE,  F_CONTROL);
			printf("  Default:  0x%X  %s\n\n", M2_F_MODE_DEFAULT, M2_F_MODE_DEFAULT_STR);
			return 1;
		}

		i++;
	}

#ifdef MINGW
	if (f_mode == 0x0) printf("Loading ...\n");
#endif
	f_init(f_mode);
	f_start(F_NONE);

	GtkWidget *splash_window = run_splash_window();

	status_init();
	status_add(0, supercat("Debug mode set to 0x%X. See Help for an explanation.\n", f_mode));

	gtk_rc_parse(atg(sharepath(darkpanel ? "themes/gtkrc-dark" : "themes/gtkrc-light")));

	verify_config_dir();
	verify_config_file("compute.py",  atg(supercat("# Place your custom channel and trigger functions here. See %s for examples.\n\n", atg(libpath("mezurit2compute.py")))));
	verify_config_file("terminal.py", atg(supercat("# Place your custom terminal functions here. See %s for examples.\n\n",            atg(libpath("mezurit2control.py")))));

	timing_init();
	daq_init();
	gpib_init();
	control_init();
	compute_init();
	mcf_init();
	gui_init();
	entry_init();

	Mezurit2 m2;
	mezurit2_init(&m2);
	mezurit2_register(&m2);

	hardware_array_block(m2.setup.hw);
	mcf_load_defaults("setup");

	// load config file, if found:

	oldvars_reset(&m2.oldvars);
	char *default_file = atg(configpath("default.mcf"));
	if (!g_file_test(default_file, G_FILE_TEST_EXISTS) || mcf_read_file(default_file, "setup") == -1)
	{
		mcf_read_file(atg(sharepath("default.mcf")), "setup");
		mcf_write_file(default_file);
	}
	oldvars_mention(&m2.oldvars);

	hardware_array_unblock(m2.setup.hw);

	f_gc();

	spawn_terminal(&m2.terminal);
	gtk_widget_destroy(splash_window);
	show_all(m2.page.main_window, NULL);
	if (m2.terminal.vte != NULL) gtk_widget_grab_focus(m2.terminal.vte);

	status_add(1, supercat("Welcome to %s %s!\n", quote(PROG2), quote(VERSION)));
	f_print(F_BENCH, "startup: %f sec\n", timer_elapsed(timer));

#ifdef MINGW
	if (f_mode == M2_F_MODE_DEFAULT) FreeConsole();
#endif

	while (m2.tv.pid != -2)
	{
		while (gtk_events_pending()) gtk_main_iteration();

		if (control_server_poll(M2_TS_ID))
		{
			bool matched = control_server_iterate(M2_TS_ID, M2_CODE_SETUP);
			if (!matched) control_server_reply(M2_TS_ID, "command_error");
		}

		if (m2.tv.pid >= 0) run_gui_thread(&m2.tv, m2.setup.channel, m2.panel, m2.page.flipbook);

		xleep(1.0 / M2_DEFAULT_GUI_RATE);

		if (status_regenerate()) message_update(&m2.setup.message);
	}

	gtk_widget_hide(m2.page.main_window);
	while (gtk_events_pending()) gtk_main_iteration();

	mcf_write_file(atg(configpath("last.mcf")));

	gui_final();
	mcf_final();
	control_final();
	compute_final();
	gpib_final();
	daq_final();
	timing_final();
	status_final();
	mezurit2_final(&m2);

	f_print(F_WARNING, "Goodbye!\n");
	return 0;
}

GtkWidget * run_splash_window (void)
{
	f_start(F_INIT);

	gtk_window_set_default_icon_from_file(atg(sharepath("pixmaps/mezurit2.png")), NULL);

	GtkWidget *widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(widget), 0);
	gtk_window_set_title(GTK_WINDOW(widget), atg(cat2(quote(PROG2), quote(VERSION))));
	container_add(gtk_image_new_from_file(atg(sharepath("pixmaps/splash.png"))), widget);

	gtk_widget_show_all(widget);
	while (gtk_events_pending()) gtk_main_iteration();

	return widget;
}

void mezurit2_init (Mezurit2 *m2)
{
	f_start(F_INIT);

	m2->page.main_window = name_widget(gtk_window_new(GTK_WINDOW_TOPLEVEL), "m2_main");
	gtk_window_set_default_size(GTK_WINDOW(m2->page.main_window), -1, M2_DEFAULT_HEIGHT);

	GtkWidget *vbox = container_add(gtk_vbox_new(0, 0), m2->page.main_window);

	GtkWidget *menubar = pack_start(gtk_menu_bar_new(), 0, vbox);
	m2->page.flipbook  = pack_start(gtk_hbox_new(0, 0), 1, vbox);

	int port = control_server_listen(M2_TS_ID);

	// setup:
	setup_init(&m2->setup, m2->page.flipbook);

	// menus:
	page_init     (&m2->page,     menubar);
	config_init   (&m2->config,   menubar);
	bufmenu_init  (&m2->bufmenu,  menubar);
	terminal_init (&m2->terminal, menubar, m2->setup.terminal_scroll, port);
	help_init     (&m2->help,     menubar);

	// panels:
	for (int pid = 0; pid < M2_NUM_PANEL; pid++) panel_init(&m2->panel[pid], pid, m2->page.flipbook);

	// threads:
	thread_init_all(&m2->tv);

	// inheritance:
	for (int pid = 0; pid < M2_NUM_PANEL; pid++)
	{
		m2->panel[pid].plot.colorscheme   = &m2->colorscheme;
		m2->panel[pid].buffer.link_tzero  = &m2->bufmenu.link_tzero;
		m2->panel[pid].buffer.save_header = &m2->bufmenu.save_header;
		m2->panel[pid].buffer.save_mcf    = &m2->bufmenu.save_mcf;
		m2->panel[pid].buffer.save_scr    = &m2->bufmenu.save_scr;
	
		for (int n = 0; n < M2_MAX_CHAN; n++) m2->panel[pid].sweep[n].mutex = &m2->panel[pid].sweep_mutex;
	}

	m2->page.setup = &m2->setup;
	m2->page.panel_array = m2->panel;
	m2->page.terminal_vte = m2->terminal.vte;
	m2->tv.chanset = &m2->chanset;

	set_page(&m2->page, &m2->config, -1);
}

void mezurit2_final (Mezurit2 *m2)
{
	f_start(F_INIT);

	thread_final_all(&m2->tv);

	hardware_array_final(m2->setup.hw);
	channel_array_final(m2->setup.channel);
	for (int pid = 0; pid < M2_NUM_PANEL; pid++) panel_final(&m2->panel[pid]);
}

void mezurit2_register (Mezurit2 *m2)
{
	f_start(F_INIT);

	mcf_register(NULL, atg(supercat("# %s %s", quote(PROG2), quote(VERSION))), MCF_W);

	mcf_register(NULL, "# Common",     0);  // obsolete heading
	mcf_register(NULL, "# Timescale",  0);  // obsolete heading

	/**/                                   thread_register_daq(&m2->tv);
	/**/                                   thread_register_gui(&m2->tv);
	for (int i = 0; i < M2_NUM_PANEL; i++) thread_register_panel(&m2->tv, &m2->panel[i]);

	setup_register(&m2->setup);
	colorscheme_register(&m2->colorscheme);

	for (int i = 0; i < M2_NUM_PANEL; i++)
	{
		mcf_register(NULL, atg(supercat("# Panel %d", i)), MCF_W);
		panel_register(&m2->panel[i], &m2->chanset);
	}
	panel_register_legacy(&m2->panel[0]);

	oldvars_register(&m2->oldvars);
	
	page_register     (&m2->page, &m2->tv, &m2->config, &m2->terminal);
	config_register   (&m2->config, m2->setup.hw, &m2->tv.panel, &m2->oldvars);
	bufmenu_register  (&m2->bufmenu);
	terminal_register (&m2->terminal, &m2->tv);
	help_register     (&m2->help);
}
