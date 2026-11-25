// SlideBake - the slides of the OWLOS Flight Radar 0.16 release film, 1920x1080, in the site's look (the beta films'
// SlideBake, their ground, frames and type): a dark ground with a faint grid, the name in mono, the slide's line, the
// pictures in frames with their corners cut and lit. a frame with nothing in it is where a clip goes: its place is
// written to places.sh for assemble.sh. the map is home in Trencin; the film does not say "made in": the map says where.
//   SlideBake <film folder>      reads script.txt, clips\, site\ and boards\, writes slides\ and places.sh
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Text;

static class Program
{
    const int W = 1920, H = 1080;
    static readonly Color Bg0 = Color.FromArgb(3, 8, 26), Bg1 = Color.FromArgb(10, 26, 54), Line = Color.FromArgb(29, 74, 120);
    static readonly Color Prim = Color.FromArgb(92, 225, 255), Focus = Color.FromArgb(234, 247, 255), Dim = Color.FromArgb(165, 198, 226);
    static readonly Color Panel = Color.FromArgb(6, 20, 44), Amber = Color.FromArgb(255, 196, 92), Yellow = Color.FromArgb(255, 214, 64);
    const string Name = "OWLOS FLIGHT RADAR 0.16";
    const string Drawn = "the board's own code, drawn on a PC, at home in Trenčín";
    const string OnSite = "the site, with a made-up board at home in Trenčín";
    const string FromGit = "the site's own history window, drawn from the project's git log";

    static string dir = "";
    static readonly Dictionary<string, string> caption = new Dictionary<string, string>();
    static readonly StringBuilder places = new StringBuilder();

    static int Main(string[] args)
    {
        if (args.Length < 1) {
            Console.WriteLine("SlideBake <film folder>");
            return 1;
        }
        dir = args[0];
        foreach (string row in File.ReadAllLines(Path.Combine(dir, "script.txt"))) {
            string[] f = row.Split('|');
            if (f.Length >= 3) {
                caption[f[0]] = f[1];
            }
        }
        Directory.CreateDirectory(Path.Combine(dir, "slides"));
        places.AppendLine("# where SlideBake left room for each clip: x y w h, in the 1920x1080 frame");

        Save("01", Opening());
        Save("02", Screen("02"));
        (Bitmap s03, Bitmap t03) = DialParts();
        Save("03", s03);
        Save("03-top", t03);
        Save("04", Screen("04"));
        Save("04-top", PanelMark());
        Save("05", Screen("05"));
        Save("06", Warnings());
        Save("07a", Units());
        Save("07b", Auto());
        string[] steps = { "100%", "50%", "25%", "10%" };
        for (int b = 0; b < 4; b++) {
            Save("08" + (char)('a' + b), Light("08", Clip("d1-set-view-b" + b + ".png"), "SET, VIEW, BACKLIGHT", steps[b], false));
        }
        Save("08e", Light("08", Clip("d1-set-view-night.png"), "SET, VIEW, NIGHT", "25%", true));
        Save("09", Room("09", 1140, 760, Drawn, "THE 3.5\" MAP AT THE WIDE RANGE: 0.15, THEN 0.16"));
        Save("10a", Room("10", 1140, 760, Drawn, "THE COUNTRY'S ROADS, UP CLOSE: 0.15, THEN 0.16"));
        Save("10b", Names());
        Save("11", Room("11", 1600, 600, "the board's own code, drawn on a PC: planes flown on known paths", "THE RADAR AT 10 KM, THEN A TURN UP CLOSE"));
        Save("12", Faster());
        Save("13a", SiteShots("13", Site("f5-site-main.png"), Site("f5-site-side.png")));
        Save("13b", SiteShots("13", Site("f5-site-main-p.png"), Site("f5-site-side-p.png")));
        Save("14", Shell("14", "boards  ·  cat config  ·  echo theme=amber > config  ·  cat config > /boards/1.28-round/config"));
        Save("15", Shell("15", "set look=radar theme=synth --all  ·  home Bratislava --board 1.28-round  ·  man flash"));
        Save("16", Boards());
        Save("17", History("17"));
        Save("18", History("18"));
        Save("19", Stable());
        Save("20", Next());
        Save("21", End(caption["21"], "https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/"));
        File.WriteAllText(Path.Combine(dir, "places.sh"), places.ToString().Replace("\r\n", "\n"));
        return 0;
    }

