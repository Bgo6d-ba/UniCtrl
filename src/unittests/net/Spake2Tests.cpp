/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Spake2Tests.h"

#include "net/Spake2.h"

using deskflow::Spake2;

namespace {

// pA of the first RFC 9382 vector, a valid point to derive broken encodings from
const auto kPoint = QByteArray::fromHex(
    "04a56fa807caaa53a4d28dbb9853b9815c61a411118a6fe516a8798434751470"
    "f9010153ac33d0d5f2047ffdb1a3e42c9b4e6be662766e1eeb4116988ede5f912c"
);

// RFC 9382 M and N for P-256, uncompressed
const auto kM = QByteArray::fromHex(
    "04886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f"
    "5ff355163e43ce224e0b0e65ff02ac8e5c7be09419c785e0ca547d55a12e2d20"
);
const auto kN = QByteArray::fromHex(
    "04d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49"
    "07d60aa6bfade45008a636337f5168c64d9bd36034808cd564490b1e656edbe7"
);

const auto kOrder = QByteArray::fromHex("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551");

struct Vector
{
  const char *idA;
  const char *idB;
  const char *w;
  const char *x;
  const char *y;
  const char *pA;
  const char *pB;
  const char *ke;
  const char *confA;
  const char *confB;
};

// RFC 9382 appendix B
constexpr Vector kVectors[] = {
    {
        .idA = "server",
        .idB = "client",
        .w = "2ee57912099d31560b3a44b1184b9b4866e904c49d12ac5042c97dca461b1a5f",
        .x = "43dd0fd7215bdcb482879fca3220c6a968e66d70b1356cac18bb26c84a78d729",
        .y = "dcb60106f276b02606d8ef0a328c02e4b629f84f89786af5befb0bc75b6e66be",
        .pA = "04a56fa807caaa53a4d28dbb9853b9815c61a411118a6fe516a8798434751470f9"
              "010153ac33d0d5f2047ffdb1a3e42c9b4e6be662766e1eeb4116988ede5f912c",
        .pB = "0406557e482bd03097ad0cbaa5df82115460d951e3451962f1eaf4367a420676d0"
              "9857ccbc522686c83d1852abfa8ed6e4a1155cf8f1543ceca528afb591a1e0b7",
        .ke = "0e0672dc86f8e45565d338b0540abe69",
        .confA = "58ad4aa88e0b60d5061eb6b5dd93e80d9c4f00d127c65b3b35b1b5281fee38f0",
        .confB = "d3e2e547f1ae04f2dbdbf0fc4b79f8ecff2dff314b5d32fe9fcef2fb26dc459b",
    },
    {
        .idA = "",
        .idB = "client",
        .w = "0548d8729f730589e579b0475a582c1608138ddf7054b73b5381c7e883e2efae",
        .x = "403abbe3b1b4b9ba17e3032849759d723939a27a27b9d921c500edde18ed654b",
        .y = "903023b6598908936ea7c929bd761af6039577a9c3f9581064187c3049d87065",
        .pA = "04a897b769e681c62ac1c2357319a3d363f610839c4477720d24cbe32f5fd85f44"
              "fb92ba966578c1b712be6962498834078262caa5b441ecfa9d4a9485720e918a",
        .pB = "04e0f816fd1c35e22065d5556215c097e799390d16661c386e0ecc84593974a61b"
              "881a8c82327687d0501862970c64565560cb5671f696048050ca66ca5f8cc7fc",
        .ke = "642f05c473c2cd79909f9a841e2f30a7",
        .confA = "47d29e6666af1b7dd450d571233085d7a9866e4d49d2645e2df975489521232b",
        .confB = "3313c5cefc361d27fb16847a91c2a73b766ffa90a4839122a9b70a2f6bd1d6df",
    },
    {
        .idA = "server",
        .idB = "",
        .w = "626e0cdc7b14c9db3e52a0b1b3a768c98e37852d5db30febe0497b14eae8c254",
        .x = "07adb3db6bc623d3399726bfdbfd3d15a58ea776ab8a308b00392621291f9633",
        .y = "b6a4fc8dbb629d4ba51d6f91ed1532cf87adec98f25dd153a75accafafedec16",
        .pA = "04f88fb71c99bfffaea370966b7eb99cd4be0ff1a7d335caac4211c4afd855e2e1"
              "5a873b298503ad8ba1d9cbb9a392d2ba309b48bfd7879aefd0f2cea6009763b0",
        .pB = "040c269d6be017dccb15182ac6bfcd9e2a14de019dd587eaf4bdfd353f031101e7"
              "cca177f8eb362a6e83e7d5e729c0732e1b528879c086f39ba0f31a9661bd34db",
        .ke = "005184ff460da2ce59062c87733c299c",
        .confA = "bc9f9bbe99f26d0b2260e6456e05a86196a3307ec6663a18bf6ac825736533b2",
        .confB = "c2370e1bf813b086dff0d834e74425a06e6390f48f5411900276dcccc5a297ec",
    },
    {
        .idA = "",
        .idB = "",
        .w = "7bf46c454b4c1b25799527d896508afd5fc62ef4ec59db1efb49113063d70cca",
        .x = "8cef65df64bb2d0f83540c53632de911b5b24b3eab6cc74a97609fd659e95473",
        .y = "d7a66f64074a84652d8d623a92e20c9675c61cb5b4f6a0063e4648a2fdc02d53",
        .pA = "04a65b367a3f613cf9f0654b1b28a1e3a8a40387956c8ba6063e8658563890f46c"
              "a1ef6a676598889fc28de2950ab8120b79a5ef1ea4c9f44bc98f585634b46d66",
        .pB = "04589f13218822710d98d8b2123a079041052d9941b9cf88c6617ddb2fcc049466"
              "2eea8ba6b64692dc318250030c6af045cb738bc81ba35b043c3dcb46adf6f58d",
        .ke = "fc6374762ba5cf11f4b2caa08b2cd1b9",
        .confA = "dfb4db8d48ae5a675963ea5e6c19d98d4ea028d8e898dad96ea19a80ade95dca",
        .confB = "d0f0609d1613138d354f7e95f19fb556bf52d751947241e8c7118df5ef0ae175",
    },
};

QByteArray smallScalar(char value)
{
  QByteArray scalar(Spake2::kScalarSize, '\0');
  scalar.back() = value;
  return scalar;
}

struct Outcome
{
  bool aAccepts = false;
  bool bAccepts = false;
  bool sameKey = false;
};

// Runs a whole exchange, where B may have been told other identities than A (as by something in between)
Outcome exchange(
    const QString &passwordA, const QString &passwordB, const QByteArray &idA = "a", const QByteArray &idB = "b",
    const QByteArray &idAAsSeenByB = "a", const QByteArray &idBAsSeenByB = "b"
)
{
  Spake2 a(Spake2::Role::A, Spake2::passwordScalar(passwordA));
  Spake2 b(Spake2::Role::B, Spake2::passwordScalar(passwordB));
  const auto pA = a.share();
  const auto pB = b.share();
  if (!a.finish(pB, idA, idB) || !b.finish(pA, idAAsSeenByB, idBAsSeenByB)) {
    return {};
  }
  return {a.verifyPeer(b.confirmation()), b.verifyPeer(a.confirmation()), a.sharedKey() == b.sharedKey()};
}

} // namespace

