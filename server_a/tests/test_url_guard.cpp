#include <gtest/gtest.h>

#include "url_guard.hpp"

namespace {

tds::UrlPolicy public_policy() {
  tds::UrlPolicy policy;
  policy.allow_http = false;
  policy.allow_private = false;
  return policy;
}

}  // namespace

TEST(UrlGuard, RejectsDangerousTargets) {
  const auto policy = public_policy();
  for (const auto* url : {
           "http://example.com/video.mp4",      // http no permitido
           "ftp://example.com/video.mp4",       // esquema
           "file:///etc/passwd",                // esquema
           "https://127.0.0.1/x",               // loopback
           "https://localhost/x",               // resolver a loopback
           "https://10.0.0.5/x",                // privada
           "https://192.168.1.1/x",             // privada
           "https://169.254.169.254/latest/meta-data",  // metadata cloud
           "https://[::1]/x",                   // loopback v6
           "https://[fd00::1]/x",               // ULA v6
           "https://user:pass@example.com/x",   // credenciales embebidas
           "https:///sin-host",                 // sin host
           "https://example.com:99999/x",       // puerto inválido
       }) {
    const auto check = tds::validate_url(url, policy);
    EXPECT_FALSE(check.ok) << url;
    EXPECT_FALSE(check.error.empty()) << url;
  }
}

TEST(UrlGuard, AcceptsPublicHttpsAndNormalizes) {
  const auto policy = public_policy();
  const auto check = tds::validate_url("https://example.com/video.mp4?a=1", policy);
  // example.com es público y estable en DNS.
  ASSERT_TRUE(check.ok) << check.error;
  EXPECT_EQ(check.scheme, "https");
  EXPECT_EQ(check.host, "example.com");
  EXPECT_EQ(check.port, 443);
  EXPECT_FALSE(check.pinned_ip.empty());
  EXPECT_EQ(check.normalized, "https://example.com/video.mp4?a=1");
}

TEST(UrlGuard, AllowlistOfSuffixes) {
  auto policy = public_policy();
  policy.allowed_suffixes = {"example.com"};
  EXPECT_TRUE(tds::validate_url("https://example.com/x", policy).ok);
  EXPECT_TRUE(tds::validate_url("https://www.example.com/x", policy).ok);
  EXPECT_FALSE(tds::validate_url("https://badexample.com/x", policy).ok);
}

TEST(UrlGuard, AllowPrivateForTestsOnly) {
  auto policy = public_policy();
  policy.allow_private = true;
  policy.allow_http = true;
  const auto check = tds::validate_url("http://127.0.0.1:8080/video.bin", policy);
  ASSERT_TRUE(check.ok) << check.error;
  EXPECT_EQ(check.host, "127.0.0.1");
  EXPECT_EQ(check.port, 8080);
}

TEST(UrlGuard, ResolvesLocations) {
  EXPECT_EQ(tds::resolve_location("https://a.com/x/y", "https://b.com/z"),
            "https://b.com/z");
  EXPECT_EQ(tds::resolve_location("https://a.com/x/y", "//b.com/z"), "https://b.com/z");
  EXPECT_EQ(tds::resolve_location("https://a.com/x/y", "/z"), "https://a.com/z");
  EXPECT_EQ(tds::resolve_location("https://a.com:8443/x/y", "rel"), "https://a.com:8443/x/rel");
  EXPECT_EQ(tds::resolve_location("https://a.com/x/y?q=1", "rel"), "https://a.com/x/rel");
}