    static string In(string sub, string name) => Path.Combine(dir, sub, name);
    static Bitmap Clip(string name) => Load(In("clips", name));
    static Bitmap Site(string name) => Load(In("site", name));
    static Bitmap Board(string name) => Load(In("boards", name));

    static Bitmap Load(string path)
    {
        using Bitmap src = new Bitmap(path);
        return src.Clone(new Rectangle(0, 0, src.Width, src.Height), PixelFormat.Format32bppArgb);
    }

    static Bitmap Crop(Bitmap src, Rectangle r)
    {
        return src.Clone(Rectangle.Intersect(r, new Rectangle(0, 0, src.Width, src.Height)), PixelFormat.Format32bppArgb);
    }

    static void Save(string id, Bitmap b)
    {
        string to = Path.Combine(dir, "slides", id + ".png");
        b.Save(to, ImageFormat.Png);
        b.Dispose();
        Console.WriteLine(to);
    }

    static void Place(string name, RectangleF r)
    {
        places.AppendLine(string.Format(CultureInfo.InvariantCulture, "{0}=\"{1} {2} {3} {4}\"", name, (int)r.X, (int)r.Y, (int)r.Width, (int)r.Height));
    }

    static Graphics Canvas(Bitmap b)
    {
        Graphics g = Graphics.FromImage(b);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        return g;
    }

    // the ground every slide stands on, and the name at the top
    static (Bitmap, Graphics) Ground(string? name = Name)
    {
        Bitmap b = new Bitmap(W, H, PixelFormat.Format32bppArgb);
        Graphics g = Canvas(b);
        using (LinearGradientBrush sky = new LinearGradientBrush(new Rectangle(0, 0, W, H), Bg1, Bg0, LinearGradientMode.Vertical)) {
            g.FillRectangle(sky, 0, 0, W, H);
        }
        using (Pen grid = new Pen(Color.FromArgb(34, Line), 1)) {
            for (int x = 0; x < W; x += 64) {
                g.DrawLine(grid, x, 0, x, H);
            }
            for (int y = 0; y < H; y += 64) {
                g.DrawLine(grid, 0, y, W, y);
            }
        }
        if (name != null) {
            using Font f = new Font("Cascadia Mono", 26, FontStyle.Bold, GraphicsUnit.Pixel);
            using SolidBrush prim = new SolidBrush(Prim);
            g.FillRectangle(prim, 80, 66, 6, 30);
            g.DrawString(name, f, prim, 98, 64);
        }
        return (b, g);
    }

    static void Caption(Graphics g, string text, int y = 110, int size = 58)
    {
        using Font f = new Font("Segoe UI Semibold", size, FontStyle.Regular, GraphicsUnit.Pixel);
        using SolidBrush ink = new SolidBrush(Focus);
        g.DrawString(text, f, ink, new RectangleF(80, y, W - 160, size * 1.5f));
    }

    static void Footnote(Graphics g, string text = Drawn)
    {
        using Font f = new Font("Segoe UI", 22, FontStyle.Regular, GraphicsUnit.Pixel);
        using SolidBrush dim = new SolidBrush(Color.FromArgb(170, Dim));
        using StringFormat right = new StringFormat { Alignment = StringAlignment.Far };
        g.DrawString(text, f, dim, new RectangleF(0, 68, W - 80, 30), right);
    }

    static void Text(Graphics g, string text, string font, float size, Color c, RectangleF r, bool bold = false, StringAlignment align = StringAlignment.Near)
    {
        using Font f = new Font(font, size, bold ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel);
        using SolidBrush ink = new SolidBrush(c);
        using StringFormat fmt = new StringFormat { Alignment = align };
        g.DrawString(text, f, ink, r, fmt);
    }

    static GraphicsPath Cut(RectangleF r, float c)
    {
        GraphicsPath p = new GraphicsPath();
        p.AddPolygon(new[] {
            new PointF(r.Left + c, r.Top), new PointF(r.Right, r.Top), new PointF(r.Right, r.Bottom - c),
            new PointF(r.Right - c, r.Bottom), new PointF(r.Left, r.Bottom), new PointF(r.Left, r.Top + c),
        });
        return p;
    }

