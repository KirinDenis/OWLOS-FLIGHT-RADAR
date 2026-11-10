// RadarStats - how many OWLOS Flight Radar boards there are, for the map of boards on the site
// (site/stats.js). Counts only: a report says a build and a version, the address it came from is turned
// into a country and dropped, nothing that tells one board or one person from another is kept or logged.
// Behind Caddy on the same machine: it listens on the loopback only, Caddy gives it the client's address.
using System;
using System.Collections.Generic;
using System.Net;
using System.Text.RegularExpressions;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using RadarStats;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

StatsOptions settings = builder.Configuration.GetSection(StatsOptions.Section).Get<StatsOptions>() ?? new StatsOptions();
builder.Services.AddOptions<StatsOptions>()
    .Bind(builder.Configuration.GetSection(StatsOptions.Section))
    .Validate((o) => o.IsValid(), "Stats needs Builds, AllowedOrigins, and AliveMinutes, CableMinutes, RequestsPerMinute above 0")
    .ValidateOnStart();
builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddSingleton<Counts>();
builder.Services.AddSingleton((sp) => Countries.Load(
    sp.GetRequiredService<IOptions<StatsOptions>>().Value.CountryFile,
    sp.GetRequiredService<ILoggerFactory>().CreateLogger<Countries>()));
builder.Services.AddHostedService<CountsSaver>();

builder.WebHost.ConfigureKestrel((k) => k.Limits.MaxRequestBodySize = 1024);   // a report is a few dozen bytes
builder.Services.Configure<ForwardedHeadersOptions>((o) =>
{
    o.ForwardedHeaders = ForwardedHeaders.XForwardedFor;   // Caddy's, and only Caddy's: it is on the loopback
    o.KnownProxies.Clear();
    o.KnownProxies.Add(IPAddress.Loopback);
    o.KnownProxies.Add(IPAddress.IPv6Loopback);
});
builder.Services.AddCors((o) => o.AddDefaultPolicy((p) => p.WithOrigins(settings.AllowedOrigins).WithMethods("GET", "POST").WithHeaders("Content-Type")));
builder.Services.AddRateLimiter((o) =>
{
    o.RejectionStatusCode = StatusCodes.Status429TooManyRequests;
    // the address is the limiter's key for one minute, in memory: it is not kept past that, nor written
    o.GlobalLimiter = PartitionedRateLimiter.Create<HttpContext, string>((ctx) => RateLimitPartition.GetFixedWindowLimiter(
        ctx.Connection.RemoteIpAddress?.ToString() ?? "-",
        (_) => new FixedWindowRateLimiterOptions { PermitLimit = settings.RequestsPerMinute, Window = TimeSpan.FromMinutes(1), QueueLimit = 0 }));
});

WebApplication app = builder.Build();
app.Services.GetRequiredService<Countries>();          // the table read at start, not on the first report
app.UseForwardedHeaders();
app.UseCors();
app.UseRateLimiter();

HashSet<string> builds = new HashSet<string>(settings.Builds);
Regex versionForm = new Regex("^[0-9]{1,3}(\\.[0-9]{1,3}){1,3}$", RegexOptions.CultureInvariant);
bool Known(BoardReport r) => r.Build != null && builds.Contains(r.Build) && r.Version != null && versionForm.IsMatch(r.Version);
string CountryOf(HttpContext ctx, Countries countries) => countries.Of(ctx.Connection.RemoteIpAddress);

app.MapGet("/api/stats", (Counts counts) => Results.Ok(counts.Answer()));

// the page, once a flash it made went through and its owner allowed counting it
app.MapPost("/api/flash", (BoardReport r, HttpContext ctx, Counts counts, Countries countries) =>
{
    if (!Known(r))
    {
        return Results.BadRequest();
    }
    return counts.Flash(r.Build!, r.Version!, CountryOf(ctx, countries)) ? Results.NoContent() : Results.BadRequest();
});

// a board, every AliveMinutes, when its owner allowed it
app.MapPost("/api/alive", (BoardReport r, HttpContext ctx, Counts counts, Countries countries) =>
{
    if (!Known(r))
    {
        return Results.BadRequest();
    }
    counts.Alive(r.Build!, CountryOf(ctx, countries));
    return Results.NoContent();
});

// a page with boards on its cable, every CableMinutes, when allowed
app.MapPost("/api/cable", (CableReport r, HttpContext ctx, Counts counts, Countries countries) =>
{
    if (r.N < 1 || r.N > 16)
    {
        return Results.BadRequest();
    }
    counts.Cable(r.N, CountryOf(ctx, countries));
    return Results.NoContent();
});

// a page shown: a number a day and a country, no cookie, nothing else
app.MapPost("/api/visit", (HttpContext ctx, Counts counts, Countries countries) =>
{
    counts.Visit(CountryOf(ctx, countries));
    return Results.NoContent();
});

// the visits, for us: asked on the server itself (curl 127.0.0.1), never through Caddy
app.MapGet("/api/visits", (HttpContext ctx, Counts counts) =>
    ctx.Connection.RemoteIpAddress != null && IPAddress.IsLoopback(ctx.Connection.RemoteIpAddress)
        ? Results.Ok(counts.Visits())
        : Results.NotFound());

app.Run();
