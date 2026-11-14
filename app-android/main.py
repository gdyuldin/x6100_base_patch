"""X6100 BASE firmware uploader - Android app (Kivy).

Same flow as the desktop app: pick .xgf, find radio on WiFi,
upload to /usr/firmware, then finish the flash manually on the radio.

Built with Buildozer / python-for-android (see buildozer.spec).
x6100_core.py is copied next to this file at build time (see build.sh).
"""
import os
import threading

from kivy.app import App
from kivy.clock import Clock
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
    DONE_TITLE, discover_radios, display_names, done_instructions, download_file,
    fetch_latest_asset_url, fw_label, list_local_fw, remote_path,
    start_checklist, upload,
)

try:
    from android.permissions import request_permissions, Permission  # type: ignore
    request_permissions([Permission.INTERNET, Permission.ACCESS_NETWORK_STATE,
                         Permission.ACCESS_WIFI_STATE])
except ImportError:
    pass


def bundled_fw_dirs():
    from kivy.utils import platform
    if platform == "android":
        from android.storage import app_storage_path  # type: ignore
        return (app_storage_path(),)
    return ()


class Root(BoxLayout):
    def __init__(self, **kwargs):
        super().__init__(orientation="vertical", padding=10, spacing=8, **kwargs)
        self.fw_paths = list_local_fw(extra_dirs=bundled_fw_dirs())
        self.found = []
        self.log_lines = []

        self.add_widget(Label(text="1. Firmware (.xgf)", size_hint_y=None, height=28))
        self.fw_spinner = Spinner(text="pick firmware", size_hint_y=None, height=44)
        self.add_widget(self.fw_spinner)
        row = BoxLayout(size_hint_y=None, height=44, spacing=8)
        browse = Button(text="Browse…")
        browse.bind(on_release=self.browse)
        dl = Button(text="Download latest")
        dl.bind(on_release=self.download)
        row.add_widget(browse)
        row.add_widget(dl)
        self.add_widget(row)

        self.add_widget(Label(text="2. Radio on your WiFi", size_hint_y=None, height=28))
        row2 = BoxLayout(size_hint_y=None, height=44, spacing=8)
        self.ip_in = TextInput(hint_text="radio IP, e.g. 192.168.1.100", multiline=False)
        find = Button(text="Find radio", size_hint_x=0.4)
        find.bind(on_release=self.find)
        row2.add_widget(self.ip_in)
        row2.add_widget(find)
        self.add_widget(row2)

        cred = BoxLayout(size_hint_y=None, height=44, spacing=8)
        self.user_in = TextInput(text=DEFAULT_USER, hint_text="user", multiline=False)
        self.pass_in = TextInput(text=DEFAULT_PASS, hint_text="password",
                                 multiline=False, password=True)
        self.port_in = TextInput(text=str(DEFAULT_PORT), hint_text="port",
                                 multiline=False, input_filter="int")
        cred.add_widget(self.user_in)
        cred.add_widget(self.pass_in)
        cred.add_widget(self.port_in)
        self.add_widget(cred)

        up = Button(text="3. Upload to radio → /usr/firmware", size_hint_y=None, height=52)
        up.bind(on_release=self.upload)
        self.add_widget(up)

        self.bar = ProgressBar(max=100, size_hint_y=None, height=20)
        self.add_widget(self.bar)
        self.status = Label(text="Boot radio WITHOUT SD card, join WiFi, external power.",
                            size_hint_y=None, height=56)
        self.add_widget(self.status)

        self.log = Label(text="", size_hint_y=None, halign="left", valign="top")
        self.log.bind(texture_size=self._log_resize)
        scroll = ScrollView()
        scroll.add_widget(self.log)
        self.add_widget(scroll)

        bottom = BoxLayout(size_hint_y=None, height=44, spacing=8)
        steps = Button(text="Manual steps")
        steps.bind(on_release=lambda *_: self.popup("What you will do manually",
                                                    start_checklist()))
        about = Button(text="About")
        about.bind(on_release=lambda *_: self.popup("About", ABOUT_TEXT))
        bottom.add_widget(steps)
        bottom.add_widget(about)
        self.add_widget(bottom)
        self.refresh_fw()
        self.say("Ready. Join the same WiFi as the radio first.")
        Clock.schedule_once(lambda _dt: self.popup("Before you start",
                                                   start_checklist()))

    def _log_resize(self, _inst, size):
        self.log.height = size[1]
        self.log.text_size = (self.log.width, None)

    def say(self, msg):
        self.log_lines.append(msg)
        Clock.schedule_once(lambda _dt: setattr(
            self.log, "text", "\n".join(self.log_lines[-80:])))

    def set_status(self, msg):
        Clock.schedule_once(lambda _dt: setattr(self.status, "text", msg))

    def set_bar(self, v):
        Clock.schedule_once(lambda _dt: setattr(self.bar, "value", v))

    def popup(self, title, text):
        box = BoxLayout(orientation="vertical", spacing=8)
        lab = Label(text=text)
        close = Button(text="OK", size_hint_y=None, height=48)
        box.add_widget(lab)
        box.add_widget(close)
        pop = Popup(title=title, content=box, size_hint=(0.92, 0.8))
        close.bind(on_release=pop.dismiss)
        pop.open()

    def refresh_fw(self):
        self.fw_spinner.values = display_names(self.fw_paths)
        if self.fw_paths and self.fw_spinner.text == "pick firmware":
            self.fw_spinner.text = display_names(self.fw_paths)[0]

    def _display_for(self, path):
        try:
            return display_names(self.fw_paths)[self.fw_paths.index(path)]
        except (ValueError, IndexError):
            base = os.path.basename(path)
            return f"{base}  {fw_label(base)}".strip()

    def selected_fw(self):
        base = self.fw_spinner.text.split()[0] if self.fw_spinner.text else ""
        for p in self.fw_paths:
            if os.path.basename(p) == base:
                return p
        return ""

    def browse(self, *_):
        fc = FileChooserListView(filters=["*.xgf"], path=os.path.expanduser("~"))
        box = BoxLayout(orientation="vertical")
        box.add_widget(fc)
        pop = Popup(title="Pick .xgf", content=box, size_hint=(0.95, 0.9))

        def pick(*_a):
            if fc.selection:
                p = fc.selection[0]
                if p not in self.fw_paths:
                    self.fw_paths.append(p)
                    self.refresh_fw()
                self.fw_spinner.text = f"{os.path.basename(p)}  {fw_label(p)}".strip()
                pop.dismiss()
        ok = Button(text="Select", size_hint_y=None, height=48)
        ok.bind(on_release=pick)
        box.add_widget(ok)
        pop.open()

    def download(self, *_):
        self.set_status("Downloading latest release…")
        threading.Thread(target=self._dl_worker, daemon=True).start()

    def _dl_worker(self):
        from kivy.utils import platform
        try:
            url, name = fetch_latest_asset_url()
            self.say(f"downloading {url}")
            if platform == "android":
                from android.storage import app_storage_path  # type: ignore
                dest = os.path.join(app_storage_path(), name)
            else:
                dest = os.path.join(os.path.expanduser("~"), "Downloads", name)
            download_file(url, dest,
                          progress_cb=lambda pct, _t: (self.set_bar(pct),
                                                       self.set_status(f"Downloading… {pct}%")))
            self.fw_paths.append(dest)
            Clock.schedule_once(lambda _dt: self._select_downloaded(dest))
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
        self.set_status("Scanning WiFi for X6100…")
        threading.Thread(target=self._find_worker, args=(port,), daemon=True).start()

    def _find_worker(self, port):
        found = discover_radios(port, log=self.say)
        if found:
            self.ip_in.text = found[0]
            self.say(f"candidates: {', '.join(found)}")
            self.set_status(f"Found radio at {found[0]}" if len(found) == 1
                            else f"Found {len(found)} - check radio WiFi screen")
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
        self.set_status(f"Uploading {name} …")
        try:
            out = upload(ip, port, user, pw, fw, remote,
                         progress_cb=lambda sent, total: self.set_bar(
                             min(int(sent * 100 / total), 100) if total else 0))
            self.say(str(out))
            self.say(f"OK: {remote}")
            self.set_bar(100)
            self.set_status("Done - finish the flash on the radio (see popup)")
            Clock.schedule_once(lambda _dt: self.popup(DONE_TITLE, done_instructions(name)))
        except Exception as e:  # noqa: BLE001
            self.say(f"upload failed: {e}")
            self.set_status("Upload failed")
            Clock.schedule_once(lambda _dt, m=str(e): self.popup(
                "Upload failed",
                f"{m}\n\nSame WiFi? IP correct? password '123'?"))


class X6100App(App):
    title = "X6100 Base Patch Uploader"

    def build(self):
        return Root()


if __name__ == "__main__":
    X6100App().run()
