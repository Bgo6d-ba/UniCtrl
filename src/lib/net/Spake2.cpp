/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Spake2.h"

#include <QtEndian>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/obj_mac.h>
#include <openssl/params.h>

#include <cstring>
#include <memory>

namespace deskflow {

namespace {

template <auto Free> struct Deleter
{
  template <typename T> void operator()(T *object) const
  {
    Free(object);
  }
};

using Bn = std::unique_ptr<BIGNUM, Deleter<BN_clear_free>>;
using BnCtx = std::unique_ptr<BN_CTX, Deleter<BN_CTX_free>>;
using Group = std::unique_ptr<EC_GROUP, Deleter<EC_GROUP_free>>;
using Point = std::unique_ptr<EC_POINT, Deleter<EC_POINT_clear_free>>;
using Kdf = std::unique_ptr<EVP_KDF, Deleter<EVP_KDF_free>>;
using KdfCtx = std::unique_ptr<EVP_KDF_CTX, Deleter<EVP_KDF_CTX_free>>;

constexpr int kScalarSize = Spake2::kScalarSize;
constexpr int kKeySize = Spake2::kKeySize;
constexpr int kPointSize = Spake2::kShareSize;
constexpr int kHashSize = 32;
constexpr int kPasswordHashSize = 64;
constexpr unsigned int kPbkdf2Iterations = 10000;
constexpr char kPbkdf2Salt[] = "deskflow-plus/pairing/v1";
constexpr char kConfirmationInfo[] = "ConfirmationKeys";

// RFC 9382 section 6: M and N for P-256, SEC1 compressed
constexpr char kM[] = "02886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f";
constexpr char kN[] = "03d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49";

const unsigned char *bytes(const QByteArray &data)
{
  return reinterpret_cast<const unsigned char *>(data.constData());
}

bool derive(const char *kdfName, const OSSL_PARAM *params, unsigned char *out, size_t size)
{
  Kdf kdf(EVP_KDF_fetch(nullptr, kdfName, nullptr));
  KdfCtx ctx(kdf ? EVP_KDF_CTX_new(kdf.get()) : nullptr);
  return ctx && EVP_KDF_derive(ctx.get(), out, size, params) == 1;
}

bool hmac(const unsigned char *key, const QByteArray &data, unsigned char *out)
{
  unsigned int size = 0;
  return HMAC(EVP_sha256(), key, kKeySize, bytes(data), data.size(), out, &size) && size == kHashSize;
}

// P-256 with a scratch context; the helpers return null when OpenSSL fails or an input is invalid
struct Curve
{
  Group group{EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1)};
  BnCtx ctx{BN_CTX_secure_new()};

  bool isValid() const
  {
    return group && ctx;
  }

  const BIGNUM *order() const
  {
    return EC_GROUP_get0_order(group.get());
  }

  // a secret scalar in [1, order)
  Bn scalar(const unsigned char *data) const
  {
    Bn value(BN_secure_new());
    if (!value || !BN_bin2bn(data, kScalarSize, value.get()) || BN_is_zero(value.get()) ||
        BN_cmp(value.get(), order()) >= 0) {
      return nullptr;
    }
    BN_set_flags(value.get(), BN_FLG_CONSTTIME);
    return value;
  }

  // a group element: a valid encoding, on the curve and not the identity (P-256 has cofactor 1)
  Point point(const QByteArray &encoded) const
  {
    Point point(EC_POINT_new(group.get()));
    if (!point || EC_POINT_oct2point(group.get(), point.get(), bytes(encoded), encoded.size(), ctx.get()) != 1 ||
        EC_POINT_is_at_infinity(group.get(), point.get()) ||
        EC_POINT_is_on_curve(group.get(), point.get(), ctx.get()) != 1) {
      return nullptr;
    }
    return point;
  }

  // k*base, or k*P without a base; one scalar per call keeps OpenSSL on its constant-time ladder
  Point mul(const BIGNUM *k, const EC_POINT *base = nullptr) const
  {
    Point result(EC_POINT_new(group.get()));
    const BIGNUM *generatorScalar = base ? nullptr : k;
    const BIGNUM *pointScalar = base ? k : nullptr;
    if (!result || EC_POINT_mul(group.get(), result.get(), generatorScalar, base, pointScalar, ctx.get()) != 1) {
      return nullptr;
    }
    return result;
  }