    // a picture in the site's frame: its corners cut, the two cut ones lit, a soft glow round it. no picture: the
    // frame alone, dark inside, for a clip
    static void Framed(Graphics g, Image? img, RectangleF r, bool pixels, float pad = 14)
    {
        float c = pad < 10 ? 14 : 22;
        RectangleF outer = RectangleF.Inflate(r, pad, pad);
        for (int i = 6; i >= 1; i--) {
            using GraphicsPath glow = Cut(RectangleF.Inflate(outer, i * 5, i * 5), c + i * 3);
            using SolidBrush halo = new SolidBrush(Color.FromArgb(5, Prim));
            g.FillPath(halo, glow);
        }
        using (GraphicsPath back = Cut(outer, c))
        using (SolidBrush panel = new SolidBrush(Panel)) {
            g.FillPath(panel, back);
        }
        if (img != null) {
            InterpolationMode was = g.InterpolationMode;
            PixelOffsetMode po = g.PixelOffsetMode;
            g.InterpolationMode = pixels ? InterpolationMode.NearestNeighbor : InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = pixels ? PixelOffsetMode.Half : PixelOffsetMode.HighQuality;
            g.DrawImage(img, r);
            g.InterpolationMode = was;
            g.PixelOffsetMode = po;
        } else {
            using SolidBrush black = new SolidBrush(Color.Black);
            g.FillRectangle(black, r);
        }
        using (GraphicsPath edge = Cut(outer, c))
        using (Pen line = new Pen(Line, 2)) {
            g.DrawPath(line, edge);
        }
        float arm = Math.Min(46, Math.Min(outer.Width, outer.Height) / 5);
        using Pen lit = new Pen(Prim, 3);
        g.DrawLines(lit, new[] { new PointF(outer.Left, outer.Top + c + arm), new PointF(outer.Left, outer.Top + c), new PointF(outer.Left + c, outer.Top), new PointF(outer.Left + c + arm, outer.Top) });
        g.DrawLines(lit, new[] { new PointF(outer.Right, outer.Bottom - c - arm), new PointF(outer.Right, outer.Bottom - c), new PointF(outer.Right - c, outer.Bottom), new PointF(outer.Right - c - arm, outer.Bottom) });
    }

    static RectangleF Rect(float x, float y, float w, float h) => new RectangleF(x, y, w, h);

    // a small mono label in the site's colour
    static void Label(Graphics g, string text, float x, float y, Color? c = null, float size = 28)
    {
        using Font f = new Font("Cascadia Mono", size, FontStyle.Bold, GraphicsUnit.Pixel);
        using SolidBrush b = new SolidBrush(c ?? Prim);
        g.DrawString(text, f, b, x, y);
    }

    // a chip with its corners cut: the version, or what comes next
    static void Chip(Graphics g, RectangleF r, string text, Color c)
    {
        using (GraphicsPath p = Cut(r, 16))
        using (SolidBrush fill = new SolidBrush(Color.FromArgb(40, c)))
        using (Pen edge = new Pen(c, 3)) {
            g.FillPath(fill, p);
            g.DrawPath(edge, p);
        }
        Text(g, text, "Cascadia Mono", 40, c, Rect(r.X, r.Y + 12, r.Width, 50), true, StringAlignment.Center);
    }


    /* --- the slides --- */

    // the first picture: the name and what is new on the left, the 5" glass alive on the right, big. never black:
    // the networks take the first frame for the post's picture
    static Bitmap Opening()
    {
        (Bitmap b, Graphics g) = Ground(null);
        RectangleF screen = Rect(470, 150, 1400, 840);
        Framed(g, null, screen, false);
        Place("P01", screen);
        using (Font big = new Font("Cascadia Mono", 66, FontStyle.Bold, GraphicsUnit.Pixel))
        using (SolidBrush prim = new SolidBrush(Prim)) {
            g.DrawString("OWLOS", big, prim, 66, 150);
            g.DrawString("FLIGHT", big, prim, 66, 228);
            g.DrawString("RADAR", big, prim, 66, 306);
        }
        Chip(g, Rect(72, 410, 330, 70), "0.16", Prim);
        string[] what = { "The weather on", "the radar's dial", "", "A map you can read", "", "Planes that glide", "", "A console for", "your boards" };
        float y = 528;
        foreach (string s in what) {
            if (s.Length == 0) {
                y += 16;
                continue;
            }
            Text(g, s, "Segoe UI Semibold", 36, Focus, Rect(70, y, 390, 50));
            y += 44;
        }
        Footnote(g);
        g.Dispose();
        return b;
    }

