#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <xcb/xcb_ewmh.h>

#include "command.h"
#include "workspace.h"
#include "helper.h"
#include "howm.h"
#include "client.h"
#include "layout.h"
#include "scratchpad.h"

/**
 * @file command.c
 *
 * @author Harvey Hunt
 *
 * @date 2014
 *
 * @brief Commands are bound to keybindings or are executed as a result of a
 * message from IPC.
 */

static int cur_cnt = 1;

/**
 * @brief Change the mode of howm.
 *
 * Modes should be thought of in the same way as they are in vi. Different
 * modes mean keypresses cause different actions.
 *
 * @param mode The mode to be selected.
 */
void change_mode(const int mode)
{
	if (mode >= (int)END_MODES || mode == (int)cur_mode)
		return;
	cur_mode = mode;
	log_info("Changing to mode %d", cur_mode);
	howm_info();
}

/**
 * @brief Toggle a client between being in a floating or non-floating state.
 */
void toggle_float(void)
{
	if (!wss[cw].current)
		return;
	log_info("Toggling floating state of client <%p>", wss[cw].current);
	wss[cw].current->is_floating = !wss[cw].current->is_floating;
	if (wss[cw].current->is_floating && conf.center_floating) {
		wss[cw].current->x = (screen_width / 2) - (wss[cw].current->w / 2);
		wss[cw].current->y = (screen_height - wss[cw].bar_height - wss[cw].current->h) / 2;
		log_info("Centering client <%p>", wss[cw].current);
	}
	arrange_windows();
}

/**
 * @brief Change the width of a floating client.
 *
 * Negative values will shift the right edge of the window to the left. The
 * inverse is true for positive values.
 *
 * @param dw The amount of pixels that the window's size should be changed by.
 */
void resize_float_width(const int dw)
{
	if (!wss[cw].current || !wss[cw].current->is_floating || (int)wss[cw].current->w + dw <= 0)
		return;
	log_info("Resizing width of client <%p> from %d by %d", wss[cw].current, wss[cw].current->w, dw);
	wss[cw].current->w += dw;
	draw_clients();
}

/**
 * @brief Change the height of a floating client.
 *
 * Negative values will shift the bottom edge of the window to the top. The
 * inverse is true for positive values.
 *
 * @param dh The amount of pixels that the window's size should be changed by.
 */
void resize_float_height(const int dh)
{
	if (!wss[cw].current || !wss[cw].current->is_floating || (int)wss[cw].current->h + dh <= 0)
		return;
	log_info("Resizing height of client <%p> from %d to %d", wss[cw].current, wss[cw].current->h, dh);
	wss[cw].current->h += dh;
	draw_clients();
}

/**
 * @brief Change a floating window's y coordinate.
 *
 * Negative values will move the window up. The inverse is true for positive
 * values.
 *
 * @param dy The amount of pixels that the window should be moved.
 */
void move_float_y(const int dy)
{
	if (!wss[cw].current || !wss[cw].current->is_floating)
		return;
	log_info("Changing y of client <%p> from %d to %d", wss[cw].current, wss[cw].current->y, dy);
	wss[cw].current->y += dy;
	draw_clients();
}

/**
 * @brief Change a floating window's x coordinate.
 *
 * Negative values will move the window to the left. The inverse is true
 * for positive values.
 *
 * @param dx The amount of pixels that the window should be moved.
 */
void move_float_x(const int dx)
{
	if (!wss[cw].current || !wss[cw].current->is_floating)
		return;
	log_info("Changing x of client <%p> from %d to %d", wss[cw].current, wss[cw].current->x, dx);
	wss[cw].current->x += dx;
	draw_clients();
}

/**
 * @brief Teleport a floating client's window to a location on the screen.
 *
 * @param direction Which location to teleport the window to.
 */
void teleport_client(const int direction)
{
	if (!wss[cw].current || !wss[cw].current->is_floating
			|| wss[cw].current->is_transient)
		return;

	/* A bit naughty, but it looks nicer- doesn't it?*/
	uint16_t g = wss[cw].current->gap;
	uint16_t w = wss[cw].current->w;
	uint16_t h = wss[cw].current->h;
	uint16_t bh = wss[cw].bar_height;

	switch (direction) {
	case TOP_LEFT:
		wss[cw].current->x = g;
		wss[cw].current->y = (conf.bar_bottom ? 0 : bh) + g;
		break;
	case TOP_CENTER:
		wss[cw].current->x = (screen_width - w) / 2;
		wss[cw].current->y = (conf.bar_bottom ? 0 : bh) + g;
		break;
	case TOP_RIGHT:
		wss[cw].current->x = screen_width - w - g - (2 * conf.border_px);
		wss[cw].current->y = (conf.bar_bottom ? 0 : bh) + g;
		break;
	case CENTER:
		wss[cw].current->x = (screen_width - w) / 2;
		wss[cw].current->y = (screen_height - bh - h) / 2;
		break;
	case BOTTOM_LEFT:
		wss[cw].current->x = g;
		wss[cw].current->y = (conf.bar_bottom ? screen_height - bh : screen_height) - h - g - (2 * conf.border_px);
		break;
	case BOTTOM_CENTER:
		wss[cw].current->x = (screen_width / 2) - (w / 2);
		wss[cw].current->y = (conf.bar_bottom ? screen_height - bh : screen_height) - h - g - (2 * conf.border_px);
		break;
	case BOTTOM_RIGHT:
		wss[cw].current->x = screen_width - w - g - (2 * conf.border_px);
		wss[cw].current->y = (conf.bar_bottom ? screen_height - bh : screen_height) - h - g - (2 * conf.border_px);
		break;
	};
	draw_clients();
}

