#!/usr/bin/env python

"""PVSim Verilog Simulator GUI, in wxPython."""

__copyright__ = "Copyright 2012, Scott Forbes"
__license__ = "GPL"
__version__ = "6.3.0"

import sys, os, time, re, string, traceback, threading, cProfile, shutil
import wx
import wx.lib.dialogs as dialogs
import wx.aui
from wx.lib.wordwrap import wordwrap
import locale
import subprocess
import unittest
from optparse import OptionParser
import socket
from pubsub import pub
import multiprocessing as mp
from multiprocessing import managers

import pvsimu

show_profile = 0
disp_margin = 0
show_text = 1
use_snap = 0  # enable snap-cursor-to-edge (slows down highlighting though)
use_multiprocessing = 0

backend_msg_buff_len = 20
##backend_msg_buff_len = 1

t0 = time.perf_counter()

is_win = sys.platform.startswith("win")
is_linux = sys.platform.startswith("linux")
is_mac = not (is_win or is_linux)

# Not yet for Windows
if is_win:
    use_multiprocessing = 0

# standard font pointsize-- will be changed to calbrated size for this system
font_size = 11


def which(pgm):
    """Return the full path of a given executable."""
    for p in os.getenv("PATH").split(os.path.pathsep):
        p = os.path.join(p, pgm)
        if os.path.exists(p) and os.access(p, os.X_OK):
            return p
    return None


def pairwise(iterable):
    "s -> (s0,s1), (s2,s3), (s4, s5), ..."
    a = iter(iterable)
    return zip(a, a)


# Worker thread-- runs tells backend pvsimu to run simulation and gets results.

# notification event for thread completion
EVT_RESULT_ID = wx.NewIdRef()
main_frame = None
message_count = 0
error_count = 0


class ResultEvent(wx.PyEvent):
    def __init__(self, msg):
        wx.PyEvent.__init__(self)
        self.SetEventType(EVT_RESULT_ID)
        self.msg = msg


def print_backend(msg):
    """Write a message the GUI thread's log window and log file. No newline on end."""
    global main_frame, message_count, error_count
    wx.PostEvent(main_frame, ResultEvent(msg))
    if msg.find("*** ERROR") >= 0:
        error_count += 1
    # Allow GUI thread a chance to display queued lines every so often. Need a
    # better method: should print when this thread is waiting on Simulate().
    message_count += 1
    if (message_count % backend_msg_buff_len) == 0:
        ##time.sleep(0.01)
        time.sleep(0.1)  # must be this long to pause (why???)


class SimThread(threading.Thread):
    def __init__(self, frame):
        self.frame = frame
        threading.Thread.__init__(self)
        self.start()

    def run(self):
        global main_frame, error_count
        frame = main_frame = self.frame
        p = frame.p
        if not p.proj_dir:
            print_backend("*** No project specified ***")
            wx.PostEvent(frame, ResultEvent(None))
            return
        proj = os.path.join(p.proj_dir, p.proj_name)
        print_backend(40 * "-" + "\n")
        print_backend(f"Run: cd {p.proj_dir}\n")
        os.chdir(p.proj_dir)

        error_count = 0
        if p.test_choice == "All":
            if len(frame.test_choices) > 1:
                choices = frame.test_choices[:-1]
            else:
                choices = [None]
        else:
            choices = [p.test_choice]

        # use Python-extension pvsimu
        for test_choice in choices:
            time.sleep(0.1)
            if test_choice:
                print_backend(f"\n======= Test {test_choice} =======\n\n")
            else:
                test_choice = ""
            pvsimu.Init()
            pvsimu.SetSignalType(Signal)
            pvsimu.SetCallbacks(print_backend, print_backend)
            try:
                result = pvsimu.Simulate(proj, test_choice)
                if not result:
                    print_backend("*** ERROR: pvsimu.Simulate() returned NULL\n")
                    error_count += 1
                    ##break
                else:
                    frame.sigs, frame.n_ticks, frame.bar_signal = result
                    print_backend(f"n_ticks={frame.n_ticks} bar={frame.bar_signal}\n")
                    ##print_backend(f"\n{frame.sigs=}\n")
                    ##print_backend(f"\n{frame.sigs[6].events=}\n")
            except:
                print_backend(f"*** ERROR: pvsimu.Simulate() exception: {traceback.format_exc()}")
                ##break
            print_backend(40 * "-" + "\n")

        if error_count == 0:
            print_backend(f"{(time.perf_counter() - t0):2.1f}: Simulation done, no errors.\n")
            frame.read_order_file(f"{proj}.order")

        wx.PostEvent(frame, ResultEvent(None))


# Multiprocessing version of simulator-worker process.

log_name = None  # a process's local copy of its name
log_queue = None  # a process's local copy of mp_log_queue


def print_mp(msg):
    """Write a message the GUI thread's log window and log file. No newline on end."""
    global log_name, log_queue, error_count
    ##print("print_mp:", msg)
    log_queue.put((log_name, msg))
    if msg.find("*** ERROR") >= 0:
        error_count += 1


def mp_worker(args):
    """Multiprocessing worker: run a single simulation test in its own process.
    Has no contact with GUI process except for log_queue via print_mp(), and
    returned result.
    """
    (proj_dir, proj_name, n_ticks, bar_signal, q, test, is_final_test) = args
    global log_name, log_queue, error_count
    log_name = test
    log_queue = q
    if not proj_dir:
        print_mp("*** No project specified ***")
        ##wx.PostEvent(frame, ResultEvent(None))
        return None
    proj = os.path.join(proj_dir, proj_name)
    print_mp(40 * "-" + "\n")
    print_mp(f"Run: cd {proj_dir}\n")
    os.chdir(proj_dir)

    error_count = 0

    # use Python-extension pvsimu
    if test:
        print_mp(f"\n======= Test {test} =======\n\n")
    pvsimu.Init()
    pvsimu.SetSignalType(Signal)
    pvsimu.SetCallbacks(print_mp, print_mp)
    try:
        result = pvsimu.Simulate(proj, test)
        if not result:
            print_mp("*** ERROR: pvsimu.Simulate() returned NULL\n")
            error_count += 1
        else:
            sigs, n_ticks, bar_signal = result
            print_mp(f"{n_ticks=} {bar_signal=}\n")
            if is_final_test:
                mp_worker_result = result
    except:
        print_mp("*** ERROR: pvsimu.Simulate() exception\n")
    print_mp(40 * "-" + "\n")

    return (None, (sigs, n_ticks, bar_signal, error_count))[is_final_test]


def mp_worker_done(results):
    """Multiprocessing-worker completion callback, given list of all processes' results.
    Executes in GUI thread.
    """
    global main_frame, error_count
    print(f"mp_worker_done: {len(results)=}")
    if results != None:
        main_frame.sigs, main_frame.n_ticks, main_frame.bar_signal, error_count = results[-1]
        main_frame.OnMPResult()


class Prefs(object):
    """A preferences (config) file or registry entry."""

    def __init__(self):
        self._config = config = wx.Config("PVSim")

        valid, name, index = config.GetFirstEntry()
        while valid:
            # put entries into this Prefs object as attributes
            # (except for FileHistory entries-- those are handed separately)
            if not (len(name) == 5 and name[:4] == "file"):
                self._set_attr(name, config.Read(name))
            valid, name, index = config.GetNextEntry(index)

    def _set_attr(self, name, value):
        """Set a value as a named attribute."""
        ##print(f"Prefs _set_attr({name=}, {value=})")
        try:
            value = eval(value)
        except:
            pass
        setattr(self, name, value)

    def get(self, name, default_value):
        """Get a preference value, possibly using the default."""
        if not hasattr(self, name):
            setattr(self, name, default_value)
        value = getattr(self, name)
        if type(value) != type(default_value) and isinstance(value, str):
            try:
                value = eval(value)
            except:
                pass
        return value

    def save(self):
        """Write all attributes back to file."""
        attrs = vars(self)
        names = list(attrs.keys())
        names.sort()
        for name in names:
            if name[0] != "_":
                value = attrs[name]
                if not isinstance(value, str):
                    value = repr(value)
                ##print("save pref:", name, value, type(value))
                if not self._config.Write(name, value):
                    raise IOError(f"Prefs.Write({name}, {value})")
        self._config.Flush()

    def config(self):
        """Return Config object."""
        return self._config


class Logger(object):
    """A simple error-handling class to write exceptions to a text file."""

    def __init__(self, name, text_ctrl):
        self.log_file = open(name + ".logwx", "w")
        self.text_ctrl = text_ctrl

    def write(self, s):
        try:
            self.log_file.write(s)
            ##self.text_ctrl.WriteText(s)
            # thread-safe version
            wx.CallAfter(self.text_ctrl.WriteText, s)
        except:
            pass  # don't recursively crash on errors

    def flush(self):
        pass


