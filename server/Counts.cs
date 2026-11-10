using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Microsoft.Extensions.Options;

namespace RadarStats;

/// <summary>
/// The counts, all of them, behind one lock. Flashes and visits add up and are kept (<see cref="CountsSaver"/>);
/// boards online and boards on a cable are reports of the last few minutes, kept in memory only: a board says
/// it is alive every AliveMinutes, so the reports in that long are the boards online now, without anything
/// that tells one board from another. A country in IgnoredCountries is not counted at all.
/// </summary>
public sealed class Counts
{
    private readonly object _gate = new object();
    private readonly TimeProvider _clock;
    private readonly HashSet<string> _ignored;
    private readonly TimeSpan _aliveWindow;
    private readonly TimeSpan _cableWindow;
    private readonly Dictionary<(string Build, string Version, string Cc), int> _flashes = new Dictionary<(string Build, string Version, string Cc), int>();
    private readonly Dictionary<(string Day, string Cc), int> _visits = new Dictionary<(string Day, string Cc), int>();
    private readonly Queue<(DateTimeOffset At, string Build, string Cc)> _alive = new Queue<(DateTimeOffset At, string Build, string Cc)>();
    private readonly Queue<(DateTimeOffset At, int N)> _cable = new Queue<(DateTimeOffset At, int N)>();
    private readonly int _maxVersions;
    private readonly int _maxRecent;
    private bool _changed;

    public Counts(IOptions<StatsOptions> options, TimeProvider clock)
    {
        StatsOptions o = options.Value;
        _clock = clock;
        _ignored = new HashSet<string>(o.IgnoredCountries.Select((c) => c.ToUpperInvariant()));
        _aliveWindow = TimeSpan.FromMinutes(o.AliveMinutes);
        _cableWindow = TimeSpan.FromMinutes(o.CableMinutes);
        _maxVersions = o.MaxVersions;
        _maxRecent = o.MaxRecent;
    }

    /// <summary>A flash counted. False: a version this build never had, past MaxVersions of them, so not counted.</summary>
    public bool Flash(string build, string version, string cc)
    {
        if (_ignored.Contains(cc))
        {
            return true;                               // not counted, and not the reporter's fault
        }
        lock (_gate)
        {
            (string, string, string) key = (build, version, cc);
            if (!_flashes.ContainsKey(key) && !_flashes.Keys.Any((k) => k.Build == build && k.Version == version) &&
                _flashes.Keys.Where((k) => k.Build == build).Select((k) => k.Version).Distinct().Count() >= _maxVersions)
            {
                return false;
            }
            _flashes[key] = _flashes.GetValueOrDefault(key) + 1;
            _changed = true;
            return true;
        }
    }

    public void Alive(string build, string cc)
    {
        if (_ignored.Contains(cc))
        {
            return;
        }
        lock (_gate)
        {
            _alive.Enqueue((_clock.GetUtcNow(), build, cc));
            while (_alive.Count > _maxRecent)
            {
                _alive.Dequeue();
            }
        }
    }

    public void Cable(int n, string cc)
    {
        if (_ignored.Contains(cc))
        {
            return;
        }
        lock (_gate)
        {
            _cable.Enqueue((_clock.GetUtcNow(), n));
            while (_cable.Count > _maxRecent)
            {
                _cable.Dequeue();
            }
        }
    }

    public void Visit(string cc)
    {
        if (_ignored.Contains(cc))
        {
            return;
        }
        lock (_gate)
        {
            (string, string) key = (_clock.GetUtcNow().ToString("yyyy-MM-dd", CultureInfo.InvariantCulture), cc);
            _visits[key] = _visits.GetValueOrDefault(key) + 1;
            _changed = true;
        }
    }

