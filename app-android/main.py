"""Xiegu X6100 Base Patch Firmware Uploader - Android app (Kivy).

Same flow as the desktop app: pick .xgf, find radio on WiFi,
upload to /usr/firmware, then finish the flash manually on the radio.

Built with Buildozer / python-for-android (see buildozer.spec).
x6100_core.py is copied next to this file at build time (see build.sh).
"""
import os
import threading

from kivy.app import App
from kivy.clock import Clock
from kivy.graphics import Color, Rectangle
from kivy.metrics import dp, sp
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.button import Button
from kivy.uix.filechooser import FileChooserListView
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.progressbar import ProgressBar
from kivy.uix.scrollview import ScrollView
from kivy.uix.spinner import Spinner
from kivy.uix.textinput import TextInput

from x6100_core import (
    ABOUT_TEXT, DEFAULT_PASS, DEFAULT_PORT, DEFAULT_USER,
    DONE_TITLE, discover_radios, done_instructions, download_file,
    fetch_latest_asset_url, fw_label, fw_sort_key, list_local_fw,
    remote_path, start_checklist, upload,
)

ACCENT = (0.12, 0.44, 0.92, 1)
BG = (0.07, 0.09, 0.13, 1)
CARD = (0.13, 0.16, 0.22, 1)
TEXT = (0.93, 0.95, 0.98, 1)
DIM = (0.65, 0.71, 0.78, 1)
BTN = (0.25, 0.29, 0.36, 1)

try:
    from android.permissions import request_permissions, Permission  # type: ignore
    request_permissions([Permission.INTERNET, Permission.ACCESS_NETWORK_STATE,
                         Permission.ACCESS_WIFI_STATE])
except ImportError:
    pass


def tx(s):
    """Phone fonts miss some glyphs - swap them for safe ASCII."""
    return s.replace("→", ">").replace("•", "-").replace("–", "-")


def wrap_left(label):
    label.bind(size=lambda i, v: setattr(i, "text_size", (v[0], None)))
    return label


def section(text):
    return wrap_left(Label(text=text, size_hint_y=None, height=dp(32),
                           halign="left", valign="middle",
                           font_size=sp(16), bold=True,
                           color=(0.45, 0.75, 1, 1)))


def btn(text, primary=False, **kw):
    return Button(text=tx(text), size_hint_y=None, height=dp(52),
                  font_size=sp(16), background_normal="",
                  background_color=ACCENT if primary else BTN, **kw)


def field(**kw):
    kw.setdefault("multiline", False)
    kw.setdefault("size_hint_y", None)
    kw.setdefault("height", dp(50))
    kw.setdefault("font_size", sp(16))
    return TextInput(**kw)


def bundled_fw_dirs():
    try:
        from android.storage import app_storage_path  # type: ignore
        return (app_storage_path(),)
    except ImportError:
        return ()


