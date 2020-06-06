#ifndef KMSP11_TEST_RESOURCE_HELPERS_H_
#define KMSP11_TEST_RESOURCE_HELPERS_H_

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/cloud/kms/v1/resources.pb.h"
#include "google/cloud/kms/v1/service.grpc.pb.h"
#include "google/cloud/kms/v1/service.pb.h"
#include "kmsp11/test/test_status_macros.h"

namespace kmsp11 {

// A location name where test resources should be created.
ABSL_CONST_INIT extern const absl::string_view kTestLocation;

// Creates a KeyRing with the provided attributes, or CHECK-fails.
google::cloud::kms::v1::KeyRing CreateKeyRingOrDie(
    google::cloud::kms::v1::KeyManagementService::Stub* kms_stub,
    absl::string_view location_name, absl::string_view key_ring_id,
    const google::cloud::kms::v1::KeyRing& key_ring);

// Creates a CryptoKey with the provided attributes, or CHECK-fails.
google::cloud::kms::v1::CryptoKey CreateCryptoKeyOrDie(
    google::cloud::kms::v1::KeyManagementService::Stub* kms_stub,
    absl::string_view key_ring_name, absl::string_view crypto_key_id,
    const google::cloud::kms::v1::CryptoKey& crypto_key,
    bool skip_initial_version_creation);

// Creates a CryptoKeyVersion with the provided attributes, or CHECK-fails.
google::cloud::kms::v1::CryptoKeyVersion CreateCryptoKeyVersionOrDie(
    google::cloud::kms::v1::KeyManagementService::Stub* kms_stub,
    absl::string_view crypto_key_name,
    const google::cloud::kms::v1::CryptoKeyVersion& crypto_key_version);

// Invokes GetCryptoKeyVersion in a loop, waiting poll_interval between each
// request, until the specified CryptoKeyVersion's state is ENABLED.
google::cloud::kms::v1::CryptoKeyVersion WaitForEnablement(
    google::cloud::kms::v1::KeyManagementService::Stub* kms_stub,
    const google::cloud::kms::v1::CryptoKeyVersion& crypto_key_version,
    absl::Duration poll_interval = absl::Milliseconds(1));

// Returns a randomized string suitable for use as a KMS resource identifier.
std::string RandomId(absl::string_view prefix = "test-");

}  // namespace kmsp11

#endif  // KMSP11_TEST_RESOURCE_HELPERS_H_
