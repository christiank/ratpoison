/* Ratpoison X events
 * Copyright (C) 2000, 2001 Shawn Betts
 *
 * This file is part of ratpoison.
 *
 * ratpoison is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * ratpoison is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include "ratpoison.h"

/* The event currently being processed. Mostly used in functions from
   action.c which need to forward events to other windows. */
XEvent rp_current_event;

static void
new_window (XCreateWindowEvent *e)
{
  rp_window *win;
  screen_info *s;

  if (e->override_redirect)
    return;

  s = find_screen (e->parent);
  win = find_window (e->window);

  if (s && win == NULL
      && e->window != s->key_window 
      && e->window != s->bar_window 
      && e->window != s->input_window 
      && e->window != s->frame_window 
      && e->window != s->help_window)
    {
      win = add_to_window_list (s, e->window);
      update_window_information (win);
    }
}

static void
cleanup_frame (rp_window_frame *frame)
{
  rp_window *last_win;
  rp_window *win;

  win = find_window_other ();
  if (win == NULL)
    {
      set_frames_window (frame, NULL);
      return;
    }

  last_win = set_frames_window (frame, win);

  maximize (win);
  unhide_window (win);


#ifdef MAXSIZE_WINDOWS_ARE_TRANSIENTS
  if (!win->transient
      && !(win->hints->flags & PMaxSize 
	   && win->hints->max_width < win->scr->root_attr.width
	   && win->hints->max_height < win->scr->root_attr.height))
#else
  if (!win->transient)
#endif
    hide_others (win);
}

static void 
unmap_notify (XEvent *ev)
{
  rp_window_frame *frame;
  rp_window *win;

  /* ignore SubstructureNotify unmaps. */
  if(ev->xunmap.event != ev->xunmap.window
     && ev->xunmap.send_event != True)
    return;

  /* FIXME: Should we only look in the mapped window list? */
  win = find_window_in_list (ev->xunmap.window, rp_mapped_window_sentinel);

  if (win == NULL)
    return;

  switch (win->state)
    {
    case IconicState:
      PRINT_DEBUG ("Withdrawing iconized window '%s'\n", window_name (win));
      if (ev->xunmap.send_event) withdraw_window (win);
      break;
    case NormalState:
      PRINT_DEBUG ("Withdrawing normal window '%s'\n", window_name (win));
      /* If the window was inside a frame, fill the frame with another
	 window. */
      frame = find_windows_frame (win);
      if (frame) cleanup_frame (frame);
      if (frame == win->scr->rp_current_frame) set_active_frame (frame);

      withdraw_window (win);
      break;
    }

  update_window_names (win->scr);
}

static void 
map_request (XEvent *ev)
{
  rp_window *win;

  win = find_window (ev->xmap.window);
  if (win == NULL) 
    {
      PRINT_DEBUG ("Map request from an unknown window.\n");
      XMapWindow (dpy, ev->xmap.window);
      return;
    }

  PRINT_DEBUG ("Map request from a managed window\n");

  switch (win->state)
    {
    case WithdrawnState:
      if (unmanaged_window (win->w))
	{
	  PRINT_DEBUG ("Mapping Unmanaged Window\n");
	  XMapWindow (dpy, win->w);
	  break;
	}
      else
	{
	  PRINT_DEBUG ("Mapping Withdrawn Window\n");
	  map_window (win);
	  break;
	}
      break;
    case IconicState:
      PRINT_DEBUG ("Mapping Iconic window\n");
      if (win->last_access == 0)
	{
	  /* Depending on the rudeness level, actually map the
	     window. */
	  if ((rp_honour_transient_map && win->transient)
	      || (rp_honour_normal_map && !win->transient))
	    set_active_window (win);
	}
      else
	{
	  /* Depending on the rudeness level, actually map the
	     window. */
	  if ((rp_honour_transient_raise && win->transient)
	      || (rp_honour_normal_raise && !win->transient))
	    set_active_window (win);
	  else
	    {
	      if (win->transient)
		marked_message_printf (0, 0, MESSAGE_RAISE_TRANSIENT, 
				       win->number, window_name (win));
	      else
		marked_message_printf (0, 0, MESSAGE_RAISE_WINDOW,
				       win->number, window_name (win));
	    }
	}
      break;
    }
}

