#!/usr/bin/env python3.10

###############################################################################
#
#                   PVSim Verilog Simulator GUI, in wxPython
#
# Copyright 2012 Scott Forbes
#
# This file is part of PVSim.
#
license = """
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with PVSim; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""
###############################################################################

import sys
##print("Python:", sys.version)
import os, time, re, string, traceback, threading, cProfile, shutil
import wx
import wx.lib.dialogs as dialogs
import wx.aui
from wx.lib.wordwrap import wordwrap
import locale
import subprocess
import unittest
##_ = wx.GetTranslation
from optparse import OptionParser
import socket
from pubsub import pub

import multiprocessing as mp
from multiprocessing import managers

import pvsimu

guiVersion = "6.2.2b2"

showProfile = 0
dispMargin = 0
showText = 1
useSnap = 0     # enable snap-cursor-to-edge (slows down highlighting though)
useMultiprocessing = 0

backendMsgBuffLen = 20
##backendMsgBuffLen = 1

##logFile = None
t0 = time.perf_counter()

isWin   = (sys.platform == "win32")
isLinux = (sys.platform == "linux2")
isMac = not (isWin or isLinux)

# Not yet for Windows
if isWin:
    useMultiprocessing = 0

# standard font pointsize-- will be changed to calbrated size for this system
fontSize = 11

#==============================================================================
# Return the full path of a given executable.

def which(pgm):
    for p in os.getenv("PATH").split(os.path.pathsep):
        p = os.path.join(p, pgm)
        if os.path.exists(p) and os.access(p, os.X_OK):
            return p
    return None

def pairwise(iterable):
    "s -> (s0,s1), (s2,s3), (s4, s5), ..."
    a = iter(iterable)
    return zip(a, a)

#==============================================================================
# Worker thread-- runs tells backend pvsimu to run simulation and gets results.

# notification event for thread completion
EVT_RESULT_ID = wx.NewIdRef()
mainFrame = None
msgCount = 0
errCount = 0

class ResultEvent(wx.PyEvent):
    def __init__(self, msg):
        wx.PyEvent.__init__(self)
        self.SetEventType(EVT_RESULT_ID)
        self.msg = msg

#------------------------------------------------------------------------------
# Write a message the GUI thread's log window and log file. No newline on end.

def printBE(msg):
    global mainFrame, msgCount, errCount
    wx.PostEvent(mainFrame, ResultEvent(msg))
    if msg.find("*** ERROR") >= 0:
        errCount += 1
    # Allow GUI thread a chance to display queued lines every so often. Need a
    # better method: should print when this thread is waiting on Simulate().
    msgCount += 1
    if (msgCount % backendMsgBuffLen) == 0:
        ##time.sleep(0.01)
        time.sleep(0.1)     # must be this long to pause (why???)

#------------------------------------------------------------------------------

class SimThread(threading.Thread):
    def __init__(self, frame):
        self.frame = frame
        threading.Thread.__init__(self)
        self.start()

    def run(self):
        global mainFrame, errCount
        frame = mainFrame = self.frame
        p = frame.p
        if not p.projDir:
            printBE("*** No project specified ***")
            wx.PostEvent(frame, ResultEvent(None))
            return
        proj = os.path.join(p.projDir, p.projName)
        printBE(40*"-"+"\n")
        printBE("Run: cd %s\n" % p.projDir)
        os.chdir(p.projDir)

        errCount = 0
        if p.testChoice == "All":
            if len(frame.testChoices) > 1:
                choices = frame.testChoices[:-1]
            else:
                choices = [None]
        else:
            choices = [p.testChoice]

        # use Python-extension pvsimu
        for testChoice in choices:
            time.sleep(0.1)
            if testChoice:
                printBE("\n======= Test %s =======\n\n" % testChoice)
            else:
                testChoice = ""
            pvsimu.Init()
            pvsimu.SetSignalType(Signal)
            pvsimu.SetCallbacks(printBE, printBE)
            try:
                result = pvsimu.Simulate(proj, testChoice)
                if not result:
                    printBE("*** ERROR: pvsimu.Simulate() returned NULL\n")
                    errCount += 1
                    ##break
                else:
                    frame.sigs, frame.nTicks, frame.barSignal = result
                    printBE("nTicks= %d bar=%s\n" %
                            (frame.nTicks, frame.barSignal))
                    ##printBE(f"\n{frame.sigs=}\n")
                    ##printBE(f"\n{frame.sigs[6].events=}\n")
            except:
                printBE("*** ERROR: pvsimu.Simulate() exception: %s" %
                        traceback.format_exc())
                ##break
            printBE(40*"-"+"\n")

        if errCount == 0:
            printBE("%2.1f: Simulation done, no errors.\n" % \
                (time.perf_counter() - t0))
            frame.ReadOrderFile("%s.order" % proj)

        wx.PostEvent(frame, ResultEvent(None))

#==============================================================================
# Multiprocessing version of simulator-worker process.

logName = None      # a process's local copy of its name
logQueue = None     # a process's local copy of mpLogQueue

#------------------------------------------------------------------------------
# Write a message the GUI thread's log window and log file. No newline on end.

def printMP(msg):
    global logName, logQueue, errCount
    ##print("printMP:", msg)
    logQueue.put((logName, msg))
    if msg.find("*** ERROR") >= 0:
        errCount += 1

#------------------------------------------------------------------------------
# Multiprocessing worker: run a single simulation test in its own process.
# Has no contact with GUI process except for logQueue via printMP(), and
# returned result.

def mpWorker(args):
    (projDir, projName, nTicks, barSignal, q, test, isFinalTest) = args
    global logName, logQueue, errCount
    logName = test
    logQueue = q
    if not projDir:
        printMP("*** No project specified ***")
        ##wx.PostEvent(frame, ResultEvent(None))
        return None
    proj = os.path.join(projDir, projName)
    printMP(40*"-"+"\n")
    printMP("Run: cd %s\n" % projDir)
    os.chdir(projDir)

    errCount = 0

    # use Python-extension pvsimu
    if test:
        printMP("\n======= Test %s =======\n\n" % test)
    pvsimu.Init()
    pvsimu.SetSignalType(Signal)
    pvsimu.SetCallbacks(printMP, printMP)
    try:
        result = pvsimu.Simulate(proj, test)
        if not result:
            printMP("*** ERROR: pvsimu.Simulate() returned NULL\n")
            errCount += 1
        else:
            sigs, nTicks, barSignal = result
            printMP("nTicks= %d bar=%s\n" % (nTicks, barSignal))
            if isFinalTest:
                mpWorkerResult = result
    except:
        printMP("*** ERROR: pvsimu.Simulate() exception\n")
    printMP(40*"-"+"\n")

    return (None, (sigs, nTicks, barSignal, errCount))[isFinalTest]

#------------------------------------------------------------------------------
# Multiprocessing-worker completion callback, given list of all processes'
# results. Executes in GUI thread.

def mpWorkerDone(results):
    global mainFrame, errCount
    print("mpWorkerDone: len(results)=", len(results))
    if results != None:
        mainFrame.sigs, mainFrame.nTicks, mainFrame.barSignal, \
                errCount = results[-1]
        mainFrame.OnMPResult()


#==============================================================================
# A preferences (config) file or registry entry.

class Prefs(object):
    def __init__(self):
        self._config = config = wx.Config("PVSim")

        valid, name, index = config.GetFirstEntry()
        while valid:
            # put entries into this Prefs object as attributes
            # (except for FileHistory entries-- those are handed separately)
            if not (len(name) == 5 and name[:4] == "file"):
                self._setAttr(name, config.Read(name))
            valid, name, index = config.GetNextEntry(index)

    #--------------------------------------------------------------------------
    # Set a value as a named attribute.

    def _setAttr(self, name, value):
        ##print("Prefs _setAttr(", name, ",", value, ")")
        try:
            value = eval(value)
        except:
            pass
        setattr(self, name, value)

    #--------------------------------------------------------------------------
    # Get a preference value, possibly using the default.

    def get(self, name, defaultValue):
        if not hasattr(self, name):
            setattr(self, name, defaultValue)
        value = getattr(self, name)
        if type(value) != type(defaultValue) and isinstance(value, str):
            try:
                value = eval(value)
            except:
                pass
        return value

    #--------------------------------------------------------------------------
    # Write all attributes back to file.

    def save(self):
        attrs = vars(self)
        names = list(attrs.keys())
        names.sort()
        for name in names:
            if name[0] != '_':
                value = attrs[name]
                if not isinstance(value, str):
                    value = repr(value)
                ##print("save pref:", name, value, type(value))
                if not self._config.Write(name, value):
                    raise IOError("Prefs.Write(%s, %s)" % (name, value))
        self._config.Flush()

    #--------------------------------------------------------------------------
    # Return Config object.

    def config(self):
        return self._config

#==============================================================================
# A simple error-handling class to write exceptions to a text file.

class Logger(object):
    def __init__(self, name, textCtrl):
        ##global logFile
        self.logFile = open(name + ".logwx", "w")
        self.textCtrl = textCtrl

    def write(self, s):
        ##global logFile
        try:
            self.logFile.write(s)
            ##self.textCtrl.WriteText(s)
            # thread-safe version
            wx.CallAfter(self.textCtrl.WriteText, s)
        except:
            pass # don't recursively crash on errors

    def flush(self):
        pass


#==============================================================================
# Inter-Process Communication Thread.

class IPCThread(threading.Thread):
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
                            cmd = rlines[i+1][1:].decode()
                            wx.CallAfter(pub.sendMessage, "doCmd", cmd=cmd)
                    except Exception as e:
                        print("rlines=", rlines, "Exception:", e)

            except socket.error as msg:
                if self.running:  # Check if the socket error is due to stopping the thread
                    print("Socket error: %s" % msg)
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
red     = wx.Colour(255,   0,   0)
yellow  = wx.Colour(255, 255,   0)
green   = wx.Colour(  0, 150,   0)
blue    = wx.Colour(  0,   0, 255)
ltblue  = wx.Colour( 47, 254, 255)
dkblue  = wx.Colour( 10,   0, 200)
dkgray  = wx.Colour(128, 128, 128)

Level2V = {"L": 0, "Z":0.5, "H":1, "X":2, "S": 0}
tickPlaces = 3
ticksNS = 10**tickPlaces

#==============================================================================
# A signal timing display pane.

class TimingPane(wx.ScrolledWindow):
    def __init__(self, parent, frame):
        self.frame = frame
        p = frame.p
        wx.ScrolledWindow.__init__(self, parent, -1,
                                        (dispMargin, dispMargin))
        self.wavesBitmap = None
        self.xmax = 0
        self.ymax = 0
        self.xsPosLast = None
        self.ysPosLast = None
        self.wWinLast = None
        self.hWinLast = None
        self.wTickLast = None
        self.tCenter = None
        p.wTick = frame.wTick
        self.haveSPos = False
        self.xMouse = 0             # screen position of mouse pointer
        self.yMouse = 0
        self.mouseDown = False      # True if mouse button down
        self.mouseWasDown = False   # True if mouse was down previously
        self.tiCursorsStart = None  # (snapped tick, name index) where mouse
                                    #   click started, or None
        self.iNameCursor = None
        self.tTimeCursor = None     # snapped tick of last mouse down
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

    #--------------------------------------------------------------------------

    def getWidth(self):
        return self.xmax + dispMargin

    def getHeight(self):
        return self.ymax + dispMargin

    #--------------------------------------------------------------------------
    # Convert horizontal pixel position to time tick.

    def X2T(self, x):
        p = self.frame.p
        xsPos = self.GetScrollPos(wx.HORIZONTAL)
        t = (x + xsPos * self.wScroll - self.wNames) // p.wTick
        return t

    #--------------------------------------------------------------------------
    # Convert vertical pixel position to dispSig list index.

    def Y2I(self, y):
        index = int((y+self.y0) / self.hRow + 0.5) - 1
        return index

    #--------------------------------------------------------------------------
    # Recompute timing pane dimensions and scrollbar positions.
    # Sets scrollbars to (p.xsPos,p.ysPos), scrolling image if self.haveSPos
    # is yet to be set.

    def AdjustMyScrollbars(self, startTick=None, endTick=None):
        frame = self.frame
        p = frame.p

        # rebuild list of displayed signals
        dispSigs = []
        for sig in list(frame.sigs.values()):
            if sig.isDisplayed:
                dispSigs.append(sig)
        self.dispSigs = dispSigs
        nDisp = len(dispSigs)

        # compute signal row dimensions based on timing pane scale factor
        s = p.timingScale
        # pixel width of signal names part of diagram
        self.wNames = wNames = int(100*s)
        # signal's row-to-row spacing in display
        self.hRow = hRow     = int(10*s)
        ##print("hRow=", hRow)
        # amount to raise up text (pixels)
        self.textBaseline = (3, 2)[isMac]

        # xmax,ymax: timing pane virtual dimensions
        wWaves = frame.wTick * frame.nTicks
        self.xmax = xmax = int(wWaves + wNames + 0.5)
        self.ymax = ymax = (nDisp + 1) * hRow
        # wScroll: width of one scroll unit
        self.wScroll = wScroll = 20
        # xsmax,ysmax: timing pane virtual dimensions in scroll units
        self.xsmax = xsmax = int(xmax / wScroll + 0.5)
        self.ysmax = ysmax = int(ymax / hRow + 0.5)
        # wWin,hWin: timing pane aperture size
        wWin, hWin = self.GetClientSize()
        wWin -= dispMargin; hWin -= dispMargin
        wWavePart = wWin - wNames

        # if a tick range to view is given, use it
        xsPosPre = self.GetScrollPos(wx.HORIZONTAL)
        if startTick:
            if startTick > endTick:
                startTick, endTick = endTick, startTick
            dt = endTick - startTick
            ##print("wWavePart=", wWavePart)
            frame.wTick = 0.9 * wWavePart / dt
            wWaves = frame.wTick * frame.nTicks
            self.xmax = xmax = int(wWaves + wNames + 0.5)
            self.xsmax = xsmax = int(xmax / wScroll + 0.5)
            p.xsPos = int((startTick - 0.05*dt) * frame.wTick / wScroll)
            p.ysPos = self.GetScrollPos(wx.VERTICAL)
            self.haveSPos = False
            p.wTick = None

        # scroll to (p.xsPos,p.ysPos) if self.haveSPos is yet to be set
        if not self.haveSPos:
            self.SetScrollPos(wx.HORIZONTAL, p.get("xsPos", 0))
            self.SetScrollPos(wx.VERTICAL, p.get("ysPos", 0))
            ##self.Scroll(p.xsPos, p.ysPos)
            self.haveSPos = True

        if p.wTick != frame.wTick:
            if startTick:
                # expanding time to selection: no adjustments
                xsPos = p.xsPos
                ysPos = p.ysPos
            else:
                # have zoomed: keep center tick centered
                # determine center tick before changing zoom
                xsPos = self.GetScrollPos(wx.HORIZONTAL)
                ysPos = self.GetScrollPos(wx.VERTICAL)
                tLeft = xsPos * wScroll / p.wTick
                wHalf = wWavePart / 2
                dtHalf = wHalf / p.wTick
                tCenter = tLeft + dtHalf
                # recalculate scroll position with new zoom
                dtHalf = wHalf / frame.wTick
                tLeft = tCenter - dtHalf
                xsPos = max(int(tLeft * frame.wTick / wScroll + 0.5), 0)
            ##print("Adjust start=", startTick, "end=", endTick,
            ##        "xsPos=", xsPosPre, "->", xsPos,
            ##        "xsmax=", xsmax, "x/m=", 100*xsPos/xsmax,
            ##        "wid=", p.wTick, "->", frame.wTick)
            p.wTick = frame.wTick
            self.SetScrollbars(wScroll, hRow, xsmax, ysmax, xsPos, ysPos,
                               False)
            ##self.Scroll(xsPos, -1)
        else:
            # no zoom: just insure scrollbars match scrolled contents
            p.xsPos = self.GetScrollPos(wx.HORIZONTAL)
            p.ysPos = self.GetScrollPos(wx.VERTICAL)
            self.SetScrollbars(wScroll, hRow, xsmax, ysmax, p.xsPos, p.ysPos,
                               False)

        self.xsPosLast = -1
        self.Refresh()

    #--------------------------------------------------------------------------
    # Prepare to draw a line segment of one signal.
    # Gathers lines and text for the next segment of a signal from xp to x,
    # using vp to v to determine its shape and contents.
    #
    # (xp, yp):   end of previous event edge
    # (x-1, yp):  start of current event edge
    # (x, y):     end of current event edge

    def GthrLineSegment(self, dc, sig, xp, vp, x, v, showSigName=True):
        yL = self.yL
        lines = self.lines
        if self.debug:
            print("GthrLineSegment: xp=", xp, "vp=", vp, "x=", x, "v=", v)

        yp = yL - int(Level2V[vp] * self.hHL)
        y = yL - int(Level2V[v] * self.hHL)
        lines.append((xp, yp, x-1, yp))
        lines.append((x-1, yp, x, y))
        # include signal name every so often
        if showText and x-self.w-self.wEventName > self.xtext and \
                (x - xp) > (self.w + 4) and showSigName:
            (w, h) = dc.GetTextExtent(sig.name)
            self.texts.append(sig.name)
            bl = self.textBaseline
            self.textCoords.append((x - w - 2,
                                    yL - self.hName + 1 + (vp == "H") - bl))
            self.textFGs.append(green)
            self.xtext = x

    #--------------------------------------------------------------------------
    # Prepare to draw a bus segment of one signal.
    # Gathers lines and text for the next segment of a signal from xp to x,
    # using vp to v to determine its shape and contents.
    #
    # (xp, yp):   end of previous event
    # (x, y):     end of current event

    def GthrBusSegment(self, dc, sig, xp, vp, x, v):
        yL = self.yL
        lines = self.lines
        if self.debug:
            print("GthrBusSegment: xp=", xp, "vp=", vp, "x=", x, "v=", v)

        yH = yL - self.hHL
        if vp == None or vp == "X":
            # a "don't care" or unresolvable-edges segment: filled with gray
            self.Xpolys.append(((xp, yL), (xp, yH), (x, yH), (x, yL)))
        else:
            lines.append((xp, yL, x,  yL))
            lines.append((x,  yL, x,  yH))
            lines.append((x,  yH, xp, yH))
            lines.append((xp, yH, xp, yL))
            # include bus-value text if there's room for it
            nbits = abs(sig.lsub - sig.rsub) + 1
            if showText and isinstance(vp, int):
                valName = "%0*X" % ((nbits+2)//4, vp)
                (w, h) = dc.GetTextExtent(valName)
                if x-w-2 > xp:
                    dxp = min((x - xp)//2, 100)
                    self.texts.append(valName)
                    bl = self.textBaseline
                    self.textCoords.append((xp + dxp - w//2,
                                            yL - self.hName + 2 - bl))
                    self.textFGs.append(wx.BLACK)

    #--------------------------------------------------------------------------
    # Draw a attached text on one signal.

    def DrawAttachedText(self, dc, sig, x, v, xbeg, xend):
        if self.debug:
            print("DrawAttachedText: x=", x, "v=", v)
        if not showText:
            return

        inFront, text = v
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
        if Level2V[inFront]:
            # place text in front of x
            x -= (w + 2)
            self.xtext = x
        else:
            # place text after x
            x += 2
        y = self.yL - self.hName + 1 - self.textBaseline
        if x > xbeg and x + w < xend:
            if weight != wx.NORMAL:
                dc.SetFont(wx.Font(self.nameFontSz, wx.SWISS, wx.NORMAL,
                                   weight))
                dc.SetTextForeground(color)
                dc.DrawText(text, x, y)
                dc.SetFont(wx.Font(self.nameFontSz, wx.SWISS, wx.NORMAL,
                                   wx.NORMAL))
            else:
                self.texts.append(text)
                self.textCoords.append((x, y))
                self.textFGs.append(color)

    #--------------------------------------------------------------------------
    # Draw timing window.
    # Only draws the (L H X Z) subset of possible signal levels.

    def OnPaint(self, event):
        ##print("OnPaint: size=", self.xmax, self.ymax)
        frame = self.frame
        p = frame.p
        self.SetBackgroundColour("WHITE")

        xmax = self.xmax
        ymax = self.ymax
        wWin, hWin = self.GetClientSize()
        wWin -= dispMargin; hWin -= dispMargin

        # (t, i, v): time, indexToSignal, voltageOfIndexedSignal.
        # (x, y): scrolled-screen coords, in pixels, (x0,y0): upper left corner
        xsPos = self.GetScrollPos(wx.HORIZONTAL)
        ysPos = self.GetScrollPos(wx.VERTICAL)
        hRow   = self.hRow   # signal's row-to-row spacing in display
        self.x0 = x0 = xsPos * self.wScroll
        self.y0 = y0 = ysPos * hRow

        wNames = self.wNames # pixel width of signal names part of diagram
        xbeg = wNames
        # right, bottom edge of visible waveforms
        xend = min(wWin, xmax)
        yend = min(hWin, ymax)
        dx = frame.wTick
        bl = self.textBaseline
        xyEdgeHilite = None
        x, y = self.xMouse, self.yMouse
        tMouse = self.X2T(x)
        iMouse = self.Y2I(y)
        if x < self.wNames or x > xend:
            tMouse = None
        ##print("mouse=", iMouse, tMouse, self.mouseDown)

        if useSnap or not \
              (xsPos == self.xsPosLast and ysPos == self.ysPosLast and \
               wWin == self.wWinLast and hWin == self.hWinLast and \
                p.wTick == self.wTickLast):
            self.xsPosLast = xsPos
            self.ysPosLast = ysPos
            self.wWinLast = wWin
            self.hWinLast = hWin
            self.wTickLast = p.wTick
            dc = wx.MemoryDC()
            self.wavesBitmap = wx.Bitmap(wWin, hWin)
            dc.SelectObject(self.wavesBitmap)
            ##print("OnPaint clip=", dc.GetClippingBox())
            dc.SetBackground(wx.Brush(self.GetBackgroundColour()))
            dc.Clear()

            s = p.timingScale
            self.hHL        = int(8*s)    # signal's L-to-H spacing
            ##self.hHW      = int(s)      # signal's L-to-V and H-to-W spacing
            self.hName      = int(8*s)    # signal name text size
            self.nameFontSz = self.hName * fontSize // 11
            self.wEventName = int(200*s)  # min pixels from prev event, drawing
            # draw names separator line
            dc.SetPen(wx.Pen("BLACK"))
            ##print("  OnPaint gen line=", xbeg, xsPos, ysPos)
            dc.DrawLine(xbeg-1, 0, xbeg-1, hWin)
            dc.SetFont(wx.Font(self.nameFontSz, wx.SWISS, wx.NORMAL,wx.NORMAL))
            ##print("UserScale=", dc.GetUserScale(), "Mode=", dc.GetMapMode(),
            ##    "PPI=", dc.GetPPI())

            gridPen = wx.Pen(ltblue, 1, wx.SOLID)
            haveDrawnBars = False

            # draw vertical bars at rising edges of given 'bar' signal
            if frame.barSignal != None:
                sig = frame.sigs[frame.barSignal]
                # xtext: next allowable position of drawn text
                xtext = xbeg
                xp = -x0
                for i in range(0, len(sig.events), 2):
                    t, v = sig.events[i:i+2]
                    x = wNames + int(t * dx) - x0
                    if x > xend:
                        break
                    if x > xbeg and x > xp+4 and v == "H":
                        barName = "%g" % (t / ticksNS)
                        if showText:
                            (w, h) = dc.GetTextExtent(barName)
                            if x-w//2-5 > xtext:
                                dc.SetTextForeground(dkblue)
                                dc.DrawText(barName, x-w//2, 2-bl)
                                xtext = x + w//2
                                haveDrawnBars = True
                        dc.SetPen(gridPen)
                        dc.DrawLine(x, hRow, x, ymax)
                    xp = x

            # if bar signal too fine, just draw 1 usec intervals
            if showText and not haveDrawnBars:
                xtext = xbeg
                for t in range(0, frame.nTicks, 10000):
                    x = wNames + int(t * dx) - x0
                    if x > xend:
                        break
                    if x > xbeg:
                        barName = "%g" % (t / ticksNS)
                        (w, h) = dc.GetTextExtent(barName)
                        if x-w-5 > xtext:
                            dc.SetTextForeground(dkblue)
                            dc.DrawText(barName, x-w//2, 2-bl)
                            xtext = x
                        ##dc.SetPen(gridPen)
                        ##dc.DrawLine(x, hRow, x, ymax)

            # draw each signal's name and waveform
            i = (y0+hRow-1) // hRow
            yL = (i+2)*hRow - y0

            for sig in self.dispSigs[i:]:
                self.yL = yL
                if 0 and sig.isBus:
                    name = "%s[%d:%d]" % (sig.name, sig.lsub, sig.rsub)
                else:
                    name = sig.name
                if showText:
                    (w, h) = dc.GetTextExtent(name)
                    self.w = w
                    dc.SetTextForeground(green)
                    dc.DrawText(name, xbeg - w - 3, yL-self.hName+1-bl)

                dc.SetPen(gridPen)
                dc.DrawLine(xbeg, yL, xend, yL)
                self.lines = []
                self.Xpolys = []
                self.texts = []
                self.textCoords = []
                self.textFGs = []
                # xp and vp are prior-displayed edge position and value
                xp = xpd = max(wNames, xbeg-1)
                vp = sig.events[1]
                self.xtext = 0
                self.debug = 0 and (sig.name == "Vec1[1:0]")

                # gather-to-draw each segment of a signal
                for t, v in pairwise(sig.events):
                    x = min(wNames - x0 + int(t * dx), xend+2)
                    if self.debug:
                        print("\nDraw loop:", sig.name, "x=", x, "xp=", xp)
                    if sig.isBus or len(v) == 1:
                        if x >= xbeg:
                            if x > xp+1:
                                if v != vp:
                                    if (useSnap and iMouse == i and tMouse
                                            and abs(tMouse - t) < 500):
                                        # snap mouse ptr to nearby edge
                                        xyEdgeHilite = (x + x0,
                                                yL-self.hHL//2 + y0)
                                        tMouse = t
                                    
                                    if sig.isBus or vp == "X":
                                        self.GthrBusSegment(dc, sig,
                                                     xp, vp, x, v)
                                    else:
                                        self.GthrLineSegment(dc, sig,
                                                xp, vp, x, v, x < xend)
                                    if xp != xpd:
                                        self.GthrBusSegment(dc, sig,
                                                   xpd, "X", xp, vp)
                                    xpd = xp = x
                            else:
                                xp = x
                        if x > xend:
                            break
                        vp = v
                    else:
                        self.DrawAttachedText(dc, sig, x, v, xbeg, xend)

                # finish gathering-to-draw signal up to right edge
                if xend > xp+1:
                    if sig.isBus or vp == "X":
                        self.GthrBusSegment(dc, sig, xp, vp, xend, v)
                    else:
                        if len(v) == 1:
                            self.GthrLineSegment(dc, sig, xp, vp,
                                                 xend, v, False)
                if xp != xpd:
                    self.GthrBusSegment(dc, sig, xpd, "X", xp, vp)

                # draw gathered lines and polygons
                dc.SetPen(wx.Pen("BLACK"))
                dc.DrawLineList(self.lines)
                if len(self.Xpolys) > 0:
                    dc.SetBrush(wx.Brush(dkgray))
                    dc.DrawPolygonList(self.Xpolys)
                dc.DrawTextList(self.texts, self.textCoords, self.textFGs)
                self.debug = False
                yL += hRow
                i += 1
                if yL > yend:
                    break
            dc.SelectObject(wx.NullBitmap)

        pdc = wx.PaintDC(self)
        try:
            # use newer GCDC graphics under GTK for cursor's alpha channel
            dc = wx.GCDC(pdc)
        except:
            dc = pdc

        self.PrepareDC(dc)
        dc.DrawBitmap(self.wavesBitmap, x0, y0)

        # determine cursor state
        if self.mouseDown:
            self.iNameCursor = None
            self.tTimeCursor = None
            if not self.mouseWasDown:
                self.tiCursorsStart = tMouse, iMouse
            else:
                if self.mouseArea == "names":
                    self.iNameCursor = iMouse
                else:
                    self.tTimeCursor = tMouse
        self.mouseWasDown = self.mouseDown

        # draw name or time cursor, if active
        xbeg += x0
        xend += x0
        yend += y0
        dc.SetFont(wx.Font(self.nameFontSz, wx.SWISS, wx.NORMAL, wx.NORMAL))
        dc.SetTextForeground(dkblue)
        if not tMouse:
            tMouse = None
            if self.tiCursorsStart:
                tMouse = self.tiCursorsStart[0]
        if tMouse:
            s = locale._format("%%3.%df" % tickPlaces, float(tMouse)/ticksNS,
                              grouping=True)
            if self.tTimeCursor:
                dc.DrawText(s, x0+1, y0+2-bl)
            else:
                dc.DrawText("%s ns" % s, x0+10, y0+2-bl)
        if self.tiCursorsStart:
            dc.SetBrush(wx.Brush(wx.Colour(255, 255, 0, 64)))
            dc.SetPen(wx.Pen(wx.Colour(255, 255, 0, 64)))
            tm0, im0 = self.tiCursorsStart
            if self.iNameCursor:
                y = max((self.iNameCursor + 1) * hRow, y0+hRow)
                ym0 = max((im0 + 1) * hRow, y0+hRow)
                if ym0 < y:
                    y, ym0 = ym0, y
                dc.DrawRectangle(x0, y, xend - x0, ym0 - y)
            elif self.tTimeCursor:
                xm0 = min(max(wNames + int(tm0 * dx), xbeg), xend)
                xm = min(max(wNames + int(self.tTimeCursor * dx), xbeg), xend)
                if xm0 > xm:
                  xm, xm0 = xm0, xm
                dc.DrawRectangle(xm0, y0, xm - xm0, yend - y0)
                dt = float(abs(self.tTimeCursor - tm0)) / ticksNS
                s = locale._format("%%3.%df" % tickPlaces, dt, grouping=True)
                dc.DrawText("\u0394%s ns" % s, x0+65, y0+2-bl)

        if xyEdgeHilite:
            # snap: draw a small blue box to highlight signal edge
            x, y = xyEdgeHilite
            dc.SetBrush(wx.TRANSPARENT_BRUSH)
            dc.SetPen(wx.Pen(blue))
            dc.DrawRectangle(x-3, y-3, 6, 6)

        ##print("%3.3f: OnPaint() end" % (time.perf_counter() - t0))

    #--------------------------------------------------------------------------
    # Left mouse button pressed: start a cursor drag-select.

    def OnLeftDown(self, event):
        ##mods = ('-','A')[event.AltDown()] + ('-','S')[event.ShiftDown()] + \
        ##       ('-','M')[event.MetaDown()]
        ##print("OnLeftDown", mods)
        self.mouseDown = True

        x, y = event.GetPosition()
        self.xMouse = x
        self.yMouse = y
        self.mouseClicked = True
        ##print("OnLeftDown: x,y=", x, y)

        if x < self.wNames:
            self.mouseArea = "names"
        else:
            self.mouseArea = "main"

        self.Refresh()
        event.Skip()

    #--------------------------------------------------------------------------
    # Mouse button released.

    def OnLeftUp(self, event):
        self.mouseDown = False
        event.Skip()

    #--------------------------------------------------------------------------
    # Left mouse button double-clicked.

    def OnLeftDClick(self, event):
        x, y = event.GetPosition()
        ##print("OnLeftDClick: x,y=", x, y)
        if x < self.wNames:
            i = self.Y2I(y)
            if i < len(self.dispSigs):
                sig = self.dispSigs[i]
                if sig.srcFile:
                    self.frame.GotoSource(sig)
        event.Skip()

    #--------------------------------------------------------------------------
    # Right mouse button pressed.

    def OnRightDown(self, event):
        x, y = event.GetPosition()
        if x < self.wNames:
            i = self.Y2I(y)
            if i < len(self.dispSigs):
                sig = self.dispSigs[i]
                if sig.isBus:
                    # name area: expand bus bits
                    self.clip = sig.bitSigs
                    self.tiCursorsStart = None, i+1
                    self.iNameCursor = i+2
                    self.OnPaste(None)
        event.Skip()

    #--------------------------------------------------------------------------
    # Mouse moved: update cursor position.

    def OnMotion(self, event):
        frame = self.frame
        p = frame.p
        x, y = event.GetPosition()
        self.xMouse = x
        self.yMouse = y
        self.mouseDown = not event.Moving()
        ##print("OnMotion: x,y=", x, y, "moving=", event.Moving())

        self.Refresh()
        event.Skip()

    #--------------------------------------------------------------------------
    # Window scrolling event: update displayed cursor position.

    def OnScrollWin(self, event):
        ##print("OnScrollWin: orient=", event.GetOrientation())
        if event.GetOrientation() == wx.HORIZONTAL:
            self.Refresh()
        event.Skip()

    #--------------------------------------------------------------------------
    # Mouse left timing pane area.

    def OnLeave(self, event):
        self.xMouse = 0
        self.Refresh()
        event.Skip()

    #--------------------------------------------------------------------------
    # Regular key was pressed.

    def OnKeyDown(self, event):
        p = self.frame.p
        k = event.GetKeyCode()
        m = event.GetModifiers()
        ##print("KeyDown:", k, m)

        # handle Command-arrow combinations to scroll to ends
        if m == wx.MOD_CONTROL:
            xsPos = self.GetScrollPos(wx.HORIZONTAL)
            ysPos = self.GetScrollPos(wx.VERTICAL)
            if   k == wx.WXK_UP:    ysPos = 0
            elif k == wx.WXK_DOWN:  ysPos = self.ysmax
            elif k == wx.WXK_LEFT:  xsPos = 0
            elif k == wx.WXK_RIGHT: xsPos = self.xsmax
            else: k = None
            if k:
                p.xsPos, p.ysPos = xsPos, ysPos
                self.haveSPos = False
                self.AdjustMyScrollbars()

        event.Skip()
        self.Refresh()

    #--------------------------------------------------------------------------
    # Get indecies into self.dispSigs[] of signals selected by name cursor.

    def NameCursorIndecies(self):
        i0 = self.iNameCursor
        if not i0:
            return None, None
        i1 = self.tiCursorsStart[1] 
        if i0 > i1:
            i0, i1 = i1, i0
        return i0, i1

    #--------------------------------------------------------------------------
    # Cut selected signals to the clipboard.

    def OnCut(self, event):
        i0, i1 = self.NameCursorIndecies()
        if i0:
            self.clip = self.dispSigs[i0:i1]
            for sig in self.clip:
                sig.isDisplayed = False
            self.iNameCursor = None
            p = self.frame.p
            self.AdjustMyScrollbars()

    #--------------------------------------------------------------------------
    # Copy selected signals to the clipboard.

    def OnCopy(self, event):
        i0, i1 = self.NameCursorIndecies()
        if i0:
            self.clip = self.dispSigs[i0:i1]

    #--------------------------------------------------------------------------
    # Paste clipboard signals to cursor position.

    def OnPaste(self, event):
        i0, i1 = self.NameCursorIndecies()
        if i0 and self.clip:
            p = self.frame.p
            newSigs = {}
            for sig in self.dispSigs[:i0]:
                newSigs[sig.index] = sig
            for sig in self.clip:
                newSigs[sig.index] = sig
                sig.isDisplayed = True
            for sig in self.dispSigs[i0:]:
                newSigs[sig.index] = sig
            self.frame.sigs = newSigs
            self.iNameCursor = i0
            self.tiCursorsStart = None, i0 + len(self.clip)
            self.AdjustMyScrollbars()

    #--------------------------------------------------------------------------
    # Find string within a signal name and center window on it.

    def Find(self, findString, flags, fromTop=False):
        p = self.frame.p
        ignoreCase = not (flags & wx.FR_MATCHCASE)
        whole = flags & wx.FR_WHOLEWORD
        s = findString
        if ignoreCase:
            s = s.upper()
        i0 = -1
        for i0, sig in enumerate(self.dispSigs):
            if (fromTop or i0 > self.iNameCursor):
                name = sig.name
                if ignoreCase:
                    name = name.upper()
                if (name.find(s) >= 0, name == s)[whole]:
                    ##print("Found signal", i0, sig.name)
                    self.iNameCursor = i0
                    self.tiCursorsStart = None, i0 + 1
                    break
        else:
            ##print("'%s' not found." % findString)
            wx.Bell()
            self.iNameCursor = 0
            self.tiCursorsStart = None, 0

        if i0 >= 0:
            wWin, hWin = self.GetClientSize()
            p.ysPos = (i0*self.hRow - hWin//2) // self.hRow
            p.xsPos = self.GetScrollPos(wx.HORIZONTAL)
            self.haveSPos = False
            self.AdjustMyScrollbars()

#==============================================================================
# A signal or bus of signals, read from events file.

class Signal(object):
    def __init__(self, index, name, events, srcFile, srcPos, isBus=False,
                 lsub=0, rsub=0):
        self.index = index
        self.name = name
        self.srcFile = srcFile
        self.srcPos = srcPos
        self.events = events
        self.isDisplayed = True
        self.isBus = isBus
        self.lsub = lsub
        self.rsub = rsub
        self.busSig = None
        if isBus:
            self.bitSigs = []
        else:
            self.bitSigs = None
        self.sub = None

    def __str__(self):
        return "Signal('%s', %d)" % (self.name, self.index)

    def __repr__(self):
        return self.__str__()


#==============================================================================
# The main GUI frame.

class PVSimFrame(wx.Frame):
    def __init__(self, parent):
        global fontSize

        self.mpPool = None
        self.mpLogQueue = None
        sp = wx.StandardPaths.Get()
        if isMac:
            # in OSX, get PVSim.app/Contents/Resources/pvsim.py path
            # because GetResourcesDir() returns Python.app path instead
            self.resDir = os.path.dirname(sys.argv[0])
        else:
            self.resDir = sp.GetResourcesDir()
        if isMac:
            pvsimPath = os.path.dirname(os.path.dirname(self.resDir))
        elif isLinux:
            pvsimPath = os.path.abspath(sys.argv[0])
        else:
            pvsimPath = sp.GetExecutablePath()
        pvsimDir = os.path.dirname(pvsimPath)
        sep = os.path.sep
        if len(pvsimDir) > 0:
            pvsimDir += sep
        self.pvsimDir = pvsimDir
        self.p = p = Prefs()

        # dataDir is where log file gets written
        dataDir = sp.GetUserLocalDataDir()
        if not os.path.exists(dataDir):
            os.makedirs(dataDir)
        os.chdir(dataDir)

        # use previous project, or copy in example project if none
        p.get("projName", "example1")
        projDir = p.get("projDir", None)
        if not (projDir and os.path.exists(projDir)):
            projDir = dataDir
            for f in ("example1.psim", "example1.v"):
                src = os.path.join(self.resDir, f)
                if os.path.exists(src):
                    shutil.copy(src, projDir)
                else:
                    projDir = None
                    break

        if projDir and not os.path.exists(projDir):
            projDir = None
        p.projDir = projDir
        if projDir:
            title = "PVSim - %s" % p.projName
        else:
            title = "PVSim"

        wx.Frame.__init__(self, parent, -1, title,
                          pos=p.get("framePos", (100, 30)),
                          size=p.get("frameSize", (900, 950)))
        
        self.drawEnabled = False
        self.barSignal = None
        self.sigs = {}
        self.orderFileName = None
        self.nTicks = 0
        self.wTick = p.get("wTick", 0.5)
        p.get("timingScale", 1.4)
        self.testChoices = None

        # tell FrameManager to manage this frame        
        self._mgr = mgr = wx.aui.AuiManager()
        mgr.SetManagedWindow(self)
        self.SetMinSize(wx.Size(400, 300))
        self._mpLogNB = None
        
        self._perspectives = []
        self.n = 0
        self.x = 0

        # calibrate font point size for this system
        font = wx.Font(fontSize, wx.SWISS, wx.NORMAL, wx.NORMAL)
        font.SetPixelSize((200, 200))
        pointSize200px = font.GetPointSize()
        adjFontSize = fontSize * pointSize200px // (170, 148)[isLinux]
        if 0:
            # test it
            dc = wx.ClientDC(self)
            dc.SetFont(font)
            extent = dc.GetTextExtent("M")
            print("pointSize200px=", pointSize200px, "orig fs=", fontSize,
                "new fs=", adjFontSize, "extent=", extent)
            font10 = wx.Font(adjFontSize, wx.SWISS, wx.NORMAL, wx.NORMAL)
            ps10 = font10.GetPointSize()
            font27 = wx.Font(int(adjFontSize*2.7), wx.SWISS, wx.NORMAL, wx.NORMAL)
            ps27 = font27.GetPointSize()
            print("10-point pointsize=", ps10, "27-point pointsize=", ps27)
        fontSize = adjFontSize

        # set up menu bar
        self.menubar = wx.MenuBar()
        self.fileMenu = self.CreateMenu("&File", [
            ("Quit\tCTRL-Q",            "OnExit", wx.ID_EXIT),
            ("About PVSim...",          "OnAbout", wx.ID_ABOUT),
            ("Open .psim File...\tCTRL-O", "OnOpen", -1),
            ("Save image to File...\tCTRL-S", "SaveToFile", -1),
            ("Add divider line to log\tCTRL-D", "PrintDivider", -1),
        ])
        self.editMenu = self.CreateMenu("&Edit", [
            ("Cut\tCTRL-X",              "OnCut", -1),
            ("Copy\tCTRL-C",             "OnCopy", -1),
            ("Paste\tCTRL-V",            "OnPaste", -1),
            ("-",                        None, -1),
            ("Find\tCTRL-F",             "OnShowFind", -1),
            ("Find Again\tCTRL-G",       "OnFindAgain", -1),
        ])
        self.viewMenu = self.CreateMenu("&View", [
            ("Zoom In\tCTRL-=",         "ZoomIn", -1),
            ("Zoom Out\tCTRL--",        "ZoomOut", -1),
            ("Zoom to Selection\tCTRL-E", "ZoomToSelection", -1),
            ("Scale Smaller\tCTRL-[",   "ScaleSmaller", -1),
            ("Scale Larger\tCTRL-]",    "ScaleLarger", -1),
        ])
        self.simulateMenu = self.CreateMenu("&Simulate", [
            ("Run Simulation\tCTRL-R",  "RunSimulation", -1),
            ("-",                        None, -1),
        ])
        self.testChoicesSubMenu = None

        self.fileHistory = wx.FileHistory(8)
        self.fileHistory.UseMenu(self.fileMenu)
        self.fileHistory.Load(p.config())
        self.Bind(wx.EVT_MENU_RANGE, self.OnFileHistory, id=wx.ID_FILE1,
                  id2=wx.ID_FILE9)
        if projDir:
            projPath = os.path.join(projDir, p.projName) + ".psim"
            self.fileHistory.AddFileToHistory(projPath)
            self.fileHistory.Save(p.config())

        self.SetMenuBar(self.menubar)

        # create a log text panel below and set it to log all output
        self.logP = logP = wx.TextCtrl(self, 
                             style=wx.TE_MULTILINE|wx.TE_READONLY|wx.TE_RICH)
        logP.SetFont(wx.Font(fontSize, wx.TELETYPE, wx.NORMAL, wx.NORMAL))
        # allow panes to be inited to 70% width, default is 30%
        mgr.SetDockSizeConstraint(0.7, 0.7)
        mgr.AddPane(logP, (wx.aui.AuiPaneInfo()
                            .Direction(p.get("logDir", 3))
                            .BestSize(p.get("logSize", (898, 300)))
                            .Caption("Log")))

        ##self.ChChartPanel = ChartPanel(self, self)
        ##mgr.AddPane(self.ChChartPanel, wx.aui.AuiPaneInfo().CenterPane())
        self.timingP = timeP = TimingPane(self, self)
        mgr.AddPane(timeP, (wx.aui.AuiPaneInfo().CenterPane()
                            .BestSize(p.get("timingSize", (898, 600)))
                            ))
 
        # redirect all output to a log file
        self.rootName = "pvsim"
        if 1:
            self.origStdout = sys.stdout
            self.origStderr = sys.stderr
            logPath = os.path.join(projDir,
                self.rootName) if projDir is not None else self.rootName
            ##print("logPath=", logPath)
            sys.stdout = Logger(logPath, logP)
            sys.stderr = Logger(logPath, logP)
        if isWin:
            curLocale = None
        else:
            curLocale = locale.setlocale(locale.LC_ALL, "en_US")
        if 1:
            print("Python:", sys.version)
            print("%d-bit Python" % (len(bin(sys.maxsize)) - 1))
            print("wxPython:", wx.version())
            print("env LANG=", os.getenv("LANG"))
            print("Locale:", curLocale)
            print("Platform:", sys.platform)
            print("Resource dir:", self.resDir)
            print("PVSim    dir:", pvsimDir)
            print("Project  dir:", projDir)
            print()
        print("PVSim GUI", guiVersion, "started", time.ctime())

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

        self.findString = ""
        self.drawEnabled = True
        self.timingP.AdjustMyScrollbars()
        ##timeP.DoDrawing()
        self.Show()
        ##print(time.perf_counter(), "PVSimFrame done")
        self.timingP.SetFocus()

        if isMac:
            # start the IPC server
            (lis, success) = pub.subscribe(self.DoEditorCmd, "doCmd")
            if not success:
                print("listener already subscribed to 'doCmd'")
            self.ipc = IPCThread()

        self.OpenFile()

        ##self.RunSimulation()      # include this when debugging

    #--------------------------------------------------------------------------
    # Create a menu from a list.

    def CreateMenu(self, name, itemList):
        menu = wx.Menu()
        for itemName, handlerName, id in itemList:
            if itemName == "-":
                menu.AppendSeparator()
            else:
                if id == -1:
                    id = wx.NewIdRef()
                item = menu.Append(id, itemName)
                if handlerName and hasattr(self, handlerName):
                    self.Connect(id, -1, wx.wxEVT_COMMAND_MENU_SELECTED, \
                                getattr(self, handlerName))
                else:
                    item.Enable(False)
        self.menubar.Append(menu, name)
        return menu

    #--------------------------------------------------------------------------
    # Create a toolbar from a list.

    def CreateToolbar(self, bitmapSize, itemList):
        tb = wx.ToolBar(self, -1, wx.DefaultPosition, wx.DefaultSize)
        tb.SetToolBitmapSize(bitmapSize)
        for itemName, handlerName, bmp in itemList:
            if isinstance(bmp, str):
                bmp = wx.ArtProvider_GetBitmap(bmp)
            if isinstance(itemName, str):
                item = tb.AddLabelTool(wx.ID_ANY, itemName, bmp)
                if hasattr(self, handlerName):
                    self.Bind(wx.EVT_MENU, getattr(self, handlerName), item)
            else:
                item = tb.AddControl(itemName)
        tb.Realize()
        return tb

    def PrintDivider(self, event):
        print("----------------------------")

    #--------------------------------------------------------------------------
    # 'About PVSim' dialog box.

    def OnAbout(self, event):
        version = pvsimu.GetVersion()
        info = wx.AboutDialogInfo()
        info.Name = "PVSim"
        if version == guiVersion:
            info.Version = version
        else:
            info.Version = "%s/%s" % (version, guiVersion)
        info.Copyright = "(C) 2012 Scott Forbes"
        info.Description = wordwrap(
            "PVSim is a portable Verilog simulator that features a fast "
            "compile-simulate-display cycle.",
            350, wx.ClientDC(self))
        info.WebSite = ("http://sourceforge.net/projects/pvsim",
            "PVSim home page")
        info.Developers = ["Scott Forbes"]
        info.License = wordwrap(license.replace("# ", ""),
            500, wx.ClientDC(self))

        if isMac:
            # AboutBox causes a crash on app exit if parent is omitted here
            # (see wxWidgets ticket #12402)
            wx.AboutBox(info, self)
        else:
            wx.AboutBox(info)

    #--------------------------------------------------------------------------
    # Pass cut,copy,paste to timing window.

    def OnCut(self, event):
        widget = self.FindFocus()
        if widget == self.timingP:
            widget.OnCut(event)
        else:
            widget.Cut()

    def OnCopy(self, event):
        widget = self.FindFocus()
        if widget == self.timingP:
            widget.OnCopy(event)
        else:
            widget.Copy()

    def OnPaste(self, event):
        widget = self.FindFocus()
        if widget == self.timingP:
            widget.OnPaste(event)
        else:
            widget.Paste()

    #--------------------------------------------------------------------------
    # Bring up a Find Dialog to find a signal in the timing window.

    def OnShowFind(self, event):
        data = wx.FindReplaceData()
        data.SetFindString(self.findString)
        dlg = wx.FindReplaceDialog(self, data, "Find", style=wx.FR_NOUPDOWN)
        dlg.data = data
        dlg.Show(True)

    def OnFind(self, event):
        dlg = event.GetDialog()
        self.findString = event.GetFindString()
        self.findFlags = event.GetFlags()
        self.timingP.Find(self.findString, self.findFlags, True)
        dlg.Destroy()

    def OnFindAgain(self, event=None):
        self.timingP.Find(self.findString, self.findFlags)

    def OnFindCancel(self, event):
        event.GetDialog().Destroy()

    #--------------------------------------------------------------------------
    # Execute a command from the external editor.

    def DoEditorCmd(self, cmd):
        print("Doing editor command", cmd)
        if cmd == "find":
            # Get selected text from BBEdit editor
            result = subprocess.run(
                ['osascript', '-e', 'return text of selection of application "BBEdit" as string'],
                capture_output=True,
                text=True
            )
            name = result.stdout.strip()
            print("Find signal", name)
            self.timingP.Find(name, 0, True)

    #--------------------------------------------------------------------------
    # Zoom timing window in/out.

    def ZoomIn(self, event=None):
        self.wTick *= 2.
        self.timingP.AdjustMyScrollbars()

    def ZoomOut(self, event=None):
        self.wTick *= 0.5
        self.timingP.AdjustMyScrollbars()

    def ZoomToSelection(self, event=None):
        tp = self.timingP
        if tp.tiCursorsStart and tp.tTimeCursor:
            tp.AdjustMyScrollbars(startTick=tp.tiCursorsStart[0],
                                  endTick=tp.tTimeCursor)

    #--------------------------------------------------------------------------
    # Adjust timing window text and detail size.

    def ScaleSmaller(self, event=None):
        self.p.timingScale /= 1.05
        self.timingP.AdjustMyScrollbars()

    def ScaleLarger(self, event=None):
        self.p.timingScale *= 1.05
        self.timingP.AdjustMyScrollbars()

    #--------------------------------------------------------------------------
    # Handle a resize event of the main frame or log pane sash.

    def OnSize(self, event):
        self.p.frameSize = self.GetSize()
        self.Refresh()
        event.Skip()

    #--------------------------------------------------------------------------
    # Read the signal display-order file, if any, and put sigs in that order.

    def ReadOrderFile(self, orderFileName):
        self.orderFileName = orderFileName
        try:
            print("Reading order file", orderFileName, "...")
            ordf = open(orderFileName, "r")
            newSigs = {}
            for sig in list(self.sigs.values()):
                sig.isInOrder = False
            for name in ordf.readlines():
                name = name.strip()
                if len(name) > 1 and name[0] != "{":
                    names = [name]
                    i = name.find("[")
                    if i > 0:
                        names.append(name[:i])
                    foundSig = False
                    for i, sig in list(self.sigs.items()):
                        ##print("sig:", i, sig.name, sig.isBus, sig.sub)
                        if sig.name in names and not sig.isInOrder:
                            ##print(" ordered", name, sig.isBus, sig.sub)
                            newSigs[i] = sig
                            sig.isInOrder = True
                            sig.isDisplayed = True
                            foundSig = True
                            break
                    if not foundSig:
                        print(" Not found:", names)
            ordf.close()
            for i, sig in list(self.sigs.items()):
                if not sig.isInOrder:
                    newSigs[i] = sig
            self.sigs = newSigs
            print(" Signals ordered.")

        except IOError:
            # file didn't exist: ignore
            print("No existing order file.")

    #--------------------------------------------------------------------------
    # Save signal order back to file.

    def SaveOrderFile(self):
        if self.orderFileName:
            ordf = open(self.orderFileName, "w")
            for sig in self.timingP.dispSigs:
                ordf.write("%s\n" % sig.name)
            ordf.close()

    #--------------------------------------------------------------------------
    # A new test was chosen in the Simulate menu.

    def OnTestChoice(self, event):
        for item in self.simulateMenu.GetMenuItems():
            if item.IsChecked():
                self.p.testChoice = item.GetLabel()
                break
        self.UpdateTestChoicesView()

    #--------------------------------------------------------------------------
    # Add test choices to Simulate menu.

    def AddTestChoicesMenu(self):
        p = self.p

        # add the test choices to the Simulation menu
        for choice in self.testChoices:
            id = wx.NewIdRef()
            item = self.simulateMenu.AppendRadioItem(id, choice)
            self.Connect(id, -1, wx.wxEVT_COMMAND_MENU_SELECTED, \
                         self.OnTestChoice)
            if choice == p.testChoice:
                self.simulateMenu.Check(id, True)

    #--------------------------------------------------------------------------
    # Update test choices log page(s) from p.testChoice after selection.

    def UpdateTestChoicesView(self):
        p = self.p
        mgr = self._mgr
        self.mpLogs = {}
        if self._mpLogNB != None:
            mgr.ClosePane(mgr.GetPane(self._mpLogNB))
            self._mpLogNB = None

        if len(self.testChoices) > 1 and p.testChoice == "All":
            # split the log window into pages, one per choice
            nb = wx.aui.AuiNotebook(self, style=wx.aui.AUI_NB_TOP|
                                                wx.aui.AUI_NB_TAB_SPLIT|
                                                wx.aui.AUI_NB_SCROLL_BUTTONS)
            self._mpLogNB = nb
            mgr.AddPane(nb, (wx.aui.AuiPaneInfo()
                            .Direction(p.get("logDir", 3))
                            .Position(1)
                            .BestSize(p.get("logSize", (898, 300)))
                            .Caption("Tests")))
            numberedAttrPat = re.compile(r"^[A-Z]+([0-9]+[A-Z]*)$")
            for choice in self.testChoices[:-1]:
                pageTC = wx.TextCtrl(self, style=wx.TE_MULTILINE|
                                   wx.TE_READONLY|wx.TE_RICH)
                pageTC.SetFont(wx.Font(fontSize, wx.TELETYPE, wx.NORMAL,
                             wx.NORMAL))
                words = choice.split("_")
                if words[0] == "TEST":
                    words = words[1:]
                title = []
                for word in words:
                    m = numberedAttrPat.match(word)
                    if m:
                        word = m.group(1)
                    title.append(word)
                title = ".".join(title)
                ##title = ".".join([w[-2:] for w in words])
                nb.AddPage(pageTC, title)
                logPath = os.path.join(p.projDir, self.rootName+"_"+choice)
                self.mpLogs[choice] = Logger(logPath, pageTC)
                ##self.logP = pageTC
        else:
            self.mpLogs[p.testChoice] = sys.stdout
        mgr.Update()

    #--------------------------------------------------------------------------
    # Gather any test choice lines from .psim file.
    # Also initializes testChoice preference if needed, and makes menu.

    def GatherTestChoices(self, projFileFullName):
        p = self.p
        choices = []
        try:
            for line in open(projFileFullName):
                line = line.strip()
                words = line.split()
                if len(words) > 1 and words[0] == "testChoice":
                    choices.append(words[1])
        except:
            print("Project file", projFileFullName, "not found")
        choices.append("All")
        self.testChoices = choices
        p.get("testChoice", choices[0])
        self.AddTestChoicesMenu()
        self.UpdateTestChoicesView()

    #--------------------------------------------------------------------------
    # Set given .psim project file for future simulation runs.

    def OpenFile(self, fileName=None):
        ##print("OpenFile:", fileName)
        p = self.p
        if fileName == None:
            if self.fileHistory.GetCount() == 0:
                return
            fileName = os.path.join(p.projDir, p.projName + ".psim")
        else:
            self.fileHistory.AddFileToHistory(fileName)
            self.fileHistory.Save(p.config())
        projDir, name = os.path.split(fileName)
        if len(projDir) == 0:
            projDir, name = os.path.split(os.path.abspath(fileName))
        projDir += os.path.sep
        ##print("OpenFile:", fileName, projDir, name)
        p.projDir = projDir
        p.projName, ext = os.path.splitext(name)
        if ext == ".psim":
            self.SetTitle("PVSim - %s" % p.projName)
            print("Set %s in %s as simulation source" % (p.projName,p.projDir))
            ##self.RunSimulation()

            # gather any test choice lines from .psim file
            self.GatherTestChoices(os.path.join(projDir, name))

        else:
            wx.Bell()

    #--------------------------------------------------------------------------
    # Choose a .psim project file for future simulation runs.

    def OnOpen(self, event):
        print("OnOpen")
        wildcard = "PVSim file (*.psim)|*.psim"
        dlg = wx.FileDialog(self, message="Load...", defaultDir=os.getcwd(),
                defaultFile="", wildcard=wildcard, style=wx.FD_OPEN)
        dlg.SetFilterIndex(0)
        if dlg.ShowModal() == wx.ID_OK:
            self.OpenFile(dlg.GetPath())

    #--------------------------------------------------------------------------
    # Have chosen a past file from the File menu: open it.

    def OnFileHistory(self, event):
        # get the file based on the menu ID
        fileNum = event.GetId() - wx.ID_FILE1
        self.OpenFile(self.fileHistory.GetHistoryFile(fileNum))

    #--------------------------------------------------------------------------
    # Start timer for periodic log window updating.

    def StartMPTimer(self, mpPool, mpLogQueue):
        self.mpPool = mpPool
        self.mpLogQueue = mpLogQueue
        self.timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.OnTimer)
        # start display-update timer, given interval in ms
        msPerUpdate = 100
        self.timer.Start(msPerUpdate)

    #--------------------------------------------------------------------------
    # Start a simulation process in the background for each selected test.

    def StartMPSimulation(self):
        p = self.p
        if p.testChoice == "All":
            if len(self.testChoices) > 1:
                tests = self.testChoices[:-1]
            else:
                tests = [None]
        else:
            tests = [p.testChoice]

        ##print("StartMPSimulation: tests=", tests)
        testArgs = [(p.projDir, p.projName, self.nTicks, self.barSignal,
             self.mpLogQueue, test, test == tests[-1]) for test in tests]
        r = self.mpPool.map_async(mpWorker, testArgs, callback=mpWorkerDone)
        ##print("StartMPSimulation: active=", mp.active_children())

    #--------------------------------------------------------------------------
    # All simulation processes have finished: draw it and report any errors.

    def OnMPResult(self):
        global errCount
        p = self.p
        ##print("OnMPResult: errCount=", errCount)

        if errCount == 0:
            print("%2.1f: Simulation done, no errors.\n" % \
                (time.perf_counter() - t0))
            proj = os.path.join(p.projDir, p.projName)
            self.ReadOrderFile("%s.order" % proj)

        # draw final test's results in timing pane
        self.timingP.AdjustMyScrollbars()

    #--------------------------------------------------------------------------
    # Timer tick: display any message lines from the back end process(es).

    def OnTimer(self, event):
        if self.mpLogQueue:
            while not self.mpLogQueue.empty():
                name, msg = self.mpLogQueue.get()
                self.mpLogs[name].write(msg)

    #--------------------------------------------------------------------------
    # Run pvsimu simulation to generate events file, then display timing.

    def RunSimulation(self, event=None):
        global mainFrame
        mainFrame = self
        p = self.p
        print("\nRunSimulation")

        self.logP.SetInsertionPointEnd()
        if len(self.timingP.dispSigs) > 0:
            self.SavePrefs()

        pvsimu.Init()
        backendVersion = pvsimu.GetVersion()
        print("Found PVSim %s backend" % backendVersion)

        if useMultiprocessing:
            print("Using Multiprocessing")
            self.StartMPSimulation()
        else:
            print("Single process only")
            self.Connect(-1, -1, EVT_RESULT_ID, self.OnResult)
            self.worker = SimThread(self)

    #--------------------------------------------------------------------------
    # The threaded simulation task finished: terminate it and draw results.

    def OnResult(self, event):
        if event.msg:
            print(event.msg, end="")
        else:
            self.worker = None

            # convert strings from backend to Unicode
            for sig in list(self.timingP.frame.sigs.values()):
                ##print(sig.name)
                convertedEvents = []
                for t, level in pairwise(sig.events):
                    ##print(f"{t} {level}", end="")
                    if type(level) is type(b""):
                        level = level.decode('utf-8')
                    ##print(f" {level}")
                    convertedEvents.append(t)
                    convertedEvents.append(level)
                sig.events = convertedEvents

            # draw results in timing pane
            self.timingP.AdjustMyScrollbars()

    #--------------------------------------------------------------------------
    # Open the source file for given signal and center on its definition.

    def GotoSource(self, sig):
        p = self.p
        print("GotoSource", sig.name, sig.srcFile, sig.srcPos)
        if isMac:
            name = sig.name
            i = name.find("[")
            if i > 0:
                name = name[:i]
            name = name.split(".")[-1]
            if 0:
                # Select word using Alpha editor
                os.system("osascript -e 'tell application \"AlphaX\"" \
                    " to doscript \"gotoFileLocation {%s%s} %d %s\"'" % \
                    (p.projDir, sig.srcFile, sig.srcPos, name))
                os.system("osascript -e 'tell application \"AlphaX\"" \
                    " to activate'")
            else:
                winName = sig.srcFile.split("/")[-1]
                print (name, sig.srcPos, sig.srcPos+len(name)+2,
                     sig.srcFile, winName)
                # Select word using BBEdit editor
                # (would like to also update Find's search string, but
                #  that property is read-only)
                result = subprocess.run(
                    ['osascript', '-e', 'tell application "Finder" to get '
                        'application id "com.barebones.bbedit"'],
                    capture_output=True,
                    text=True
                )
                editor = result.stdout.strip()
                print(f"{editor=}")
                if editor != '':
                    os.system("osascript -e 'tell application \"%s\"\n" \
                        "  open \"%s%s\"\n" \
                        "  find \"%s\" searching in characters %d thru %d" \
                        "   of document \"%s\" options {case sensitive:true," \
                        "   match words:true}\n" \
                        "  if found of result then\n" \
                        "    select found object of result\n" \
                        "    activate window \"%s\"\n" \
                        "  end if\n" \
                        "end tell'" % \
                        (editor, p.projDir, sig.srcFile, name, sig.srcPos,
                         sig.srcPos+len(name)+2, winName, winName))
                else:
                    print("External editor BBEdit not found")

    #--------------------------------------------------------------------------
    # Return a dictionary of a pane's settings.

    def GetPanePrefs(self, pane):
        info = self._mgr.SavePaneInfo(self._mgr.GetPane(pane))
        infoDict = {}
        for nv in info.split(";"):
            name, val = nv.split("=")
            infoDict[name] = val
        return infoDict

    #--------------------------------------------------------------------------
    # Save prefs to file.

    def SavePrefs(self):
        p = self.p
        p.framePos = tuple(self.GetPosition())
        logInfo = self.GetPanePrefs(self.logP)
        p.logDir = logInfo["dir"]
        p.logSize = self.logP.GetSize()
        ##print("logSize=", p.logSize)
        p.timingSize = self.timingP.GetSize()
        ##print("timingSize=", p.timingSize)
        p.xsPos = self.timingP.GetScrollPos(wx.HORIZONTAL)
        p.ysPos = self.timingP.GetScrollPos(wx.VERTICAL)
        p.save()

        self.SaveOrderFile()

    #--------------------------------------------------------------------------
    # Quitting: save prefs to file.

    def OnExit(self, event=None):
        ##print("OnExit")
        # debug prints here go to standard output since log soon won't exist
        if hasattr(self, "origStdout"):
            sys.stdout = self.origStdout
            sys.stderr = self.origStderr

        self.SavePrefs()
        if isMac:
            self.ipc.stop()
            self.ipc.join()  # Wait for the thread to finish
        self.Destroy()     # just exit from App so cProfile may complete
        if not self.mpPool is None:
            # terminate the processes
            self.mpPool.close()
            ##print("OnExit: pool closed")
            self.mpPool.join()
            ##print("OnExit: pool joined")


#==============================================================================
# Start up application.

class PVSimApp(wx.App):
    def OnInit(self):
        ##print("OnInit")
        psimFile = None
        if not (len(sys.argv) == 2 and sys.argv[1][:4] == "-psn"):
            # *** UNUSED ***
            ##print("args=", sys.argv)
            ##parser = OptionParser()
            ##(cmdLineOpts, remainder) = parser.parse_args()
            if len(sys.argv) == 2:
                psimFile = os.path.abspath(sys.argv[1])

        self.frame = frame = PVSimFrame(None)
        ##if psimFile and os.path.splitext(psimFile)[1] == ".psim":
        ##    # *** UNUSED ***
        ##    print("PVSimApp: running", psimFile)
        ##    frame.OpenFile(psimFile)
        ##    frame.RunSimulation()
        ##print("OnInit end")
        return True

def RunApp():
    global app
    try:
        if useMultiprocessing:
            # create pool of sim-worker processes before starting app
            ##mpPool = mp.Pool(mp.cpu_count()-1)    # too many-- bogs down
            mpPool = mp.Pool(max(mp.cpu_count()//2, 1))  # = 4 procs on i7
            mpLogQueue = mp.Manager().Queue()

        ##print("RunApp")
        app = PVSimApp(redirect=False)
        if useMultiprocessing:
            app.frame.StartMPTimer(mpPool, mpLogQueue)
        prof = app.frame.pvsimDir +"pvsim.profile"
        ##print("RunApp profile=", showProfile, prof)
        if showProfile:
            cProfile.run("app.MainLoop()", prof)
            print("To see the stats, type: ./showprof.py")
        else:
            app.MainLoop()
        ##print("RunApp end")

    except:
        exctype, value = sys.exc_info()[:2]
        if exctype != SystemExit:
            dlg = dialogs.ScrolledMessageDialog(None, traceback.format_exc(),
                                            "Error")
            dlg.ShowModal()
        print("PVSim app exiting")
        sys.exit(-1)


#------------------------------------------------------------------------------

if __name__ == "__main__":
    RunApp()
