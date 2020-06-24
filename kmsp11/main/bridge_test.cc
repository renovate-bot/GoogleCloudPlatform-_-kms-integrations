#include "kmsp11/main/bridge.h"

#include <fstream>

#include "gmock/gmock.h"
#include "kmsp11/config/config.h"
#include "kmsp11/test/fakekms/cpp/fakekms.h"
#include "kmsp11/test/matchers.h"
#include "kmsp11/test/resource_helpers.h"
#include "kmsp11/test/test_status_macros.h"
#include "kmsp11/util/cleanup.h"
#include "kmsp11/util/platform.h"

namespace kmsp11 {
namespace {

namespace kms_v1 = ::google::cloud::kms::v1;

using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::IsSupersetOf;

class BridgeTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(fake_kms_, FakeKms::New());

    auto client = fake_kms_->NewClient();
    kr1_ = CreateKeyRingOrDie(client.get(), kTestLocation, RandomId(), kr1_);
    kr2_ = CreateKeyRingOrDie(client.get(), kTestLocation, RandomId(), kr2_);

    config_file_ = std::tmpnam(nullptr);
    std::ofstream(config_file_)
        << absl::StrFormat(R"(
tokens:
  - key_ring: "%s"
    label: "foo"
  - key_ring: "%s"
    label: "bar"
kms_endpoint: "%s"
use_insecure_grpc_channel_credentials: true
)",
                           kr1_.name(), kr2_.name(), fake_kms_->listen_addr());

    init_args_ = {0};
    init_args_.pReserved = const_cast<char*>(config_file_.c_str());
  }

  void TearDown() override { std::remove(config_file_.c_str()); }

  std::unique_ptr<FakeKms> fake_kms_;
  kms_v1::KeyRing kr1_;
  kms_v1::KeyRing kr2_;
  std::string config_file_;
  CK_C_INITIALIZE_ARGS init_args_;
};

TEST_F(BridgeTest, InitializeFromArgs) {
  EXPECT_OK(Initialize(&init_args_));
  EXPECT_OK(Finalize(nullptr));
}

TEST_F(BridgeTest, InitializeFailsOnSecondCall) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(Initialize(&init_args_),
              StatusRvIs(CKR_CRYPTOKI_ALREADY_INITIALIZED));
}

TEST_F(BridgeTest, InitializeFromEnvironment) {
  SetEnvVariable(kConfigEnvVariable, config_file_);
  Cleanup c([]() { ClearEnvVariable(kConfigEnvVariable); });

  EXPECT_OK(Initialize(nullptr));
  // Finalize so that other tests see an uninitialized state
  EXPECT_OK(Finalize(nullptr));
}

TEST_F(BridgeTest, InitArgsWithoutReservedLoadsFromEnv) {
  SetEnvVariable(kConfigEnvVariable, config_file_);
  Cleanup c([]() { ClearEnvVariable(kConfigEnvVariable); });

  CK_C_INITIALIZE_ARGS init_args = {0};
  EXPECT_OK(Initialize(&init_args));
  // Finalize so that other tests see an uninitialized state
  EXPECT_OK(Finalize(nullptr));
}