static void
destroy_window (XDestroyWindowEvent *ev)
{
  rp_window_frame *frame;
  rp_window *win;

  win = find_window (ev->window);
  if (win == NULL) return;

  ignore_badwindow++;

  /* A destroyed window should never have a frame, since it should
     have been cleaned up with an unmap notify event, but just in
     case... */
  frame = find_windows_frame (win);
  if (frame) cleanup_frame (frame);
  if (frame == win->scr->rp_current_frame) set_active_frame (frame);

  unmanage (win);

  ignore_badwindow--;
}

static void
configure_notify (XConfigureEvent *e)
{
  rp_window *win;

  win = find_window (e->window);

  if (win && win->state == NormalState)
    {
      if (win->height != e->height
	  || win->width != e->width
	  || win->border != e->border_width
	  || win->x != e->x
	  || win->y != e->y)
	{
	  /* The notify event was generated from a granted configure
	     request which means we must re-maximize the window. 

	     If the window has resize increments then ratpoison has to
	     know the real size of the window to increment properly. So,
	     update the structure before calling maximize. */

	  win->x = e->x;
	  win->y = e->y;
	  win->width = e->width;
	  win->height = e->height;
	  win->border = e->border_width;

	  maximize (win);
	}
    }
}


static void
configure_request (XConfigureRequestEvent *e)
{
  int border;
  XWindowChanges changes;
  rp_window *win;

  win = find_window (e->window);

  if (win)
    {
      /* Initialize border variable. */
      border = win->border;

      if (e->value_mask & CWStackMode)
	{
	  if (e->detail == Above)
	    {
	      /* Depending on the rudeness level, actually map the
		 window. */
	      if ((rp_honour_transient_raise && win->transient)
		  || (rp_honour_normal_raise && !win->transient))
		{
		  if (win->state == IconicState)
		    set_active_window (win);
		  else if (find_windows_frame (win))
		    goto_window (win);
		}
	      else
		{
		  if (win->transient)
		    marked_message_printf (0, 0, MESSAGE_RAISE_TRANSIENT,
					   win->number, window_name (win));
		  else
		    marked_message_printf (0, 0, MESSAGE_RAISE_WINDOW,
					   win->number, window_name (win));
		}

	    }

	  PRINT_DEBUG("request CWStackMode %d\n", e->detail);
	}

      PRINT_DEBUG ("'%s' window size: %d %d %d %d %d\n", window_name (win),
		   win->x, win->y, win->width, win->height, win->border);

      /* Collect the changes to be granted. */
      if (e->value_mask & CWBorderWidth)
	{
	  changes.border_width = e->border_width;
	  border = e->border_width;
	  PRINT_DEBUG("request CWBorderWidth %d\n", e->border_width);
	}

      if (e->value_mask & CWWidth)
	{
	  changes.width = e->width;
	  PRINT_DEBUG("request CWWidth %d\n", e->width);
	}
      else
	{
	  changes.width = win->width;
	}

      if (e->value_mask & CWHeight)
	{
	  changes.height = e->height;
	  PRINT_DEBUG("request CWHeight %d\n", e->height);
	}
      else
	{
	  changes.height = win->height;
	}

      if (e->value_mask & CWX)
	{
	  changes.x = e->x + border;
	  PRINT_DEBUG("request CWX %d\n", e->x);
	}
      else
	{
	  changes.x = win->x;
	}

      if (e->value_mask & CWY)
	{
	  changes.y = e->y + border;
	  PRINT_DEBUG("request CWY %d\n", e->y);
	}
      else
	{
	  changes.y = win->y;
	}

      if (e->value_mask & (CWX|CWY|CWBorderWidth|CWWidth|CWHeight))
	{
	  XConfigureWindow (dpy, win->w, 
			    e->value_mask & (CWX|CWY|CWBorderWidth|CWWidth|CWHeight), 
			    &changes);

	  send_configure (win->w, changes.x, changes.y, changes.width, changes.height,
			  border);
	}
    }
  else
    {
      PRINT_DEBUG ("FIXME: Don't handle this\n");
    }
}