    public StatsAnswer Answer()
    {
        lock (_gate)
        {
            DateTimeOffset now = _clock.GetUtcNow();
            Forget(now);
            Dictionary<string, int> onlineByBuild = new Dictionary<string, int>();
            Dictionary<string, int> onlineByCc = new Dictionary<string, int>();
            foreach ((DateTimeOffset _, string build, string cc) in _alive)
            {
                onlineByBuild[build] = onlineByBuild.GetValueOrDefault(build) + 1;
                onlineByCc[cc] = onlineByCc.GetValueOrDefault(cc) + 1;
            }
            List<BoardAnswer> boards = _flashes.Keys.Select((k) => k.Build).Concat(onlineByBuild.Keys).Distinct()
                .Select((b) => new BoardAnswer(
                    b,
                    _flashes.Where((f) => f.Key.Build == b).Sum((f) => f.Value),
                    onlineByBuild.GetValueOrDefault(b),
                    _flashes.Where((f) => f.Key.Build == b)
                        .GroupBy((f) => f.Key.Version)
                        .OrderByDescending((g) => g.Key, VersionOrder.Instance)
                        .Select((g) => new object[] { g.Key, g.Sum((f) => f.Value) })
                        .ToList()))
                .OrderByDescending((b) => b.Flashed)
                .ToList();
            List<CountryAnswer> countries = _flashes.Keys.Select((k) => k.Cc).Concat(onlineByCc.Keys).Distinct()
                .Where((c) => c != Countries.Unknown)
                .Select((c) => new CountryAnswer(c, _flashes.Where((f) => f.Key.Cc == c).Sum((f) => f.Value), onlineByCc.GetValueOrDefault(c)))
                .OrderByDescending((c) => c.Flashed)
                .ToList();
            return new StatsAnswer(
                now.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture),
                _flashes.Values.Sum(),
                _alive.Count,
                _cable.Sum((c) => c.N),
                boards,
                countries);
        }
    }

    public IReadOnlyList<VisitAnswer> Visits()
    {
        lock (_gate)
        {
            return _visits.OrderBy((v) => v.Key.Day).ThenByDescending((v) => v.Value)
                .Select((v) => new VisitAnswer(v.Key.Day, v.Key.Cc, v.Value)).ToList();
        }
    }

    /// <summary>What is kept between runs, when anything changed since the last time: null when nothing did.</summary>
    public CountsFile? TakeChanges()
    {
        lock (_gate)
        {
            if (!_changed)
            {
                return null;
            }
            _changed = false;
            return new CountsFile(
                _flashes.Select((f) => new CountsFile.FlashRow(f.Key.Build, f.Key.Version, f.Key.Cc, f.Value)).ToList(),
                _visits.Select((v) => new CountsFile.VisitRow(v.Key.Day, v.Key.Cc, v.Value)).ToList());
        }
    }

    /// <summary>What TakeChanges gave could not be written: it is to be taken again.</summary>
    public void MarkChanged()
    {
        lock (_gate)
        {
            _changed = true;
        }
    }

    public void Restore(CountsFile file)
    {
        lock (_gate)
        {
            foreach (CountsFile.FlashRow f in file.Flashes)
            {
                _flashes[(f.Build, f.Version, f.Cc)] = f.N;
            }
            foreach (CountsFile.VisitRow v in file.Visits)
            {
                _visits[(v.Day, v.Cc)] = v.N;
            }
        }
    }

    private void Forget(DateTimeOffset now)
    {
        while (_alive.Count > 0 && now - _alive.Peek().At > _aliveWindow)
        {
            _alive.Dequeue();
        }
        while (_cable.Count > 0 && now - _cable.Peek().At > _cableWindow)
        {
            _cable.Dequeue();
        }
    }
}

/// <summary>The counts as data/counts.json holds them.</summary>
public sealed record CountsFile(IReadOnlyList<CountsFile.FlashRow> Flashes, IReadOnlyList<CountsFile.VisitRow> Visits)
{
    public sealed record FlashRow(string Build, string Version, string Cc, int N);

    public sealed record VisitRow(string Day, string Cc, int N);
}

/// <summary>0.9 before 0.10: versions compared number by number.</summary>
public sealed class VersionOrder : IComparer<string>
{
    public static readonly VersionOrder Instance = new VersionOrder();

    public int Compare(string? x, string? y)
    {
        string[] a = (x ?? "").Split('.');
        string[] b = (y ?? "").Split('.');
        for (int i = 0; i < Math.Max(a.Length, b.Length); i++)
        {
            int p = i < a.Length && int.TryParse(a[i], NumberStyles.None, CultureInfo.InvariantCulture, out int u) ? u : 0;
            int q = i < b.Length && int.TryParse(b[i], NumberStyles.None, CultureInfo.InvariantCulture, out int v) ? v : 0;
            if (p != q)
            {
                return p.CompareTo(q);
            }
        }
        return 0;
    }
}