TEST_F(BridgeTest, InitializeFailsWithoutConfig) {
  EXPECT_THAT(Initialize(nullptr),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(BridgeTest, InitializeFailsWithArgsNoConfig) {
  CK_C_INITIALIZE_ARGS init_args = {0};
  EXPECT_THAT(Initialize(&init_args),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(BridgeTest, FinalizeFailsWithoutInitialize) {
  EXPECT_THAT(Finalize(nullptr), StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetInfoSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  CK_INFO info;
  EXPECT_OK(GetInfo(&info));
  EXPECT_OK(Finalize(nullptr));
}

TEST_F(BridgeTest, GetInfoFailsWithoutInitialize) {
  EXPECT_THAT(GetInfo(nullptr), StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetInfoFailsNullPtr) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(GetInfo(nullptr), StatusRvIs(CKR_ARGUMENTS_BAD));
}

TEST_F(BridgeTest, GetFunctionListSuccess) {
  CK_FUNCTION_LIST* function_list;
  EXPECT_OK(GetFunctionList(&function_list));
}

TEST_F(BridgeTest, FunctionListValidPointers) {
  CK_FUNCTION_LIST* f;
  EXPECT_OK(GetFunctionList(&f));

  EXPECT_EQ(f->C_Initialize(&init_args_), CKR_OK);
  CK_INFO info;
  EXPECT_EQ(f->C_GetInfo(&info), CKR_OK);
  EXPECT_EQ(f->C_Finalize(nullptr), CKR_OK);
}

TEST_F(BridgeTest, GetFunctionListFailsNullPtr) {
  EXPECT_THAT(GetFunctionList(nullptr), StatusRvIs(CKR_ARGUMENTS_BAD));
}

TEST_F(BridgeTest, GetSlotListFailsNotInitialized) {
  EXPECT_THAT(GetSlotList(false, nullptr, nullptr),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetSlotListReturnsSlots) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  std::vector<CK_SLOT_ID> slots(2);
  CK_ULONG slots_size = slots.size();
  EXPECT_OK(GetSlotList(false, slots.data(), &slots_size));
  EXPECT_EQ(slots_size, 2);
  EXPECT_THAT(slots, ElementsAre(0, 1));
}

TEST_F(BridgeTest, GetSlotListReturnsSize) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_ULONG slots_size;
  EXPECT_OK(GetSlotList(false, nullptr, &slots_size));
  EXPECT_EQ(slots_size, 2);
}

TEST_F(BridgeTest, GetSlotListFailsBufferTooSmall) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  std::vector<CK_SLOT_ID> slots(1);
  CK_ULONG slots_size = slots.size();
  EXPECT_THAT(GetSlotList(false, slots.data(), &slots_size),
              StatusRvIs(CKR_BUFFER_TOO_SMALL));
  EXPECT_EQ(slots_size, 2);
}

TEST_F(BridgeTest, GetSlotInfoSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SLOT_INFO info;
  EXPECT_OK(GetSlotInfo(0, &info));

  // Sanity check for any piece of information we set
  EXPECT_EQ(info.flags & CKF_TOKEN_PRESENT, CKF_TOKEN_PRESENT);
}

TEST_F(BridgeTest, GetSlotInfoFailsNotInitialized) {
  EXPECT_THAT(GetSlotInfo(0, nullptr),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetSlotInfoFailsInvalidSlotId) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(GetSlotInfo(2, nullptr), StatusRvIs(CKR_SLOT_ID_INVALID));
}

TEST_F(BridgeTest, GetTokenInfoSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_TOKEN_INFO info;
  EXPECT_OK(GetTokenInfo(0, &info));

  // Sanity check for any piece of information we set
  EXPECT_EQ(info.flags & CKF_TOKEN_INITIALIZED, CKF_TOKEN_INITIALIZED);
}

TEST_F(BridgeTest, GetTokenInfoFailsNotInitialized) {
  EXPECT_THAT(GetTokenInfo(0, nullptr),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetTokenInfoFailsInvalidSlotId) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(GetTokenInfo(2, nullptr), StatusRvIs(CKR_SLOT_ID_INVALID));
}

TEST_F(BridgeTest, OpenSession) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));
  EXPECT_NE(handle, CK_INVALID_HANDLE);
}

TEST_F(BridgeTest, OpenSessionFailsNotInitialized) {
  CK_SESSION_HANDLE handle;
  EXPECT_THAT(OpenSession(0, 0, nullptr, nullptr, &handle),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, OpenSessionFailsInvalidSlotId) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_THAT(OpenSession(2, CKF_SERIAL_SESSION, nullptr, nullptr, &handle),
              StatusRvIs(CKR_SLOT_ID_INVALID));
}

TEST_F(BridgeTest, OpenSessionFailsNotSerial) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_THAT(OpenSession(0, 0, nullptr, nullptr, &handle),
              StatusRvIs(CKR_SESSION_PARALLEL_NOT_SUPPORTED));
}

TEST_F(BridgeTest, OpenSessionFailsReadWrite) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_THAT(OpenSession(0, CKF_SERIAL_SESSION | CKF_RW_SESSION, nullptr,
                          nullptr, &handle),
              StatusRvIs(CKR_TOKEN_WRITE_PROTECTED));
}

TEST_F(BridgeTest, CloseSessionSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));
  EXPECT_OK(CloseSession(handle));
}

