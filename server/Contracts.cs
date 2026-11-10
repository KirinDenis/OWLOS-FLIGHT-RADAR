using System.Collections.Generic;

namespace RadarStats;

/// <summary>A board, or the page for one it flashed: which build and which version. Nothing that names it.</summary>
public sealed record BoardReport(string? Build, string? Version);

/// <summary>A page: how many boards are on its cable now.</summary>
public sealed record CableReport(int N);

/// <summary>GET /api/stats, as site/stats.js reads it.</summary>
public sealed record StatsAnswer(
    string Updated,
    int Flashed,
    int Online,
    int Cable,
    IReadOnlyList<BoardAnswer> Boards,
    IReadOnlyList<CountryAnswer> Countries);

/// <summary>One build: flashed in all, online now, and the flashes by version, the newest first, each [version, n].</summary>
public sealed record BoardAnswer(string Build, int Flashed, int Online, IReadOnlyList<object[]> Versions);

public sealed record CountryAnswer(string Cc, int Flashed, int Online);

/// <summary>The page visits of one day in one country, for us: GET /api/visits from the server itself only.</summary>
public sealed record VisitAnswer(string Day, string Cc, int N);