void Spake2Tests::rfc9382Vectors_data()
{
  QTest::addColumn<QByteArray>("idA");
  QTest::addColumn<QByteArray>("idB");
  QTest::addColumn<QByteArray>("w");
  QTest::addColumn<QByteArray>("x");
  QTest::addColumn<QByteArray>("y");
  QTest::addColumn<QByteArray>("pA");
  QTest::addColumn<QByteArray>("pB");
  QTest::addColumn<QByteArray>("ke");
  QTest::addColumn<QByteArray>("confA");
  QTest::addColumn<QByteArray>("confB");

  for (const auto &v : kVectors) {
    QTest::addRow("A='%s' B='%s'", v.idA, v.idB)
        << QByteArray(v.idA) << QByteArray(v.idB) << QByteArray::fromHex(v.w) << QByteArray::fromHex(v.x)
        << QByteArray::fromHex(v.y) << QByteArray::fromHex(v.pA) << QByteArray::fromHex(v.pB)
        << QByteArray::fromHex(v.ke) << QByteArray::fromHex(v.confA) << QByteArray::fromHex(v.confB);
  }
}

void Spake2Tests::rfc9382Vectors()
{
  QFETCH(QByteArray, idA);
  QFETCH(QByteArray, idB);
  QFETCH(QByteArray, w);
  QFETCH(QByteArray, x);
  QFETCH(QByteArray, y);
  QFETCH(QByteArray, pA);
  QFETCH(QByteArray, pB);
  QFETCH(QByteArray, ke);
  QFETCH(QByteArray, confA);
  QFETCH(QByteArray, confB);

  Spake2 a(Spake2::Role::A, w);
  Spake2 b(Spake2::Role::B, w);
  a.setScalarForTest(x);
  b.setScalarForTest(y);

  QCOMPARE(a.share().toHex(), pA.toHex());
  QCOMPARE(b.share().toHex(), pB.toHex());
  QVERIFY(a.finish(pB, idA, idB));
  QVERIFY(b.finish(pA, idA, idB));
  QCOMPARE(a.sharedKey().toHex(), ke.toHex());
  QCOMPARE(b.sharedKey().toHex(), ke.toHex());
  QCOMPARE(a.confirmation().toHex(), confA.toHex());
  QCOMPARE(b.confirmation().toHex(), confB.toHex());
  QVERIFY(a.verifyPeer(confB));
  QVERIFY(b.verifyPeer(confA));
}

