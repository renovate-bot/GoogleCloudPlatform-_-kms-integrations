package contract

import (
	"context"
	"testing"

	"oss-tools/kmsp11/test/fakekms/testutil"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	kmspb "google.golang.org/genproto/googleapis/cloud/kms/v1"
)

func TestGetCryptoKeyVersionEqualsCreated(t *testing.T) {
	ctx := context.Background()
	kr := client.CreateTestKR(ctx, t, &kmspb.CreateKeyRingRequest{Parent: location})
	ck := client.CreateTestCK(ctx, t, &kmspb.CreateCryptoKeyRequest{
		Parent: kr.Name,
		CryptoKey: &kmspb.CryptoKey{
			Purpose: kmspb.CryptoKey_ENCRYPT_DECRYPT,
		},
		SkipInitialVersionCreation: true,
	})

	want := client.CreateTestCKVAndWait(ctx, t, &kmspb.CreateCryptoKeyVersionRequest{
		Parent: ck.Name,
	})

	got, err := client.GetCryptoKeyVersion(ctx, &kmspb.GetCryptoKeyVersionRequest{
		Name: want.Name,
	})
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(want, got, testutil.ProtoDiffOpts()...); diff != "" {
		t.Errorf("proto mismatch (-want +got): %s", diff)
	}
}

func TestGetCryptoKeyVersionMalformedName(t *testing.T) {
	ctx := context.Background()

	_, err := client.GetCryptoKeyVersion(ctx, &kmspb.GetCryptoKeyVersionRequest{
		Name: "malformed name",
	})
	if status.Code(err) != codes.InvalidArgument {
		t.Errorf("err=%v, want code=%s", err, codes.InvalidArgument)
	}
}

func TestGetCryptoKeyVersionNotFound(t *testing.T) {
	ctx := context.Background()
	kr := client.CreateTestKR(ctx, t, &kmspb.CreateKeyRingRequest{Parent: location})

	_, err := client.GetCryptoKeyVersion(ctx, &kmspb.GetCryptoKeyVersionRequest{
		Name: kr.Name + "/cryptoKeys/foo/cryptoKeyVersions/1",
	})
	if status.Code(err) != codes.NotFound {
		t.Errorf("err=%v, want code=%s", err, codes.NotFound)
	}
}