TEST_F(BridgeTest, CloseSessionFailsNotInitialized) {
  EXPECT_THAT(CloseSession(0), StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, CloseSessionFailsInvalidHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));
  EXPECT_THAT(CloseSession(0), StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, CloseSessionFailsAlreadyClosed) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));
  EXPECT_OK(CloseSession(handle));

  EXPECT_THAT(CloseSession(handle), StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, GetSessionInfoSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));

  CK_SESSION_INFO info;
  EXPECT_OK(GetSessionInfo(handle, &info));

  // Sanity check for any piece of information
  EXPECT_EQ(info.state, CKS_RO_PUBLIC_SESSION);
}

TEST_F(BridgeTest, GetSessionInfoFailsNotInitialized) {
  CK_SESSION_INFO info;
  EXPECT_THAT(GetSessionInfo(0, &info),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetSessionInfoFailsInvalidHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_INFO info;
  EXPECT_THAT(GetSessionInfo(0, &info), StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, LoginSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));

  EXPECT_OK(Login(handle, CKU_USER, nullptr, 0));

  CK_SESSION_INFO info;
  EXPECT_OK(GetSessionInfo(handle, &info));
  EXPECT_EQ(info.state, CKS_RO_USER_FUNCTIONS);
}

TEST_F(BridgeTest, LoginAppliesToAllSessions) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle1;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle1));

  CK_SESSION_HANDLE handle2;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle2));

  EXPECT_OK(Login(handle2, CKU_USER, nullptr, 0));

  EXPECT_THAT(Login(handle1, CKU_USER, nullptr, 0),
              StatusRvIs(CKR_USER_ALREADY_LOGGED_IN));
  CK_SESSION_INFO info;
  EXPECT_OK(GetSessionInfo(handle1, &info));
  EXPECT_EQ(info.state, CKS_RO_USER_FUNCTIONS);
}

TEST_F(BridgeTest, LoginFailsNotInitialized) {
  EXPECT_THAT(Login(0, CKU_USER, nullptr, 0),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, LoginFailsInvalidHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(Login(0, CKU_USER, nullptr, 0),
              StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, LoginFailsUserSo) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));

  EXPECT_THAT(Login(handle, CKU_SO, nullptr, 0), StatusRvIs(CKR_PIN_LOCKED));
}

TEST_F(BridgeTest, LogoutSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));

  EXPECT_OK(Login(handle, CKU_USER, nullptr, 0));
  EXPECT_OK(Logout(handle));

  CK_SESSION_INFO info;
  EXPECT_OK(GetSessionInfo(handle, &info));
  EXPECT_EQ(info.state, CKS_RO_PUBLIC_SESSION);
}

TEST_F(BridgeTest, LogoutAppliesToAllSessions) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle1;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle1));

  CK_SESSION_HANDLE handle2;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle2));

  EXPECT_OK(Login(handle2, CKU_USER, nullptr, 0));
  EXPECT_OK(Logout(handle1));

  EXPECT_THAT(Logout(handle2), StatusRvIs(CKR_USER_NOT_LOGGED_IN));
  CK_SESSION_INFO info;
  EXPECT_OK(GetSessionInfo(handle2, &info));
  EXPECT_EQ(info.state, CKS_RO_PUBLIC_SESSION);
}

TEST_F(BridgeTest, LogoutFailsNotInitialized) {
  EXPECT_THAT(Logout(0), StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, LogoutFailsInvalidHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(Logout(0), StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, LogoutFailsNotLoggedIn) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));

  EXPECT_THAT(Logout(handle), StatusRvIs(CKR_USER_NOT_LOGGED_IN));
}

TEST_F(BridgeTest, LogoutFailsSecondCall) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE handle;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &handle));

  EXPECT_OK(Login(handle, CKU_USER, nullptr, 0));
  EXPECT_OK(Logout(handle));

  EXPECT_THAT(Logout(handle), StatusRvIs(CKR_USER_NOT_LOGGED_IN));
}

TEST_F(BridgeTest, GetMechanismListSucceeds) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_ULONG count;
  EXPECT_OK(GetMechanismList(0, nullptr, &count));

  std::vector<CK_MECHANISM_TYPE> types(count);
  EXPECT_OK(GetMechanismList(0, types.data(), &count));
  EXPECT_EQ(types.size(), count);
  EXPECT_THAT(types, IsSupersetOf({CKM_RSA_PKCS, CKM_RSA_PKCS_PSS,
                                   CKM_RSA_PKCS_OAEP, CKM_ECDSA}));
}

TEST_F(BridgeTest, GetMechanismListFailsInvalidSize) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  std::vector<CK_MECHANISM_TYPE> types(1);
  CK_ULONG count = 1;
  EXPECT_THAT(GetMechanismList(0, types.data(), &count),
              StatusRvIs(CKR_BUFFER_TOO_SMALL));
  EXPECT_THAT(count, Ge(4));
}

