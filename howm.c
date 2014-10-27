#include <err.h>
#include <errno.h>
#include <sys/select.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/X.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_ewmh.h>

/**
 * @file howm.c
 *
 * @author Harvey Hunt
 *
 * @date 2014
 *
 * @brief howm
 */

/*
 *┌────────────┐
 *│╻ ╻┏━┓╻ ╻┏┳┓│
 *│┣━┫┃ ┃┃╻┃┃┃┃│
 *│╹ ╹┗━┛┗┻┛╹ ╹│
 *└────────────┘
*/

/* Modes */
static void change_mode(const Arg *arg);

enum modes { NORMAL, FOCUS, FLOATING, END_MODES };

#include "config.h"

static void (*operator_func)(const unsigned int type, int cnt);

static xcb_connection_t *dpy;
static int numlockmask, retval, last_ws, prev_layout, cw = DEFAULT_WORKSPACE;
static uint32_t border_focus, border_unfocus, border_prev_focus, border_urgent;
static unsigned int cur_mode, cur_state = OPERATOR_STATE, cur_cnt = 1;
static uint16_t screen_height, screen_width;
static bool running = true, restart;


/**
 * @brief Occurs when howm first starts.
 *
 * A connection to the X11 server is attempted and keys are then grabbed.
 *
 * Atoms are gathered.
 */
void setup(void)
{
	screen = xcb_setup_roots_iterator(xcb_get_setup(dpy)).data;
	if (!screen)
		log_err("Can't acquire the default screen.");
	screen_height = screen->height_in_pixels;
	screen_width = screen->width_in_pixels;

	log_info("Screen's height is: %d", screen_height);
	log_info("Screen's width is: %d", screen_width);

	grab_keys();

	get_atoms(WM_ATOM_NAMES, wm_atoms);

	setup_ewmh();

	border_focus = get_colour(BORDER_FOCUS);
	border_unfocus = get_colour(BORDER_UNFOCUS);
	border_prev_focus = get_colour(BORDER_PREV_FOCUS);
	border_urgent = get_colour(BORDER_URGENT);
	stack_init(&del_reg);

	howm_info();
}

/**
 * @brief The code that glues howm together...
 */
int main(int argc, char *argv[])
{
	UNUSED(argc);
	UNUSED(argv);
	fd_set descs;
	int sock_fd, dpy_fd, cmd_fd, ret;
	ssize_t n;
	xcb_generic_event_t *ev;
	char *data = calloc(IPC_BUF_SIZE, sizeof(char));

	if (!data) {
		log_err("Can't allocate memory for socket buffer.");
		exit(EXIT_FAILURE);
	}

	dpy = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(dpy)) {
		log_err("Can't open X connection");
		exit(EXIT_FAILURE);
	}
	sock_fd = ipc_init();
	setup();
	check_other_wm();
	dpy_fd = xcb_get_file_descriptor(dpy);
	while (running) {
		if (!xcb_flush(dpy))
			log_err("Failed to flush X connection");

		FD_ZERO(&descs);
		FD_SET(dpy_fd, &descs);
		FD_SET(sock_fd, &descs);

		if (select(MAX_FD(dpy_fd, sock_fd), &descs, NULL, NULL, NULL) > 0) {
			if (FD_ISSET(sock_fd, &descs)) {
				cmd_fd = accept(sock_fd, NULL, 0);
				if (cmd_fd == -1) {
					log_err("Failed to accept connection");
					continue;
				}
				n = read(cmd_fd, data, IPC_BUF_SIZE - 1);
				if (n > 0) {
					data[n] = '\0';
					ret = ipc_process_cmd(data, n);
					if (write(cmd_fd, &ret, sizeof(int)) == -1)
						log_err("Unable to send response. errno: %d", errno);
					close(cmd_fd);
				}
			}
			if (FD_ISSET(dpy_fd, &descs)) {
				while ((ev = xcb_poll_for_event(dpy)) != NULL) {
					if (ev && handler[ev->response_type & ~0x80])
						handler[ev->response_type & ~0x80](ev);
					else
						log_debug("Unimplemented event: %d", ev->response_type & ~0x80);
					free(ev);
				}
			}
			if (xcb_connection_has_error(dpy)) {
				log_err("XCB connection encountered an error.");
				running = false;
			}
		}
	}

	cleanup();
	xcb_disconnect(dpy);
	close(sock_fd);
	free(data);

	if (!running && !restart) {
		return retval;
	} else if (!running && restart) {
		char *const argv[] = {HOWM_PATH, NULL};

		execv(argv[0], argv);
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

/**
 * @brief Print debug information about the current state of howm.
 *
 * This can be parsed by programs such as scripts that will pipe their input
 * into a status bar.
 */
void howm_info(void)
{
	unsigned int w = 0;
#if DEBUG_ENABLE
	for (w = 1; w <= WORKSPACES; w++) {
		fprintf(stdout, "%u:%d:%u:%u:%u\n", cur_mode,
		       wss[w].layout, w, cur_state, wss[w].client_cnt);
	}
	fflush(stdout);
#else
	UNUSED(w);
	fprintf(stdout, "%u:%d:%u:%u:%u\n", cur_mode,
		wss[cw].layout, cw, cur_state, wss[cw].client_cnt);
	fflush(stdout);
#endif
}

/**
 * @brief Quit howm and set the return value.
 *
 * @param arg The return value that howm will send.
 */
static void quit_howm(const Arg *arg)
{
	log_warn("Quitting");
	retval = arg->i;
	running = false;
}


/**
 * @brief Cleanup howm's resources.
 *
 * Delete all of the windows that have been created, remove button and key
 * grabs and remove pointer focus.
 */
static void cleanup(void)
{
	xcb_window_t *w;
	xcb_query_tree_reply_t *q;
	uint16_t i;

	log_warn("Cleaning up");
	xcb_ungrab_key(dpy, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);

	q = xcb_query_tree_reply(dpy, xcb_query_tree(dpy, screen->root), 0);
	if (q) {
		w = xcb_query_tree_children(q);
		for (i = 0; i != q->children_len; ++i)
			delete_win(w[i]);
	free(q);
	}
	xcb_set_input_focus(dpy, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root,
			XCB_CURRENT_TIME);
	xcb_ewmh_connection_wipe(ewmh);
	if (ewmh)
		free(ewmh);
	stack_free(&del_reg);
}

/**
 * @brief Restart howm.
 *
 * @param arg Unused.
 */
static void restart_howm(const Arg *arg)
{
	UNUSED(arg);
	log_warn("Restarting.");
	running = false;
	restart = true;
}