/**
 * @brief Resize the master window of a stack for the current workspace.
 *
 * @param ds The amount to resize the master window by. Treated as a
 * percentage. e.g. ds = 5 will increase the master window's size by 5% of
 * it maximum.
 */
void resize_master(const int ds)
{
	/* Resize master only when resizing is visible (i.e. in Stack layouts). */
	if (wss[cw].layout != HSTACK && wss[cw].layout != VSTACK)
		return;

	float change = ((float)ds) / 100;

	if (wss[cw].master_ratio + change >= 1
			|| wss[cw].master_ratio + change <= 0.1)
		return;
	log_info("Resizing master_ratio from <%.2f> to <%.2f>", wss[cw].master_ratio, wss[cw].master_ratio + change);
	wss[cw].master_ratio += change;
	arrange_windows();
}

/**
 * @brief Toggle the space reserved for a status bar.
 */
void toggle_bar(void)
{
	if (wss[cw].bar_height == 0 && conf.bar_height > 0) {
		wss[cw].bar_height = conf.bar_height;
		log_info("Toggled bar to shown");
	} else if (wss[cw].bar_height == conf.bar_height) {
		wss[cw].bar_height = 0;
		log_info("Toggled bar to hidden");
	} else {
		return;
	}
	xcb_ewmh_geometry_t workarea[] = { { 0, conf.bar_bottom ? 0 : wss[cw].bar_height,
				screen_width, screen_height - wss[cw].bar_height } };
	xcb_ewmh_set_workarea(ewmh, 0, LENGTH(workarea), workarea);
	arrange_windows();
}

/**
 * @brief Moves the current window to the master window, when in stack mode.
 */
void make_master(void)
{
	if (!wss[cw].current || !wss[cw].head->next
			|| wss[cw].head == wss[cw].current
			|| !(wss[cw].layout == HSTACK
			|| wss[cw].layout == VSTACK))
		return;
	while (wss[cw].current != wss[cw].head)
		move_up(wss[cw].current);
	update_focused_client(wss[cw].head);
}

/**
 * @brief Remove a list of clients from howm's delete register stack and paste
 * them after the currently focused window.
 */
void paste(void)
{
	Client *head = stack_pop(&del_reg);
	Client *t, *c = head;

	if (!head) {
		log_warn("No clients on stack.");
		return;
	}

	if (!wss[cw].current) {
		wss[cw].head = head;
		wss[cw].current = head;
		while (c) {
			xcb_map_window(dpy, c->win);
			wss[cw].current = c;
			c = c->next;
			wss[cw].client_cnt++;
		}
	} else if (!wss[cw].current->next) {
		wss[cw].current->next = head;
		while (c) {
			xcb_map_window(dpy, c->win);
			wss[cw].current = c;
			c = c->next;
			wss[cw].client_cnt++;
		}
	} else {
		t = wss[cw].current->next;
		wss[cw].current->next = head;
		while (c) {
			xcb_map_window(dpy, c->win);
			wss[cw].client_cnt++;
			if (!c->next) {
				c->next = t;
				wss[cw].current = c;
				break;
			} else {
				wss[cw].current = c;
				c = c->next;
			}
		}
	}
	update_focused_client(wss[cw].current);
}

/**
 * @brief Change the layout of the current workspace.
 *
 * @param layout Represents the layout that should be used.
 */
void change_layout(const int layout)
{
	if (layout == wss[cw].layout || layout >= END_LAYOUT || layout < ZOOM)
		return;
	wss[cw].layout = layout;
	update_focused_client(wss[cw].current);
	log_info("Changed layout from %d to %d", previous_layout,  wss[cw].layout);
	previous_layout = wss[cw].layout;
}

/**
 * @brief Change to the previous layout.
 */
