using System;
using System.IO;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace RadarStats;

/// <summary>
/// Keeps the counts in DataFile: read at start, written once a minute when they changed and once more when
/// the service stops. Written to a new file first and moved over the old one, so a stop half way through
/// leaves the last whole one.
/// </summary>
public sealed class CountsSaver : BackgroundService
{
    private static readonly JsonSerializerOptions Json = new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = true };

    private readonly Counts _counts;
    private readonly string _path;
    private readonly ILogger<CountsSaver> _log;

    public CountsSaver(Counts counts, IOptions<StatsOptions> options, ILogger<CountsSaver> log)
    {
        _counts = counts;
        _path = options.Value.DataFile;
        _log = log;
    }

    /// <summary>
    /// The counts kept, read back. A file that does not read as counts is moved aside, not deleted, and the
    /// service starts from none: a service that would not start at all counts nothing, and goes round and
    /// round under systemd.
    /// </summary>
    public override Task StartAsync(CancellationToken cancellationToken)
    {
        if (!File.Exists(_path))
        {
            _log.LogInformation("No counts at {Path} yet: starting from none", _path);
            return base.StartAsync(cancellationToken);
        }
        CountsFile? file = null;
        try
        {
            file = JsonSerializer.Deserialize<CountsFile>(File.ReadAllText(_path), Json);
        }
        catch (JsonException e)
        {
            _log.LogError(e, "{Path} does not read as counts", _path);
        }
        if (file?.Flashes == null || file.Visits == null)
        {
            string aside = $"{_path}.bad-{DateTime.UtcNow:yyyyMMdd-HHmmss}";
            File.Move(_path, aside);
            _log.LogError("Counts in {Path} could not be read: moved to {Aside}, starting from none", _path, aside);
            return base.StartAsync(cancellationToken);
        }
        _counts.Restore(file);
        _log.LogInformation("Counts read from {Path}: {Flashes} flash rows, {Visits} visit rows", _path, file.Flashes.Count, file.Visits.Count);
        return base.StartAsync(cancellationToken);
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using PeriodicTimer timer = new PeriodicTimer(TimeSpan.FromMinutes(1));
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
            {
                Save();
            }
        }
        catch (OperationCanceledException)
        {
            // stopping: StopAsync writes what is left
        }
    }

    public override async Task StopAsync(CancellationToken cancellationToken)
    {
        await base.StopAsync(cancellationToken);
        Save();
    }

    // a disk that will not take it (full, gone read-only) is said in the log, and the counts stay to be
    // written the next minute: the service goes on counting
    private void Save()
    {
        CountsFile? file = _counts.TakeChanges();
        if (file == null)
        {
            return;
        }
        try
        {
            string? dir = Path.GetDirectoryName(Path.GetFullPath(_path));
            if (dir != null)
            {
                Directory.CreateDirectory(dir);
            }
            string fresh = _path + ".new";
            File.WriteAllText(fresh, JsonSerializer.Serialize(file, Json));
            File.Move(fresh, _path, true);
        }
        catch (IOException e)
        {
            _counts.MarkChanged();
            _log.LogError(e, "The counts could not be written to {Path}: kept to try again", _path);
        }
        catch (UnauthorizedAccessException e)
        {
            _counts.MarkChanged();
            _log.LogError(e, "The counts may not be written to {Path}: kept to try again", _path);
        }
    }
}
