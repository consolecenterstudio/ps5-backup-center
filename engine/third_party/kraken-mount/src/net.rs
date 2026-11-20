//! A minimal HTTPS GET over the console's own libSceHttp/libSceSsl (ported from
//! ShadowMountPlus' sm_ampr_updater.c). Blocking HTTP/1.1, trust limited to the 4 root CAs below
//! (the chains GitHub releases use).

use std::ffi::{c_char, c_int, c_void, CString};
use std::io;
use std::sync::Once;

const NET_POOL_SIZE: c_int = 32 * 1024;
const SSL_POOL_SIZE: usize = (256 + 4 * CAS.len()) * 1024;
const HTTP_POOL_SIZE: usize = 256 * 1024;
const HTTP_HEADER_MAX_SIZE: usize = 32 * 1024;
const IO_BUFFER_SIZE: usize = 64 * 1024;
const HTTP_TIMEOUT_US: u32 = 30 * 1000 * 1000;
const HTTP_VERSION_1_1: c_int = 2;

#[repr(C)]
struct SceSslData {
    ptr: *mut c_char,
    size: usize,
}

extern "C" {
    fn sceNetInit() -> c_int;
    fn sceNetPoolCreate(name: *const c_char, size: c_int, flags: c_int) -> c_int;
    fn sceNetPoolDestroy(pool_id: c_int) -> c_int;
    fn sceSslInit(pool_size: usize) -> c_int;
    fn sceSslTerm(ssl_id: c_int) -> c_int;
    fn sceSslLoadCert(
        ssl_id: c_int,
        ca_count: c_int,
        ca_list: *const *const SceSslData,
        cert: *const SceSslData,
        private_key: *const SceSslData,
    ) -> c_int;
    fn sceSslUnloadCert(ssl_id: c_int) -> c_int;
    fn sceHttpInit(net_pool_id: c_int, ssl_id: c_int, pool_size: usize) -> c_int;
    fn sceHttpTerm(http_id: c_int) -> c_int;
    fn sceHttpCreateTemplate(
        http_id: c_int,
        user_agent: *const c_char,
        http_version: c_int,
        auto_proxy_config: c_int,
    ) -> c_int;
    fn sceHttpDeleteTemplate(template_id: c_int) -> c_int;
    fn sceHttpCreateConnectionWithURL(
        template_id: c_int,
        url: *const c_char,
        enable_keepalive: c_int,
    ) -> c_int;
    fn sceHttpDeleteConnection(connection_id: c_int) -> c_int;
    fn sceHttpCreateRequestWithURL2(
        connection_id: c_int,
        method: *const c_char,
        url: *const c_char,
        content_length: u64,
    ) -> c_int;
    fn sceHttpDeleteRequest(request_id: c_int) -> c_int;
    fn sceHttpSendRequest(request_id: c_int, data: *const c_void, size: usize) -> c_int;
    fn sceHttpGetStatusCode(request_id: c_int, status_code: *mut c_int) -> c_int;
    fn sceHttpReadData(request_id: c_int, buffer: *mut c_void, size: usize) -> c_int;
    fn sceHttpSetAutoRedirect(id: c_int, enabled: c_int) -> c_int;
    fn sceHttpSetResponseHeaderMaxSize(id: c_int, size: usize) -> c_int;
    fn sceHttpSetResolveTimeOut(id: c_int, timeout_us: u32) -> c_int;
    fn sceHttpSetConnectTimeOut(id: c_int, timeout_us: u32) -> c_int;
    fn sceHttpSetSendTimeOut(id: c_int, timeout_us: u32) -> c_int;
    fn sceHttpSetRecvTimeOut(id: c_int, timeout_us: u32) -> c_int;
}

/// Runs its release function on drop: the handles go away in reverse order on every path.
struct Guard(c_int, unsafe extern "C" fn(c_int) -> c_int);

impl Drop for Guard {
    fn drop(&mut self) {
        unsafe { (self.1)(self.0) };
    }
}

fn sce_err(op: &str, url: &str, rc: c_int) -> io::Error {
    io::Error::other(format!("{op} failed for {url}: 0x{:08x}", rc as u32))
}

