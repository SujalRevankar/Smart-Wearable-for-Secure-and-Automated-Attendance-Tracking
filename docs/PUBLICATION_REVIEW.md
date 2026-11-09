# Publication review

Prepared from the four supplied project source files.

Removed from prepared source:
- Hardcoded Wi-Fi network names and passwords.
- The original Firebase hostname, including its occurrence in a comment.
- The geofence centre coordinates.
- Personal ECG template means, standard deviations and threshold.
- The hardcoded student database-path identifier.
- The Windows enrollment CSV path and filename.
- All saved notebook outputs, execution counts and cell metadata.

Added:
- A local configuration include in each of the three Arduino sketches.
- Three configuration examples with an explicit compilation guard and placeholder values.
- A configurable local notebook input path.
- Ignore-rule additions and setup documentation.

Preserved:
- Original input uploads, which were not edited.
- Firmware sampling, verification, GPIO assignments, circular geofence radius, dashboard, attendance and database request logic.
- Notebook cleaning, feature statistics and threshold calculations.
- Original filenames inside Arduino-compatible matching folders.

Checks completed:
- Targeted scans for the original Wi-Fi settings, database hostname and coordinates throughout the prepared package.
- Inspection that firmware templates were replaced by private configuration includes.
- Notebook JSON validation, output clearing and Python source syntax validation.
- Original Arduino file SHA-256 comparison before/after preparation.
- Confirmation of a 50.0-metre circular radius in the combined sketch.

Limits: no Gitleaks executable or Arduino toolchain was available. No independent secret-scanner result, full Arduino compilation, live Firebase access or hardware test is claimed. This targeted review cannot guarantee absence of every possible sensitive item. No enrollment data, Firebase exports, screenshots or original media are included.

Runtime issues still present include unauthenticated database requests, disabled TLS verification, overlapping duration accounting, GPS startup/fix handling and ECG freshness behavior. See CONFIGURATION.md and the project README. No connection to the original Firebase database was attempted.
