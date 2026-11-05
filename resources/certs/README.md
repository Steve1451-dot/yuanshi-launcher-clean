# TLS trust resources

CloudGenshin native alpha imports `cacert.pem`, the standard Mozilla-derived
CA bundle from https://curl.se/ca/cacert.pem. Retrieved 2026-10-11 over verified
HTTPS; its SHA-256 was compared to curl.se's `cacert.pem.sha256` before adding
it. The file's own header identifies the generation date, source and MPL 2.0
license. This supports service chains beyond the original NVIDIA-specific
single-root resource. TLS peer, hostname and date verification remain enabled;
a wrong Switch clock, unsupported chain or backend import failure is still an
error, never a reason to disable verification.

Package this directory at `romfs:/certs`. For updates, fetch the bundle and
published hash over verified HTTPS, verify the exact bytes, retain its header
and license, then rebuild. Never import a user's/interception proxy certificate
or a leaf server certificate to suppress a service validation failure.

## Historical NVIDIA supplemental root

`DigiCertGlobalRootG3.pem` is the unmodified public root downloaded from
https://cacerts.digicert.com/DigiCertGlobalRootG3.crt.pem. Its DER SHA-256 fingerprint is
`31AD6648F8104138C738F39EA4320133393E3A18CC02296EF97C2AC9EF6731D0`.

On 2026-09-12, the `static.nvidiagrid.net` catalog certificate chained through
`DigiCert Global G3 TLS ECC SHA384 2020 CA1` to this root. The PEM was also checked
against Ubuntu's Mozilla-derived trust store.

The original Switch curl configuration imported this single root through
`CURLOPT_CAINFO` in addition to the system trust store. Peer, hostname and certificate-date verification stay
enabled. Horizon includes this root starting with version 11.0.0, so the resource
provides compatibility for older trust stores; it does not establish the cause of
certificate failures on current firmware. A wrong system clock or a different
server chain still requires diagnosis on the console.

Package this directory at `romfs:/certs`. Do not replace this root with a server
certificate or an unverified download. When updating it, verify its fingerprint
against the issuing CA and a maintained public trust store.