static void
client_msg (XClientMessageEvent *ev)
{
  PRINT_DEBUG ("Received client message.\n");

  if (ev->message_type == wm_change_state)
    {
      rp_window *win;

      PRINT_DEBUG ("WM_CHANGE_STATE\n")

      win = find_window (ev->window);
      if (win == NULL) return;
      if (ev->format == 32 && ev->data.l[0] == IconicState)
	{
	  /* FIXME: This means clients can hide themselves without the
	     user's intervention. This is bad, but Emacs is the only
	     program I know of that iconifies itself and this is
	     generally from the user pressing C-z.  */
	  PRINT_DEBUG ("Iconify Request.\n");
	  if (win->state == NormalState)
	    {
	      rp_window *w = find_window_other();

	      if (w)
		set_active_window (w);
	      else
		blank_frame (win->scr->rp_current_frame);
	    }
	}
      else
	{
	  PRINT_ERROR ("Non-standard WM_CHANGE_STATE format\n");
	}
    }
}

static void
grab_rat ()
{
  XGrabPointer (dpy, current_screen()->root, True, 0, 
		GrabModeAsync, GrabModeAsync, 
		None, current_screen()->rat, CurrentTime);
}

static void
ungrab_rat ()
{
  XUngrabPointer (dpy, CurrentTime);
}

static void
handle_key (screen_info *s)
{
  char *keysym_name;
  rp_action *key_action;
  int revert;			
  Window fwin;			/* Window currently in focus */
  KeySym keysym;		/* Key pressed */
  unsigned int mod;		/* Modifiers */
  int rat_grabbed = 0;

  PRINT_DEBUG ("handling key...\n");

  /* All functions hide the program bar and the frame indicator. */
  if (defaults.bar_timeout > 0) hide_bar (s);
  hide_frame_indicator();

  /* Disable any alarm that was going to go off. */
  alarm (0);
  alarm_signalled = 0;

  XGetInputFocus (dpy, &fwin, &revert);
  XSetInputFocus (dpy, s->key_window, RevertToPointerRoot, CurrentTime);

  /* Change the mouse icon to indicate to the user we are waiting for
     more keystrokes */
  if (defaults.wait_for_key_cursor)
    {
      grab_rat();
      rat_grabbed = 1;
    }

  read_key (&keysym, &mod, NULL, 0);
  XSetInputFocus (dpy, fwin, revert, CurrentTime);

  if ((key_action = find_keybinding (keysym, x11_mask_to_rp_mask (mod))))
    {
      char *result;
      result = command (1, key_action->data);
      
      /* Gobble the result. */
      if (result)
	free (result);
    }
  else
    {
      /* No key match, notify user. */
      keysym_name = keysym_to_string (keysym, x11_mask_to_rp_mask (mod));
      marked_message_printf (0, 0, " %s unbound key ", keysym_name);
      free (keysym_name);
    }

  if (rat_grabbed)
    ungrab_rat();
}

static void
key_press (XEvent *ev)
{
  screen_info *s;
  unsigned int modifier;
  KeySym ks;

  s = find_screen (ev->xkey.root);

#ifdef HIDE_MOUSE
  XWarpPointer (dpy, None, s->root, 0, 0, 0, 0, s->root_attr.width - 2, s->root_attr.height - 2); 
#endif

  if (!s) return;

  modifier = ev->xkey.state;
  cook_keycode ( &ev->xkey, &ks, &modifier, NULL, 0, 1);

  if (ks == prefix_key.sym && (x11_mask_to_rp_mask (modifier) == prefix_key.state))
    {
      handle_key (s);
    }
  else
    { 
      if (current_window())
	{
	  ignore_badwindow++;
	  ev->xkey.window = current_window()->w;
	  XSendEvent (dpy, current_window()->w, False, KeyPressMask, ev);
	  XSync (dpy, False);
	  ignore_badwindow--;
	}
    }
}

/* Read a command off the window and execute it. Some commands return
   text. This text is passed back using the RP_COMMAND_RESULT
   Atom. The client will wait for this property change so something
   must be returned. */
static char *
execute_remote_command (Window w)
{
  char *result = NULL;
  Atom type_ret;
  int format_ret;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned char *req;

  if (XGetWindowProperty (dpy, w, rp_command,
			  0, 0, False, XA_STRING,
			  &type_ret, &format_ret, &nitems, &bytes_after,
			  &req) == Success
      &&
      XGetWindowProperty (dpy, w, rp_command,
			  0, (bytes_after / 4) + (bytes_after % 4 ? 1 : 0),
			  True, XA_STRING, &type_ret, &format_ret, &nitems, 
			  &bytes_after, &req) == Success)
    {
      if (req)
	{
	  PRINT_DEBUG ("command: %s\n", req);
	  result = command (0, req);
	}
      XFree (req);
    }
  else
    {
      PRINT_DEBUG ("Couldn't get RP_COMMAND Property\n");
    }

  return result;
}

