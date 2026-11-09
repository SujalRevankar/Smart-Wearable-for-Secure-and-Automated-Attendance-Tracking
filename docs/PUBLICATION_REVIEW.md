# Publication review — 9 November 2026

Scope: the 15-file GitHub ZIP supplied for review on 9 November 2026. This review does not cover Git history, actual Firebase rules or hardware execution.

## Credential review

Gitleaks 8.30.1 returned zero findings for the extracted snapshot. The official release archive was checked against its published SHA-256 checksum. Targeted checks against 46 private configuration entries from the original uploads also found no original-value matches.

No original Wi-Fi settings, Firebase hostname, geofence coordinates, ECG template values, API tokens, private keys, service-account files, local configuration headers, private CSVs or database exports were found. Notebook outputs and execution counts are cleared. Configuration examples contain placeholders and deliberately block compilation until configured locally.

The GIF and wiring PNG match the prepared demonstration assets. They contain illustrative/reference material rather than the user's original video or location screenshot. Public CDN, map, NTP and badge URLs are not private credentials.

## Code checks

Notebook JSON and Python syntax checks passed. Running the actual enrollment cell on synthetic data produced finite values with four varied valid rows, invalid statistics with one valid row, and a calculation error when all rows were filtered out.

A desktop C++ harness exercised extracted attendance functions and the ECG peak-detection block using simulated time and mocked hardware/network calls. It reproduced duration double-counting, inconsistent first-session timestamps, and an incorrect RR mean across analysis windows. These checks are not a full Arduino build or hardware test.

## Unresolved implementation issues

- Firebase requests have no implemented authentication, and TLS certificate validation is disabled.
- Dashboard HTTP endpoints have no authentication.
- Duration/session accounting, GPS startup validity, RR interval counting, ECG freshness and inadequate enrollment-data handling need correction.
- Synchronous networking can interrupt sampling; no sustained 250-Hz rate has been measured here.
- The second ESP32's GPS sketch is absent, and its exact UART GPIOs/final power connection remain unverified.
- The wiring image refers to third-party artwork sources. Its attribution notices must be retained; a missing SOURCES.md can be replaced by equivalent notices in the README.

No repository source was changed by the review. No Firebase connection or physical hardware test was performed. A clean snapshot scan does not establish that earlier commits or the live database are free of exposure.
