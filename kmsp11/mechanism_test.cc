#include "kmsp11/mechanism.h"

#include "gmock/gmock.h"
#include "kmsp11/test/matchers.h"
#include "kmsp11/test/test_status_macros.h"

namespace kmsp11 {
namespace {

TEST(MechanismTest, SupportedMechanisms) {
  EXPECT_THAT(Mechanisms(),
              testing::ElementsAre(CKM_RSA_PKCS, CKM_RSA_PKCS_OAEP,
                                   CKM_RSA_PKCS_PSS, CKM_ECDSA));
}

TEST(MechanismTest, DecryptFlag) {
  ASSERT_OK_AND_ASSIGN(CK_MECHANISM_INFO info,
                       MechanismInfo(CKM_RSA_PKCS_OAEP));
  EXPECT_EQ(info.flags & CKF_DECRYPT, CKF_DECRYPT);
}

TEST(MechanismTest, SignFlag) {
  ASSERT_OK_AND_ASSIGN(CK_MECHANISM_INFO info, MechanismInfo(CKM_RSA_PKCS_PSS));
  EXPECT_EQ(info.flags & CKF_SIGN, CKF_SIGN);
}

TEST(MechanismTest, RsaMin2048) {
  ASSERT_OK_AND_ASSIGN(CK_MECHANISM_INFO info,
                       MechanismInfo(CKM_RSA_PKCS_OAEP));
  EXPECT_EQ(info.ulMinKeySize, 2048);
}

TEST(MechanismTest, RsaMax4096) {
  ASSERT_OK_AND_ASSIGN(CK_MECHANISM_INFO info, MechanismInfo(CKM_RSA_PKCS));
  EXPECT_EQ(info.ulMaxKeySize, 4096);
}

TEST(MechanismTest, EcMax384) {
  ASSERT_OK_AND_ASSIGN(CK_MECHANISM_INFO info, MechanismInfo(CKM_ECDSA));
  EXPECT_EQ(info.ulMaxKeySize, 384);
}

TEST(MechanismTest, EcFlags) {
  ASSERT_OK_AND_ASSIGN(CK_MECHANISM_INFO info, MechanismInfo(CKM_ECDSA));
  EXPECT_EQ(info.flags & CKF_EC_F_P, CKF_EC_F_P);
  EXPECT_EQ(info.flags & CKF_EC_NAMEDCURVE, CKF_EC_NAMEDCURVE);
  EXPECT_EQ(info.flags & CKF_EC_UNCOMPRESS, CKF_EC_UNCOMPRESS);
}

TEST(MechanismTest, UnsupportedMechanism) {
  EXPECT_THAT(MechanismInfo(CKM_AES_GCM),
              testing::AllOf(StatusIs(absl::StatusCode::kNotFound),
                             StatusRvIs(CKR_MECHANISM_INVALID)));
}

}  // namespace
}  // namespace kmsp11