    // a 5" clip as big as it goes under the caption
    static Bitmap Screen(string id)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption[id]);
        RectangleF r = Rect(260, 215, 1400, 840);
        Framed(g, null, r, false);
        Place("P" + id, r);
        Footnote(g);
        g.Dispose();
        return b;
    }

    /* the dial up close (a square of the 5" glass round the dial, 480x480, its middle at 240,240 and its rim 222
     * out: film3.c printed them) and what each part is, the lines over the clip */
    static (Bitmap, Bitmap) DialParts()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["03"]);
        RectangleF r = Rect(110, 215, 840, 840);
        Framed(g, null, r, false);
        Place("P03", r);
        Footnote(g);
        g.Dispose();
        Bitmap top = new Bitmap(W, H, PixelFormat.Format32bppArgb);
        Graphics t = Canvas(top);
        float k = r.Width / 480f, cx = r.X + 240 * k, cy = r.Y + 240 * k, rim = 222 * k;
        (float a, float rr, float ly, string head, string sub)[] parts = {
            (40, 0.93f, 250, "THE SKY, HOUR BY HOUR", "its clouds and rain, the night with its stars"),
            (102, 0.60f, 520, "THE TEMPERATURE", "frost blue to a hot red, nearer the rim warmer"),
            (152, 0.50f, 790, "THE WIND", "an arrow on the loop every three hours"),
        };
        foreach ((float a, float rr, float ly, string head, string sub) in parts) {
            double rad = a * Math.PI / 180;
            float px = cx + (float)Math.Sin(rad) * rim * rr, py = cy - (float)Math.Cos(rad) * rim * rr;
            float lx = 1110;
            using (Pen lead = new Pen(Amber, 3)) {
                t.DrawLines(lead, new[] { new PointF(px, py), new PointF(lx - 40, ly + 22), new PointF(lx - 10, ly + 22) });
            }
            using (SolidBrush dot = new SolidBrush(Amber))
            using (Pen ring = new Pen(Color.Black, 2)) {
                t.FillEllipse(dot, px - 9, py - 9, 18, 18);
                t.DrawEllipse(ring, px - 9, py - 9, 18, 18);
            }
            Label(t, head, lx, ly, Amber, 34);
            Text(t, sub, "Segoe UI", 32, Focus, Rect((int)lx, (int)ly + 50, 760, 90));
        }
        Text(t, "noon at the top, midnight at the bottom, a hand at now", "Segoe UI Semibold", 30, Dim, Rect(1110, 950, 760, 100));
        t.Dispose();
        return (b, top);
    }

    // the panel picked out over the clip (its place on the 5" glass: x 463 to 797, the whole height)
    static Bitmap PanelMark()
    {
        Bitmap top = new Bitmap(W, H, PixelFormat.Format32bppArgb);
        Graphics t = Canvas(top);
        float k = 1400f / 800f;
        RectangleF p = new RectangleF(260 + 463 * k, 215 + 4, (797 - 463) * k, 840 - 8);
        using (GraphicsPath edge = Cut(RectangleF.Inflate(p, 6, 6), 18))
        using (Pen amber = new Pen(Amber, 4)) {
            t.DrawPath(amber, edge);
        }
        using (SolidBrush dimmer = new SolidBrush(Color.FromArgb(70, 0, 0, 0))) {
            t.FillRectangle(dimmer, 260, 215, 463 * k - 8, 840);
        }
        t.Dispose();
        return top;
    }

    // the warning on the 2.8": the clip on the left, SET's switches and the LED's row on the right
    static Bitmap Warnings()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["06"]);
        RectangleF clip = Rect(100, 260, 960, 720);
        Framed(g, null, clip, true);
        Place("P06", clip);
        using (Bitmap data = Clip("d1-set-data.png"))
        using (Bitmap led = Clip("d1-set-led.png")) {
            Framed(g, data, Rect(1160, 236, 448, 336), true, 8);
            Label(g, "SET, DATA: each one on or off", 1160, 584, Prim, 24);
            Framed(g, led, Rect(1160, 660, 448, 336), true, 8);
            Label(g, "SET, LED: WEATHER, yellow", 1160, 1006, Prim, 24);
        }
        float lx = 1730, ly = 828;
        for (int i = 9; i >= 1; i--) {
            using SolidBrush glow = new SolidBrush(Color.FromArgb(14, Yellow));
            g.FillEllipse(glow, lx - 30 - i * 9, ly - 30 - i * 9, 60 + i * 18, 60 + i * 18);
        }
        using (SolidBrush pack = new SolidBrush(Color.FromArgb(196, 200, 206))) {
            g.FillRectangle(pack, lx - 34, ly - 34, 68, 68);
        }
        using (SolidBrush lit = new SolidBrush(Yellow))
        using (SolidBrush core = new SolidBrush(Color.FromArgb(255, 250, 220))) {
            g.FillEllipse(lit, lx - 24, ly - 24, 48, 48);
            g.FillEllipse(core, lx - 10, ly - 10, 20, 20);
        }
        Text(g, "the LED", "Segoe UI", 24, Dim, Rect((int)lx - 70, (int)ly + 60, 140, 34), false, StringAlignment.Center);
        Footnote(g);
        g.Dispose();
        return b;
    }

    static Bitmap Units()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["07"]);
        using (Bitmap units = Clip("d1-set-units.png")) {
            Framed(g, units, Rect(100, 260, 960, 720), true);
        }
        (string head, string list)[] rows = {
            ("TEMPERATURE", "°C  °F"),
            ("PRESSURE", "hPa  inHg  mmHg"),
            ("WIND", "km/h  m/s  mph  knots"),
            ("RAIN", "mm  inches"),
        };
        float y = 290;
        foreach ((string head, string list) in rows) {
            Label(g, head, 1150, y, Prim, 32);
            Text(g, list, "Segoe UI Semibold", 46, Focus, Rect(1150, y + 42, 720, 70));
            y += 150;
        }
        Text(g, "AUTO first: what home's country uses", "Segoe UI", 32, Amber, Rect(1150, y + 6, 720, 50));
        Footnote(g);
        g.Dispose();
        return b;
    }

    static Bitmap Auto()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["07"]);
        using (Bitmap tn = Clip("d10-auto-trencin.png"))
        using (Bitmap chi = Clip("d10-auto-chicago.png")) {
            Framed(g, tn, Rect(60, 270, 880, 528), false);
            Framed(g, chi, Rect(980, 270, 880, 528), false);
        }
        Label(g, "TRENČÍN", 60, 832, Prim, 34);
        Text(g, "AUTO: °C, hPa, km/h, mm", "Segoe UI Semibold", 38, Focus, Rect(60, 878, 880, 60));
        Label(g, "CHICAGO", 980, 832, Prim, 34);
        Text(g, "AUTO: °F, inHg, mph, inches", "Segoe UI Semibold", 38, Focus, Rect(980, 878, 880, 60));
        Text(g, "mmHg: read at home's height, as a barometer there shows it", "Segoe UI", 32, Amber, Rect(60, 960, 1800, 50));
        Footnote(g);
        g.Dispose();
        return b;
    }

    // the light: the radar dimmed in the clip on the left, SET's row on the right with the step it is at
    static Bitmap Light(string id, Bitmap set, string where, string step, bool night)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption[id]);
        RectangleF clip = Rect(100, 260, 960, 720);
        Framed(g, null, clip, true);
        Place("P" + id, clip);
        RectangleF s = Rect(1180, 300, 640, 480);
        Framed(g, set, s, true, 8);
        Label(g, where, 1180, 806);
        if (night) {
            float hy = 940, x0 = 1180, x1 = 1820;
            using (Pen hz = new Pen(Color.FromArgb(160, Dim), 2)) {
                g.DrawLine(hz, x0, hy, x1, hy);
            }
            g.SetClip(new RectangleF(x0, hy - 80, 200, 80));
            using (SolidBrush sun = new SolidBrush(Amber)) {
                g.FillEllipse(sun, x0 + 60, hy - 36, 72, 72);
            }
            using (Pen ray = new Pen(Amber, 3)) {
                for (int k = -3; k <= 3; k++) {
                    double a = Math.PI / 2 + k * 0.42;
                    float cx = x0 + 96, cy = hy;
                    g.DrawLine(ray, cx + (float)Math.Cos(a) * 50, cy - (float)Math.Sin(a) * 50, cx + (float)Math.Cos(a) * 66, cy - (float)Math.Sin(a) * 66);
                }
            }
            g.ResetClip();
            using (SolidBrush moon = new SolidBrush(Focus))
            using (SolidBrush shade = new SolidBrush(Bg0)) {
                g.FillEllipse(moon, x1 - 150, hy - 92, 54, 54);
                g.FillEllipse(shade, x1 - 134, hy - 100, 54, 54);
            }
            using (SolidBrush star = new SolidBrush(Focus)) {
                g.FillEllipse(star, x1 - 230, hy - 70, 6, 6);
                g.FillEllipse(star, x1 - 60, hy - 60, 5, 5);
                g.FillEllipse(star, x1 - 190, hy - 30, 4, 4);
            }
            Text(g, "dusk", "Segoe UI", 28, Dim, Rect(x0 + 60, hy + 12, 120, 40));
            Text(g, "dawn", "Segoe UI", 28, Dim, Rect(x1 - 160, hy + 12, 120, 40));
            Text(g, "the sun at home, worked out by the board", "Segoe UI", 26, Dim, Rect(x0, hy + 52, 660, 40));
        } else {
            Text(g, step, "Cascadia Mono", 96, Focus, Rect(1180, 860, 640, 120), true);
        }
        Footnote(g);
        g.Dispose();
        set.Dispose();
        return b;
    }

    // a clip of w x h under the caption, in the middle, a small mono line over it saying what it is
    static Bitmap Room(string id, int w, int h, string foot, string what)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption[id]);
        float top = 260 + (760 - h) / 2f;
        RectangleF r = Rect((W - w) / 2f, top, w, h);
        Framed(g, null, r, true);
        Place("P" + id, r);
        Label(g, what, r.Left, r.Top - 56, Dim, 24);
        Footnote(g, foot);
        g.Dispose();
        return b;
    }

    // the names: the tile, 0.15 and 0.16 side by side, and how the board finds them
    static Bitmap Names()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["10"]);
        using (Bitmap tri = Clip("c05-names.png")) {
            RectangleF r = Rect((W - tri.Width) / 2f, 330, tri.Width, tri.Height);
            Framed(g, tri, r, true, 10);
            Text(g, "a grey under 100 is a letter, a lighter grey beside it its edge: OpenStreetMap keeps its colours",
                 "Segoe UI", 32, Dim, Rect(160, r.Bottom + 70, W - 320, 50), false, StringAlignment.Center);
            Text(g, "the letters light, the light halo dark: a name reads on the dark map", "Segoe UI Semibold", 38, Focus,
                 Rect(160, r.Bottom + 150, W - 320, 60), false, StringAlignment.Center);
        }
        Footnote(g, Drawn);
        g.Dispose();
        return b;
    }

    static Bitmap Faster()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["12"]);
        RectangleF clip = Rect(100, 260, 960, 720);
        Framed(g, null, clip, true);
        Place("P12", clip);
        Label(g, "ON THE 2.8\" CYD, FRAMES A SECOND", 1150, 290, Prim, 28);
        (string look, string from, string to)[] rows = { ("RADAR", "9", "12"), ("MAP", "16", "19"), ("CLASSIC", "13", "15") };
        float y = 360;
        foreach ((string look, string from, string to) in rows) {
            Label(g, look, 1150, y + 22, Dim, 36);
            Text(g, from, "Cascadia Mono", 72, Dim, Rect(1450, y, 140, 90), true, StringAlignment.Far);
            using (Pen arrow = new Pen(Amber, 5)) {
                arrow.EndCap = LineCap.ArrowAnchor;
                g.DrawLine(arrow, 1610, y + 46, 1680, y + 46);
            }
            Text(g, to, "Cascadia Mono", 72, Focus, Rect(1690, y, 160, 90), true);
            y += 130;
        }
        Text(g, "the second core draws half of every look and every page, on the boards that have one", "Segoe UI", 32, Dim, Rect(1150, y + 20, 720, 120));
        Footnote(g);
        g.Dispose();
        return b;
    }

    /* the site: the board's live screen on the left; its VIEW tab, the new rows of it (the look and its page, then
     * the light, the night, the warnings and the units: what lies between them is left out) on the right. the shots
     * are twice the page's size, 920 and 680 wide */
    static Bitmap SiteShots(string id, Bitmap main, Bitmap side)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption[id]);
        float k = 0.72f;
        Framed(g, main, Rect(80, 250, (int)(main.Width * k), (int)(main.Height * k)), false, 10);
        using (Bitmap a = Crop(side, new Rectangle(0, 0, side.Width, 345)))
        using (Bitmap c = Crop(side, new Rectangle(0, 1110, side.Width, side.Height - 1110))) {
            float ks = 0.72f;
            RectangleF ra = Rect(820, 236, (int)(a.Width * ks), (int)(a.Height * ks));
            RectangleF rc = Rect(1360, 236, (int)(c.Width * ks), (int)(c.Height * ks));
            Framed(g, a, ra, false, 10);
            Framed(g, c, rc, false, 10);
            Text(g, "SET on the site: the board's own settings, over the USB cable", "Segoe UI", 28, Dim, Rect(820, (int)ra.Bottom + 34, 500, 120));
        }
        Footnote(g, OnSite);
        main.Dispose();
        side.Dispose();
        g.Dispose();
        return b;
    }

    // the console: the site's page as it is (800 x 552, half again as big), the commands written large over it
    static Bitmap Shell(string id, string commands)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption[id]);
        RectangleF r = Rect((W - 1100) / 2f, 260, 1100, 759);
        Framed(g, null, r, false, 8);
        Place("P" + id, r);
        Text(g, "$ " + commands, "Cascadia Mono", 26, Amber, Rect(80, r.Top - 54, W - 160, 44), true, StringAlignment.Center);
        Footnote(g, OnSite);
        g.Dispose();
        return b;
    }

    /* the boards side by side, each as big as its glass is (a millimetre 4.2 px): the round 1.28", the 2.8" and the
     * 3.5" CYD, the 4.3" and the 5". the shots are the README's, twice the glass's pixels */
    static Bitmap Boards()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["16"]);
        (string file, string label, float wmm, float hmm, bool round)[] row = {
            ("round.png", "1.28\"", 32.4f, 32.4f, true),
            ("cyd28.png", "2.8\"", 57.6f, 43.2f, false),
            ("cyd35.png", "3.5\"", 73.4f, 48.9f, false),
            ("s043.png", "4.3\"", 95.0f, 53.9f, false),
            ("crow5.png", "5\"", 108.0f, 64.8f, false),
        };
        const float mm = 4.2f, gap = 44;
        float total = 0;
        foreach (var r in row) {
            total += r.wmm * mm;
        }
        total += gap * (row.Length - 1);
        float x = (W - total) / 2f, baseY = 640;
        foreach ((string file, string label, float wmm, float hmm, bool round) in row) {
            float w = wmm * mm, h = hmm * mm;
            RectangleF r = Rect(x, baseY - h, w, h);
            using (Bitmap shot = Board(file)) {
                if (round) {
                    using GraphicsPath disc = new GraphicsPath();
                    disc.AddEllipse(RectangleF.Inflate(r, 8, 8));
                    using (SolidBrush rim = new SolidBrush(Panel)) {
                        g.FillPath(rim, disc);
                    }
                    using GraphicsPath glass = new GraphicsPath();
                    glass.AddEllipse(r);
                    g.SetClip(glass);
                    g.DrawImage(shot, r);
                    g.ResetClip();
                    using Pen lit = new Pen(Prim, 3);
                    g.DrawEllipse(lit, RectangleF.Inflate(r, 8, 8));
                } else {
                    Framed(g, shot, r, false, 8);
                }
            }
            Text(g, label, "Cascadia Mono", 30, Prim, Rect(x - 20, baseY + 30, w + 40, 40), true, StringAlignment.Center);
            x += w + gap;
        }
        Text(g, "and the 7\" and 10.1\" panels, the M5Stack Tab5, the CrowPanels Advance, the RPi LCD",
             "Segoe UI", 32, Dim, Rect(120, 760, W - 240, 50), false, StringAlignment.Center);
        Text(g, "ESP32  ·  ESP32-S3  ·  ESP32-C3  ·  ESP32-C5  ·  ESP32-P4", "Cascadia Mono", 32, Focus, Rect(120, 830, W - 240, 50), true, StringAlignment.Center);
        Chip(g, Rect(560, 920, 800, 70), "NEW: ZLX-ESP32-2, EXPERIMENTAL", Amber);
        Footnote(g, Drawn);
        g.Dispose();
        return b;
    }

    // the history window, maximized on the site, as big as the frame lets it be (its shots are 1241 x 844)
    static Bitmap History(string id)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption[id]);
        float h = 790, w = h * 1241f / 844f;
        RectangleF r = Rect((W - w) / 2f, 250, w, h);
        Framed(g, null, r, false, 8);
        Place("P" + id, r);
        Footnote(g, FromGit);
        g.Dispose();
        return b;
    }

    // the stable version: a board on 0.15 that heard of 0.16, and what that means
    static Bitmap Stable()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["19"]);
        using (Bitmap nv = Clip("d1-newversion.png")) {
            Framed(g, nv, Rect(100, 280, 960, 720), true);
        }
        Label(g, "A BOARD ON 0.15: NEW 0.16", 100, 1012, Amber, 28);
        Chip(g, Rect(1180, 300, 330, 70), "0.16", Prim);
        Text(g, "the stable version", "Segoe UI Semibold", 46, Focus, Rect(1180, 400, 680, 60));
        string[] lines = { "every board flashed from the site gets it", "a board on 0.15 shows NEW VERSION by itself", "its settings stay when it is flashed again" };
        float y = 500;
        foreach (string s in lines) {
            Text(g, s, "Segoe UI", 34, Dim, Rect(1180, y, 680, 90));
            y += 96;
        }
        Footnote(g, Drawn);
        g.Dispose();
        return b;
    }

    // what comes next: 0.17's weather on the 2.8" and the 3.5", built from its own branch
    static Bitmap Next()
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, caption["20"]);
        RectangleF a = Rect(140, 330, 640, 480);
        RectangleF c = Rect(1000, 330, 720, 480);
        Framed(g, null, a, true);
        Framed(g, null, c, true);
        Place("P20a", a);
        Place("P20b", c);
        Label(g, "2.8\"", a.Left, a.Top - 56, Dim, 28);
        Label(g, "3.5\"", c.Left, c.Top - 56, Dim, 28);
        Chip(g, Rect(1390, 238, 330, 64), "COMING", Amber);
        Text(g, "the dial in colour beside the time, the pressure on a gauge, the wind on a compass", "Segoe UI", 34, Focus,
             Rect(140, 880, W - 280, 60), false, StringAlignment.Center);
        Footnote(g, "the 0.17 branch's own code, drawn on a PC, at home in Trenčín");
        g.Dispose();
        return b;
    }

    static Bitmap End(string line, string site)
    {
        (Bitmap b, Graphics g) = Ground();
        Caption(g, line);
        bool[,] m = Qr.Encode(Encoding.UTF8.GetBytes(site), out int ver, out int mask);
        int n = m.GetLength(0), quiet = 4, size = 560, mod = size / (n + 2 * quiet);
        int qx = 150, qy = 300, side = mod * (n + 2 * quiet);
        using (SolidBrush plate = new SolidBrush(Color.FromArgb(240, 248, 255)))
        using (SolidBrush dark = new SolidBrush(Bg0)) {
            g.FillRectangle(plate, qx, qy, side, side);
            for (int y = 0; y < n; y++) {
                for (int x = 0; x < n; x++) {
                    if (m[y, x]) {
                        g.FillRectangle(dark, qx + (x + quiet) * mod, qy + (y + quiet) * mod, mod, mod);
                    }
                }
            }
        }
        using (Pen lit = new Pen(Prim, 3)) {
            g.DrawRectangle(lit, qx - 10, qy - 10, side + 20, side + 20);
        }
        (string Label, string Text)[] links = {
            ("THE SITE: FLASH IT", "kirindenis.github.io/OWLOS-FLIGHT-RADAR"),
            ("THE CODE, FREE AND OPEN", "github.com/KirinDenis/OWLOS-FLIGHT-RADAR"),
            ("OUR GROUP", "facebook.com/groups/OWLOS"),
        };
        using Font label = new Font("Cascadia Mono", 28, FontStyle.Bold, GraphicsUnit.Pixel);
        using Font text = new Font("Segoe UI Semibold", 42, FontStyle.Regular, GraphicsUnit.Pixel);
        using SolidBrush prim = new SolidBrush(Prim);
        using SolidBrush ink = new SolidBrush(Focus);
        float lx = qx + side + 90, ly = qy + 10;
        foreach ((string l, string t) in links) {
            g.DrawString(l, label, prim, lx, ly);
            g.DrawString(t, text, ink, lx - 4, ly + 40);
            ly += 170;
        }
        Text(g, "Weather: Open-Meteo, CC BY 4.0. Maps: © OpenStreetMap contributors.", "Segoe UI", 28, Dim, Rect((int)lx, (int)ly + 10, 1100, 50));
        Console.WriteLine($"QR: version {ver}, {n}x{n}, {mod} px a module, mask {mask}");
        g.Dispose();
        return b;
    }
}