void prev_layout(void)
{
	int i = wss[cw].layout < 1 ? END_LAYOUT - 1 : wss[cw].layout - 1;

	log_info("Changing to previous layout (%d)", i);
	change_layout(i);
}

/**
 * @brief Change to the next layout.
 */
void next_layout(void)
{
	int i = (wss[cw].layout + 1) % END_LAYOUT;

	log_info("Changing to layout (%d)", i);
	change_layout(i);
}

/**
 * @brief Change to the last used layout.
 *
 */
void last_layout(void)
{
	log_info("Changing to last layout (%d)", previous_layout);
	change_layout(previous_layout);
}

/**
 * @brief Restart howm.
 *
 */
void restart_howm(void)
{
	log_warn("Restarting.");
	running = false;
	restart = true;
}

/**
 * @brief Quit howm and set the return value.
 *
 * @param exit_status The return value that howm will send.
 */
void quit_howm(const int exit_status)
{
	log_warn("Quitting");
	retval = exit_status;
	running = false;
}

/**
 * @brief Toggle the fullscreen state of the current client.
 */
void toggle_fullscreen(void)
{
	if (wss[cw].current != NULL)
		set_fullscreen(wss[cw].current, !wss[cw].current->is_fullscreen);
}

/**
 * @brief Focus a client that has an urgent hint.
 */
void focus_urgent(void)
{
	Client *c;
	unsigned int w;

	for (w = 1; w <= WORKSPACES; w++)
		for (c = wss[w].head; c && !c->is_urgent; c = c->next)
			;
	if (c) {
		log_info("Focusing urgent client <%p> on workspace <%d>", c, w);
		change_ws(w);
		update_focused_client(c);
	}
}

/**
 * @brief Spawns a command.
 */
void spawn(char *cmd[])
{
	if (fork())
		return;
	if (dpy)
		close(screen->root);
	setsid();
	log_info("Spawning command: %s", (char *)cmd[0]);
	execvp((char *)cmd[0], (char **)cmd);
	log_err("execvp of command: %s failed.", (char *)cmd[0]);
	exit(EXIT_FAILURE);
}

/**
 * @brief Focus the previous workspace.
 */
void focus_prev_ws(void)
{
	log_info("Focusing previous workspace");
	change_ws(correct_ws(cw - 1));
}

/**
 * @brief Focus the last focused workspace.
 */
void focus_last_ws(void)
{
	log_info("Focusing last workspace");
	change_ws(last_ws);
}

/**
 * @brief Focus the next workspace.
 */
void focus_next_ws(void)
{
	log_info("Focusing previous workspace");
	change_ws(correct_ws(cw + 1));
}

/**
 * @brief Change to a different workspace and map the correct windows.
 *
 * @param ws Indicates which workspace howm should change to.
 */
void change_ws(const int ws)
{
	Client *c = wss[ws].head;

	if ((unsigned int)ws > WORKSPACES || ws <= 0 || ws == cw)
		return;
	last_ws = cw;
	log_info("Changing from workspace <%d> to <%d>.", last_ws, ws);
	for (; c; c = c->next)
		xcb_map_window(dpy, c->win);
	for (c = wss[last_ws].head; c; c = c->next)
		xcb_unmap_window(dpy, c->win);
	cw = ws;
	update_focused_client(wss[cw].current);

	xcb_ewmh_set_current_desktop(ewmh, 0, cw - 1);
	xcb_ewmh_geometry_t workarea[] = { { 0, conf.bar_bottom ? 0 : wss[cw].bar_height,
				screen_width, screen_height - wss[cw].bar_height } };
	xcb_ewmh_set_workarea(ewmh, 0, LENGTH(workarea), workarea);

	howm_info();
}

/**
 * @brief Set the current count for the current operator.
 *
 * @param cnt The amount of motions the operator should affect.
 */
void count(const int cnt)
{
	if (cur_state != COUNT_STATE)
		return;
	cur_cnt = cnt;
	cur_state = MOTION_STATE;
}

/**
 * @brief Tell howm which motion is to be performed.
 *
 * This allows keybinding using an external program to still use operators.
 *
 * @param target A single char representing the motion that the operator should
 * be applied to.
 */
void motion(char *target)
{
	int type;

	if (strncmp(target, "w", 1) == 0)
		type = WORKSPACE;
	else if (strncmp(target, "c", 1) == 0)
		type = CLIENT;
	else
		return;

	operator_func(type, cur_cnt);
	cur_state = OPERATOR_STATE;
	/* Reset so that qc is equivalent to q1c. */
	cur_cnt = 1;
}

/**
 * @brief Moves the current client to the workspace passed in.
 *
 * @param ws The target workspace.
 */
void current_to_ws(const int ws)
{
	client_to_ws(wss[cw].current, ws, conf.follow_move);
}

