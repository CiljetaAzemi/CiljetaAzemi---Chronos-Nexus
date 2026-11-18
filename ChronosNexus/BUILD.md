# Windows build and execution

## Visual Studio

1. Install the Desktop development with C++ workload, MSVC v145, and a Windows SDK compatible with the project.
2. Open [ChronosNexus.slnx](ChronosNexus.slnx), select **Release | x64**, and build the solution.
3. Under **Project Properties → Configuration Properties → Debugging**, set **Command Arguments** and start without debugging with **Ctrl+F5**.

| Task | Command arguments |
|---|---|
| Regression tests | `--self-test` |
| Console demonstration | Leave blank |
| Benchmark | `--benchmark small 20261118 custom` |

Substitute `medium`, `large`, or `extreme` for the dataset size. Direct IDE execution uses no external budget controller. Use the [benchmark runner](Run-Benchmarks.ps1) for resource limits and CSV output. Its default executable is `ChronosNexus/x64/Release/ChronosNexus.exe` relative to the repository root.

## Local troubleshooting

If the linker reports `LNK1101` with an MSPDB140.DLL mismatch, verify the Visual Studio installation and run from Developer PowerShell. The recorded Release build used the following temporary workaround, which suppresses linker debug output and manifest generation while retaining Release optimization. Execute it from the repository root in a fresh Developer PowerShell session.

```powershell
$env:_LINK_ = '/DEBUG:NONE /INCREMENTAL:NO'
try {
    msbuild .\ChronosNexus\ChronosNexus.slnx /t:Build /p:Configuration=Release /p:Platform=x64 /p:GenerateManifest=false /p:TrackFileAccess=false /m:1 /nr:false
} finally {
    Remove-Item Env:_LINK_ -ErrorAction SilentlyContinue
}
```

If local PowerShell script execution is restricted, follow the applicable workstation policy for running `Run-Benchmarks.ps1`. The [experimental evaluation](benchmark-results/README.md) lists the reproduction commands and their output directories.