  Point add(const EC_POINT *a, const EC_POINT *b) const
  {
    Point result(EC_POINT_new(group.get()));
    if (!result || EC_POINT_add(group.get(), result.get(), a, b, ctx.get()) != 1) {
      return nullptr;
    }
    return result;
  }

  bool encode(const EC_POINT *point, unsigned char *out) const
  {
    return EC_POINT_point2oct(group.get(), point, POINT_CONVERSION_UNCOMPRESSED, out, kPointSize, ctx.get()) ==
           kPointSize;
  }
};

} // namespace

QByteArray Spake2::passwordScalar(const QString &password)
{
  auto utf8 = password.normalized(QString::NormalizationForm_C).toUtf8();
  std::array<unsigned char, kPasswordHashSize> hash{};
  unsigned int iterations = kPbkdf2Iterations;
  char digest[] = OSSL_DIGEST_NAME_SHA2_256;
  const OSSL_PARAM params[] = {
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, utf8.data(), utf8.size()),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char *>(kPbkdf2Salt), sizeof(kPbkdf2Salt) - 1),
      OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ITER, &iterations),
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
      OSSL_PARAM_construct_end(),
  };
  const bool derived = derive(OSSL_KDF_NAME_PBKDF2, params, hash.data(), hash.size());
  OPENSSL_cleanse(utf8.data(), utf8.size());

  QByteArray w;
  Curve curve;
  Bn wide(BN_secure_new());
  Bn reduced(BN_secure_new());
  if (wide && reduced) {
    BN_set_flags(wide.get(), BN_FLG_CONSTTIME);
  }
  if (derived && curve.isValid() && wide && reduced && BN_bin2bn(hash.data(), kPasswordHashSize, wide.get()) &&
      BN_nnmod(reduced.get(), wide.get(), curve.order(), curve.ctx.get()) == 1) {
    w.resize(kScalarSize);
    BN_bn2binpad(reduced.get(), reinterpret_cast<unsigned char *>(w.data()), kScalarSize);
  }
  OPENSSL_cleanse(hash.data(), hash.size());
  return w;
}

Spake2::Spake2(Role role, const QByteArray &w) : m_role(role), m_hasW(w.size() == kScalarSize)
{
  if (m_hasW) {
    std::memcpy(m_secrets.w.data(), w.constData(), kScalarSize);
  }
}

Spake2::~Spake2()
{
  OPENSSL_cleanse(&m_secrets, sizeof(m_secrets));
}

void Spake2::setScalarForTest(const QByteArray &scalar)
{
  if (scalar.size() == kScalarSize && m_share.isEmpty()) {
    std::memcpy(m_secrets.scalar.data(), scalar.constData(), kScalarSize);
    m_hasScalar = true;
  }
}

QByteArray Spake2::share()
{
  if (!m_share.isEmpty() || !m_hasW) {
    return m_share;
  }
  Curve curve;
  if (!curve.isValid()) {
    return {};
  }

  if (!m_hasScalar) {
    Bn random(BN_secure_new());
    do {
      if (!random || BN_priv_rand_range_ex(random.get(), curve.order(), 0, curve.ctx.get()) != 1) {
        return {};
      }
    } while (BN_is_zero(random.get()));
    if (BN_bn2binpad(random.get(), m_secrets.scalar.data(), kScalarSize) != kScalarSize) {
      return {};
    }
    m_hasScalar = true;
  }

  const auto scalar = curve.scalar(m_secrets.scalar.data());
  const auto w = curve.scalar(m_secrets.w.data());
  const auto blind = curve.point(QByteArray::fromHex(m_role == Role::A ? kM : kN));
  if (!scalar || !w || !blind) {
    return {};
  }
  const auto publicPart = curve.mul(scalar.get());
  const auto blinding = curve.mul(w.get(), blind.get());
  const auto share = publicPart && blinding ? curve.add(publicPart.get(), blinding.get()) : nullptr;
  QByteArray encoded(kPointSize, Qt::Uninitialized);
  if (share && curve.encode(share.get(), reinterpret_cast<unsigned char *>(encoded.data()))) {
    m_share = encoded;
  }
  return m_share;
}