/* Command requests are posted as a property change using the
   RP_COMMAND_REQUEST Atom on the root window. A Command request is a
   Window that holds the actual command as a property using the
   RP_COMMAND Atom. receive_command reads the list of Windows and
   executes their associated command. */
static void
receive_command ()
{
  char *result;
  Atom type_ret;
  int format_ret;
  unsigned long nitems;
  unsigned long bytes_after;
  void *prop_return;

  do
    {
      if (XGetWindowProperty (dpy, DefaultRootWindow (dpy),
			      rp_command_request, 0, 
			      sizeof (Window) / 4 + (sizeof (Window) % 4 ?1:0),
			      True, XA_WINDOW, &type_ret, &format_ret, &nitems,
			      &bytes_after, (unsigned char **)&prop_return) == Success)
	{
	  if (prop_return)
	    {
	      Window w;

	      w = *(Window *)prop_return;
	      XFree (prop_return);

	      result = execute_remote_command (w);
	      if (result)
		{
		  XChangeProperty (dpy, w, rp_command_result, XA_STRING,
				   8, PropModeReplace, result, strlen (result));
		  free (result);
		}
	      else
		{
		  XChangeProperty (dpy, w, rp_command_result, XA_STRING,
				   8, PropModeReplace, NULL, 0);
		}
	    }
	  else
	    {
	      PRINT_DEBUG ("Couldn't get RP_COMMAND_REQUEST Property\n");
	    }

	  PRINT_DEBUG ("command requests: %ld\n", nitems);
	}
    } while (nitems > 0);
			  
}

static void
property_notify (XEvent *ev)
{
  rp_window *win;

  PRINT_DEBUG ("atom: %ld\n", ev->xproperty.atom);

  if (ev->xproperty.atom == rp_command_request
      && ev->xproperty.window == DefaultRootWindow (dpy)
      && ev->xproperty.state == PropertyNewValue)
    {
      PRINT_DEBUG ("ratpoison command\n");
      receive_command();
    }

  win = find_window (ev->xproperty.window);
  
  if (win)
    {
      switch (ev->xproperty.atom)
	{
	case XA_WM_NAME:
	  PRINT_DEBUG ("updating window name\n");
	  update_window_name (win);
	  update_window_names (win->scr);
	  break;

	case XA_WM_NORMAL_HINTS:
	  PRINT_DEBUG ("updating window normal hints\n");
	  update_normal_hints (win);
	  break;

	case XA_WM_TRANSIENT_FOR:
	  PRINT_DEBUG ("Transient for\n");
	  win->transient = XGetTransientForHint (dpy, win->w, &win->transient_for);
	  break;

	default:
	  PRINT_DEBUG ("Unhandled property notify event\n");
	  break;
	}
    }
}

static void
colormap_notify (XEvent *ev)
{
  rp_window *win;

  win = find_window (ev->xcolormap.window);

  if (win != NULL)
    {
      XWindowAttributes attr;

      /* SDL sets the colormap just before destroying the window, so
	 ignore BadWindow errors. */
      ignore_badwindow++;

      XGetWindowAttributes (dpy, win->w, &attr);
      win->colormap = attr.colormap;

      if (win == current_window())
	{
	  XInstallColormap (dpy, win->colormap);
	}

      ignore_badwindow--;
    }
}	  

static void
focus_change (XFocusChangeEvent *ev)
{
  rp_window *win;

  /* We're only interested in the NotifyGrab mode */
  if (ev->mode != NotifyGrab) return;

  win = find_window (ev->window);

  if (win != NULL)
    {
      PRINT_DEBUG ("Re-grabbing prefix key\n");
      grab_prefix_key (win->w);
    }
}

static void
mapping_notify (XMappingEvent *ev)
{
  rp_window *cur;

  /* Remove the grab on the current prefix key */
  for (cur = rp_mapped_window_sentinel->next; 
       cur != rp_mapped_window_sentinel; 
       cur = cur->next)
    {
      ungrab_prefix_key (cur->w);
    }

  switch (ev->request)
    {
    case MappingModifier:
      update_modifier_map();
      /* This is meant to fall through.  */
    case MappingKeyboard:
      XRefreshKeyboardMapping (ev);
      break;
    }

  /* Add the grab on the current prefix key */
  for (cur = rp_mapped_window_sentinel->next; 
       cur != rp_mapped_window_sentinel; 
       cur = cur->next)
    {
      grab_prefix_key (cur->w);
    }
}

