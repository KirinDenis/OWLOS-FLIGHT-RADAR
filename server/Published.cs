using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace RadarStats;

/// <summary>
/// The versions the site has published, read from its fw/versions.json every half hour. A flash is counted only for
/// one of them: a stranger posting made-up versions ("999.1", "12.34.56") filled a build's MaxVersions, and from then
/// on every real new version was refused, for good (the 2026-11-27 review). No list read yet, or no VersionsUrl: the
/// version's form alone, as before.
/// </summary>
public sealed class Published : BackgroundService
{
    private static readonly Regex VersionField =
        new Regex("\"version\":\\s*\"([0-9]{1,3}(?:\\.[0-9]{1,3}){1,3})\"", RegexOptions.CultureInvariant);

    private readonly HttpClient _http = new HttpClient { Timeout = TimeSpan.FromSeconds(20) };
    private readonly string _url;
    private readonly ILogger<Published> _log;
    private volatile HashSet<string>? _versions;

    public Published(IOptions<StatsOptions> options, ILogger<Published> log)
    {
        _url = options.Value.VersionsUrl;
        _log = log;
    }

    /// <summary>True for a version the site has published, or for any while no list has been read.</summary>
    public bool Has(string version)
    {
        HashSet<string>? now = _versions;
        return now == null || now.Contains(version);
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        if (string.IsNullOrEmpty(_url))
        {
            return;
        }
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                string json = await _http.GetStringAsync(_url, stoppingToken);
                HashSet<string> found = new HashSet<string>(StringComparer.Ordinal);
                foreach (Match m in VersionField.Matches(json))
                {
                    found.Add(m.Groups[1].Value);
                }
                if (found.Count > 0)
                {
                    _versions = found;                 // one that came empty or broken: the last list stays
                }
            }
            catch (HttpRequestException e)
            {
                _log.LogWarning("the site's versions not read: {Message}", e.Message);
            }
            catch (TaskCanceledException) when (!stoppingToken.IsCancellationRequested)
            {
                _log.LogWarning("the site's versions not read: no answer in 20 s");
            }
            try
            {
                await Task.Delay(TimeSpan.FromMinutes(30), stoppingToken);
            }
            catch (TaskCanceledException)
            {
                return;
            }
        }
    }

    public override void Dispose()
    {
        _http.Dispose();
        base.Dispose();
    }
}
