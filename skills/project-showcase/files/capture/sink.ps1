# sink.ps1: a page POSTs a picture to http://localhost:8798/NAME and it lands in the folder given, as NAME.
# POST or GET /quit stops it. The agent's browser pane cannot save files itself; this is how its shots reach the disk.
#   powershell -ExecutionPolicy Bypass -File sink.ps1 -Out C:\path\to\shots [-Port 8798]
# Only names of letters, digits, dot, dash and underscore are saved, so a page cannot write outside the folder.
param(
  [Parameter(Mandatory = $true)][string]$Out,
  [int]$Port = 8798
)
if (-not (Test-Path $Out)) {
  New-Item -ItemType Directory -Force $Out | Out-Null
}
$l = New-Object Net.HttpListener
$l.Prefixes.Add("http://localhost:$Port/")
$l.Start()
Write-Host "sink on http://localhost:$Port/ into $Out"
try {
  while ($true) {
    $c = $l.GetContext()
    $n = $c.Request.Url.AbsolutePath.TrimStart('/')
    $c.Response.AddHeader('Access-Control-Allow-Origin', '*')
    if ($n -eq 'quit') {
      $c.Response.Close()
      break
    }
    if ($c.Request.HttpMethod -eq 'POST' -and $n -match '^[A-Za-z0-9._-]+$') {
      $ms = New-Object IO.MemoryStream
      $c.Request.InputStream.CopyTo($ms)
      [IO.File]::WriteAllBytes((Join-Path $Out $n), $ms.ToArray())
    }
    $c.Response.Close()
  }
}
finally {
  $l.Stop()
}