void Spake2Tests::matchingPasswords_bothConfirm()
{
  const auto outcome = exchange(QStringLiteral("482915"), QStringLiteral("482915"));
  QVERIFY(outcome.aAccepts);
  QVERIFY(outcome.bAccepts);
  QVERIFY(outcome.sameKey);
}

void Spake2Tests::wrongPassword_bothFail()
{
  Spake2 a(Spake2::Role::A, Spake2::passwordScalar(QStringLiteral("482915")));
  Spake2 b(Spake2::Role::B, Spake2::passwordScalar(QStringLiteral("482916")));
  const auto pA = a.share();
  const auto pB = b.share();
  QVERIFY(a.finish(pB, "a", "b"));
  QVERIFY(b.finish(pA, "a", "b"));

  QVERIFY(!a.verifyPeer(b.confirmation()));
  QVERIFY(!b.verifyPeer(a.confirmation()));
  QVERIFY(a.sharedKey() != b.sharedKey());
}

void Spake2Tests::differentIdentities_bothFail_data()
{
  QTest::addColumn<QByteArray>("idAAsSeenByB");
  QTest::addColumn<QByteArray>("idBAsSeenByB");

  // the same password, and the shares are passed on as they were: only the identities were changed on the way
  QTest::newRow("client's identity changed") << QByteArray("mallory") << QByteArray("b");
  QTest::newRow("host's identity changed") << QByteArray("a") << QByteArray("mallory");
  QTest::newRow("both changed") << QByteArray("mallory") << QByteArray("mallory");
  QTest::newRow("prefix") << QByteArray("a ") << QByteArray("b");
  QTest::newRow("emptied") << QByteArray("") << QByteArray("b");
}

void Spake2Tests::differentIdentities_bothFail()
{
  QFETCH(QByteArray, idAAsSeenByB);
  QFETCH(QByteArray, idBAsSeenByB);

  const auto outcome =
      exchange(QStringLiteral("482915"), QStringLiteral("482915"), "a", "b", idAAsSeenByB, idBAsSeenByB);
  QVERIFY(!outcome.aAccepts);
  QVERIFY(!outcome.bAccepts);
  QVERIFY(!outcome.sameKey);
}