TEST_F(BridgeTest, GetMechanismListFailsNotInitialized) {
  CK_ULONG count;
  EXPECT_THAT(GetMechanismList(0, nullptr, &count),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetMechanismListFailsInvalidSlotId) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_ULONG count;
  EXPECT_THAT(GetMechanismList(5, nullptr, &count),
              StatusRvIs(CKR_SLOT_ID_INVALID));
}

TEST_F(BridgeTest, GetMechanismInfo) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_MECHANISM_INFO info;
  EXPECT_OK(GetMechanismInfo(0, CKM_RSA_PKCS_PSS, &info));

  EXPECT_EQ(info.ulMinKeySize, 2048);
  EXPECT_EQ(info.ulMaxKeySize, 4096);
  EXPECT_EQ(info.flags, CKF_SIGN);
}

TEST_F(BridgeTest, GetMechanismInfoFailsInvalidMechanism) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_MECHANISM_INFO info;
  EXPECT_THAT(GetMechanismInfo(0, CKM_RSA_X9_31, &info),
              StatusRvIs(CKR_MECHANISM_INVALID));
}

TEST_F(BridgeTest, GetMechanismInfoFailsNotInitialized) {
  CK_MECHANISM_INFO info;
  EXPECT_THAT(GetMechanismInfo(0, CKM_RSA_PKCS, &info),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetMechanismInfoFailsInvalidSlotId) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_MECHANISM_INFO info;
  EXPECT_THAT(GetMechanismInfo(5, CKM_RSA_PKCS_PSS, &info),
              StatusRvIs(CKR_SLOT_ID_INVALID));
}

TEST_F(BridgeTest, GetAttributeValueSuccess) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1));

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  CK_KEY_TYPE key_type;
  CK_ATTRIBUTE key_type_attr = {CKA_KEY_TYPE, &key_type, sizeof(key_type)};
  EXPECT_OK(GetAttributeValue(session, object, &key_type_attr, 1));
  EXPECT_EQ(key_type, CKK_EC);
}

TEST_F(BridgeTest, GetAttributeValueFailsSensitiveAttribute) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1));

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  char key_value[256];
  CK_ATTRIBUTE value_attr = {CKA_VALUE, key_value, 256};
  EXPECT_THAT(GetAttributeValue(session, object, &value_attr, 1),
              StatusRvIs(CKR_ATTRIBUTE_SENSITIVE));
  EXPECT_EQ(value_attr.ulValueLen, CK_UNAVAILABLE_INFORMATION);
}

TEST_F(BridgeTest, GetAttributeValueFailsNonExistentAttribute) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1));

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  char modulus[256];
  CK_ATTRIBUTE mod_attr = {CKA_MODULUS, modulus, 256};
  EXPECT_THAT(GetAttributeValue(session, object, &mod_attr, 1),
              StatusRvIs(CKR_ATTRIBUTE_TYPE_INVALID));
  EXPECT_EQ(mod_attr.ulValueLen, CK_UNAVAILABLE_INFORMATION);
}

TEST_F(BridgeTest, GetAttributeValueSuccessNoBuffer) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1))

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  CK_ATTRIBUTE public_key = {CKA_PUBLIC_KEY_INFO, nullptr, 0};
  EXPECT_OK(GetAttributeValue(session, object, &public_key, 1));
}

TEST_F(BridgeTest, GetAttributeValueFailureBufferTooShort) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1))

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  char buf[2];
  CK_ATTRIBUTE ec_params = {CKA_EC_PARAMS, buf, 2};
  EXPECT_THAT(GetAttributeValue(session, object, &ec_params, 1),
              StatusRvIs(CKR_BUFFER_TOO_SMALL));
  EXPECT_GT(ec_params.ulValueLen, 2);
}