/* Given an event, call the correct function to handle it. */
static void
delegate_event (XEvent *ev)
{
  switch (ev->type)
    {
    case ConfigureRequest:
      PRINT_DEBUG ("--- Handling ConfigureRequest ---\n");
      configure_request (&ev->xconfigurerequest);
      break;

    case CreateNotify:
      PRINT_DEBUG ("--- Handling CreateNotify ---\n");
      new_window (&ev->xcreatewindow);
      break;

    case DestroyNotify:
      PRINT_DEBUG ("--- Handling DestroyNotify ---\n");
      destroy_window (&ev->xdestroywindow);
      break;

    case ClientMessage:
      PRINT_DEBUG ("--- Handling ClientMessage ---\n");
      client_msg (&ev->xclient);
      break;

    case ColormapNotify:
      PRINT_DEBUG ("--- Handling ColormapNotify ---\n");
      colormap_notify (ev);
      break;

    case PropertyNotify:
      PRINT_DEBUG ("--- Handling PropertyNotify ---\n");
      property_notify (ev);
      break;

    case MapRequest:
      PRINT_DEBUG ("--- Handling MapRequest ---\n");
      map_request (ev);
      break;

    case KeyPress:
      PRINT_DEBUG ("--- Handling KeyPress ---\n");
      key_press (ev);
      break;

    case UnmapNotify:
      PRINT_DEBUG ("--- Handling UnmapNotify ---\n");
      unmap_notify (ev);
      break;

    case FocusOut:
      PRINT_DEBUG ("--- Handling FocusOut ---\n");
      focus_change (&ev->xfocus);
      break;

    case FocusIn:
      PRINT_DEBUG ("--- Handling FocusIn ---\n");
      focus_change (&ev->xfocus);
      break;

    case MappingNotify:
      PRINT_DEBUG ("--- Handling MappingNotify ---\n");
      mapping_notify( &ev->xmapping );
      break;

    case ConfigureNotify:
      PRINT_DEBUG ("--- Handling ConfigureNotify ---\n");
      configure_notify (&ev->xconfigure);
      break;

    case MapNotify:
    case Expose:
    case MotionNotify:
    case KeyRelease:
    case ReparentNotify:
    case EnterNotify:
    case SelectionRequest:
    case SelectionNotify:
    case SelectionClear:
    case CirculateRequest:
      /* Ignore these events. */
      break;

    default:
      PRINT_DEBUG ("--- Unknown event %d ---\n",- ev->type);
    }
}

static void
handle_signals ()
{
  /* An alarm means we need to hide the popup windows. */
  if (alarm_signalled > 0)
    {
      int i;

      PRINT_DEBUG ("Alarm recieved.\n");

      for (i=0; i<num_screens; i++)
	{
	  hide_bar (&screens[i]);
	}
      hide_frame_indicator();
      alarm_signalled = 0;
    }

  if (hup_signalled > 0)
    {
      clean_up (); 
      execvp(myargv[0], myargv);
    }

  if (kill_signalled > 0)
    {
      PRINT_DEBUG ("Exiting\n");
      clean_up ();
      exit (EXIT_SUCCESS);
    }
}

/* The main loop. */
void
listen_for_events ()
{
  int x_fd;
  fd_set fds;

  x_fd = ConnectionNumber (dpy);
  FD_ZERO (&fds);

  /* Loop forever. */
  for (;;) 
    {
      handle_signals ();

      /* Report any X11 errors that have occurred. */
      if (rp_error_msg)
	{
	  marked_message_printf (0, 6, "ERROR: %s", rp_error_msg);
	  free (rp_error_msg);
	  rp_error_msg = NULL;
	}

      /* Handle the next event. */
      FD_SET (x_fd, &fds);
      XFlush(dpy);
      
      if (QLength (dpy) > 0
	  || select(x_fd+1, &fds, NULL, NULL, NULL) == 1)
	{
	  XNextEvent (dpy, &rp_current_event);
	  delegate_event (&rp_current_event);
	}
    }
}
