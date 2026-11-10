# Putting RadarStats on the server

The ViewOwl and OWLOS LAN machine (Ubuntu 24.04, arm64), behind its Caddy. RadarStats gets its own user
(`radarstats`), its own folder (`/opt/radar-stats`), its own service (`radar-stats`) and its own Caddy
file (`/etc/caddy/sites/radar-stats.caddy`). Nothing else on the machine is touched. First put there
2026-11-10.

## Before, once

- **DNS:** an A record `radar.owlos.sk` to the machine. Check: `nslookup radar.owlos.sk 8.8.8.8`.
- **.NET:** the ASP.NET Core 8 runtime, `aspnetcore-runtime-8.0` from Ubuntu's own packages. OWLOS LAN
  uses it too, so it is there already.
- **The Caddyfile imports the sites folder:** `import /etc/caddy/sites/*.caddy`. ViewOwl's deploy writes
  the Caddyfile anew each time, so a block of our own lives in that folder, never in the Caddyfile.

## By hand

On Windows, in `server/`:

```
dotnet publish -c Release -o publish/radar-stats
copy deploy\radar-stats.service publish\radar-stats\
copy deploy\Caddyfile publish\radar-stats\
tar -czf radar-stats.tar.gz -C publish radar-stats
scp radar-stats.tar.gz root@<server>:/tmp/
```

On the server:

```
cd /tmp && tar -xzf radar-stats.tar.gz
id radarstats || useradd --system --no-create-home --home-dir /opt/radar-stats --shell /usr/sbin/nologin radarstats
mkdir -p /opt/radar-stats/data
cp radar-stats/RadarStats.dll radar-stats/RadarStats.deps.json radar-stats/RadarStats.runtimeconfig.json radar-stats/appsettings.json /opt/radar-stats/
chown radarstats:radarstats /opt/radar-stats/data
install -m 644 radar-stats/radar-stats.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now radar-stats
curl -s http://127.0.0.1:5088/api/stats
```

The country table, once and then each month (DB-IP puts out a new one at the start of a month):

```
curl -fsS -o /opt/radar-stats/data/dbip.csv.gz https://download.db-ip.com/free/dbip-country-lite-YYYY-MM.csv.gz
gunzip -c /opt/radar-stats/data/dbip.csv.gz > /opt/radar-stats/data/dbip-country-lite.csv && rm /opt/radar-stats/data/dbip.csv.gz
chown radarstats:radarstats /opt/radar-stats/data/dbip-country-lite.csv && systemctl restart radar-stats
```

Caddy, only when the whole file is valid:

```
install -m 644 radar-stats/Caddyfile /etc/caddy/sites/radar-stats.caddy
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile && systemctl reload caddy
```

If it is not valid, take the file out again (`rm /etc/caddy/sites/radar-stats.caddy`): the other sites go
on as they were. Caddy gets the certificate for `radar.owlos.sk` itself on the first reload.

## A new version

`dotnet publish` again and copy the files to `/tmp/radar-stats` on the server. Then, in this order:

```
systemctl stop radar-stats
cp /tmp/radar-stats/RadarStats.dll /tmp/radar-stats/RadarStats.runtimeconfig.json /tmp/radar-stats/appsettings.json /opt/radar-stats/
systemctl start radar-stats
```

Stop first: a dll written over while the service still runs is read back by it as it stops, and it dies
there (2026-11-10: a core dump, and the last minute's counts not written). The counts in `data/` stay.

## Look at it

```
systemctl status radar-stats
journalctl -u radar-stats -n 50
curl -s https://radar.owlos.sk/api/stats
curl -s http://127.0.0.1:5088/api/visits          (on the server itself: the visits, for us)
```

## Turning the counting on and off

The site's `version.json` says where boards and pages report: `server/public-url.txt` in the repository,
written in by the site's workflow. Empty, and no page asks and no board sends anything, whatever runs
here. `https://radar.owlos.sk` in it, and the next push turns it on.
