#include <gtest/gtest.h>

#include <string>

#include "tds/config.hpp"
#include "tds/job.hpp"
#include "tds/protobuf_auth.hpp"
#include "tds/util.hpp"

TEST(Job, TransitionsFollowTheSpecifiedPipeline) {
  using tds::JobStatus;
  EXPECT_TRUE(tds::can_transition(JobStatus::kCreated, JobStatus::kQueued));
  EXPECT_TRUE(tds::can_transition(JobStatus::kQueued, JobStatus::kDownloading));
  EXPECT_TRUE(tds::can_transition(JobStatus::kDownloading, JobStatus::kEncoding));
  EXPECT_TRUE(tds::can_transition(JobStatus::kEncoding, JobStatus::kReadyForDelivery));
  EXPECT_TRUE(tds::can_transition(JobStatus::kReadyForDelivery, JobStatus::kSending));
  EXPECT_TRUE(tds::can_transition(JobStatus::kSending,
                                  JobStatus::kAwaitingClientConfirmation));
  EXPECT_TRUE(tds::can_transition(JobStatus::kAwaitingClientConfirmation,
                                  JobStatus::kCompleted));
  EXPECT_TRUE(tds::can_transition(JobStatus::kCompleted, JobStatus::kCleaned));

  EXPECT_FALSE(tds::can_transition(JobStatus::kCreated, JobStatus::kCompleted));
  EXPECT_FALSE(tds::can_transition(JobStatus::kCompleted, JobStatus::kSending));
  EXPECT_FALSE(tds::can_transition(JobStatus::kCleaned, JobStatus::kQueued));
  EXPECT_TRUE(tds::can_transition(JobStatus::kSending, JobStatus::kFailedDelivery));
}

TEST(Job, StatusNamesRoundTrip) {
  for (const auto* name : {"created", "queued", "downloading", "encoding",
                           "ready_for_delivery", "sending",
                           "awaiting_client_confirmation", "completed", "cleaned",
                           "failed_download", "failed_encoding", "failed_delivery",
                           "failed_reconstruction", "cancelled", "expired"}) {
    tds::JobStatus status{};
    ASSERT_TRUE(tds::parse_status(name, status)) << name;
    EXPECT_EQ(tds::to_string(status), name);
  }
  tds::JobStatus ignored{};
  EXPECT_FALSE(tds::parse_status("inventado", ignored));
}

TEST(Util, IdentifiersAndUuids) {
  EXPECT_TRUE(tds::is_valid_id("abc-123_X"));
  EXPECT_FALSE(tds::is_valid_id(""));
  EXPECT_FALSE(tds::is_valid_id("con espacio"));
  EXPECT_FALSE(tds::is_valid_id("con|pipe"));

  const auto a = tds::new_uuid();
  const auto b = tds::new_uuid();
  EXPECT_EQ(a.size(), 36u);
  EXPECT_EQ(a[8], '-');
  EXPECT_EQ(a[13], '-');
  EXPECT_EQ(a[18], '-');
  EXPECT_EQ(a[23], '-');
  EXPECT_EQ(a[14], '4');
  EXPECT_NE(a, b);
  EXPECT_TRUE(tds::is_valid_id(a));
  EXPECT_TRUE(tds::is_valid_id(tds::random_hex(8)));
}

TEST(Util, TimeFormat) {
  EXPECT_EQ(tds::now_iso8601(0), "1970-01-01T00:00:00Z");
  EXPECT_EQ(tds::now_iso8601(1700000000), "2023-11-14T22:13:20Z");
}

TEST(Util, NormalizesTodusJid) {
  // Un teléfono cubano de 8 dígitos recibe el prefijo 53.
  EXPECT_EQ(tds::todus_jid("55123456", "im.todus.cu"),
            "5355123456@im.todus.cu");
  EXPECT_EQ(tds::todus_jid("5355123456", "im.todus.cu"),
            "5355123456@im.todus.cu");
  // JID completo: se acepta tal cual.
  EXPECT_EQ(tds::todus_jid("5355123456@im.todus.cu", "im.todus.cu"),
            "5355123456@im.todus.cu");
  EXPECT_TRUE(tds::todus_jid("", "im.todus.cu").empty());
  EXPECT_TRUE(tds::todus_jid("1234567", "im.todus.cu").empty());
  EXPECT_TRUE(tds::todus_jid("5512345678", "im.todus.cu").empty());
  EXPECT_TRUE(tds::todus_jid("5512345a", "im.todus.cu").empty());
  EXPECT_TRUE(tds::todus_jid("55123456@", "im.todus.cu").empty());
}

TEST(ProtobufAuth, EncodesRequestFields) {
  const auto body = tds::encode_auth_token_request("5300000000", "x", "1");
  // campo 1 (0x0A), longitud 10, "5300000000"
  EXPECT_EQ(static_cast<unsigned char>(body[0]), 0x0A);
  EXPECT_EQ(static_cast<unsigned char>(body[1]), 10);
  EXPECT_EQ(body.substr(2, 10), "5300000000");
  EXPECT_NE(body.find("x"), std::string::npos);
}

TEST(ProtobufAuth, DecodesTokenField) {
  // Respuesta mínima: field 2, wire 2, longitud 3, "jwt".
  const std::string body = std::string("\x12\x03jwt", 5);
  std::string token;
  std::string err;
  ASSERT_TRUE(tds::decode_auth_token_response(body, token, err)) << err;
  EXPECT_EQ(token, "jwt");

  EXPECT_FALSE(tds::decode_auth_token_response("", token, err));
  EXPECT_FALSE(tds::decode_auth_token_response(
      std::string("\x12\x09" "ab", 3), token, err));
}

TEST(Config, Validation) {
  tds::Config cfg;
  cfg.database_url = "postgresql://localhost/x";
  cfg.internal_token = "0123456789abcdef";
  cfg.transport = "mock";
  std::string err;
  EXPECT_TRUE(tds::validate_config(cfg, err)) << err;

  cfg.internal_token = "corto";
  EXPECT_FALSE(tds::validate_config(cfg, err));

  cfg.internal_token = "0123456789abcdef";
  cfg.transport = "todus";
  EXPECT_FALSE(tds::validate_config(cfg, err));  // falta usuario
  cfg.todus_username = "5300000000";
  cfg.todus_token = "jwt";
  EXPECT_TRUE(tds::validate_config(cfg, err)) << err;
}