class IPCThread(threading.Thread):
    """Inter-Process Communication Thread."""

    def __init__(self):
        threading.Thread.__init__(self)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.bind(("127.0.0.1", 8080))
        self.socket.listen(5)
        self.daemon = True
        self.running = True
        self.start()

    def run(self):
        while self.running:
            try:
                client, addr = self.socket.accept()
                rlines = client.recv(4096).split()
                client.send(b"\n")
                client.close()
                if b"GET" in rlines:
                    i = rlines.index(b"GET")
                    try:
                        if len(rlines) > i:
                            cmd = rlines[i + 1][1:].decode()
                            wx.CallAfter(pub.sendMessage, "doCmd", cmd=cmd)
                    except Exception as e:
                        print("rlines=", rlines, "Exception:", e)

            except socket.error as msg:
                if self.running:  # Check if the socket error is due to stopping the thread
                    print(f"Socket error: {msg}")
                break

        self.socket.close()

    def stop(self):
        self.running = False
        # Connect to the socket to unblock the accept call
        try:
            socket.socket(socket.AF_INET, socket.SOCK_STREAM).connect(("127.0.0.1", 8080))
        except Exception as e:
            print("Error connecting to socket to unblock: ", e)


###############################################################################
# GUI SECTION
###############################################################################

# Waveform display colors

# colors from old PVSim, adjusted to match on screen
red = wx.Colour(255, 0, 0)
yellow = wx.Colour(255, 255, 0)
green = wx.Colour(0, 150, 0)
blue = wx.Colour(0, 0, 255)
ltblue = wx.Colour(47, 254, 255)
dkblue = wx.Colour(10, 0, 200)
dkgray = wx.Colour(128, 128, 128)

level_to_voltage = {"L": 0, "Z": 0.5, "H": 1, "X": 2, "S": 0}
tick_places = 3
tick_ns = 10**tick_places


