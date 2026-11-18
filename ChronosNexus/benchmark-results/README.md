# Release x64 baseline evidence

All benchmark rows use seed 20261118. Each run began in a separate process. Times are single observations on this Windows machine. Engine and runner wall time are separate CSV columns.

- [Initial size sweep and comparisons](results.csv). This contains the full extreme attempt. It exited at the 600-second budget during scheduling. The peak for this sweep is the highest Windows peak working set observed by the controller.
- [Final-source comparisons and stress runs](follow-up/results.csv). This contains fresh small, medium, and large custom runs and matched alternatives. Completed runs include an in-process peak working set reading. The follow-up added exact edge IDs to allocation results, aligned priority tie ordering for the basic allocator, added stress modes, and improved telemetry. The default custom outcomes agreed on all three repeated sizes.
- [Memory guard check](limit-check/results.csv). This deliberately used a 64 MiB limit on the full extreme configuration to exercise enforcement, with no claim about completing the dataset.
- [Release build](follow-up/build.log), [regression tests](follow-up/self-test.txt), [original demos](follow-up/demo.txt), and [final executable fingerprint](follow-up/binary-sha256.txt).

The initial executable SHA-256 was `28FB6FDE9D14F455A36295EBE82A34CF6B71908407DC1D91792844CED7D4808B`. Both builds used Visual Studio MSBuild 18.11, MSVC v145, Release x64, and command-line overrides to avoid a local linker PDB DLL mismatch. The project retains its ordinary Visual Studio options.

The ordinary seeded scenario did not require route recomputation when exact edges were revalidated. In the medium stress pair, 33 selected departure edges were explicitly closed. Conditional validation solved 35 routes again, compared with 215 when every eligible departure route was recomputed. Both ran the same original fleet and inventory plan and finished 207 missions, failed 66, and delivered 5164.64 units. The comparison covers route recomputation under fixed assignments. Full civilization replanning was outside this experiment.