/// rc as a handle (>= 0) or the error.
fn handle(op: &str, url: &str, rc: c_int) -> io::Result<c_int> {
    if rc < 0 {
        Err(sce_err(op, url, rc))
    } else {
        Ok(rc)
    }
}

/// rc == 0 or the error.
fn ok(op: &str, url: &str, rc: c_int) -> io::Result<()> {
    if rc != 0 {
        Err(sce_err(op, url, rc))
    } else {
        Ok(())
    }
}

/// GETs url (https:// or http://) and returns the body: Err on a non-200 status, a sce error
/// (hex code in the message) or a body over max bytes.
pub fn https_get(url: &str, max: usize) -> io::Result<Vec<u8>> {
    static NET_INIT: Once = Once::new();
    // ponytail: the result is ignored (it is non-zero when the net library is already up); a
    // real failure shows up at sceNetPoolCreate below. Never retried for the process' life.
    NET_INIT.call_once(|| unsafe {
        sceNetInit();
    });
    let c_url = CString::new(url).map_err(|_| io::Error::other(format!("bad url {url}")))?;
    unsafe {
        let pool = handle("sceNetPoolCreate", url, sceNetPoolCreate(c"kraken-mount".as_ptr(), NET_POOL_SIZE, 0))?;
        let _pool = Guard(pool, sceNetPoolDestroy);
        let ssl = handle("sceSslInit", url, sceSslInit(SSL_POOL_SIZE))?;
        let _ssl = Guard(ssl, sceSslTerm);
        // The library gets writable copies (SMP passes non-const arrays); they outlive the unload.
        let mut pems: Vec<Vec<u8>> = CAS.iter().map(|c| c.to_vec()).collect();
        let datas: Vec<SceSslData> = pems
            .iter_mut()
            .map(|p| SceSslData { ptr: p.as_mut_ptr().cast(), size: p.len() })
            .collect();
        let list: Vec<*const SceSslData> = datas.iter().map(|d| d as *const _).collect();
        let n = list.len() as c_int;
        ok("sceSslLoadCert", url, sceSslLoadCert(ssl, n, list.as_ptr(), std::ptr::null(), std::ptr::null()))?;
        let _certs = Guard(ssl, sceSslUnloadCert);
        // Blocking HTTP/1.1 avoids libhttp2's internal event-thread affinity (SMP).
        let http = handle("sceHttpInit", url, sceHttpInit(pool, ssl, HTTP_POOL_SIZE))?;
        let _http = Guard(http, sceHttpTerm);
        let agent = concat!("kraken-mount/", env!("CARGO_PKG_VERSION"), "\0");
        let tmpl = sceHttpCreateTemplate(http, agent.as_ptr().cast(), HTTP_VERSION_1_1, 1);
        let tmpl = handle("sceHttpCreateTemplate", url, tmpl)?;
        let _tmpl = Guard(tmpl, sceHttpDeleteTemplate);
        ok("sceHttpSetAutoRedirect", url, sceHttpSetAutoRedirect(tmpl, 1))?;
        ok("sceHttpSetResponseHeaderMaxSize", url, sceHttpSetResponseHeaderMaxSize(tmpl, HTTP_HEADER_MAX_SIZE))?;
        ok("sceHttpSetResolveTimeOut", url, sceHttpSetResolveTimeOut(tmpl, HTTP_TIMEOUT_US))?;
        ok("sceHttpSetConnectTimeOut", url, sceHttpSetConnectTimeOut(tmpl, HTTP_TIMEOUT_US))?;
        ok("sceHttpSetSendTimeOut", url, sceHttpSetSendTimeOut(tmpl, HTTP_TIMEOUT_US))?;
        ok("sceHttpSetRecvTimeOut", url, sceHttpSetRecvTimeOut(tmpl, HTTP_TIMEOUT_US))?;
        let conn = sceHttpCreateConnectionWithURL(tmpl, c_url.as_ptr(), 1);
        let conn = handle("sceHttpCreateConnectionWithURL", url, conn)?;
        let _conn = Guard(conn, sceHttpDeleteConnection);
        let req = sceHttpCreateRequestWithURL2(conn, c"GET".as_ptr(), c_url.as_ptr(), 0);
        let req = handle("sceHttpCreateRequestWithURL2", url, req)?;
        let _req = Guard(req, sceHttpDeleteRequest);
        ok("sceHttpSendRequest", url, sceHttpSendRequest(req, std::ptr::null(), 0))?;
        let mut status: c_int = 0;
        ok("sceHttpGetStatusCode", url, sceHttpGetStatusCode(req, &mut status))?;
        if status != 200 {
            return Err(io::Error::other(format!("GET {url}: HTTP status {status}")));
        }
        let mut body = Vec::new();
        let mut buf = vec![0u8; IO_BUFFER_SIZE];
        loop {
            let n = sceHttpReadData(req, buf.as_mut_ptr().cast(), buf.len());
            let n = handle("sceHttpReadData", url, n)? as usize;
            if n == 0 {
                return Ok(body);
            }
            if n > max - body.len() {
                return Err(io::Error::other(format!("GET {url}: body over {max} bytes")));
            }
            body.extend_from_slice(&buf[..n]);
        }
    }
}