TEST_F(BridgeTest, GetAttributeValueFailureAllAttributesProcessed) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1))

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  CK_BBOOL decrypt, token;
  char value_buf[2], point_buf[2], modulus_buf[2];
  CK_ATTRIBUTE attr_results[5] = {
      {CKA_DECRYPT, &decrypt, sizeof(decrypt)},
      {CKA_VALUE, value_buf, sizeof(value_buf)},
      {CKA_EC_POINT, point_buf, sizeof(point_buf)},
      {CKA_MODULUS, modulus_buf, sizeof(modulus_buf)},
      {CKA_TOKEN, &token, sizeof(token)},
  };

  EXPECT_THAT(GetAttributeValue(session, object, attr_results, 5),
              StatusRvIs(AnyOf(CKR_BUFFER_TOO_SMALL, CKR_ATTRIBUTE_SENSITIVE,
                               CKR_ATTRIBUTE_TYPE_INVALID)));

  // All valid attributes with sufficient buffer space were processed.
  EXPECT_EQ(decrypt, CK_FALSE);
  EXPECT_EQ(attr_results[0].ulValueLen, 1);
  EXPECT_EQ(token, CK_TRUE);
  EXPECT_EQ(attr_results[4].ulValueLen, 1);

  // Sensitive attribute is unavailable.
  EXPECT_EQ(attr_results[1].ulValueLen, CK_UNAVAILABLE_INFORMATION);
  // Buffer too small attribute is unavailable.
  EXPECT_THAT(attr_results[2].ulValueLen, CK_UNAVAILABLE_INFORMATION);
  // Not found attribute is unavailable.
  EXPECT_EQ(attr_results[3].ulValueLen, CK_UNAVAILABLE_INFORMATION);
}

TEST_F(BridgeTest, GetAttributeValueFailureNotInitialized) {
  EXPECT_THAT(GetAttributeValue(0, 0, nullptr, 0),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, GetAttributeValueFailureInvalidSessionHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(GetAttributeValue(0, 0, nullptr, 0),
              StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, GetAttributeValueFailureInvalidObjectHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_THAT(GetAttributeValue(session, 0, nullptr, 0),
              StatusRvIs(CKR_OBJECT_HANDLE_INVALID));
}

TEST_F(BridgeTest, GetAttributeValueFailureNullTemplate) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv1;
  ckv1 = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv1);
  ckv1 = WaitForEnablement(fake_client.get(), ckv1);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr_template = {CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1))

  CK_OBJECT_HANDLE object;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &object, 1, &found_count));
  EXPECT_EQ(found_count, 1);

  EXPECT_THAT(GetAttributeValue(session, object, nullptr, 1),
              StatusRvIs(CKR_ARGUMENTS_BAD));
}

TEST_F(BridgeTest, FindEcPrivateKey) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey_CryptoKeyPurpose_ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion_CryptoKeyVersionAlgorithm_EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv;
  ckv = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv);
  ckv = WaitForEnablement(fake_client.get(), ckv);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_EC;
  std::vector<CK_ATTRIBUTE> attrs({
      {CKA_CLASS, &obj_class, sizeof(obj_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
  });
  EXPECT_OK(FindObjectsInit(session, &attrs[0], attrs.size()));

  CK_OBJECT_HANDLE handles[2];
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &handles[0], 2, &found_count));
  EXPECT_EQ(found_count, 1);

  char label[2];
  std::vector<CK_ATTRIBUTE> found_attrs({
      {CKA_CLASS, &obj_class, sizeof(obj_class)},
      {CKA_LABEL, label, 2},
  });
  EXPECT_OK(GetAttributeValue(session, handles[0], found_attrs.data(), 2));

  EXPECT_EQ(obj_class, CKO_PRIVATE_KEY);
  EXPECT_EQ(std::string(label, 2), "ck");

  EXPECT_OK(FindObjectsFinal(session));
}

TEST_F(BridgeTest, FindCertificate) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey::ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion::EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv;
  ckv = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv);
  ckv = WaitForEnablement(fake_client.get(), ckv);

  std::ofstream(config_file_, std::ofstream::out | std::ofstream::app)
      << "generate_certs: true" << std::endl;

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_CERTIFICATE;
  CK_ATTRIBUTE attr_template{CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1));

  CK_OBJECT_HANDLE handles[2];
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &handles[0], 2, &found_count));
  EXPECT_EQ(found_count, 1);
}

