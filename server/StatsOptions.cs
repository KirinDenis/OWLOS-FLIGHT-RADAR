using System;

namespace RadarStats;

/// <summary>What the service counts and where it keeps it: the "Stats" section of appsettings.json.</summary>
public sealed class StatsOptions
{
    public const string Section = "Stats";

    /// <summary>The counts, kept between runs: flashes and page visits. Nothing else is written.</summary>
    public string DataFile { get; set; } = "data/counts.json";

    /// <summary>DB-IP's free IP to Country Lite, its CSV. Missing: every country is "ZZ", the counts go on.</summary>
    public string CountryFile { get; set; } = "data/dbip-country-lite.csv";

    /// <summary>The firmware builds a report may name, as the site's BOARDS have them.</summary>
    public string[] Builds { get; set; } = Array.Empty<string>();

    /// <summary>The pages allowed to ask and to report from a browser.</summary>
    public string[] AllowedOrigins { get; set; } = Array.Empty<string>();

    /// <summary>Countries not counted at all, ISO 3166 two letters.</summary>
    public string[] IgnoredCountries { get; set; } = Array.Empty<string>();

    /// <summary>A board says it is alive this often: the reports in that long are the boards online now.</summary>
    public int AliveMinutes { get; set; } = 10;

    /// <summary>A page says how many boards are on its cable this often: the same, for the cable count.</summary>
    public int CableMinutes { get; set; } = 5;

    /// <summary>The most requests one address may make in a minute. A phone network puts many people behind one.</summary>
    public int RequestsPerMinute { get; set; } = 120;

    /// <summary>The most versions one build may have in the counts: more is not a real one, it is someone filling the file.</summary>
    public int MaxVersions { get; set; } = 64;

    /// <summary>The most reports of the last minutes kept in memory, each of the two kinds: past it the oldest go.</summary>
    public int MaxRecent { get; set; } = 200000;

    public bool IsValid()
    {
        return Builds.Length > 0 && AllowedOrigins.Length > 0 && AliveMinutes > 0 && CableMinutes > 0 && RequestsPerMinute > 0 &&
            MaxVersions > 0 && MaxRecent > 0;
    }
}
