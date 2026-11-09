# Configure the revised prototype privately

The three Arduino sketches are alternative programs. The combined ECG/GPS sketch runs on the main ESP32. The second ESP32 had separate GPS firmware installed, but that sketch is not included or modified by this update.

## Files that belong in GitHub

Commit the `.ino` files and `config.example.h` examples. The examples contain placeholders and deliberately fail compilation with `PROJECT_CONFIGURED` set to zero. Never commit an actual `config.local.h`, ID token, Wi-Fi password, database address, private coordinates, ECG template or enrollment export.

## Configure on your own computer

1. Open the chosen sketch's folder and copy `config.example.h` to `config.local.h` in the same folder.
2. Fill in the private ECG means, deviations and threshold. The revised detector resets peak history and counts RR intervals explicitly; recollect/recompute enrollment features with the revised extraction before fitting a new profile. The original acquisition/export sketch is still absent from the supplied source set. Do not assume an old profile is compatible.
3. For either web-enabled sketch, set `WIFI_SSID`, `WIFI_PASSWORD`, `DASHBOARD_USERNAME` and a unique `DASHBOARD_PASSWORD` of at least 12 characters. The placeholder password is refused at runtime.
4. For the combined sketch, set `FIREBASE_HOST` to your own database hostname, without `https://` or a trailing slash. Set the student identifier and geofence centre coordinates locally. The radius remains 50 metres.
5. Set `FIREBASE_ID_TOKEN` to a current Firebase Authentication ID token for a user authorized by your database rules. This is a user ID token, not a service-account private key or an OAuth access token. Requests use Firebase's `auth` query parameter over verified HTTPS. This prototype does not implement token refresh; replace an expired token locally and reflash when testing. Do not publish a token or open database rules to bypass authentication.
6. Replace the `FIREBASE_ROOT_CA` placeholder with the trusted root CA certificate in PEM format for your actual database endpoint. Establish trust through the issuing CA's official documentation or another trusted source; do not blindly trust a certificate obtained from an unverified connection. No particular CA was assumed, embedded or live-tested here.
7. After replacing every required placeholder, set `PROJECT_CONFIGURED` to `1` in the private file. Do not set this flag in the committed example.
8. Build with the appropriate ESP32 Arduino board support and TinyGPSPlus library where needed. The supplied validation used desktop mocks, so an actual Arduino build and hardware test remain required.

The Firebase REST authentication scheme is documented at https://firebase.google.com/docs/database/rest/auth . Proper rules and current tokens are necessary independently of this source code.

## Dashboard transport

The browser will request a username/password. Both the dashboard and JSON data routes require it. The server still uses HTTP Basic over HTTP, so credentials are not encrypted on that network. Use a trusted local network; do not expose the server directly to the internet or describe this as end-to-end encrypted dashboard access.

## Notebook

Install `numpy`, `pandas` and `jupyter`, then open `training/IOT_ML_VIRAJ.ipynb`. It reads the private path in `ECG_CSV_PATH`, or `data/enrollment.csv` relative to the kernel working directory. With a kernel running in `training/`, use `training/data/enrollment.csv`.

The notebook rejects fewer than two retained rows, invalid statistics and a nonpositive/nonfinite suggested threshold. Two rows are only a computational minimum. Collect a meaningful dataset and evaluate held-out authorized/unauthorized recordings. The notebook prints private model statistics: clear outputs before committing a notebook after running it.

## Remaining limitations

No real Firebase request, certificate chain, ESP32 build, firmware upload or physical sensor was tested here. Synchronous networking can delay ECG samples. There is no durable offline queue or automatic token renewal. Failed initial summary reads still fall back to zero, and failed writes are not durably recovered. Restart behavior and persistence are not validated. The device begins time-dependent recording only when its clock is valid; large clock corrections still require separate validation.

The existing `.gitignore` exclusions must be preserved. They do not remove already tracked secrets and do not stop deliberate browser uploads. No private local file is included in this update.