bool Spake2::finish(const QByteArray &peerShare, const QByteArray &idA, const QByteArray &idB)
{
  if (m_used) {
    return false;
  }
  m_used = true;

  const auto mine = share();
  Curve curve;
  if (mine.isEmpty() || !curve.isValid() || peerShare.size() != kPointSize || peerShare.at(0) != 0x04) {
    return false;
  }
  const auto peer = curve.point(peerShare);
  const auto unblind = curve.point(QByteArray::fromHex(m_role == Role::A ? kN : kM));
  const auto scalar = curve.scalar(m_secrets.scalar.data());
  const auto w = curve.scalar(m_secrets.w.data());
  if (!peer || !unblind || !scalar || !w) {
    return false;
  }

  // K = h*x*(pB - w*N) for A and h*y*(pA - w*M) for B, where h = 1
  const auto blinding = curve.mul(w.get(), unblind.get());
  if (!blinding || EC_POINT_invert(curve.group.get(), blinding.get(), curve.ctx.get()) != 1) {
    return false;
  }
  const auto unblinded = curve.add(peer.get(), blinding.get());
  const auto k = unblinded ? curve.mul(scalar.get(), unblinded.get()) : nullptr;
  std::array<unsigned char, kPointSize> kBytes{};
  if (!k || EC_POINT_is_at_infinity(curve.group.get(), k.get()) || !curve.encode(k.get(), kBytes.data())) {
    OPENSSL_cleanse(kBytes.data(), kBytes.size());
    return false;
  }

  // TT = len(A) || A || len(B) || B || len(pA) || pA || len(pB) || pB || len(K) || K || len(w) || w
  const auto &pA = m_role == Role::A ? mine : peerShare;
  const auto &pB = m_role == Role::A ? peerShare : mine;
  QByteArray tt;
  // exactly sized, so appending never reallocates and leaves a copy of the secrets behind
  tt.reserve(6 * sizeof(quint64) + idA.size() + idB.size() + 3 * kPointSize + kScalarSize);
  const auto append = [&tt](const void *data, qsizetype size) {
    const auto length = qToLittleEndian<quint64>(size);
    tt.append(reinterpret_cast<const char *>(&length), sizeof(length));
    tt.append(static_cast<const char *>(data), size);
  };
  append(idA.constData(), idA.size());
  append(idB.constData(), idB.size());
  append(pA.constData(), pA.size());
  append(pB.constData(), pB.size());
  append(kBytes.data(), kPointSize);
  append(m_secrets.w.data(), kScalarSize);

  // Ke || Ka = Hash(TT), KcA || KcB = KDF(Ka, nil, "ConfirmationKeys" || AAD) with no AAD, cA = MAC(KcA, TT)
  std::array<unsigned char, kHashSize> hash{};
  std::array<unsigned char, 2 * kKeySize> confirmationKeys{};
  char digest[] = OSSL_DIGEST_NAME_SHA2_256;
  const OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, hash.data() + kKeySize, kKeySize),
      OSSL_PARAM_construct_octet_string(
          OSSL_KDF_PARAM_INFO, const_cast<char *>(kConfirmationInfo), sizeof(kConfirmationInfo) - 1
      ),
      OSSL_PARAM_construct_end(),
  };
  const auto *keyA = confirmationKeys.data();
  const auto *keyB = confirmationKeys.data() + kKeySize;
  const bool isA = m_role == Role::A;
  m_finished = EVP_Digest(tt.constData(), tt.size(), hash.data(), nullptr, EVP_sha256(), nullptr) == 1 &&
               derive(OSSL_KDF_NAME_HKDF, params, confirmationKeys.data(), confirmationKeys.size()) &&
               hmac(isA ? keyA : keyB, tt, m_secrets.mac.data()) &&
               hmac(isA ? keyB : keyA, tt, m_secrets.peerMac.data());
  std::memcpy(m_secrets.key.data(), hash.data(), kKeySize);

  OPENSSL_cleanse(tt.data(), tt.size());
  OPENSSL_cleanse(kBytes.data(), kBytes.size());
  OPENSSL_cleanse(hash.data(), hash.size());
  OPENSSL_cleanse(confirmationKeys.data(), confirmationKeys.size());
  return m_finished;
}

QByteArray Spake2::confirmation() const
{
  return m_finished ? QByteArray(reinterpret_cast<const char *>(m_secrets.mac.data()), kMacSize) : QByteArray();
}

bool Spake2::verifyPeer(const QByteArray &mac) const
{
  return m_finished && mac.size() == kMacSize &&
         CRYPTO_memcmp(mac.constData(), m_secrets.peerMac.data(), kMacSize) == 0;
}

QByteArray Spake2::sharedKey() const
{
  return m_finished ? QByteArray(reinterpret_cast<const char *>(m_secrets.key.data()), kKeySize) : QByteArray();
}

} // namespace deskflow
