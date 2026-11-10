using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.Logging;

namespace RadarStats;

/// <summary>
/// The country of an address, by DB-IP's free IP to Country Lite (CC BY 4.0, db-ip.com): a CSV of ranges,
/// "first,last,CC" a line, IPv4 and IPv6 mixed. Read once at start into sorted arrays, asked by binary
/// search. The address itself is not kept anywhere: it is turned into two letters and dropped.
/// </summary>
public sealed class Countries
{
    public const string Unknown = "ZZ";

    private readonly UInt128[] _first;
    private readonly UInt128[] _last;
    private readonly string[] _cc;

    private Countries(UInt128[] first, UInt128[] last, string[] cc)
    {
        _first = first;
        _last = last;
        _cc = cc;
    }

    public int Ranges => _cc.Length;

    public static Countries Load(string path, ILogger log)
    {
        if (!File.Exists(path))
        {
            log.LogWarning("No country file at {Path}: every country counts as {Unknown}", path, Unknown);
            return new Countries(Array.Empty<UInt128>(), Array.Empty<UInt128>(), Array.Empty<string>());
        }
        List<(UInt128 First, UInt128 Last, string Cc)> rows = new List<(UInt128 First, UInt128 Last, string Cc)>();
        foreach (string line in File.ReadLines(path))
        {
            string[] f = line.Split(',');
            if (f.Length < 3 || !IPAddress.TryParse(f[0].Trim('"'), out IPAddress? a) || !IPAddress.TryParse(f[1].Trim('"'), out IPAddress? b))
            {
                continue;
            }
            string cc = f[2].Trim('"').Trim().ToUpperInvariant();
            if (cc.Length != 2)
            {
                continue;
            }
            rows.Add((Key(a), Key(b), cc));
        }
        rows.Sort((x, y) => x.First.CompareTo(y.First));
        UInt128[] first = new UInt128[rows.Count];
        UInt128[] last = new UInt128[rows.Count];
        string[] codes = new string[rows.Count];
        for (int i = 0; i < rows.Count; i++)
        {
            first[i] = rows[i].First;
            last[i] = rows[i].Last;
            codes[i] = string.Intern(rows[i].Cc);       // a few hundred codes over half a million ranges
        }
        log.LogInformation("Countries: {Ranges} address ranges from {Path}", rows.Count, path);
        Countries table = new Countries(first, last, codes);
        rows.Clear();
        rows.TrimExcess();
        GC.Collect();                                  // once, at start: the half a million lines read are garbage now
        return table;
    }

    /// <summary>Two letters for the address, <see cref="Unknown"/> when the table has none for it.</summary>
    public string Of(IPAddress? address)
    {
        if (address == null || _cc.Length == 0)
        {
            return Unknown;
        }
        UInt128 k = Key(address);
        int lo = 0;
        int hi = _first.Length - 1;
        int at = -1;
        while (lo <= hi)
        {
            int mid = lo + (hi - lo) / 2;
            if (_first[mid] <= k)
            {
                at = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return at >= 0 && k <= _last[at] ? _cc[at] : Unknown;
    }

    /// <summary>
    /// An address as one number: IPv4 in the IPv4-mapped part of IPv6 (::ffff:a.b.c.d), so both kinds sort
    /// in one table, and a mapped address from the network asks the same row as its plain IPv4 one.
    /// </summary>
    private static UInt128 Key(IPAddress address)
    {
        IPAddress v6 = address.AddressFamily == AddressFamily.InterNetwork ? address.MapToIPv6() : address;
        byte[] b = v6.GetAddressBytes();
        UInt128 k = 0;
        for (int i = 0; i < 16; i++)
        {
            k = (k << 8) | b[i];
        }
        return k;
    }
}
