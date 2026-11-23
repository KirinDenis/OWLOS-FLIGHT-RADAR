// ESP32 Web Flasher: the example firmware.
//
// The smallest program that shows the whole road from a push on GitHub to a board on your WiFi.
// It says hello on the serial port, takes its WiFi as lines typed over the USB cable, keeps them
// in flash, joins the network and answers a browser at its address. Nothing in it is for one
// board: any ESP32, ESP32-S2, S3, C3 or C6 with a USB port.
//
// What it understands, typed in any serial terminal at 115200 (the page's own, Arduino's Serial
// Monitor, PuTTY):
//   help            this list
//   ssid NAME       the network's name, spaces and all
//   pass PASSWORD   its password, never said back
//   join            join that network now, and every time it starts
//   forget          forget the network
//   status          which chip, which build, where it is on the network

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "version.h"

Preferences settings;        // the flash's NVS partition: kept through a reset, a power cut and an update
WebServer web(80);           // the page the board serves once it is on the network
String typed;                // the line coming in over the cable, till its Enter
String ssid, pass;           // the network to join
wl_status_t was = WL_IDLE_STATUS;   // the WiFi state last time round, to say when it changes
String told;                 // the last thing said about the WiFi, not to say it again

// Where the USB cable ends up inside depends on the board. A board with a USB-to-serial chip
// (CH340, CP2102) wires it to UART0, that's Serial0. A board on the chip's own USB (many S2, S3,
// C3 and C6 boards) has it as Serial, when built with "USB CDC on boot" as here. Which one is
// plugged in the firmware can't know, so it talks on both and listens to both.
void say(const String &text)
{
    Serial.println(text);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.println(text);
#endif
}

// a character from whichever port has one, -1 when neither has
int next_char()
{
    if (Serial.available()) {
        return Serial.read();
    }
#if ARDUINO_USB_CDC_ON_BOOT
    if (Serial0.available()) {
        return Serial0.read();
    }
#endif
    return -1;
}

void status()
{
    say(String("ESP32 Web Flasher example, ") + FW_VERSION);
    int rev = ESP.getChipRevision();            // 301 is v3.1: the major times 100 plus the minor
    say(String(ESP.getChipModel()) + " v" + rev / 100 + "." + rev % 100 + ", " + ESP.getChipCores() +
        " core(s), " + ESP.getFlashChipSize() / (1024 * 1024) + " MB flash");
    if (WiFi.status() == WL_CONNECTED) {
        say("on " + ssid + ": open http://" + WiFi.localIP().toString() + "/");
    } else if (ssid.isEmpty()) {
        say("no network yet: type ssid NAME");
    } else {
        say("not on " + ssid + " yet");
    }
}

void join()
{
    if (ssid.isEmpty()) {
        say("no network yet: type ssid NAME first");
        return;
    }
    say("joining " + ssid + " ...");
    told = "";
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void command(String line)
{
    line.trim();
    if (line.isEmpty()) {
        return;
    }
    int space = line.indexOf(' ');
    String word = space < 0 ? line : line.substring(0, space);
    String rest = space < 0 ? "" : line.substring(space + 1);
    word.toLowerCase();
    // say what came, so a terminal that shows nothing of its own typing shows it too. the password
    // as stars, always eight: not even its length goes back
    say("> " + (word == "pass" ? String("pass ********") : line));

    if (word == "help") {
        say("ssid NAME       the network's name");
        say("pass PASSWORD   its password");
        say("join            join it, now and at every start");
        say("forget          forget the network");
        say("status          chip, build, network");
    } else if (word == "ssid") {
        ssid = rest;
        settings.putString("ssid", ssid);
        say("network: " + ssid + ". now pass PASSWORD, then join");
    } else if (word == "pass") {
        pass = rest;
        settings.putString("pass", pass);
        say("password kept. now join");
    } else if (word == "join") {
        join();
    } else if (word == "forget") {
        settings.clear();
        ssid = "";
        pass = "";
        WiFi.disconnect();
        say("forgotten");
    } else if (word == "status") {
        status();
    } else {
        say("what is " + word + "? type help");
    }
}

// the board's own page, for a browser on the same network
void page()
{
    String html = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                  "<title>ESP32</title><body style='font:18px system-ui;margin:2em'>"
                  "<h1>Hello from the " + String(ESP.getChipModel()) + "</h1>"
                  "<p>" FW_VERSION ", running for " + String(millis() / 1000) + " s.</p>";
    web.send(200, "text/html", html);
}

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
    settings.begin("wifi", false);
    ssid = settings.getString("ssid", "");
    pass = settings.getString("pass", "");
    WiFi.mode(WIFI_STA);
    web.on("/", page);
    web.begin();
    status();
    say("type help for what it understands");
    if (!ssid.isEmpty()) {
        join();
    }
}

void loop()
{
    int c;
    while ((c = next_char()) >= 0) {
        if (c == '\n' || c == '\r') {
            command(typed);
            typed = "";
        } else if (typed.length() < 200) {
            typed += (char)c;
        }
    }

    // the WiFi state: said when it changes, and only when there is something new to say. a
    // network out of reach keeps trying, round the same few states
    wl_status_t now = WiFi.status();
    if (now != was) {
        was = now;
        String news;
        if (now == WL_CONNECTED) {
            news = "joined " + ssid + ": open http://" + WiFi.localIP().toString() + "/ on the same network";
        } else if (now == WL_NO_SSID_AVAIL) {
            news = "can't see " + ssid + " (an ESP32 hears 2.4 GHz networks only)";
        } else if (now == WL_CONNECT_FAILED) {
            news = "can't join " + ssid + ": wrong password?";
        } else if (now == WL_CONNECTION_LOST) {
            news = "lost " + ssid;
        }
        if (!news.isEmpty() && news != told) {
            told = news;
            say(news);
        }
    }
    web.handleClient();
}
