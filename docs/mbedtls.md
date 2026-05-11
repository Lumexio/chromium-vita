mbedTLS integration (Vita)

This project expects the mbedTLS source tree to live at:

third_party/mbedtls

Quick setup (one-time):

1. Clone mbedTLS into third_party/mbedtls (or add as a submodule).
2. Build with CHROMIUM_VITA_USE_MBEDTLS=ON (default).
3. Provide a CA bundle at ux0:data/chromium-vita/ca-bundle.pem on the device.

Notes:

- The socket-based client uses mbedTLS for both HTTP and HTTPS.
- The CA bundle must be PEM-encoded (use the standard Mozilla CA bundle).
- If the CA bundle is missing, HTTPS requests will fail with a clear error.
