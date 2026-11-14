#!/usr/bin/env python3
"""X6100 BASE firmware uploader - desktop GUI (Windows / Linux / macOS).

Thin tkinter wrapper around x6100_core (shared with the Android app).

Run from source:
    pip install -r requirements.txt
    python x6100_uploader.py
"""
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, ttk

from x6100_core import (
    ABOUT_TEXT, DEFAULT_PASS, DEFAULT_PORT, DEFAULT_USER,
    DONE_TITLE, discover_radios, display_names, done_instructions, download_file,
    fetch_latest_asset_url, fw_label, list_local_fw, remote_path,
    start_checklist, upload,
)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("X6100 Base Patch Uploader")
        self.resizable(True, True)
        self.jobs = queue.Queue()
        self.stop_scan = threading.Event()
        self.fw_paths = list_local_fw()
        self._build()
        self._poll()
        self._refresh_fw_menu()

    def _build(self):
        menubar = tk.Menu(self)
        helpmenu = tk.Menu(menubar, tearoff=0)
        helpmenu.add_command(label="Manual steps", command=self._manual)
        helpmenu.add_command(label="About", command=self._about)
        menubar.add_cascade(label="Help", menu=helpmenu)
        self.configure(menu=menubar)
        self.title("X6100 Base Patch Uploader")
        self.geometry("660x640")
        self._style()

        wrap = ttk.Frame(self, style="BG.TFrame")
        wrap.pack(fill="both", expand=True, padx=14, pady=12)

        head = tk.Frame(wrap, background="#1c2330")
        head.pack(fill="x", pady=(0, 2))
        title_lbl = tk.Label(head, text="X6100 Base Patch Uploader",
                             background="#1c2330", foreground="white",
                             font=("TkDefaultFont", 11, "bold"))
        title_lbl.pack(side="left", padx=10, pady=7)
        for txt, cmd in (("✕", self.destroy), ("-", self.iconify)):
            tk.Button(head, text=txt, command=cmd, background="#1c2330",
                      foreground="white", activebackground="#cf222e",
                      activeforeground="white", relief="flat", width=4,
                      font=("TkDefaultFont", 10, "bold")).pack(side="right", padx=(0, 2))
        for txt, cmd in (("About", self._about), ("Manual steps", self._manual)):
            tk.Button(head, text=txt, command=cmd, background="#2d3748",
                      foreground="white", activebackground="#1f6feb",
                      relief="flat", font=("TkDefaultFont", 9)).pack(side="right", padx=3, pady=4)
        head.bind("<ButtonPress-1>", lambda e: setattr(head, "_off", (e.x, e.y)))
        head.bind("<B1-Motion>", lambda e: self.geometry(
            f"+{e.x_root - head._off[0]}+{e.y_root - head._off[1]}"))
        title_lbl.bind("<ButtonPress-1>", lambda e: setattr(head, "_off", (e.x, e.y)))
        title_lbl.bind("<B1-Motion>", lambda e: self.geometry(
            f"+{e.x_root - head._off[0]}+{e.y_root - head._off[1]}"))
        ttk.Label(wrap, text="root / 123  →  /usr/firmware",
                  style="Dim.TLabel").pack(anchor="e", pady=(0, 10))

        sec1 = self._card(wrap, "1 · Firmware (.xgf)")
        row = ttk.Frame(sec1, style="Card.TFrame")
        row.pack(fill="x")
        self.fw_var = tk.StringVar()
        self.fw_box = ttk.Combobox(row, textvariable=self.fw_var, state="readonly")
        self.fw_box.pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Browse…", command=self._browse).pack(side="left", padx=(6, 0))
        ttk.Button(row, text="Download latest", command=self._download).pack(side="left", padx=(6, 0))
        self._build_radio(wrap)
        self._build_bottom(wrap)

    def _style(self):
        s = ttk.Style(self)
        for theme in ("clam", "alt", "default"):
            try:
                s.theme_use(theme)
                break
            except tk.TclError:
                continue
        BG, CARD, INK, DIM, ACC = "#eef1f6", "#ffffff", "#1c2330", "#5b6472", "#1f6feb"
        self.configure(background=BG)
        s.configure("BG.TFrame", background=BG)
        s.configure("Card.TFrame", background=CARD)
        s.configure("Card.TLabelframe", background=CARD, borderwidth=0)
        s.configure("Card.TLabelframe.Label", background=CARD,
                    foreground=ACC, font=("TkDefaultFont", 10, "bold"))
        s.configure("Title.TLabel", background=BG, foreground=INK,
                    font=("TkDefaultFont", 13, "bold"))
        s.configure("Dim.TLabel", background=BG, foreground=DIM,
                    font=("TkDefaultFont", 9))
        s.configure("Card.TLabel", background=CARD, foreground=DIM)
        s.configure("TButton", padding=(10, 6))
        s.configure("Accent.TButton", background=ACC, foreground="white",
                    font=("TkDefaultFont", 10, "bold"), padding=(12, 8))
        s.map("Accent.TButton", background=[("active", "#1a5fd0")])
        s.configure("TCombobox", padding=5)
        s.configure("TEntry", padding=5)
        s.configure("Horizontal.TProgressbar", thickness=10, background=ACC)

    def _card(self, parent, title):
        box = ttk.LabelFrame(parent, text=title, style="Card.TLabelframe",
                             padding=(12, 10))
        box.pack(fill="x", pady=(0, 10))
        return box

    def _build_radio(self, wrap):
        sec2 = self._card(wrap, "2 · Radio on your WiFi")
        row2 = ttk.Frame(sec2, style="Card.TFrame")
        row2.pack(fill="x")
        self.ip_var = tk.StringVar()
        self.ip_box = ttk.Combobox(row2, textvariable=self.ip_var)
        self.ip_box.pack(side="left", fill="x", expand=True)
        ttk.Button(row2, text="Find radio", command=self._find).pack(side="left", padx=(6, 0))
        self.scan_lbl = ttk.Label(row2, text="", style="Card.TLabel")
        self.scan_lbl.pack(side="left", padx=(6, 0))

        cred = ttk.Frame(sec2, style="Card.TFrame")
        cred.pack(fill="x", pady=(8, 0))
        self.user_var = tk.StringVar(value=DEFAULT_USER)
        self.pass_var = tk.StringVar(value=DEFAULT_PASS)
        self.port_var = tk.StringVar(value=str(DEFAULT_PORT))
        for lbl, var, w in (("User", self.user_var, 10), ("Password", self.pass_var, 14), ("Port", self.port_var, 7)):
            ttk.Label(cred, text=lbl, style="Card.TLabel").pack(side="left")
            e = ttk.Entry(cred, textvariable=var, width=w,
                          show="•" if lbl == "Password" else "")
            e.pack(side="left", padx=(4, 12))
        return sec2

    def _build_bottom(self, wrap):
        sec3 = self._card(wrap, "3 · Upload")
        ttk.Button(sec3, text="Upload to radio  →  /usr/firmware",
                   style="Accent.TButton", command=self._upload).pack(fill="x")
        self.bar = ttk.Progressbar(sec3, mode="determinate", maximum=100)
        self.bar.pack(fill="x", pady=(8, 0))
        self.status = tk.StringVar(value="Boot radio WITHOUT SD card, join WiFi, plug external power.")
        ttk.Label(sec3, textvariable=self.status, wraplength=580,
                  style="Card.TLabel").pack(anchor="w", pady=(6, 0))

        self.log = tk.Text(wrap, height=9, relief="flat", highlightthickness=1,
                           highlightbackground="#cfd6e0",
                           font=("TkFixedFont", 9))
        self.log.pack(fill="both", expand=True)
        self.log.configure(state="disabled")
        ttk.Label(wrap, text="After upload: System settings → Firmware upgrade → pick file → Update. Never flash on battery.",
                  wraplength=600, style="Dim.TLabel").pack(anchor="w", pady=(6, 0))

    def _about(self):
        self._dlg("info", "About", ABOUT_TEXT)

    def _manual(self):
        self._dlg("info", "What you will do manually", start_checklist())

    def _dlg(self, kind, title, text):
        colors = {"info": "#1f6feb", "done": "#1a7f37", "error": "#cf222e"}
        d = tk.Toplevel(self)
        d.title(title)
        d.transient(self)
        d.resizable(False, False)
        d.configure(background="white")
        strip = tk.Frame(d, background=colors.get(kind, "#1f6feb"))
        strip.pack(fill="x")
        tk.Label(strip, text=title, background=strip["background"],
                 foreground="white",
                 font=("TkDefaultFont", 11, "bold")).pack(side="left", padx=12, pady=8)
        tk.Label(d, text=text, background="white", foreground="#1c2330",
                 wraplength=440, justify="left",
                 font=("TkDefaultFont", 10)).pack(padx=16, pady=14)
        ttk.Button(d, text="OK", style="Accent.TButton",
                   command=d.destroy).pack(pady=(0, 14))
        d.update_idletasks()
        x = self.winfo_x() + (self.winfo_width() - d.winfo_reqwidth()) // 2
        y = self.winfo_y() + (self.winfo_height() - d.winfo_reqheight()) // 2
        d.geometry(f"+{max(x, 0)}+{max(y, 0)}")
        d.grab_set()
        d.wait_window()

    def _poll(self):
        try:
            while True:
                fn = self.jobs.get_nowait()
                fn()
        except queue.Empty:
            pass
        self.after(100, self._poll)

    def ui(self, fn):
        self.jobs.put(fn)

    def say(self, msg):
        def _do():
            self.log.configure(state="normal")
            self.log.insert("end", msg + "\n")
            self.log.see("end")
            self.log.configure(state="disabled")
        self.ui(_do)

    def _refresh_fw_menu(self):
        self.fw_box["values"] = display_names(self.fw_paths)
        if self.fw_paths and not self.fw_var.get():
            self.fw_var.set(display_names(self.fw_paths)[0])

    def _display_for(self, path):
        try:
            return display_names(self.fw_paths)[self.fw_paths.index(path)]
        except (ValueError, IndexError):
            base = os.path.basename(path)
            return f"{base}  {fw_label(base)}".strip()

    def _selected_fw(self):
        sel = self.fw_var.get()
        base = sel.split()[0] if sel else ""
        for p in self.fw_paths:
            if os.path.basename(p) == base:
                return p
        return sel if sel and os.path.isfile(sel) else ""

    def _browse(self):
        p = filedialog.askopenfilename(filetypes=[("X6100 firmware", "*.xgf"), ("All", "*.*")])
        if p:
            if p not in self.fw_paths:
                self.fw_paths.append(p)
                self._refresh_fw_menu()
            self.fw_var.set(self._display_for(p))

    def _download(self):
        self.status.set("Downloading latest release…")
        threading.Thread(target=self._dl_worker, daemon=True).start()

    def _dl_worker(self):
        try:
            url, name = fetch_latest_asset_url()
            self.say(f"downloading {url}")
            dest = os.path.join(os.path.expanduser("~"), "Downloads", name)
            download_file(url, dest, progress_cb=lambda pct, total: self.ui(
                lambda v=pct: (self.bar.configure(value=v),
                               self.status.set(f"Downloading… {v}%"))))
            self.fw_paths.append(dest)
            self.ui(lambda: (self._refresh_fw_menu(),
                             self.fw_var.set(self._display_for(dest)),
                             self.status.set(f"Downloaded {dest}"),
                             self.bar.configure(value=100)))
            self.say(f"saved {dest}")
        except Exception as e:  # noqa: BLE001
            self.say(f"download failed: {e}")
            self.ui(lambda: (self._dlg("error", "Download failed", str(e)),
                             self.status.set("Download failed")))

    def _find(self):
        self.stop_scan.clear()
        self.scan_lbl.configure(text="scanning…")
        self.status.set("Looking for X6100 (x6100.local + subnet SSH scan)…")
        threading.Thread(target=self._find_worker, daemon=True).start()

    def _find_worker(self):
        try:
            port = int(self.port_var.get() or DEFAULT_PORT)
        except ValueError:
            port = DEFAULT_PORT
        found = discover_radios(port, log=self.say, stop=self.stop_scan)

        def _done():
            self.scan_lbl.configure(text="")
            if found:
                self.ip_box["values"] = found
                self.ip_var.set(found[0])
                self.status.set(f"Found radio at {found[0]}" if len(found) == 1
                                else f"Found {len(found)} - pick yours (check radio WiFi screen)")
                self.say(f"candidates: {', '.join(found)}")
            else:
                self.status.set("Not found - type the IP from the radio WiFi screen.")
                self.say("scan empty: same WiFi? booted without SD? port 22?")
                self._dlg("info", "Not found", "Type the IP shown on the radio's WiFi screen.")
        self.ui(_done)

    def _upload(self):
        fw = self._selected_fw()
        ip = self.ip_var.get().strip()
        if not fw or not os.path.isfile(fw):
            self._dlg("error", "No firmware", "Pick a .xgf file first.")
            return
        if not ip:
            self._dlg("error", "No radio", "Enter the radio IP (or Find radio).")
            return
        try:
            port = int(self.port_var.get() or DEFAULT_PORT)
        except ValueError:
            self._dlg("error", "Bad port", "Port must be a number.")
            return
        user, pw = self.user_var.get().strip() or DEFAULT_USER, self.pass_var.get()
        remote = remote_path(fw)
        self.bar.configure(value=0)
        self.status.set(f"Uploading {os.path.basename(fw)} to {ip}:{remote} …")
        threading.Thread(target=self._up_worker,
                         args=(fw, ip, port, user, pw, remote), daemon=True).start()

    def _up_worker(self, fw, ip, port, user, pw, remote):
        name = os.path.basename(fw)
        try:
            out = upload(ip, port, user, pw, fw, remote,
                         progress_cb=lambda sent, total: self.ui(
                             lambda v=min(int(sent * 100 / total), 100) if total else 0: (
                                 self.bar.configure(value=v),
                                 self.status.set(f"Uploading… {v}%"))))
            self.say(str(out))
            self.say(f"OK: {remote}")
            self.ui(lambda: (self.bar.configure(value=100),
                             self.status.set("Done - finish the flash on the radio (see popup)"),
                             self._dlg("done", DONE_TITLE, done_instructions(name))))
        except Exception as e:  # noqa: BLE001
            self.say(f"upload failed: {e}")
            self.ui(lambda m=str(e): (self.status.set("Upload failed"),
                                      self._dlg("error", "Upload failed",
                                        f"{m}\n\nSame WiFi? IP correct? password '123'?")))


if __name__ == "__main__":
    app = App()
    if "--no-welcome" not in sys.argv:
        app.after(400, app._manual)  # checklist first, before any use
    app.mainloop()
