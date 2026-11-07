# Makes every "online-only" file under a folder local by reading it, several files at once.
# A cloud-synced folder (Dropbox, OneDrive) downloads a placeholder the first time it is read,
# which is mostly waiting on the network: one at a time that is seconds per file, eight at once
# several times faster. package_win.sh runs this on the staged assets before Inno Setup reads
# them one by one. Usage: powershell -ExecutionPolicy Bypass -File tools/hydrate_win.ps1 <dir> [threads]
param([Parameter(Mandatory = $true)][string]$Dir, [int]$Threads = 8)
Add-Type -TypeDefinition @'
using System; using System.IO; using System.Threading; using System.Threading.Tasks;
public static class Hydrate {
  public static int Done;
  public static void Read(string p) {
    using (var s = new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 65536, FileOptions.SequentialScan)) {
      var b = new byte[65536];
      while (s.Read(b, 0, b.Length) > 0) { }
    }
    Interlocked.Increment(ref Done);
  }
  public static Task StartAll(string[] files, int dop) {
    return Task.Run(() => Parallel.ForEach(files, new ParallelOptions { MaxDegreeOfParallelism = dop }, f => { try { Read(f); } catch (Exception) { } }));
  }
}
'@
$all = Get-ChildItem -LiteralPath $Dir -Recurse -File
$files = @($all | Where-Object { ($_.Attributes -band 0x1000) -or ($_.Attributes -band 0x400000) } | ForEach-Object FullName)
if ($files.Count -eq 0) { "all $($all.Count) files under $Dir are local"; exit 0 }
"$($files.Count) of $($all.Count) files under $Dir are online-only; reading them with $Threads threads"
$sw = [Diagnostics.Stopwatch]::StartNew()
$task = [Hydrate]::StartAll([string[]]$files, $Threads)
while (-not $task.Wait(10000)) { "  $([Hydrate]::Done)/$($files.Count) after $([int]$sw.Elapsed.TotalSeconds) s" }
"done: $([Hydrate]::Done)/$($files.Count) files in $([int]$sw.Elapsed.TotalSeconds) s"