void Spake2Tests::invalidPeerShare_rejected_data()
{
  QTest::addColumn<QByteArray>("share");

  const auto x = kPoint.mid(1, 32);
  const auto y = kPoint.mid(33);
  auto offCurve = kPoint;
  offCurve.back() = static_cast<char>(offCurve.back() ^ 1);
  const auto fieldPrime = QByteArray::fromHex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff");

  QTest::newRow("empty") << QByteArray();
  QTest::newRow("identity") << QByteArray(1, '\0');
  QTest::newRow("zeros") << QByteArray(1, '\x04') + QByteArray(64, '\0');
  QTest::newRow("off curve") << offCurve;
  QTest::newRow("x not below p") << QByteArray(1, '\x04') + fieldPrime + y;
  QTest::newRow("compressed") << QByteArray(1, '\x02') + x;
  QTest::newRow("hybrid") << QByteArray(1, '\x06') + x + y;
  QTest::newRow("truncated") << kPoint.left(64);
  QTest::newRow("trailing byte") << kPoint + '\0';
}

void Spake2Tests::invalidPeerShare_rejected()
{
  QFETCH(QByteArray, share);

  for (const auto role : {Spake2::Role::A, Spake2::Role::B}) {
    Spake2 spake(role, Spake2::passwordScalar(QStringLiteral("482915")));
    QVERIFY(!spake.share().isEmpty());
    QVERIFY(!spake.finish(share, "a", "b"));
    QVERIFY(spake.confirmation().isEmpty());
    QVERIFY(spake.sharedKey().isEmpty());
  }
}

void Spake2Tests::identityK_rejected()
{
  // with w = 1, a peer share of N (to A) or M (to B) unblinds to the identity, so K would be too
  Spake2 a(Spake2::Role::A, smallScalar(1));
  QVERIFY(!a.finish(kN, "a", "b"));

  Spake2 b(Spake2::Role::B, smallScalar(1));
  QVERIFY(!b.finish(kM, "a", "b"));

  Spake2 control(Spake2::Role::A, smallScalar(2));
  QVERIFY(control.finish(kN, "a", "b"));
}

void Spake2Tests::invalidW_neverShares_data()
{
  QTest::addColumn<QByteArray>("w");

  QTest::newRow("empty") << QByteArray();
  QTest::newRow("short") << smallScalar(1).mid(1);
  QTest::newRow("zero") << smallScalar(0);
  QTest::newRow("order") << kOrder;
}

void Spake2Tests::invalidW_neverShares()
{
  QFETCH(QByteArray, w);

  Spake2 spake(Spake2::Role::A, w);
  QVERIFY(spake.share().isEmpty());
  QVERIFY(!spake.finish(kPoint, "a", "b"));
}

void Spake2Tests::passwordScalar_knownAnswer()
{
  // PBKDF2-HMAC-SHA256("123456", "deskflow-plus/pairing/v1", 10000, 64) mod n
  QCOMPARE(
      Spake2::passwordScalar(QStringLiteral("123456")).toHex(),
      QByteArray("075672c758b9d157c0932399118a48df84df6bd450d4aa7f3e6006a4dab7bd5e")
  );
  QCOMPARE(Spake2::passwordScalar(QStringLiteral("café")), Spake2::passwordScalar(QStringLiteral("café")));
  QVERIFY(Spake2::passwordScalar(QStringLiteral("123456")) != Spake2::passwordScalar(QStringLiteral("123457")));
}

void Spake2Tests::finish_onlyOnce()
{
  Spake2 a(Spake2::Role::A, Spake2::passwordScalar(QStringLiteral("482915")));
  Spake2 b(Spake2::Role::B, Spake2::passwordScalar(QStringLiteral("482915")));
  QVERIFY(!a.verifyPeer(QByteArray(Spake2::kMacSize, '\0')));
  QVERIFY(a.confirmation().isEmpty());

  const auto pA = a.share();
  QCOMPARE(a.share(), pA);
  QVERIFY(a.finish(b.share(), "a", "b"));
  QVERIFY(!a.finish(b.share(), "a", "b"));
  QVERIFY(b.finish(pA, "a", "b"));
  QVERIFY(a.verifyPeer(b.confirmation()));
}

QTEST_MAIN(Spake2Tests)