class Root(BoxLayout):
    def __init__(self, **kwargs):
        super().__init__(orientation="vertical", spacing=0, **kwargs)
        self.fw_paths = list_local_fw(extra_dirs=bundled_fw_dirs())
        self.log_lines = []

        head = BoxLayout(size_hint_y=None, height=dp(58))
        with head.canvas.before:
            Color(*ACCENT)
            self._head_rect = Rectangle(pos=head.pos, size=head.size)
        head.bind(pos=self._head_move, size=self._head_move)
        head.add_widget(Label(text="Xiegu X6100 Base Patch\nFirmware Uploader",
                              halign="center", font_size=sp(16), bold=True,
                              color=(1, 1, 1, 1)))
        self.add_widget(head)

        body = BoxLayout(orientation="vertical", padding=dp(12),
                         spacing=dp(8))
        self.add_widget(body)

        body.add_widget(section("1. Firmware (.xgf)"))
        self.fw_spinner = Spinner(text="pick firmware", size_hint_y=None,
                                 height=dp(50), font_size=sp(14),
                                 background_normal="",
                                 background_color=CARD, color=TEXT)
        body.add_widget(self.fw_spinner)
        self.fw_info = wrap_left(Label(text="", size_hint_y=None,
                                       height=dp(24), halign="left",
                                       font_size=sp(13), color=DIM))
        body.add_widget(self.fw_info)
        self.fw_spinner.bind(text=lambda _i, _v: self._fw_hint())
        row = BoxLayout(size_hint_y=None, height=dp(52), spacing=dp(8))
        browse = btn("Browse...")
        browse.bind(on_release=self.browse)
        dl = btn("Download latest")
        dl.bind(on_release=self.download)
        row.add_widget(browse)
        row.add_widget(dl)
        body.add_widget(row)

        body.add_widget(section("2. Radio on your WiFi"))
        row2 = BoxLayout(size_hint_y=None, height=dp(52), spacing=dp(8))
        self.ip_in = field(hint_text="radio IP, e.g. 192.168.1.100")
        find = btn("Find radio", primary=True, size_hint_x=0.42)
        find.bind(on_release=self.find)
        row2.add_widget(self.ip_in)
        row2.add_widget(find)
        body.add_widget(row2)

        cred = BoxLayout(size_hint_y=None, height=dp(50), spacing=dp(8))
        self.user_in = field(text=DEFAULT_USER, hint_text="user")
        self.pass_in = field(text=DEFAULT_PASS, hint_text="password",
                             password=True)
        self.port_in = field(text=str(DEFAULT_PORT), hint_text="port",
                             input_filter="int", size_hint_x=0.5)
        cred.add_widget(self.user_in)
        cred.add_widget(self.pass_in)
        cred.add_widget(self.port_in)
        body.add_widget(cred)

        up = btn("3. Upload to radio > /usr/firmware", primary=True)
        up.bind(on_release=self.upload)
        body.add_widget(up)
        self.bar = ProgressBar(max=100, size_hint_y=None, height=dp(16))
        body.add_widget(self.bar)
        self.status = wrap_left(Label(
            text="Boot radio WITHOUT SD card, join WiFi, external power.",
            size_hint_y=None, height=dp(48), halign="left",
            font_size=sp(14), color=DIM))
        body.add_widget(self.status)

        body.add_widget(section("Log"))
        self.log = wrap_left(Label(text="", halign="left", valign="top",
                                   font_size=sp(13), color=TEXT))
        self.log.bind(texture_size=self._log_resize)
        scroll = ScrollView(size_hint_y=1)
        scroll.add_widget(self.log)
        body.add_widget(scroll)

        bottom = BoxLayout(size_hint_y=None, height=dp(52), spacing=dp(8),
                           padding=(0, dp(4), 0, 0))
        steps = btn("Manual steps")
        steps.bind(on_release=lambda *_: self.popup("What you will do manually",
                                                    start_checklist()))
        about = btn("About")
        about.bind(on_release=lambda *_: self.popup("About", ABOUT_TEXT))
        bottom.add_widget(steps)
        bottom.add_widget(about)
        self.add_widget(bottom)

        self.refresh_fw()
        self.say("Ready. Join the same WiFi as the radio first.")
        Clock.schedule_once(lambda _dt: self.popup(
            "About", ABOUT_TEXT,
            then=lambda: self.popup("Before you start", start_checklist())))

    def _head_move(self, head, _v):
        self._head_rect.pos = head.pos
        self._head_rect.size = head.size

    def _log_resize(self, _inst, size):
        self.log.height = max(size[1], dp(40))
        self.log.text_size = (self.log.width, None)

    def short_names(self):
        if not self.fw_paths:
            return []
        top = max(fw_sort_key(p) for p in self.fw_paths)
        return [os.path.basename(p) + (" [latest]" if fw_sort_key(p) == top else "")
                for p in self.fw_paths]

    def say(self, msg):
        self.log_lines.append(tx(msg))
        Clock.schedule_once(lambda _dt: setattr(
            self.log, "text", "\n".join(self.log_lines[-80:])))

    def set_status(self, msg):
        Clock.schedule_once(lambda _dt: setattr(self.status, "text", tx(msg)))

    def set_bar(self, v):
        Clock.schedule_once(lambda _dt: setattr(self.bar, "value", v))

    def popup(self, title, text, then=None):
        box = BoxLayout(orientation="vertical", spacing=dp(8),
                        padding=dp(12))
        lab = wrap_left(Label(text=tx(text), font_size=sp(15), color=TEXT,
                              halign="left", valign="top"))
        scroll = ScrollView()
        scroll.add_widget(lab)
        box.add_widget(scroll)
        close = btn("OK", primary=True)
        box.add_widget(close)
        pop = Popup(title=tx(title), title_size=sp(17),
                    title_color=(1, 1, 1, 1), separator_color=ACCENT,
                    content=box, size_hint=(0.94, 0.86),
                    background_color=CARD)
        if then is None:
            close.bind(on_release=pop.dismiss)
        else:
            close.bind(on_release=lambda *_: (pop.dismiss(), then()))
        pop.open()

    def refresh_fw(self):
        self.fw_spinner.values = self.short_names()
        if self.fw_paths and self.fw_spinner.text == "pick firmware":
            self.fw_spinner.text = self.short_names()[0]
        self._fw_hint()

    def _fw_hint(self):
        fw = self.selected_fw()
        base = os.path.basename(fw) if fw else ""
        self.fw_info.text = tx(fw_label(base)) if base else ""

    def selected_fw(self):
        sel = self.fw_spinner.text.replace(" [latest]", "")
        for p in self.fw_paths:
            if os.path.basename(p) == sel:
                return p
        return ""

    def browse(self, *_):
        fc = FileChooserListView(filters=["*.xgf"],
                                 path=os.path.expanduser("~"))
        box = BoxLayout(orientation="vertical")
        box.add_widget(fc)
        pop = Popup(title="Pick .xgf", content=box, size_hint=(0.95, 0.9),
                    background_color=CARD)

        def pick(*_a):
            if fc.selection:
                p = fc.selection[0]
                if p not in self.fw_paths:
                    self.fw_paths.append(p)
                    self.refresh_fw()
                self.fw_spinner.text = os.path.basename(p)
                pop.dismiss()
        ok = btn("Select", primary=True)
        ok.bind(on_release=pick)
        box.add_widget(ok)
        pop.open()

    def download(self, *_):
        self.set_status("Downloading latest release...")
        threading.Thread(target=self._dl_worker, daemon=True).start()

    def _dl_worker(self):
        try:
            url, name = fetch_latest_asset_url()
            self.say(f"downloading {url}")
            try:
                from android.storage import app_storage_path  # type: ignore
                dest = os.path.join(app_storage_path(), name)
            except ImportError:
                dest = os.path.join(os.path.expanduser("~"), "Downloads", name)
            download_file(url, dest,
                          progress_cb=lambda pct, _t: (self.set_bar(pct),
                             self.set_status(f"Downloading... {pct}%")))
            self.fw_paths.append(dest)
            Clock.schedule_once(lambda _dt: (self.refresh_fw(),
                                             self.fw_spinner.setter("text")(
                                                 self.fw_spinner,
                                                 os.path.basename(dest))))
            self.set_status(f"Downloaded {os.path.basename(dest)}")
            self.set_bar(100)
            self.say(f"saved {dest}")
        except Exception as e:  # noqa: BLE001
            self.say(f"download failed: {e}")
            self.set_status("Download failed")

    def find(self, *_):
        try:
            port = int(self.port_in.text or DEFAULT_PORT)
        except ValueError:
            port = DEFAULT_PORT
        self.set_status("Scanning WiFi for X6100...")
        threading.Thread(target=self._find_worker, args=(port,),
                         daemon=True).start()

    def _find_worker(self, port):
        found = discover_radios(port, log=self.say)
        if found:
            self.ip_in.text = found[0]
            self.say(f"candidates: {', '.join(found)}")
            self.set_status(f"Found radio at {found[0]}" if len(found) == 1
                            else "Found several - check radio WiFi screen")
        else:
            self.set_status("Not found - type the IP from the radio WiFi screen.")
            self.say("scan empty: same WiFi? booted without SD? port 22?")

    def upload(self, *_):
        fw = self.selected_fw()
        ip = self.ip_in.text.strip()
        if not fw or not os.path.isfile(fw):
            self.popup("No firmware", "Pick a .xgf file first.")
            return
        if not ip:
            self.popup("No radio", "Enter the radio IP (or tap Find radio).")
            return
        try:
            port = int(self.port_in.text or DEFAULT_PORT)
        except ValueError:
            self.popup("Bad port", "Port must be a number.")
            return
        user = self.user_in.text.strip() or DEFAULT_USER
        threading.Thread(target=self._up_worker,
                         args=(fw, ip, port, user, self.pass_in.text,
                               remote_path(fw)), daemon=True).start()

    def _up_worker(self, fw, ip, port, user, pw, remote):
        name = os.path.basename(fw)
        self.set_bar(0)
        self.set_status(f"Uploading {name} ...")
        try:
            out = upload(ip, port, user, pw, fw, remote,
                         progress_cb=lambda sent, total: self.set_bar(
                             min(int(sent * 100 / total), 100) if total else 0))
            self.say(str(out))
            self.say(f"OK: {remote}")
            self.set_bar(100)
            self.set_status("Done - finish the flash on the radio (see popup)")
            Clock.schedule_once(lambda _dt: self.popup(DONE_TITLE,
                                                       done_instructions(name)))
        except Exception as e:  # noqa: BLE001
            self.say(f"upload failed: {e}")
            self.set_status("Upload failed")
            Clock.schedule_once(lambda _dt, m=str(e): self.popup(
                "Upload failed",
                f"{m}\n\nSame WiFi? IP correct? password '123'?"))


class X6100App(App):
    title = "Xiegu X6100 Base Patch Firmware Uploader"

    def build(self):
        return Root()


if __name__ == "__main__":
    X6100App().run()