class TimingPane(wx.ScrolledWindow):
    """A signal timing display pane."""

    def __init__(self, parent, frame):
        self.frame = frame
        p = frame.p
        wx.ScrolledWindow.__init__(self, parent, -1, (disp_margin, disp_margin))
        self.waves_bitmap = None
        self.x_max = 0
        self.y_max = 0
        self.xs_pos_last = None
        self.ys_pos_last = None
        self.w_win_last = None
        self.h_win_last = None
        self.w_tick_last = None
        self.t_center = None
        p.w_tick = frame.w_tick
        self.have_screen_pos = False
        self.x_mouse = 0  # screen position of mouse pointer
        self.y_mouse = 0
        self.mouse_down = False  # True if mouse button down
        self.mouse_was_down = False  # True if mouse was down previously
        # (snapped tick, name index) where mouse click started, or None
        self.ti_cursors_start = None
        self.i_name_cursor = None
        self.t_time_cursor = None  # snapped tick of last mouse down
        self.clip = None
        self.SetDoubleBuffered(True)
        ##self.SetBackgroundColour("WHITE")
        self.Bind(wx.EVT_PAINT, self.OnPaint)
        self.Bind(wx.EVT_LEFT_DOWN, self.OnLeftDown)
        self.Bind(wx.EVT_LEFT_DCLICK, self.OnLeftDClick)
        self.Bind(wx.EVT_RIGHT_DOWN, self.OnRightDown)
        self.Bind(wx.EVT_LEFT_UP, self.OnLeftUp)
        self.Bind(wx.EVT_MOTION, self.OnMotion)
        self.Bind(wx.EVT_LEAVE_WINDOW, self.OnLeave)
        try:
            # wx 2.9: notify *after* scrollbar updated to get proper pos
            self.Bind(wx.EVT_SCROLLWIN_CHANGED, self.OnScrollWin)
        except:
            # or make do with 2.8
            self.Bind(wx.EVT_SCROLLWIN, self.OnScrollWin)
        self.Bind(wx.EVT_KEY_DOWN, self.OnKeyDown)
        self.AdjustMyScrollbars()

    def get_width(self):
        return self.x_max + disp_margin

    def get_height(self):
        return self.y_max + disp_margin

    def x_pos_to_time_tick(self, x):
        """Convert horizontal pixel position to time tick."""
        p = self.frame.p
        x_scroll_pos = self.GetScrollPos(wx.HORIZONTAL)
        t = (x + x_scroll_pos * self.w_scroll - self.w_names) // p.w_tick
        return t

    def y_pos_to_index(self, y):
        """Convert vertical pixel position to dispSig list index."""
        # index - 1 because first row is header
        index = int((y + self.y0) / self.h_row) - 1
        ##print(f"y_pos_to_index: {y=} {self.y0=} {self.h_row=} --> {index=}")
        return index

    def AdjustMyScrollbars(self, start_tick=None, end_tick=None):
        """Recompute timing pane dimensions and scrollbar positions.
        Sets scrollbars to (p.x_scroll_pos,p.y_scroll_pos), scrolling image if self.have_screen_pos
        is yet to be set.
        """
        frame = self.frame
        p = frame.p

        # rebuild list of displayed signals
        disp_sigs = []
        for sig in list(frame.sigs.values()):
            if sig.isDisplayed:
                disp_sigs.append(sig)
        self.disp_sigs = disp_sigs
        n_disp = len(disp_sigs)

        # compute signal row dimensions based on timing pane scale factor
        s = p.timing_scale
        # pixel width of signal names part of diagram
        self.w_names = w_names = int(100 * s)
        # signal's row-to-row spacing in display
        self.h_row = h_row = int(10 * s)
        ##print(f"{h_row=}")
        # amount to raise up text (pixels)
        self.text_baseline = (3, 2)[is_mac]

        # x_max,y_max: timing pane virtual dimensions
        w_waves = frame.w_tick * frame.n_ticks
        self.x_max = x_max = int(w_waves + w_names + 0.5)
        self.y_max = y_max = (n_disp + 1) * h_row
        # w_scroll: width of one scroll unit
        self.w_scroll = w_scroll = 20
        # x_scroll_max,y_scroll_max: timing pane virtual dimensions in scroll units
        self.x_scroll_max = x_scroll_max = int(x_max / w_scroll + 0.5)
        self.y_scroll_max = y_scroll_max = int(y_max / h_row + 0.5)
        # w_win,h_win: timing pane aperture size
        w_win, h_win = self.GetClientSize()
        w_win -= disp_margin
        h_win -= disp_margin
        w_wave_part = w_win - w_names

        # if a tick range to view is given, use it
        x_scroll_pos_pre = self.GetScrollPos(wx.HORIZONTAL)
        if start_tick:
            if start_tick > end_tick:
                start_tick, end_tick = end_tick, start_tick
            dt = end_tick - start_tick
            ##print(f"{w_wave_part=}")
            frame.w_tick = 0.9 * w_wave_part / dt
            w_waves = frame.w_tick * frame.n_ticks
            self.x_max = x_max = int(w_waves + w_names + 0.5)
            self.x_scroll_max = x_scroll_max = int(x_max / w_scroll + 0.5)
            p.x_scroll_pos = int((start_tick - 0.05 * dt) * frame.w_tick / w_scroll)
            p.y_scroll_pos = self.GetScrollPos(wx.VERTICAL)
            self.have_screen_pos = False
            p.w_tick = None

        # scroll to (p.x_scroll_pos,p.y_scroll_pos) if self.have_screen_pos is yet to be set
        if not self.have_screen_pos:
            self.SetScrollPos(wx.HORIZONTAL, p.get("x_scroll_pos", 0))
            self.SetScrollPos(wx.VERTICAL, p.get("y_scroll_pos", 0))
            ##self.Scroll(p.x_scroll_pos, p.y_scroll_pos)
            self.have_screen_pos = True

        if p.w_tick != frame.w_tick:
            if start_tick:
                # expanding time to selection: no adjustments
                x_scroll_pos = p.x_scroll_pos
                y_scroll_pos = p.y_scroll_pos
            else:
                # have zoomed: keep center tick centered
                # determine center tick before changing zoom
                x_scroll_pos = self.GetScrollPos(wx.HORIZONTAL)
                y_scroll_pos = self.GetScrollPos(wx.VERTICAL)
                t_left = x_scroll_pos * w_scroll / p.w_tick
                w_half = w_wave_part / 2
                dt_half = w_half / p.w_tick
                t_center = t_left + dt_half
                # recalculate scroll position with new zoom
                dt_half = w_half / frame.w_tick
                t_left = t_center - dt_half
                x_scroll_pos = max(int(t_left * frame.w_tick / w_scroll + 0.5), 0)
            ##print(f"Adjust {start_tick=} {end_tick=} ",
            ##        f"{x_scroll_pos_pre=} -> {x_scroll_pos} ",
            ##        f"{x_scroll_max=} x/m={100*x_scroll_pos/x_scroll_max} ",
            ##        f"wid={p.w_tick} -> {frame.w_tick}")
            p.w_tick = frame.w_tick
            self.SetScrollbars(
                w_scroll, h_row, x_scroll_max, y_scroll_max, x_scroll_pos, y_scroll_pos, False
            )
            ##self.Scroll(x_scroll_pos, -1)
        else:
            # no zoom: just insure scrollbars match scrolled contents
            p.x_scroll_pos = self.GetScrollPos(wx.HORIZONTAL)
            p.y_scroll_pos = self.GetScrollPos(wx.VERTICAL)
            self.SetScrollbars(
                w_scroll, h_row, x_scroll_max, y_scroll_max, p.x_scroll_pos, p.y_scroll_pos, False
            )

        self.xs_pos_last = -1
        self.Refresh()

    def GatherLineSegment(self, dc, sig, x_prev, v_prev, x, v, show_sig_name=True):
        """Prepare to draw a line segment of one signal.
        Gathers lines and text for the next segment of a signal from x_prev to x,
        using v_prev to v to determine its shape and contents.

        (x_prev, y_prev):   end of previous event edge
        (x-1, y_prev):  start of current event edge
        (x, y):     end of current event edge
        """
        y_low = self.y_low
        lines = self.lines
        if self.debug:
            print(f"GatherLineSegment: {x_prev=} {v_prev=} {x=} {v=}")

        y_prev = y_low - int(level_to_voltage[v_prev] * self.h_hi_low)
        y = y_low - int(level_to_voltage[v] * self.h_hi_low)
        lines.append((x_prev, y_prev, x - 1, y_prev))
        lines.append((x - 1, y_prev, x, y))
        # include signal name every so often
        if (
            show_text
            and x - self.w - self.w_event_name > self.x_text
            and (x - x_prev) > (self.w + 4)
            and show_sig_name
        ):
            (w, h) = dc.GetTextExtent(sig.name)
            self.texts.append(sig.name)
            baseline = self.text_baseline
            self.text_coords.append(
                (x - w - 2, y_low - self.h_name + 1 + (v_prev == "H") - baseline)
            )
            self.text_foregrounds.append(green)
            self.x_text = x

    def GatherBusSegment(self, dc, sig, x_prev, v_prev, x, v):
        """Prepare to draw a bus segment of one signal.
        Gathers lines and text for the next segment of a signal from x_prev to x,
        using v_prev to v to determine its shape and contents.

        (x_prev, y_prev):   end of previous event
        (x, y):     end of current event
        """
        y_low = self.y_low
        lines = self.lines
        if self.debug:
            print(f"GatherBusSegment: {x_prev=} {v_prev=} {x=} {v=}")

        y_hi = y_low - self.h_hi_low
        if v_prev == None or v_prev == "X":
            # a "don't care" or unresolvable-edges segment: filled with gray
            self.X_polys.append(((x_prev, y_low), (x_prev, y_hi), (x, y_hi), (x, y_low)))
        else:
            lines.append((x_prev, y_low, x, y_low))
            lines.append((x, y_low, x, y_hi))
            lines.append((x, y_hi, x_prev, y_hi))
            lines.append((x_prev, y_hi, x_prev, y_low))
            # include bus-value text if there's room for it
            n_bits = abs(sig.l_sub - sig.r_sub) + 1
            if show_text and isinstance(v_prev, int):
                value_name = f"{v_prev:0{(n_bits + 2) // 4}X}"
                (w, h) = dc.GetTextExtent(value_name)
                if x - w - 2 > x_prev:
                    dxp = min((x - x_prev) // 2, 100)
                    self.texts.append(value_name)
                    baseline = self.text_baseline
                    self.text_coords.append(
                        (x_prev + dxp - w // 2, y_low - self.h_name + 2 - baseline)
                    )
                    self.text_foregrounds.append(wx.BLACK)

    def DrawAttachedText(self, dc, sig, x, v, x_begin, x_end):
        """Draw a attached text on one signal."""
        if self.debug:
            print("DrawAttachedText: {x=} {v=}")
        if not show_text:
            return

        in_front, text = v
        color = blue
        weight = wx.BOLD
        # extract color codes from text, if any
        while len(text) > 2 and text[0] == "#":
            c = text[1]
            text = text[2:]
            if c == "r":
                color = red
            elif c == "g":
                color = green
            elif c == "b":
                color = glue
            elif c == "y":
                color = yellow
            elif c == "p":
                weight = wx.NORMAL
        (w, h) = dc.GetTextExtent(text)
        if level_to_voltage[in_front]:
            # place text in front of x
            x -= w + 2
            self.x_text = x
        else:
            # place text after x
            x += 2
        y = self.y_low - self.h_name + 1 - self.text_baseline
        if x > x_begin and x + w < x_end:
            if weight != wx.NORMAL:
                dc.SetFont(wx.Font(self.name_font_size, wx.SWISS, wx.NORMAL, weight))
                dc.SetTextForeground(color)
                dc.DrawText(text, x, y)
                dc.SetFont(wx.Font(self.name_font_size, wx.SWISS, wx.NORMAL, wx.NORMAL))
            else:
                self.texts.append(text)
                self.text_coords.append((x, y))
                self.text_foregrounds.append(color)

    def OnPaint(self, event):
        """Draw timing window.
        Only draws the (L H X Z) subset of possible signal levels.
        """
        ##print("OnPaint: size=", self.x_max, self.y_max)
        frame = self.frame
        p = frame.p
        self.SetBackgroundColour("WHITE")

        x_max = self.x_max
        y_max = self.y_max
        w_win, h_win = self.GetClientSize()
        w_win -= disp_margin
        h_win -= disp_margin

        # (t, i, v): time, index to signal, voltage of indexed signal.
        # (x, y): scrolled-screen coords, in pixels, (x0,y0): upper left corner
        x_scroll_pos = self.GetScrollPos(wx.HORIZONTAL)
        y_scroll_pos = self.GetScrollPos(wx.VERTICAL)
        h_row = self.h_row  # signal's row-to-row spacing in display
        self.x0 = x0 = x_scroll_pos * self.w_scroll
        self.y0 = y0 = y_scroll_pos * h_row

        w_names = self.w_names  # pixel width of signal names part of diagram
        x_begin = w_names
        # right, bottom edge of visible waveforms
        x_end = min(w_win, x_max)
        y_end = min(h_win, y_max)
        dx = frame.w_tick
        baseline = self.text_baseline
        xy_edge_highlight = None
        x, y = self.x_mouse, self.y_mouse
        t_mouse = self.x_pos_to_time_tick(x)
        i_mouse = self.y_pos_to_index(y)
        if x < self.w_names or x > x_end:
            t_mouse = None
        ##print(f"{i_mouse=} {t_mouse=} {self.mouse_down)

        if use_snap or not (
            x_scroll_pos == self.xs_pos_last
            and y_scroll_pos == self.ys_pos_last
            and w_win == self.w_win_last
            and h_win == self.h_win_last
            and p.w_tick == self.w_tick_last
        ):
            self.xs_pos_last = x_scroll_pos
            self.ys_pos_last = y_scroll_pos
            self.w_win_last = w_win
            self.h_win_last = h_win
            self.w_tick_last = p.w_tick
            dc = wx.MemoryDC()
            self.waves_bitmap = wx.Bitmap(w_win, h_win)
            dc.SelectObject(self.waves_bitmap)
            ##print("OnPaint clip=", dc.GetClippingBox())
            dc.SetBackground(wx.Brush(self.GetBackgroundColour()))
            dc.Clear()

            s = p.timing_scale
            self.h_hi_low = int(8 * s)  # signal's L-to-H spacing
            ##self.h_hi_weak = int(s)  # signal's L-to-V and H-to-W spacing
            self.h_name = int(8 * s)  # signal name text size
            self.name_font_size = self.h_name * font_size // 11
            self.w_event_name = int(200 * s)  # min pixels from prev event, drawing
            # draw names separator line
            dc.SetPen(wx.Pen("BLACK"))
            ##print("  OnPaint gen line=", x_begin, x_scroll_pos, y_scroll_pos)
            dc.DrawLine(x_begin - 1, 0, x_begin - 1, h_win)
            dc.SetFont(wx.Font(self.name_font_size, wx.SWISS, wx.NORMAL, wx.NORMAL))
            ##print("UserScale=", dc.GetUserScale(), "Mode=", dc.GetMapMode(),
            ##    "PPI=", dc.GetPPI())

            grid_pen = wx.Pen(ltblue, 1, wx.SOLID)
            have_drawn_bars = False

            # draw vertical bars at rising edges of given 'bar' signal
            if frame.bar_signal != None:
                sig = frame.sigs[frame.bar_signal]
                # x_text: next allowable position of drawn text
                x_text = x_begin
                x_prev = -x0
                for i in range(0, len(sig.events), 2):
                    t, v = sig.events[i : i + 2]
                    x = w_names + int(t * dx) - x0
                    if x > x_end:
                        break
                    if x > x_begin and x > x_prev + 4 and v == "H":
                        bar_name = f"{t / tick_ns:g}"
                        if show_text:
                            (w, h) = dc.GetTextExtent(bar_name)
                            if x - w // 2 - 5 > x_text:
                                dc.SetTextForeground(dkblue)
                                dc.DrawText(bar_name, x - w // 2, 2 - baseline)
                                x_text = x + w // 2
                                have_drawn_bars = True
                        dc.SetPen(grid_pen)
                        dc.DrawLine(x, h_row, x, y_max)
                    x_prev = x

            # if bar signal too fine, just draw 1 usec intervals
            if show_text and not have_drawn_bars:
                x_text = x_begin
                for t in range(0, frame.n_ticks, 10000):
                    x = w_names + int(t * dx) - x0
                    if x > x_end:
                        break
                    if x > x_begin:
                        bar_name = f"{t / tick_ns:g}"
                        (w, h) = dc.GetTextExtent(bar_name)
                        if x - w - 5 > x_text:
                            dc.SetTextForeground(dkblue)
                            dc.DrawText(bar_name, x - w // 2, 2 - baseline)
                            x_text = x
                        ##dc.SetPen(grid_pen)
                        ##dc.DrawLine(x, h_row, x, y_max)

            # draw each signal's name and waveform
            i = (y0 + h_row - 1) // h_row
            y_low = (i + 2) * h_row - y0

            for sig in self.disp_sigs[i:]:
                self.y_low = y_low
                if 0 and sig.is_bus:
                    name = f"{sig.name}[{sig.l_sub}:{sig.r_sub}]"
                else:
                    name = sig.name
                if show_text:
                    (w, h) = dc.GetTextExtent(name)
                    self.w = w
                    dc.SetTextForeground(green)
                    dc.DrawText(name, x_begin - w - 3, y_low - self.h_name + 1 - baseline)

                dc.SetPen(grid_pen)
                dc.DrawLine(x_begin, y_low, x_end, y_low)
                self.lines = []
                self.X_polys = []
                self.texts = []
                self.text_coords = []
                self.text_foregrounds = []
                # x_prev and v_prev are prior-displayed edge position and value
                x_prev = xpd = max(w_names, x_begin - 1)
                v_prev = sig.events[1]
                self.x_text = 0
                self.debug = 0 and (sig.name == "Vec1[1:0]")

                # gather-to-draw each segment of a signal
                for t, v in pairwise(sig.events):
                    x = min(w_names - x0 + int(t * dx), x_end + 2)
                    if self.debug:
                        print(f"\nDraw loop: {sig.name} {x=} {x_prev=}")
                    if sig.is_bus or len(v) == 1:
                        if x >= x_begin:
                            if x > x_prev + 1:
                                if v != v_prev:
                                    if (
                                        use_snap
                                        and i_mouse == i
                                        and t_mouse
                                        and abs(t_mouse - t) < 500
                                    ):
                                        # snap mouse ptr to nearby edge
                                        xy_edge_highlight = (
                                            x + x0,
                                            y_low - self.h_hi_low // 2 + y0,
                                        )
                                        t_mouse = t

                                    if sig.is_bus or v_prev == "X":
                                        self.GatherBusSegment(dc, sig, x_prev, v_prev, x, v)
                                    else:
                                        self.GatherLineSegment(
                                            dc, sig, x_prev, v_prev, x, v, x < x_end
                                        )
                                    if x_prev != xpd:
                                        self.GatherBusSegment(dc, sig, xpd, "X", x_prev, v_prev)
                                    xpd = x_prev = x
                            else:
                                x_prev = x
                        if x > x_end:
                            break
                        v_prev = v
                    else:
                        self.DrawAttachedText(dc, sig, x, v, x_begin, x_end)

                # finish gathering-to-draw signal up to right edge
                if x_end > x_prev + 1:
                    if sig.is_bus or v_prev == "X":
                        self.GatherBusSegment(dc, sig, x_prev, v_prev, x_end, v)
                    else:
                        if len(v) == 1:
                            self.GatherLineSegment(dc, sig, x_prev, v_prev, x_end, v, False)
                if x_prev != xpd:
                    self.GatherBusSegment(dc, sig, xpd, "X", x_prev, v_prev)

                # draw gathered lines and polygons
                dc.SetPen(wx.Pen("BLACK"))
                dc.DrawLineList(self.lines)
                if len(self.X_polys) > 0:
                    dc.SetBrush(wx.Brush(dkgray))
                    dc.DrawPolygonList(self.X_polys)
                dc.DrawTextList(self.texts, self.text_coords, self.text_foregrounds)
                self.debug = False
                y_low += h_row
                i += 1
                if y_low > y_end:
                    break
            dc.SelectObject(wx.NullBitmap)

        pdc = wx.PaintDC(self)
        try:
            # use newer GCDC graphics under GTK for cursor's alpha channel
            dc = wx.GCDC(pdc)
        except:
            dc = pdc

        self.PrepareDC(dc)
        dc.DrawBitmap(self.waves_bitmap, x0, y0)

        # determine cursor state
        if self.mouse_down:
            self.i_name_cursor = None
            self.t_time_cursor = None
            if not self.mouse_was_down:
                self.ti_cursors_start = t_mouse, i_mouse
            else:
                if self.mouse_area == "names":
                    self.i_name_cursor = i_mouse
                else:
                    self.t_time_cursor = t_mouse
        self.mouse_was_down = self.mouse_down

        # draw name or time cursor, if active
        x_begin += x0
        x_end += x0
        y_end += y0
        dc.SetFont(wx.Font(self.name_font_size, wx.SWISS, wx.NORMAL, wx.NORMAL))
        dc.SetTextForeground(dkblue)
        if not t_mouse:
            t_mouse = None
            if self.ti_cursors_start:
                t_mouse = self.ti_cursors_start[0]
        if t_mouse:
            s = locale._format(f"%3.{tick_places}f", float(t_mouse) / tick_ns, grouping=True)
            if self.t_time_cursor:
                dc.DrawText(s, x0 + 1, y0 + 2 - baseline)
            else:
                dc.DrawText(f"{s} ns", x0 + 10, y0 + 2 - baseline)
        if self.ti_cursors_start:
            dc.SetBrush(wx.Brush(wx.Colour(255, 255, 0, 64)))
            dc.SetPen(wx.Pen(wx.Colour(255, 255, 0, 64)))
            tm0, im0 = self.ti_cursors_start
            if self.i_name_cursor:
                y = max((self.i_name_cursor + 1) * h_row, y0 + h_row)
                ym0 = max((im0 + 1) * h_row, y0 + h_row)
                if ym0 < y:
                    y, ym0 = ym0, y
                dc.DrawRectangle(x0, y, x_end - x0, ym0 - y)
            elif self.t_time_cursor:
                xm0 = min(max(w_names + int(tm0 * dx), x_begin), x_end)
                xm = min(max(w_names + int(self.t_time_cursor * dx), x_begin), x_end)
                if xm0 > xm:
                    xm, xm0 = xm0, xm
                dc.DrawRectangle(xm0, y0, xm - xm0, y_end - y0)
                dt = float(abs(self.t_time_cursor - tm0)) / tick_ns
                s = locale._format(f"%3.{tick_places}f", dt, grouping=True)
                dc.DrawText(f"\u0394{s} ns", x0 + 65, y0 + 2 - baseline)

        if xy_edge_highlight:
            # snap: draw a small blue box to highlight signal edge
            x, y = xy_edge_highlight
            dc.SetBrush(wx.TRANSPARENT_BRUSH)
            dc.SetPen(wx.Pen(blue))
            dc.DrawRectangle(x - 3, y - 3, 6, 6)

        ##print(f"{time.perf_counter() - t0:3.3f}: OnPaint() end")

    def OnLeftDown(self, event):
        """Left mouse button pressed: start a cursor drag-select."""
        ##mods = ('-','A')[event.AltDown()] + ('-','S')[event.ShiftDown()] + \
        ##       ('-','M')[event.MetaDown()]
        ##print("OnLeftDown", mods)
        self.mouse_down = True

        x, y = event.GetPosition()
        self.x_mouse = x
        self.y_mouse = y
        self.mouse_clicked = True
        ##print("OnLeftDown: x,y=", x, y)

        if x < self.w_names:
            self.mouse_area = "names"
        else:
            self.mouse_area = "main"

        self.Refresh()
        event.Skip()

    def OnLeftUp(self, event):
        """Mouse button released."""
        self.mouse_down = False
        event.Skip()

    def OnLeftDClick(self, event):
        """Left mouse button double-clicked."""
        x, y = event.GetPosition()
        ##print("OnLeftDClick: x,y=", x, y)
        if x < self.w_names:
            i = self.y_pos_to_index(y)
            if i < len(self.disp_sigs):
                sig = self.disp_sigs[i]
                if sig.src_file:
                    self.frame.GotoSource(sig)
        event.Skip()

    def OnRightDown(self, event):
        """Right mouse button pressed."""
        x, y = event.GetPosition()
        if x < self.w_names:
            i = self.y_pos_to_index(y)
            if i < len(self.disp_sigs):
                sig = self.disp_sigs[i]
                if sig.is_bus:
                    # name area: expand bus bits
                    self.clip = sig.bit_sigs
                    self.ti_cursors_start = None, i + 1
                    self.i_name_cursor = i + 2
                    self.OnPaste(None)
        event.Skip()

    def OnMotion(self, event):
        """Mouse moved: update cursor position."""
        frame = self.frame
        p = frame.p
        x, y = event.GetPosition()
        self.x_mouse = x
        self.y_mouse = y
        self.mouse_down = not event.Moving()
        ##print("OnMotion: x,y=", x, y, "moving=", event.Moving())

        self.Refresh()
        event.Skip()

    def OnScrollWin(self, event):
        """Window scrolling event: update displayed cursor position."""
        ##print("OnScrollWin: orient=", event.GetOrientation())
        if event.GetOrientation() == wx.HORIZONTAL:
            self.Refresh()
        event.Skip()

    def OnLeave(self, event):
        """Mouse left timing pane area."""
        self.x_mouse = 0
        self.Refresh()
        event.Skip()

    def OnKeyDown(self, event):
        """Regular key was pressed."""
        p = self.frame.p
        k = event.GetKeyCode()
        m = event.GetModifiers()
        ##print("KeyDown:", k, m)

        # handle Command-arrow combinations to scroll to ends
        if m == wx.MOD_CONTROL:
            x_scroll_pos = self.GetScrollPos(wx.HORIZONTAL)
            y_scroll_pos = self.GetScrollPos(wx.VERTICAL)
            if k == wx.WXK_UP:
                y_scroll_pos = 0
            elif k == wx.WXK_DOWN:
                y_scroll_pos = self.y_scroll_max
            elif k == wx.WXK_LEFT:
                x_scroll_pos = 0
            elif k == wx.WXK_RIGHT:
                x_scroll_pos = self.x_scroll_max
            else:
                k = None
            if k:
                p.x_scroll_pos, p.y_scroll_pos = x_scroll_pos, y_scroll_pos
                self.have_screen_pos = False
                self.AdjustMyScrollbars()

        event.Skip()
        self.Refresh()

    def NameCursorIndecies(self):
        """Get indecies into self.disp_sigs[] of signals selected by name cursor."""
        i0 = self.i_name_cursor
        if not i0:
            return None, None
        i1 = self.ti_cursors_start[1]
        if i0 > i1:
            i0, i1 = i1, i0
        return i0, i1

    def OnCut(self, event):
        """Cut selected signals to the clipboard."""
        i0, i1 = self.NameCursorIndecies()
        if i0:
            self.clip = self.disp_sigs[i0:i1]
            for sig in self.clip:
                sig.isDisplayed = False
            self.i_name_cursor = None
            p = self.frame.p
            self.AdjustMyScrollbars()

    def OnCopy(self, event):
        """Copy selected signals to the clipboard."""
        i0, i1 = self.NameCursorIndecies()
        if i0:
            self.clip = self.disp_sigs[i0:i1]

    def OnPaste(self, event):
        """Paste clipboard signals to cursor position."""
        i0, i1 = self.NameCursorIndecies()
        if i0 and self.clip:
            p = self.frame.p
            new_sigs = {}
            for sig in self.disp_sigs[:i0]:
                new_sigs[sig.index] = sig
            for sig in self.clip:
                new_sigs[sig.index] = sig
                sig.isDisplayed = True
            for sig in self.disp_sigs[i0:]:
                new_sigs[sig.index] = sig
            self.frame.sigs = new_sigs
            self.i_name_cursor = i0
            self.ti_cursors_start = None, i0 + len(self.clip)
            self.AdjustMyScrollbars()

    def Find(self, find_string, flags, from_top=False):
        """Find string within a signal name and center window on it."""
        p = self.frame.p
        ignore_case = not (flags & wx.FR_MATCHCASE)
        whole = flags & wx.FR_WHOLEWORD
        s = find_string
        if ignore_case:
            s = s.upper()
        i0 = -1
        for i0, sig in enumerate(self.disp_sigs):
            if from_top or i0 > self.i_name_cursor:
                name = sig.name
                if ignore_case:
                    name = name.upper()
                if (name.find(s) >= 0, name == s)[whole]:
                    ##print("Found signal", i0, sig.name)
                    self.i_name_cursor = i0
                    self.ti_cursors_start = None, i0 + 1
                    break
        else:
            ##print(f"'{find_string}' not found.")
            wx.Bell()
            self.i_name_cursor = 0
            self.ti_cursors_start = None, 0

        if i0 >= 0:
            w_win, h_win = self.GetClientSize()
            p.y_scroll_pos = (i0 * self.h_row - h_win // 2) // self.h_row
            p.x_scroll_pos = self.GetScrollPos(wx.HORIZONTAL)
            self.have_screen_pos = False
            self.AdjustMyScrollbars()


class Signal(object):
    """A signal or bus of signals, read from events file."""

    def __init__(self, index, name, events, src_file, src_pos, src_pos_obj_name, is_bus=False,
                 l_sub=0, r_sub=0):
        # args must match Py_BuildValue() in PVSimExtension.cc newSignalPy()
        self.index = index
        self.name = name
        self.src_file = src_file
        self.src_pos = src_pos
        self.src_pos_obj_name = src_pos_obj_name
        self.events = events
        self.isDisplayed = True  # must be named "isDisplayed"
        self.is_bus = is_bus
        self.l_sub = l_sub
        self.r_sub = r_sub
        self.bus_sig = None
        if is_bus:
            self.bit_sigs = []
        else:
            self.bit_sigs = None
        self.sub = None

    def __str__(self):
        return f"Signal('{self.name}', {self.index})"

    def __repr__(self):
        return self.__str__()


class PVSimFrame(wx.Frame):
    """The main GUI frame."""

    def __init__(self, parent):
        global font_size

        self.mp_pool = None
        self.mp_log_queue = None
        sp = wx.StandardPaths.Get()
        if is_mac:
            # in OSX, get PVSim.app/Contents/Resources/pvsim.py path
            # because GetResourcesDir() returns Python.app path instead
            self.res_dir = os.path.dirname(sys.argv[0])
        else:
            self.res_dir = sp.GetResourcesDir()
        if is_mac:
            pvsim_path = os.path.dirname(os.path.dirname(self.res_dir))
        elif is_linux:
            pvsim_path = os.path.abspath(sys.argv[0])
        else:
            pvsim_path = sp.GetExecutablePath()
        pvsim_dir = os.path.dirname(pvsim_path)
        sep = os.path.sep
        if len(pvsim_dir) > 0:
            pvsim_dir += sep
        self.pvsim_dir = pvsim_dir
        self.p = p = Prefs()

        # data_dir is where log file gets written
        data_dir = sp.GetUserLocalDataDir()
        if not os.path.exists(data_dir):
            os.makedirs(data_dir)
        os.chdir(data_dir)

        # use previous project, or copy in example project if none
        p.get("proj_name", "example1")
        proj_dir = p.get("proj_dir", None)
        if not (proj_dir and os.path.exists(proj_dir)):
            proj_dir = data_dir
            ##print(f"New project directory {proj_dir}")
            for f in ("example1.psim", "example1.v"):
                src = os.path.join(self.res_dir, f)
                if os.path.exists(src):
                    ##print(f"Copying {src} to {proj_dir}")
                    shutil.copy(src, proj_dir)
                else:
                    proj_dir = None
                    break

        if proj_dir and not os.path.exists(proj_dir):
            ##print(f"Project director {proj_dir} doesn't exist!")
            proj_dir = None
        p.proj_dir = proj_dir
        if proj_dir:
            title = f"PVSim - {p.proj_name}"
        else:
            title = "PVSim"

        frame_pos = p.get("frame_pos", (100, 30))
        frame_size = p.get("frame_size", (900, 950))
        ##print(f"Creating frame {title}: {frame_pos=} {frame_size=}")
        wx.Frame.__init__(self, parent, -1, title, pos=frame_pos, size=frame_size)

        self.draw_enabled = False
        self.bar_signal = None
        self.sigs = {}
        self.order_file_name = None
        self.n_ticks = 0
        self.w_tick = p.get("w_tick", 0.5)
        p.get("timing_scale", 1.4)
        self.test_choices = None

        # tell FrameManager to manage this frame
        self._mgr = mgr = wx.aui.AuiManager()
        mgr.SetManagedWindow(self)
        self.SetMinSize(wx.Size(400, 300))
        self._mp_log_notebook = None

        self._perspectives = []
        self.n = 0
        self.x = 0

        # calibrate font point size for this system
        font = wx.Font(font_size, wx.SWISS, wx.NORMAL, wx.NORMAL)
        font.SetPixelSize((200, 200))
        point_size_200px = font.GetPointSize()
        adj_font_size = font_size * point_size_200px // (170, 148)[is_linux]
        if 1:
            # test it
            dc = wx.ClientDC(self)
            dc.SetFont(font)
            extent = dc.GetTextExtent("M")
            print(f"{point_size_200px=} {font_size=} {adj_font_size=} {extent=}")
            font10 = wx.Font(adj_font_size, wx.SWISS, wx.NORMAL, wx.NORMAL)
            ps10 = font10.GetPointSize()
            font27 = wx.Font(int(adj_font_size * 2.7), wx.SWISS, wx.NORMAL, wx.NORMAL)
            ps27 = font27.GetPointSize()
            print(f"10-point pointsize={ps10}, 27-point pointsize={ps27}")
        font_size = adj_font_size

        # set up menu bar
        self.menubar = wx.MenuBar()
        self.file_menu = self.CreateMenu(
            "&File",
            [
                ("Quit\tCTRL-Q", "OnExit", wx.ID_EXIT),
                ("About PVSim...", "OnAbout", wx.ID_ABOUT),
                ("Open .psim File...\tCTRL-O", "OnOpen", -1),
                ("Save image to File...\tCTRL-S", "SaveToFile", -1),
                ("Add divider line to log\tCTRL-D", "PrintDivider", -1),
            ],
        )
        self.edit_menu = self.CreateMenu(
            "&Edit",
            [
                ("Cut\tCTRL-X", "OnCut", -1),
                ("Copy\tCTRL-C", "OnCopy", -1),
                ("Paste\tCTRL-V", "OnPaste", -1),
                ("-", None, -1),
                ("Find\tCTRL-F", "OnShowFind", -1),
                ("Find Again\tCTRL-G", "OnFindAgain", -1),
            ],
        )
        self.view_menu = self.CreateMenu(
            "&View",
            [
                ("Zoom In\tCTRL-=", "ZoomIn", -1),
                ("Zoom Out\tCTRL--", "ZoomOut", -1),
                ("Zoom to Selection\tCTRL-E", "ZoomToSelection", -1),
                ("Scale Smaller\tCTRL-[", "ScaleSmaller", -1),
                ("Scale Larger\tCTRL-]", "ScaleLarger", -1),
            ],
        )
        self.simulate_menu = self.CreateMenu(
            "&Simulate",
            [
                ("Run Simulation\tCTRL-R", "RunSimulation", -1),
                ("-", None, -1),
            ],
        )
        self.test_choices_sub_menu = None

        self.file_history = wx.FileHistory(8)
        self.file_history.UseMenu(self.file_menu)
        self.file_history.Load(p.config())
        self.Bind(wx.EVT_MENU_RANGE, self.OnFileHistory, id=wx.ID_FILE1, id2=wx.ID_FILE9)
        if proj_dir:
            proj_path = os.path.join(proj_dir, p.proj_name) + ".psim"
            self.file_history.AddFileToHistory(proj_path)
            self.file_history.Save(p.config())

        self.SetMenuBar(self.menubar)

        # create a log text panel below and set it to log all output
        style = wx.TE_MULTILINE | wx.TE_READONLY | wx.TE_RICH
        self.log_panel = log_panel = wx.TextCtrl(self, style=style)
        log_panel.SetFont(wx.Font(font_size, wx.TELETYPE, wx.NORMAL, wx.NORMAL))
        # allow panes to be inited to 70% width, default is 30%
        mgr.SetDockSizeConstraint(0.7, 0.7)
        mgr.AddPane(
            log_panel,
            (
                wx.aui.AuiPaneInfo()
                .Direction(p.get("log_dir", 3))
                .BestSize(p.get("log_size", (898, 300)))
                .Caption("Log")
            ),
        )

        ##self.ChChartPanel = ChartPanel(self, self)
        ##mgr.AddPane(self.ChChartPanel, wx.aui.AuiPaneInfo().CenterPane())
        self.timing_panel = timing_panel = TimingPane(self, self)
        mgr.AddPane(
            timing_panel,
            (wx.aui.AuiPaneInfo().CenterPane().BestSize(p.get("timing_size", (898, 600)))),
        )

        # redirect all output to a log file (if not already in a terminal)
        self.root_name = "pvsim"
        if not (sys.stdin and sys.stdin.isatty()):
            self.orig_stdout = sys.stdout
            self.orig_stderr = sys.stderr
            log_path = (
                os.path.join(proj_dir, self.root_name) if proj_dir is not None else self.root_name
            )
            ##print(f"{log_path=}")
            sys.stdout = Logger(log_path, log_panel)
            sys.stderr = Logger(log_path, log_panel)
            ##print(f"set up stdout, stderr loggers at {os.getcwd()}/{new_stdout.log_file.name}")
        if is_win:
            cur_locale = None
        else:
            cur_locale = locale.setlocale(locale.LC_ALL, "en_US")
        if 1:
            print("Python:", sys.version)
            print(f"{(len(bin(sys.maxsize)) - 1)}-bit Python")
            print("wxPython:", wx.version())
            print("env LANG=", os.getenv("LANG"))
            print("Locale:", cur_locale)
            print("Platform:", sys.platform)
            print("Resource dir:", self.res_dir)
            print("PVSim    dir:", pvsim_dir)
            print("Project  dir:", proj_dir)
            print()
        print("PVSim GUI", __version__, "started", time.ctime())

        # "commit" all changes made to FrameManager
        mgr.Update()

        ##self.Bind(wx.EVT_ERASE_BACKGROUND, self.OnEraseBackground)
        self.Bind(wx.EVT_SIZE, self.OnSize)
        self.Bind(wx.EVT_CLOSE, self.OnExit)
        self.Bind(wx.EVT_FIND, self.OnFind)
        self.Bind(wx.EVT_FIND_CLOSE, self.OnFindCancel)

        # Show How To Use The Closing Panes Event
        ##self.Bind(wx.aui.EVT_AUI_PANE_CLOSE, self.OnPaneClose)
        self.Bind(wx.EVT_MENU, self.OnExit, id=wx.ID_EXIT)

        self.find_string = ""
        self.draw_enabled = True
        self.timing_panel.AdjustMyScrollbars()
        ##timing_panel.DoDrawing()
        self.Show()
        ##print(time.perf_counter(), "PVSimFrame done")
        self.timing_panel.SetFocus()

        if is_mac:
            # start the IPC server
            (listener, success) = pub.subscribe(self.DoEditorCmd, "doCmd")
            if not success:
                print("listener already subscribed to 'doCmd'")
            self.ipc = IPCThread()

        self.OpenFile()

        ##self.RunSimulation()      # include this when debugging

    def CreateMenu(self, name, itemList):
        """Create a menu from a list."""
        menu = wx.Menu()
        for item_name, handler_name, id in itemList:
            if item_name == "-":
                menu.AppendSeparator()
            else:
                if id == -1:
                    id = wx.NewIdRef()
                item = menu.Append(id, item_name)
                if handler_name and hasattr(self, handler_name):
                    self.Connect(
                        id, -1, wx.wxEVT_COMMAND_MENU_SELECTED, getattr(self, handler_name)
                    )
                else:
                    item.Enable(False)
        self.menubar.Append(menu, name)
        return menu

    def CreateToolbar(self, bitmapSize, itemList):
        """Create a toolbar from a list."""
        tb = wx.ToolBar(self, -1, wx.DefaultPosition, wx.DefaultSize)
        tb.SetToolBitmapSize(bitmapSize)
        for item_name, handler_name, bmp in itemList:
            if isinstance(bmp, str):
                bmp = wx.ArtProvider_GetBitmap(bmp)
            if isinstance(item_name, str):
                item = tb.AddLabelTool(wx.ID_ANY, item_name, bmp)
                if hasattr(self, handler_name):
                    self.Bind(wx.EVT_MENU, getattr(self, handler_name), item)
            else:
                item = tb.AddControl(item_name)
        tb.Realize()
        return tb

    def PrintDivider(self, event):
        print("----------------------------")

    def OnAbout(self, event):
        """Show the 'About PVSim' dialog box."""
        version = pvsimu.GetVersion()
        info = wx.AboutDialogInfo()
        info.Name = "PVSim"
        if version == __version__:
            info.Version = version
        else:
            info.Version = f"{version}/{__version__}"
        info.Copyright = __copyright__
        info.Description = wordwrap(
            "PVSim is a portable Verilog simulator that features a fast "
            "compile-simulate-display cycle.",
            350,
            wx.ClientDC(self),
        )
        info.WebSite = ("https://github.com/forbes3100/pvsim", "PVSim home page")
        info.Developers = ["Scott Forbes"]
        info.License = wordwrap(license.replace("# ", ""), 500, wx.ClientDC(self))

        if is_mac:
            # AboutBox causes a crash on app exit if parent is omitted here
            # (see wxWidgets ticket #12402)
            wx.AboutBox(info, self)
        else:
            wx.AboutBox(info)

    def OnCut(self, event):
        """Pass cut to timing window."""
        widget = self.FindFocus()
        if widget == self.timing_panel:
            widget.OnCut(event)
        else:
            widget.Cut()

    def OnCopy(self, event):
        """Pass copy to timing window."""
        widget = self.FindFocus()
        if widget == self.timing_panel:
            widget.OnCopy(event)
        else:
            widget.Copy()

    def OnPaste(self, event):
        """Pass paste to timing window."""
        widget = self.FindFocus()
        if widget == self.timing_panel:
            widget.OnPaste(event)
        else:
            widget.Paste()

    def OnShowFind(self, event):
        """Bring up a Find Dialog to find a signal in the timing window."""
        data = wx.FindReplaceData()
        data.SetFindString(self.find_string)
        dlg = wx.FindReplaceDialog(self, data, "Find", style=wx.FR_NOUPDOWN)
        dlg.data = data
        dlg.Show(True)

    def OnFind(self, event):
        dlg = event.GetDialog()
        self.find_string = event.GetFindString()
        self.find_flags = event.GetFlags()
        self.timing_panel.Find(self.find_string, self.find_flags, True)
        dlg.Destroy()

    def OnFindAgain(self, event=None):
        self.timing_panel.Find(self.find_string, self.find_flags)

    def OnFindCancel(self, event):
        event.GetDialog().Destroy()

    def DoEditorCmd(self, cmd):
        """Execute a command from the external editor."""
        print("Doing editor command", cmd)
        if cmd == "find":
            # Get selected text from BBEdit editor
            result = subprocess.run(
                ["osascript", "-e", 'return text of selection of application "BBEdit" as string'],
                capture_output=True,
                text=True,
            )
            name = result.stdout.strip()
            print("Find signal", name)
            self.timing_panel.Find(name, 0, True)

    def ZoomIn(self, event=None):
        """Zoom timing window in."""
        self.w_tick *= 2.0
        self.timing_panel.AdjustMyScrollbars()

    def ZoomOut(self, event=None):
        """Zoom timing window out."""
        self.w_tick *= 0.5
        self.timing_panel.AdjustMyScrollbars()

    def ZoomToSelection(self, event=None):
        """Zoom timing window to selection."""
        tp = self.timing_panel
        if tp.ti_cursors_start and tp.t_time_cursor:
            tp.AdjustMyScrollbars(start_tick=tp.ti_cursors_start[0], end_tick=tp.t_time_cursor)

    def ScaleSmaller(self, event=None):
        """Make timing window text and detail smaller."""
        self.p.timing_scale /= 1.05
        self.timing_panel.AdjustMyScrollbars()

    def ScaleLarger(self, event=None):
        """Make timing window text and detail larger."""
        self.p.timing_scale *= 1.05
        self.timing_panel.AdjustMyScrollbars()

    def OnSize(self, event):
        """Handle a resize event of the main frame or log pane sash."""
        self.p.frame_size = self.GetSize()
        self.Refresh()
        event.Skip()

    def read_order_file(self, order_file_name):
        """Read the signal display-order file, if any, and put sigs in that order."""
        self.order_file_name = order_file_name
        try:
            print("Reading order file", order_file_name, "...")
            ordf = open(order_file_name, "r")
            new_sigs = {}
            for sig in list(self.sigs.values()):
                sig.is_in_order = False
            for name in ordf.readlines():
                name = name.strip()
                if len(name) > 1 and name[0] != "{":
                    names = [name]
                    i = name.find("[")
                    if i > 0:
                        names.append(name[:i])
                    found_sig = False
                    for i, sig in list(self.sigs.items()):
                        ##print("sig:", i, sig.name, sig.is_bus, sig.sub)
                        if sig.name in names and not sig.is_in_order:
                            ##print(" ordered", name, sig.is_bus, sig.sub)
                            new_sigs[i] = sig
                            sig.is_in_order = True
                            sig.isDisplayed = True
                            found_sig = True
                            break
                    if not found_sig:
                        print(" Not found:", names)
            ordf.close()
            for i, sig in list(self.sigs.items()):
                if not sig.is_in_order:
                    new_sigs[i] = sig
            self.sigs = new_sigs
            print(" Signals ordered.")

        except IOError:
            # file didn't exist: ignore
            print("No existing order file.")

    def SaveOrderFile(self):
        """Save signal order back to file."""
        if self.order_file_name:
            ordf = open(self.order_file_name, "w")
            for sig in self.timing_panel.disp_sigs:
                ordf.write(f"{sig.name}\n")
            ordf.close()

    def OnTestChoice(self, event):
        """Handle new test choice in the Simulate menu."""
        for item in self.simulate_menu.GetMenuItems():
            if item.IsChecked():
                self.p.test_choice = item.GetLabel()
                break
        self.UpdateTestChoicesView()

    def AddTestChoicesMenu(self):
        """Add test choices to Simulate menu."""
        p = self.p

        for choice in self.test_choices:
            id = wx.NewIdRef()
            item = self.simulate_menu.AppendRadioItem(id, choice)
            self.Connect(id, -1, wx.wxEVT_COMMAND_MENU_SELECTED, self.OnTestChoice)
            if choice == p.test_choice:
                self.simulate_menu.Check(id, True)

    def UpdateTestChoicesView(self):
        """Update test choices log page(s) from p.test_choice after selection."""
        p = self.p
        mgr = self._mgr
        self.mp_logs = {}
        if self._mp_log_notebook != None:
            mgr.ClosePane(mgr.GetPane(self._mp_log_notebook))
            self._mp_log_notebook = None

        if len(self.test_choices) > 1 and p.test_choice == "All":
            # split the log window into pages, one per choice
            nb = wx.aui.AuiNotebook(
                self,
                style=wx.aui.AUI_NB_TOP | wx.aui.AUI_NB_TAB_SPLIT | wx.aui.AUI_NB_SCROLL_BUTTONS,
            )
            self._mp_log_notebook = nb
            mgr.AddPane(
                nb,
                (
                    wx.aui.AuiPaneInfo()
                    .Direction(p.get("log_dir", 3))
                    .Position(1)
                    .BestSize(p.get("log_size", (898, 300)))
                    .Caption("Tests")
                ),
            )
            numbered_attr_pat = re.compile(r"^[A-Z]+([0-9]+[A-Z]*)$")
            for choice in self.test_choices[:-1]:
                page_tc = wx.TextCtrl(self, style=wx.TE_MULTILINE | wx.TE_READONLY | wx.TE_RICH)
                page_tc.SetFont(wx.Font(font_size, wx.TELETYPE, wx.NORMAL, wx.NORMAL))
                words = choice.split("_")
                if words[0] == "TEST":
                    words = words[1:]
                title = []
                for word in words:
                    m = numbered_attr_pat.match(word)
                    if m:
                        word = m.group(1)
                    title.append(word)
                title = ".".join(title)
                ##title = ".".join([w[-2:] for w in words])
                nb.AddPage(page_tc, title)
                log_path = os.path.join(p.proj_dir, self.root_name + "_" + choice)
                self.mp_logs[choice] = Logger(log_path, page_tc)
                ##self.log_panel = page_tc
        else:
            self.mp_logs[p.test_choice] = sys.stdout
        mgr.Update()

    def GatherTestChoices(self, proj_file_full_name):
        """Gather any test choice lines from .psim file.
        Also initializes test_choice preference if needed, and makes menu.
        """
        p = self.p
        choices = []
        try:
            for line in open(proj_file_full_name):
                line = line.strip()
                words = line.split()
                if len(words) > 1 and words[0] == "test_choice":
                    choices.append(words[1])
        except:
            print("Project file", proj_file_full_name, "not found")
        choices.append("All")
        self.test_choices = choices
        p.get("test_choice", choices[0])
        self.AddTestChoicesMenu()
        self.UpdateTestChoicesView()

    def OpenFile(self, file_name=None):
        """Set given .psim project file for future simulation runs."""
        ##print("OpenFile:", file_name)
        p = self.p
        if file_name == None:
            if self.file_history.GetCount() == 0:
                print("No file name, no file history, exiting")
                return
            print(f"Creating file {p.proj_name}.psim in {p.proj_dir}")
            file_name = os.path.join(p.proj_dir, p.proj_name + ".psim")
        else:
            self.file_history.AddFileToHistory(file_name)
            self.file_history.Save(p.config())
        proj_dir, name = os.path.split(file_name)
        ##print(f"OpenFile: {proj_dir=} {name=}")
        if len(proj_dir) == 0:
            proj_dir, name = os.path.split(os.path.abspath(file_name))
        proj_dir += os.path.sep
        ##print("OpenFile:", file_name, proj_dir, name)
        p.proj_dir = proj_dir
        p.proj_name, ext = os.path.splitext(name)
        if ext == ".psim":
            self.SetTitle(f"PVSim - {p.proj_name}")
            print(f"Set {p.proj_name} in {p.proj_dir} as simulation source")
            ##self.RunSimulation()

            # gather any test choice lines from .psim file
            self.GatherTestChoices(os.path.join(proj_dir, name))

        else:
            wx.Bell()

    def OnOpen(self, event):
        """Choose a .psim project file for future simulation runs."""
        print("OnOpen")
        wildcard = "PVSim file (*.psim)|*.psim"
        dlg = wx.FileDialog(
            self,
            message="Load...",
            defaultDir=os.getcwd(),
            defaultFile="",
            wildcard=wildcard,
            style=wx.FD_OPEN,
        )
        dlg.SetFilterIndex(0)
        if dlg.ShowModal() == wx.ID_OK:
            self.OpenFile(dlg.GetPath())

    def OnFileHistory(self, event):
        """Open chosen past file from the File menu."""
        # get the file based on the menu ID
        file_id = event.GetId() - wx.ID_FILE1
        self.OpenFile(self.file_history.GetHistoryFile(file_id))

    def StartMPTimer(self, mp_pool, mp_log_queue):
        """Start timer for periodic log window updating."""
        self.mp_pool = mp_pool
        self.mp_log_queue = mp_log_queue
        self.timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.OnTimer)
        # start display-update timer, given interval in ms
        ms_per_update = 100
        self.timer.Start(ms_per_update)

    def StartMPSimulation(self):
        """Start a simulation process in the background for each selected test."""
        p = self.p
        if p.test_choice == "All":
            if len(self.test_choices) > 1:
                tests = self.test_choices[:-1]
            else:
                tests = [None]
        else:
            tests = [p.test_choice]

        ##print("StartMPSimulation: tests=", tests)
        test_args = [
            (
                p.proj_dir,
                p.proj_name,
                self.n_ticks,
                self.bar_signal,
                self.mp_log_queue,
                test,
                test == tests[-1],
            )
            for test in tests
        ]
        r = self.mp_pool.map_async(mp_worker, test_args, callback=mp_worker_done)
        ##print("StartMPSimulation: active=", mp.active_children())

    def OnMPResult(self):
        """All simulation processes have finished: draw it and report any errors."""
        global error_count
        p = self.p
        ##print("OnMPResult: error_count=", error_count)

        if error_count == 0:
            print(f"{time.perf_counter() - t0:2.1f}: Simulation done, no errors.\n")
            proj = os.path.join(p.proj_dir, p.proj_name)
            self.read_order_file(f"{proj}.order")

        # draw final test's results in timing pane
        self.timing_panel.AdjustMyScrollbars()

    def OnTimer(self, event):
        """Timer tick: display any message lines from the back end process(es)."""
        if self.mp_log_queue:
            while not self.mp_log_queue.empty():
                name, msg = self.mp_log_queue.get()
                self.mp_logs[name].write(msg)

    def RunSimulation(self, event=None):
        """Run pvsimu simulation to generate events file, then display timing."""
        global main_frame
        main_frame = self
        p = self.p
        print("\nRunSimulation")

        self.log_panel.SetInsertionPointEnd()
        if len(self.timing_panel.disp_sigs) > 0:
            self.SavePrefs()

        pvsimu.Init()
        backend_version = pvsimu.GetVersion()
        print(f"Found PVSim {backend_version} backend")

        if use_multiprocessing:
            print("Using Multiprocessing")
            self.StartMPSimulation()
        else:
            print("Single process only")
            self.Connect(-1, -1, EVT_RESULT_ID, self.OnResult)
            self.worker = SimThread(self)

    def OnResult(self, event):
        """The threaded simulation task finished: terminate it and draw results."""
        if event.msg:
            print(event.msg, end="")
        else:
            self.worker = None

            # convert strings from backend to Unicode
            for sig in list(self.timing_panel.frame.sigs.values()):
                ##print(sig.name)
                converted_events = []
                for t, level in pairwise(sig.events):
                    ##print(f"{t} {level}", end="")
                    if type(level) is type(b""):
                        level = level.decode("utf-8")
                    ##print(f" {level}")
                    converted_events.append(t)
                    converted_events.append(level)
                sig.events = converted_events

            # draw results in timing pane
            self.timing_panel.AdjustMyScrollbars()

    def GotoSource(self, sig):
        """Open the source file for given signal and center on its definition."""
        p = self.p
        print("GotoSource", sig.src_pos_obj_name, sig.src_file, sig.src_pos)
        if is_mac:
            name = sig.src_pos_obj_name
            i = name.find("[")
            if i > 0:
                name = name[:i]
            name = name.split(".")[-1]
            win_name = sig.src_file.split("/")[-1]
            # Select word using BBEdit editor
            # (would like to also update Find's search string, but that property is read-only)
            result = subprocess.run(
                [
                    "osascript",
                    "-e",
                    'tell application "Finder" to get application id "com.barebones.bbedit"',
                ],
                capture_output=True,
                text=True,
            )
            editor = result.stdout.strip()
            ##print(f"{editor=}")
            if editor != "":
                cmd = (
                    f'tell application "{editor}"\n'
                    f'  open "{p.proj_dir}{sig.src_file}"\n'
                    f'  find "{name}" searching in characters {sig.src_pos} thru'
                    f' {sig.src_pos+len(name)+2} of document "{win_name}" options {{case'
                    f" sensitive:true, match words:true}}\n"
                    f"  if found of result then\n"
                    f"    select found object of result\n"
                    f'    activate window "{win_name}"\n'
                    "  end if\n"
                    "end tell"
                )
                ##print(cmd)
                result = subprocess.run(["osascript", "-e", cmd], capture_output=True, text=True)
                ##print(f"stdout='{result.stdout}'\n stderr='{result.stderr}'")
                if "execution error" in result.stderr:
                    msg = result.stderr.split('got an error:')[-1].strip()
                    first, last = [int(x) for x in result.stderr.split(':')[0:2]]
                    print(f"\n{cmd[first:last]}\n^\n{editor} error: {msg}")
            else:
                print("External editor BBEdit not found")

    def GetPanePrefs(self, pane):
        """Return a dictionary of a pane's settings."""
        info = self._mgr.SavePaneInfo(self._mgr.GetPane(pane))
        info_dict = {}
        for nv in info.split(";"):
            name, value = nv.split("=")
            info_dict[name] = value
        return info_dict

    def SavePrefs(self):
        """Save preferences to file."""
        p = self.p
        p.frame_pos = tuple(self.GetPosition())
        log_info = self.GetPanePrefs(self.log_panel)
        p.log_dir = log_info["dir"]
        p.log_size = self.log_panel.GetSize()
        ##print(f"{p.log_size=}")
        p.timing_size = self.timing_panel.GetSize()
        ##print(f"{p.timing_size=}")
        p.x_scroll_pos = self.timing_panel.GetScrollPos(wx.HORIZONTAL)
        p.y_scroll_pos = self.timing_panel.GetScrollPos(wx.VERTICAL)
        p.save()

        self.SaveOrderFile()

    def OnExit(self, event=None):
        """Quitting: save prefs to file."""
        ##print("OnExit")
        # debug prints here go to standard output since log soon won't exist
        if hasattr(self, "orig_stdout"):
            sys.stdout = self.orig_stdout
            sys.stderr = self.orig_stderr

        self.SavePrefs()
        if is_mac:
            self.ipc.stop()
            self.ipc.join()  # Wait for the thread to finish
        self.Destroy()  # just exit from App so cProfile may complete
        if not self.mp_pool is None:
            # terminate the processes
            self.mp_pool.close()
            ##print("OnExit: pool closed")
            self.mp_pool.join()
            ##print("OnExit: pool joined")


class PVSimApp(wx.App):
    """PVSim application."""

    def OnInit(self):
        self.frame = frame = PVSimFrame(None)
        return True


def RunApp():
    global app
    try:
        if use_multiprocessing:
            # create pool of sim-worker processes before starting app
            ##mp_pool = mp.Pool(mp.cpu_count()-1)    # too many-- bogs down
            mp_pool = mp.Pool(max(mp.cpu_count() // 2, 1))  # = 4 procs on i7
            mp_log_queue = mp.Manager().Queue()

        ##print("RunApp")
        app = PVSimApp(redirect=False)
        if use_multiprocessing:
            app.frame.StartMPTimer(mp_pool, mp_log_queue)
        prof = app.frame.pvsim_dir + "pvsim.profile"
        ##print("RunApp profile=", show_profile, prof)
        if show_profile:
            cProfile.run("app.MainLoop()", prof)
            print("To see the stats, type: ./showprof.py")
        else:
            app.MainLoop()
        ##print("RunApp end")

    except:
        exctype, value = sys.exc_info()[:2]
        print(f"\nError: {exctype} {value}")
        if exctype != SystemExit:
            dlg = dialogs.ScrolledMessageDialog(None, traceback.format_exc(), "Error")
            dlg.ShowModal()
        print("PVSim app exiting")
        sys.exit(-1)


if __name__ == "__main__":
    RunApp()