// PEM trust anchors for the certificate chains used by GitHub releases (from SMP's
// sm_ampr_ca.inc); each size excludes a trailing NUL, as SMP's sizeof - 1.
// ponytail: pinned to these 4 roots; when GitHub or its asset CDN moves to another root every
// request fails (sceHttpSendRequest error in kraken.log): refresh the list from SMP then.
const CAS: [&[u8]; 4] = [CA_SECTIGO_E46_USERTRUST, CA_ISRG_ROOT_YR_BY_X1, CA_USERTRUST_ECC, CA_ISRG_ROOT_X1];

const CA_SECTIGO_E46_USERTRUST: &[u8] = b"-----BEGIN CERTIFICATE-----\n\
MIIDRjCCAsugAwIBAgIQGp6v7G3o4ZtcGTFBto2Q3TAKBggqhkjOPQQDAzCBiDEL\n\
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n\
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n\
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjEwMzIy\n\
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYGA1UEChMP\n\
U2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIg\n\
QXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAR2\n\
+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccCWvkEN/U0\n\
NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+6xnOQ6Oj\n\
ggEgMIIBHDAfBgNVHSMEGDAWgBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAdBgNVHQ4E\n\
FgQU0SLaTFnxS18mOKqd1u7rDcP7qWEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB\n\
/wQFMAMBAf8wHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBEGA1UdIAQK\n\
MAgwBgYEVR0gADBQBgNVHR8ESTBHMEWgQ6BBhj9odHRwOi8vY3JsLnVzZXJ0cnVz\n\
dC5jb20vVVNFUlRydXN0RUNDQ2VydGlmaWNhdGlvbkF1dGhvcml0eS5jcmwwNQYI\n\
KwYBBQUHAQEEKTAnMCUGCCsGAQUFBzABhhlodHRwOi8vb2NzcC51c2VydHJ1c3Qu\n\
Y29tMAoGCCqGSM49BAMDA2kAMGYCMQCMCyBit99vX2ba6xEkDe+YO7vC0twjbkv9\n\
PKpqGGuZ61JZryjFsp+DFpEclCVy4noCMQCwvZDXD/m2Ko1HA5Bkmz7YQOFAiNDD\n\
49IWa2wdT7R3DtODaSXH/BiXv8fwB9su4tU=\n\
-----END CERTIFICATE-----\n";
const CA_ISRG_ROOT_YR_BY_X1: &[u8] = b"-----BEGIN CERTIFICATE-----\n\
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\n\
-----END CERTIFICATE-----\n";
const CA_USERTRUST_ECC: &[u8] = b"-----BEGIN CERTIFICATE-----\n\
MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n\
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n\
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n\
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n\
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n\
Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n\
VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n\
aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n\
I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n\
o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n\
A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n\
VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n\
zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n\
RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n\
-----END CERTIFICATE-----\n";
const CA_ISRG_ROOT_X1: &[u8] = b"-----BEGIN CERTIFICATE-----\n\
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n\
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n\
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n\
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n\
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n\
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n\
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n\
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n\
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n\
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n\
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n\
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n\
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n\
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n\
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n\
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n\
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n\
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n\
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n\
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n\
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n\
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n\
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n\
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n\
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n\
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n\
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n\
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n\
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n\
-----END CERTIFICATE-----\n";
