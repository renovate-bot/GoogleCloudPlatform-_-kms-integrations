package contract

import (
	"context"

	"testing"
	"time"

	"github.com/golang/protobuf/ptypes"
	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	kmspb "google.golang.org/genproto/googleapis/cloud/kms/v1"
	fmpb "google.golang.org/genproto/protobuf/field_mask"
)

func TestDestroyEnabledCryptoKeyVersion(t *testing.T) {
	ctx := context.Background()
	kr := client.CreateTestKR(ctx, t, &kmspb.CreateKeyRingRequest{Parent: location})
	ck := client.CreateTestCK(ctx, t, &kmspb.CreateCryptoKeyRequest{
		Parent: kr.Name,
		CryptoKey: &kmspb.CryptoKey{
			Purpose: kmspb.CryptoKey_ENCRYPT_DECRYPT,
		},
	})
	ckv := ck.Primary

	got, err := client.DestroyCryptoKeyVersion(ctx, &kmspb.DestroyCryptoKeyVersionRequest{
		Name: ckv.Name,
	})
	if err != nil {
		t.Fatal(err)
	}

	expectedDestroyTime := time.Now().Add(24 * time.Hour).Truncate(time.Microsecond)

	ckv.State = kmspb.CryptoKeyVersion_DESTROY_SCHEDULED
	ckv.DestroyTime, err = ptypes.TimestampProto(expectedDestroyTime)
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(ckv, got, ProtoDiffOpts()...); diff != "" {
		t.Errorf("ckv proto mismatch on destroy RPC (-want +got): %s", diff)
	}
}

func TestDestroyDisabledCryptoKeyVersion(t *testing.T) {
	ctx := context.Background()
	kr := client.CreateTestKR(ctx, t, &kmspb.CreateKeyRingRequest{Parent: location})
	ck := client.CreateTestCK(ctx, t, &kmspb.CreateCryptoKeyRequest{
		Parent: kr.Name,
		CryptoKey: &kmspb.CryptoKey{
			Purpose: kmspb.CryptoKey_ENCRYPT_DECRYPT,
		},
	})

	// update state to disabled
	ck.Primary.State = kmspb.CryptoKeyVersion_DISABLED
	ckv, err := client.UpdateCryptoKeyVersion(ctx, &kmspb.UpdateCryptoKeyVersionRequest{
		CryptoKeyVersion: ck.Primary,
		UpdateMask: &fmpb.FieldMask{
			Paths: []string{"state"},
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	got, err := client.DestroyCryptoKeyVersion(ctx, &kmspb.DestroyCryptoKeyVersionRequest{
		Name: ckv.Name,
	})
	if err != nil {
		t.Fatal(err)
	}

	expectedDestroyTime := time.Now().Add(24 * time.Hour).Truncate(time.Microsecond)

	ckv.State = kmspb.CryptoKeyVersion_DESTROY_SCHEDULED
	ckv.DestroyTime, err = ptypes.TimestampProto(expectedDestroyTime)
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(ckv, got, ProtoDiffOpts()...); diff != "" {
		t.Errorf("ckv proto mismatch on destroy RPC (-want +got): %s", diff)
	}
}

func TestDestroyCryptoKeyVersionUnsupportedState(t *testing.T) {
	ctx := context.Background()
	kr := client.CreateTestKR(ctx, t, &kmspb.CreateKeyRingRequest{Parent: location})
	ck := client.CreateTestCK(ctx, t, &kmspb.CreateCryptoKeyRequest{
		Parent: kr.Name,
		CryptoKey: &kmspb.CryptoKey{
			Purpose: kmspb.CryptoKey_ENCRYPT_DECRYPT,
		},
	})

	destroyReq := &kmspb.DestroyCryptoKeyVersionRequest{
		Name: ck.Primary.Name,
	}

	if _, err := client.DestroyCryptoKeyVersion(ctx, destroyReq); err != nil {
		t.Fatal(err)
	}

	_, err := client.DestroyCryptoKeyVersion(ctx, destroyReq)
	if status.Code(err) != codes.FailedPrecondition {
		t.Errorf("err=%v, want code=%s", err, codes.FailedPrecondition)
	}
}

func TestDestroyCryptoKeyVersionNotFound(t *testing.T) {
	ctx := context.Background()
	kr := client.CreateTestKR(ctx, t, &kmspb.CreateKeyRingRequest{Parent: location})
	ck := client.CreateTestCK(ctx, t, &kmspb.CreateCryptoKeyRequest{
		Parent: kr.Name,
		CryptoKey: &kmspb.CryptoKey{
			Purpose: kmspb.CryptoKey_ENCRYPT_DECRYPT,
		},
		SkipInitialVersionCreation: true,
	})

	_, err := client.DestroyCryptoKeyVersion(ctx, &kmspb.DestroyCryptoKeyVersionRequest{
		Name: ck.Name + "/cryptoKeyVersions/1",
	})
	if status.Code(err) != codes.NotFound {
		t.Errorf("err=%v, want code=%s", err, codes.NotFound)
	}
}