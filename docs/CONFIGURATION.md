# Private configuration

Each Arduino sketch is a separate program in its own matching folder under `firmware/`. Upload only one to the main ESP32 at a time. The combined ECG/GPS/Firebase sketch is the main project; the other two are standalone ECG alternatives. The second ESP32 had separate GPS firmware installed, but that sketch is not included. These instructions configure only the three supplied main-board/standalone ECG sketches.

1. Copy that sketch's `config.example.h` to `config.local.h` in the SAME folder.
2. Fill in the Wi-Fi name/password and Firebase host when present. The Firebase host is your database hostname only, without `https://` or a trailing slash. Fill in your student identifier and geofence centre coordinates locally. The circular radius remains 50 metres in the combined sketch.
3. Run the enrollment notebook with your private CSV. Copy all six `MU_*` values, six `SD_*` values, and the chosen threshold into `config.local.h`. The example zeros and ones are placeholders, not a trained profile. Validate the threshold on independent recordings.
4. Set `PROJECT_CONFIGURED` to `1` only after replacing all placeholders. The deliberate `#error` prevents accidentally compiling an example as a working enrolled model.
5. Compile using the correct ESP32 board and TinyGPSPlus when required. Full Arduino compilation and hardware validation were not available in the preparation environment.

The repository's `.gitignore` already excludes local configuration headers, environment files, private data directories and enrollment CSVs. Preserve these exclusions. Ignore rules do not remove already tracked files and do not prevent deliberate browser uploads. Some credential filenames, including `firebase-adminsdk-example.json` and `private_key.json`, do not match the current patterns; do not add such files to the repository.

## Notebook

`training/IOT_ML_VIRAJ.ipynb` accepts `ECG_CSV_PATH`, defaulting to `data/enrollment.csv` relative to the notebook working directory. When opening from `training/`, place the private file in `training/data/enrollment.csv` or set an absolute local path through the environment. Install dependencies using `python -m pip install -r requirements.txt` from the repository root.

The expected columns are `beats`, `mean_rr_ms`, `std_rr_ms`, `mean_peak`, `std_peak`, and `energy`; the original column-normalization routine is retained. The supplied files do not include the acquisition/export sketch or enrollment CSV. The notebook prints private enrollment statistics: clear all outputs again before committing a notebook after running it. A usable enrollment set needs enough valid rows and variation for finite, meaningful standard deviations; no evaluation accuracy is established here.

## Remaining runtime security work

The combined prototype still makes unauthenticated Firebase REST requests and calls `setInsecure()`. Configuration separation prevents publication of the original values but does not secure database access. Do not open database rules to make this prototype connect. Before live use, implement an appropriate authenticated flow, restrictive rules and server-certificate validation. These changes have not been implemented or tested in this package.

The duration/session accounting, RR interval counting, GPS startup validity, ECG freshness and enrollment-data validation defects described in the README remain unresolved. The repository contains sanitized prototype source; the documentation update does not repair or hardware-test its firmware.