TEST_F(BridgeTest, NoCertificatesWhenConfigNotSet) {
  auto fake_client = fake_kms_->NewClient();

  kms_v1::CryptoKey ck;
  ck.set_purpose(kms_v1::CryptoKey::ASYMMETRIC_SIGN);
  ck.mutable_version_template()->set_algorithm(
      kms_v1::CryptoKeyVersion::EC_SIGN_P256_SHA256);
  ck = CreateCryptoKeyOrDie(fake_client.get(), kr1_.name(), "ck", ck, true);

  kms_v1::CryptoKeyVersion ckv;
  ckv = CreateCryptoKeyVersionOrDie(fake_client.get(), ck.name(), ckv);
  ckv = WaitForEnablement(fake_client.get(), ckv);

  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_CLASS obj_class = CKO_CERTIFICATE;
  CK_ATTRIBUTE attr_template{CKA_CLASS, &obj_class, sizeof(obj_class)};
  EXPECT_OK(FindObjectsInit(session, &attr_template, 1));

  CK_OBJECT_HANDLE handle;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &handle, 1, &found_count));
  EXPECT_EQ(found_count, 0);
}

TEST_F(BridgeTest, FindObjectsInitSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_OK(FindObjectsInit(session, nullptr, 0));
}

TEST_F(BridgeTest, FindObjectsInitFailsNotInitialized) {
  EXPECT_THAT(FindObjectsInit(0, nullptr, 0),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, FindObjectsInitFailsInvalidSessionHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(FindObjectsInit(0, nullptr, 0),
              StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, FindObjectsInitFailsAttributeTemplateNullptr) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_THAT(FindObjectsInit(session, nullptr, 1),
              StatusRvIs(CKR_ARGUMENTS_BAD));
}

TEST_F(BridgeTest, FindObjectsInitFailsAlreadyInitialized) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_OK(FindObjectsInit(session, nullptr, 0));
  EXPECT_THAT(FindObjectsInit(session, nullptr, 0),
              StatusRvIs(CKR_OPERATION_ACTIVE));
}

TEST_F(BridgeTest, FindObjectsSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_OK(FindObjectsInit(session, nullptr, 0));

  CK_OBJECT_HANDLE handle;
  CK_ULONG found_count;
  EXPECT_OK(FindObjects(session, &handle, 1, &found_count));
  EXPECT_EQ(found_count, 0);
}

TEST_F(BridgeTest, FindObjectsFailsNotInitialized) {
  EXPECT_THAT(FindObjects(0, nullptr, 0, nullptr),
              StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, FindObjectsFailsInvalidSessionHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(FindObjects(0, nullptr, 0, nullptr),
              StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, FindObjectsFailsPhObjectNull) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_OK(FindObjectsInit(session, nullptr, 0));

  CK_ULONG found_count;
  EXPECT_THAT(FindObjects(session, nullptr, 0, &found_count),
              StatusRvIs(CKR_ARGUMENTS_BAD));
}

TEST_F(BridgeTest, FindObjectsFailsPulCountNull) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_OK(FindObjectsInit(session, nullptr, 0));

  CK_OBJECT_HANDLE handles[1];
  EXPECT_THAT(FindObjects(session, &handles[0], 1, nullptr),
              StatusRvIs(CKR_ARGUMENTS_BAD));
}

TEST_F(BridgeTest, FindObjectsFailsOperationNotInitialized) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  CK_OBJECT_HANDLE obj_handle;
  CK_ULONG found_count;
  EXPECT_THAT(FindObjects(session, &obj_handle, 1, &found_count),
              StatusRvIs(CKR_OPERATION_NOT_INITIALIZED));
}

TEST_F(BridgeTest, FindObjectsFinalSuccess) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_OK(FindObjectsInit(session, nullptr, 0));
  EXPECT_OK(FindObjectsFinal(session));
}

TEST_F(BridgeTest, FindObjectsFinalFailsNotInitialized) {
  EXPECT_THAT(FindObjectsFinal(0), StatusRvIs(CKR_CRYPTOKI_NOT_INITIALIZED));
}

TEST_F(BridgeTest, FindObjectsFinalFailsInvalidSessionHandle) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  EXPECT_THAT(FindObjectsFinal(0), StatusRvIs(CKR_SESSION_HANDLE_INVALID));
}

TEST_F(BridgeTest, FindObjectsFinalFailsOperationNotInitialized) {
  EXPECT_OK(Initialize(&init_args_));
  Cleanup c([]() { EXPECT_OK(Finalize(nullptr)); });

  CK_SESSION_HANDLE session;
  EXPECT_OK(OpenSession(0, CKF_SERIAL_SESSION, nullptr, nullptr, &session));

  EXPECT_THAT(FindObjectsFinal(session),
              StatusRvIs(CKR_OPERATION_NOT_INITIALIZED));
}

}  // namespace
}  // namespace kmsp11