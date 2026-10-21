// Wi-Fi provisioning and the unattended update (docs/wifi-ota.md): the
// Improv codec and state machine, the manifest and its verdict, the
// remembered network, the power inference, the schedule, and the update
// itself through the same ota::Session the Bluetooth path uses.
#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "links.h"
#include "paint/canvas.h"   // so PlatformIO's dependency finder links lib/paint, which lib/ota's screen draws with
#include "ota/session.h"
#include "wifiota/improv.h"
#include "wifiota/manifest.h"
#include "wifiota/remembered.h"
#include "wifiota/power_sense.h"
#include "wifiota/schedule.h"
#include "../../src/hw/ota_pubkey.h"

using Bytes = std::vector<uint8_t>;
namespace improv = wifiota::improv;

namespace {

// ---- Improv ----------------------------------------------------------------------------

struct FakeImprovPort : improv::Port {
  std::vector<improv::State> states;
  std::vector<improv::Error> errors;
  std::vector<Bytes> results;
  std::vector<improv::Credentials> joins;
  void showState(improv::State s) override { states.push_back(s); }
  void showError(improv::Error e) override { errors.push_back(e); }
  void sendResult(const uint8_t* p, size_t n) override { results.emplace_back(p, p + n); }
  void join(const improv::Credentials& c) override { joins.push_back(c); }
};

const improv::DeviceInfo kInfo{"blorbarium", "0.7.13", "ESP32-S3", "mossby"};

// An RPC command packet as a client writes it.
Bytes rpc(uint8_t command, const Bytes& data) {
  Bytes p{command, uint8_t(data.size())};
  p.insert(p.end(), data.begin(), data.end());
  p.push_back(improv::checksum(p.data(), p.size()));
  return p;
}

Bytes wifiSettings(const std::string& ssid, const std::string& pass) {
  Bytes d{uint8_t(ssid.size())};
  d.insert(d.end(), ssid.begin(), ssid.end());
  d.push_back(uint8_t(pass.size()));
  d.insert(d.end(), pass.begin(), pass.end());
  return rpc(1, d);
}

// A result packet's strings, after checking its frame.
std::vector<std::string> strings(const Bytes& p, uint8_t command) {
  EXPECT_GE(p.size(), 3u);
  EXPECT_EQ(p[0], command);
  EXPECT_EQ(size_t(p[1]) + 3, p.size());
  EXPECT_EQ(improv::checksum(p.data(), p.size() - 1), p.back());
  std::vector<std::string> out;
  for (size_t at = 2; at < p.size() - 1; at += 1 + p[at]) out.emplace_back(p.begin() + at + 1, p.begin() + at + 1 + p[at]);
  return out;
}

TEST(Improv, ChecksumIsTheLowByteOfTheSum) {
  const Bytes p{0x01, 0x02, 0xff, 0xff};
  EXPECT_EQ(improv::checksum(p.data(), p.size()), uint8_t(0x01 + 0x02 + 0xff + 0xff));
}

TEST(Improv, ResultPacketMatchesTheSpecByteForByte) {
  // The spec's frame: command, data length, (length, bytes) per string, checksum.
  const std::string_view url = "https://x.y/";
  uint8_t out[64];
  const size_t n = improv::encodeResult(improv::Command::WifiSettings, &url, 1, out, sizeof out);
  Bytes want{0x01, 13, 12};
  want.insert(want.end(), url.begin(), url.end());
  want.push_back(improv::checksum(want.data(), want.size()));
  EXPECT_EQ(Bytes(out, out + n), want);
  EXPECT_EQ(improv::encodeResult(improv::Command::WifiSettings, &url, 1, out, 10), 0u) << "too small a buffer";
}

TEST(Improv, CredentialsParseAndRefuseWhatIsMalformed) {
  const Bytes ok = wifiSettings("Home Net", "hunter22");
  const std::optional<improv::Credentials> c = improv::parseCredentials(ok.data() + 2, ok[1]);
  ASSERT_TRUE(c);
  EXPECT_STREQ(c->ssid, "Home Net");
  EXPECT_STREQ(c->password, "hunter22");
  const Bytes open = wifiSettings("Cafe", "");
  ASSERT_TRUE(improv::parseCredentials(open.data() + 2, open[1]));
  EXPECT_STREQ(improv::parseCredentials(open.data() + 2, open[1])->password, "");

  const Bytes empty = wifiSettings("", "x");
  EXPECT_FALSE(improv::parseCredentials(empty.data() + 2, empty[1])) << "no SSID";
  const Bytes longSsid = wifiSettings(std::string(33, 'a'), "x");
  EXPECT_FALSE(improv::parseCredentials(longSsid.data() + 2, longSsid[1]));
  const Bytes longPass = wifiSettings("a", std::string(65, 'p'));
  EXPECT_FALSE(improv::parseCredentials(longPass.data() + 2, longPass[1]));
  const Bytes trailing{4, 'H', 'o', 'm', 'e', 1, 'p', 'X'};
  EXPECT_FALSE(improv::parseCredentials(trailing.data(), trailing.size())) << "bytes after the password";
  const Bytes shortPass{4, 'H', 'o', 'm', 'e', 3, 'p'};
  EXPECT_FALSE(improv::parseCredentials(shortPass.data(), shortPass.size()));
  const Bytes nul{3, 'a', 0, 'b', 0};
  EXPECT_FALSE(improv::parseCredentials(nul.data(), nul.size())) << "a NUL would join another network";
}

TEST(Improv, CredentialsJoinThenProvisionedWithTheUrl) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  EXPECT_EQ(m.state(), improv::State::Authorized) << "no authorization step";
  const Bytes p = wifiSettings("Home", "secret99");
  m.write(p.data(), p.size());
  ASSERT_EQ(port.joins.size(), 1u);
  EXPECT_STREQ(port.joins[0].ssid, "Home");
  EXPECT_STREQ(port.joins[0].password, "secret99");
  EXPECT_EQ(port.states, (std::vector<improv::State>{improv::State::Provisioning}));
  m.joined("https://blorb.hypnodroid.com/?d=mossby");
  EXPECT_EQ(m.state(), improv::State::Provisioned);
  ASSERT_EQ(port.results.size(), 1u);
  EXPECT_EQ(strings(port.results[0], 1), (std::vector<std::string>{"https://blorb.hypnodroid.com/?d=mossby"}));
  EXPECT_TRUE(port.errors.empty());
}

TEST(Improv, AFailedJoinSaysUnableToConnectAndGoesBackToAuthorized) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  const Bytes p = wifiSettings("Home", "wrong");
  m.write(p.data(), p.size());
  m.couldNotJoin();
  EXPECT_EQ(port.errors, (std::vector<improv::Error>{improv::Error::UnableToConnect}));
  EXPECT_EQ(port.states, (std::vector<improv::State>{improv::State::Provisioning, improv::State::Authorized}));
  EXPECT_TRUE(port.results.empty());
  // The next try clears the error first.
  m.write(p.data(), p.size());
  EXPECT_EQ(port.errors.back(), improv::Error::None);
  EXPECT_EQ(port.joins.size(), 2u);
}

TEST(Improv, APacketSplitAcrossWritesRunsOnceWhole) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  const Bytes p = wifiSettings("A long network name", "and a long password too");
  for (size_t at = 0; at < p.size(); at += 20) {
    EXPECT_TRUE(port.joins.empty());
    m.write(p.data() + at, std::min<size_t>(20, p.size() - at));
  }
  EXPECT_EQ(port.joins.size(), 1u);
}

TEST(Improv, ABadChecksumOrLengthIsAnInvalidRpc) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  Bytes p = wifiSettings("Home", "pw");
  p.back() ^= 1;
  m.write(p.data(), p.size());
  EXPECT_EQ(port.errors, (std::vector<improv::Error>{improv::Error::InvalidRpc}));
  EXPECT_TRUE(port.joins.empty());
  Bytes extra = wifiSettings("Home", "pw");
  extra.push_back(0);
  m.write(extra.data(), extra.size());
  EXPECT_EQ(port.errors.back(), improv::Error::InvalidRpc) << "bytes past the packet";
  const Bytes badCreds = rpc(1, Bytes{9, 'x'});
  m.write(badCreds.data(), badCreds.size());
  EXPECT_EQ(port.errors.back(), improv::Error::InvalidRpc) << "settings whose lengths do not add up";
  EXPECT_TRUE(port.joins.empty());
}

TEST(Improv, AnUnknownCommandIsAnUnknownRpc) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  const Bytes p = rpc(0x42, Bytes{});
  m.write(p.data(), p.size());
  EXPECT_EQ(port.errors, (std::vector<improv::Error>{improv::Error::UnknownRpc}));
}

TEST(Improv, ANewConnectionDropsTheLastOnesHalfPacket) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  const Bytes p = wifiSettings("Home", "pw");
  m.write(p.data(), 5);
  m.reset();
  m.write(p.data(), p.size());
  EXPECT_EQ(port.joins.size(), 1u);
  EXPECT_TRUE(port.errors.empty());
}

TEST(Improv, OneJoinAtATime) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  const Bytes p = wifiSettings("Home", "pw");
  m.write(p.data(), p.size());
  m.write(p.data(), p.size());
  EXPECT_EQ(port.joins.size(), 1u);
  EXPECT_EQ(port.errors.back(), improv::Error::Unknown);
}

TEST(Improv, DeviceInfoAndNeitherIdentifyNorScan) {
  FakeImprovPort port;
  improv::Machine m(port, kInfo);
  const Bytes info = rpc(3, Bytes{});
  m.write(info.data(), info.size());
  ASSERT_EQ(port.results.size(), 1u);
  EXPECT_EQ(strings(port.results[0], 3), (std::vector<std::string>{"blorbarium", "0.7.13", "ESP32-S3", "mossby"}));
  for (uint8_t optional : {2, 4}) {   // identify, scan: optional in the spec, left out
    const Bytes p = rpc(optional, Bytes{});
    m.write(p.data(), p.size());
    EXPECT_EQ(port.errors.back(), improv::Error::UnknownRpc) << int(optional);
  }
  EXPECT_EQ(port.results.size(), 1u);
  EXPECT_EQ(m.state(), improv::State::Authorized);
}

// ---- the manifest ----------------------------------------------------------------------

std::string readFile(const char* path) {
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

constexpr const char* kManifest = R"({"board":"lcd128","version":"0.7.12","flags":0,"size":2407760,
 "sha256":"glVjE8siM8qtBpgYfZQl7w8Ih5pXiQjnBa88lIVuKJw",
 "sig":"337ogIdyDn51ZliJv9bmAExysj_hArwwuKtIVA1PF4U--Jp9Oe6sAXtt42znW5ZQH8CHoJz9rWJqUXMz6fui4A",
 "image":"blorbarium-lcd128-0.7.12.bin","fmt":1})";

TEST(Manifest, TheCommittedReleaseParsesAndItsSignatureVerifiesWithTheBuiltInKey) {
  const std::string json = readFile("web/public/fw/lcd128/manifest.json");
  ASSERT_FALSE(json.empty());
  const std::optional<wifiota::Manifest> m = wifiota::parseManifest(json);
  ASSERT_TRUE(m) << json;
  // The signed text (docs/ota.md "Signing"), rebuilt from what the parser read.
  char sha[44];
  ota::b64url(m->image.sha256, sizeof m->image.sha256, sha);
  char text[160];
  const int n = std::snprintf(text, sizeof text, "blorbota2 %s %u.%u.%u %u %u %u %s", m->image.board,
                              unsigned(m->image.version.major), unsigned(m->image.version.minor),
                              unsigned(m->image.version.patch), unsigned(m->image.flags), unsigned(m->image.fmt),
                              unsigned(m->image.size), sha);
  EXPECT_TRUE(ota::verifyP256(hw::kOtaPublicKey, reinterpret_cast<const uint8_t*>(text), size_t(n), m->image.sig))
      << "the fields a badge reads over Wi-Fi are the ones the owner signed: " << text;
  EXPECT_TRUE(m->autoInstall);
}

TEST(Manifest, ReadsEveryField) {
  const std::optional<wifiota::Manifest> m = wifiota::parseManifest(kManifest);
  ASSERT_TRUE(m);
  EXPECT_STREQ(m->image.board, "lcd128");
  EXPECT_TRUE(m->image.version == (ota::Version{0, 7, 12}));
  EXPECT_EQ(m->image.flags, 0u);
  EXPECT_EQ(m->image.size, 2407760u);
  EXPECT_EQ(m->image.fmt, 1u);
  EXPECT_EQ(m->image.sha256[0], 0x82);
  EXPECT_STREQ(m->file, "blorbarium-lcd128-0.7.12.bin");
}

std::string without(std::string json, const std::string& key) {
  const size_t at = json.find("\"" + key + "\"");
  const size_t end = json.find_first_of(",}", json.find(':', at));
  const size_t from = json[end] == ',' ? at : json.rfind(',', at);
  json.erase(from, json[end] == ',' ? end + 1 - from : end - from);
  return json;
}

TEST(Manifest, EveryFieldIsRequired) {
  for (const char* key : {"board", "version", "flags", "size", "sha256", "sig", "image", "fmt"}) {
    const std::string json = without(kManifest, key);
    EXPECT_FALSE(wifiota::parseManifest(json)) << key << ": " << json;
  }
}

TEST(Manifest, RefusesWhatIsMalformed) {
  std::string s = kManifest;
  EXPECT_FALSE(wifiota::parseManifest(s + "x")) << "bytes after the object";
  EXPECT_FALSE(wifiota::parseManifest(s.substr(0, s.size() - 1))) << "cut short";
  auto swap = [&](const std::string& from, const std::string& to) {
    std::string t = s;
    t.replace(t.find(from), from.size(), to);
    return t;
  };
  EXPECT_FALSE(wifiota::parseManifest(swap("0.7.12\"", "0.7\""))) << "a version needs three parts";
  EXPECT_FALSE(wifiota::parseManifest(swap("glVjE8", "glVjE"))) << "a hash that is not 32 bytes";
  EXPECT_FALSE(wifiota::parseManifest(swap("blorbarium-lcd128-0.7.12.bin", "../../secret"))) << "a path";
  EXPECT_FALSE(wifiota::parseManifest(swap("blorbarium-lcd128-0.7.12.bin", ".hidden"))) << "a dot file";
  EXPECT_FALSE(wifiota::parseManifest(swap("2407760", "99999999999"))) << "a size past 32 bits";
  EXPECT_FALSE(wifiota::parseManifest(swap("2407760", "-1")));
  EXPECT_FALSE(wifiota::parseManifest(swap("\"lcd128\"", "\"lc\\\"d\""))) << "escapes in a field it reads";
}

TEST(Manifest, SkipsKeysItDoesNotKnowAndReadsTheKillSwitch) {
  std::string s = kManifest;
  s.insert(1, R"("notes":{"a":[1,2,{"b":"c\"d"}],"e":null},"rc":-1.5,"beta":true,)");
  const std::optional<wifiota::Manifest> m = wifiota::parseManifest(s);
  ASSERT_TRUE(m) << s;
  EXPECT_TRUE(m->autoInstall);
  std::string off = kManifest;
  off.insert(off.size() - 1, R"(,"auto":false)");
  ASSERT_TRUE(wifiota::parseManifest(off));
  EXPECT_FALSE(wifiota::parseManifest(off)->autoInstall);
  std::string bad = kManifest;
  bad.insert(bad.size() - 1, R"(,"auto":0)");
  EXPECT_FALSE(wifiota::parseManifest(bad)) << "auto is true or false";
}

TEST(Manifest, TheImageSitsBesideTheManifest) {
  char out[128];
  ASSERT_TRUE(wifiota::imageUrl(wifiota::kManifestUrl, "blorbarium-lcd128-0.7.12.bin", out, sizeof out));
  EXPECT_STREQ(out, "https://blorb.hypnodroid.com/fw/lcd128/blorbarium-lcd128-0.7.12.bin");
  EXPECT_FALSE(wifiota::imageUrl(wifiota::kManifestUrl, "blorbarium-lcd128-0.7.12.bin", out, 40));
}

TEST(Manifest, InstallsOnlyANewerImageForThisBoardThatNeverRolledBackHere) {
  wifiota::Manifest m = *wifiota::parseManifest(kManifest);
  using wifiota::Offer;
  const ota::Version running{0, 7, 11};
  EXPECT_EQ(wifiota::judge(m, "lcd128", running, 1, std::nullopt), Offer::Install);
  EXPECT_EQ(wifiota::judge(m, "lcd146", running, 1, std::nullopt), Offer::WrongBoard);
  EXPECT_EQ(wifiota::judge(m, "lcd128", ota::Version{0, 7, 12}, 1, std::nullopt), Offer::UpToDate);
  EXPECT_EQ(wifiota::judge(m, "lcd128", ota::Version{0, 8, 0}, 1, std::nullopt), Offer::UpToDate);
  m.image.flags = ota::kAllowOlder;
  EXPECT_EQ(wifiota::judge(m, "lcd128", ota::Version{0, 8, 0}, 1, std::nullopt), Offer::UpToDate)
      << "a signed downgrade still needs a hand";
  m.image.flags = 0;
  EXPECT_EQ(wifiota::judge(m, "lcd128", running, 1, ota::Version{0, 7, 12}), Offer::RolledBack);
  EXPECT_EQ(wifiota::judge(m, "lcd128", running, 1, ota::Version{0, 7, 10}), Offer::Install) << "an older rollback";
  EXPECT_EQ(wifiota::judge(m, "lcd128", running, 2, std::nullopt), Offer::OlderFormat)
      << "an image that writes an older save than this badge's cannot read the pet";
  m.image.fmt = 2;
  EXPECT_EQ(wifiota::judge(m, "lcd128", running, 1, std::nullopt), Offer::Install) << "a newer format migrates";
  m.image.fmt = 1;
  m.autoInstall = false;
  EXPECT_EQ(wifiota::judge(m, "lcd128", running, 1, std::nullopt), Offer::AutoOff);
}

TEST(Manifest, AWifiTokenIsMarkedAndAPagesIsNot) {
  const uint8_t r[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const ota::Token t = wifiota::wifiToken(r);
  EXPECT_TRUE(wifiota::isWifiToken(t));
  EXPECT_EQ(t.b[4], 1);
  EXPECT_EQ(t.b[15], 12);
  ota::Token page;
  for (uint8_t i = 0; i < 16; ++i) page.b[i] = uint8_t(i + 1);
  EXPECT_FALSE(wifiota::isWifiToken(page));
}

// ---- the remembered network -----------------------------------------------------------

TEST(Remembered, CredentialsRefuseWhatCouldNotBeJoined) {
  ASSERT_TRUE(wifiota::credentials("Home Net", ""));
  EXPECT_STREQ(wifiota::credentials("Home Net", "pw")->ssid, "Home Net");
  EXPECT_FALSE(wifiota::credentials("", "x"));
  EXPECT_FALSE(wifiota::credentials(std::string(33, 's'), "x"));
  EXPECT_FALSE(wifiota::credentials("s", std::string(65, 'p')));
  EXPECT_FALSE(wifiota::credentials(std::string_view("a\0b", 3), "x"));
  EXPECT_TRUE(wifiota::credentials(std::string(32, 's'), std::string(64, 'p')));
}

TEST(Remembered, TheStoredBlobRoundTripsAndRefusesJunk) {
  const improv::Credentials c = *wifiota::credentials("Home Net", "correct horse battery staple");
  uint8_t blob[wifiota::kRememberedMax];
  const size_t len = wifiota::encodeRemembered(c, blob, sizeof blob);
  ASSERT_EQ(len, 1 + 1 + 8 + 1 + 28u);
  const std::optional<improv::Credentials> back = wifiota::decodeRemembered(blob, len);
  ASSERT_TRUE(back);
  EXPECT_STREQ(back->ssid, "Home Net");
  EXPECT_STREQ(back->password, "correct horse battery staple");
  EXPECT_FALSE(wifiota::decodeRemembered(blob, len - 1)) << "cut short";
  EXPECT_FALSE(wifiota::decodeRemembered(blob, 2)) << "only a header";
  blob[0] = 9;
  EXPECT_FALSE(wifiota::decodeRemembered(blob, len)) << "another version";
  const improv::Credentials longest = *wifiota::credentials(std::string(32, 's'), std::string(64, 'p'));
  EXPECT_EQ(wifiota::encodeRemembered(longest, blob, sizeof blob), wifiota::kRememberedMax);
  EXPECT_EQ(wifiota::encodeRemembered(longest, blob, sizeof blob - 1), 0u);
}

// ---- knowing it is plugged in ----------------------------------------------------------

using wifiota::PowerSense;
constexpr uint32_t kMin = 60u * 1000;

TEST(PowerSense, AChargersVoltageIsPower) {
  PowerSense p;
  p.reading(4185, 0);
  EXPECT_TRUE(p.onPower());
  EXPECT_EQ(p.why(), PowerSense::Why::Charger);
}

TEST(PowerSense, ABatteryAloneStartsOnBattery) {
  PowerSense p;
  p.reading(3900, 0);
  EXPECT_FALSE(p.onPower());
  for (uint32_t t = 10000; t < 60 * kMin; t += PowerSense::kEveryMs) p.reading(3895, t);
  EXPECT_FALSE(p.onPower());
}

TEST(PowerSense, PluggingInLiftsItAndUnpluggingDropsIt) {
  PowerSense p;
  p.reading(3800, 0);
  p.reading(3810, 10000);
  EXPECT_FALSE(p.onPower()) << "noise is not a charger";
  p.reading(3900, 20000);
  EXPECT_TRUE(p.onPower());
  EXPECT_EQ(p.why(), PowerSense::Why::Rise);
  for (uint32_t t = 30000; t < 40 * kMin; t += PowerSense::kEveryMs) p.reading(uint16_t(3900 + t / kMin), t);
  EXPECT_TRUE(p.onPower()) << "charging slowly upward";
  p.reading(3850, 40 * kMin);
  EXPECT_FALSE(p.onPower());
  EXPECT_EQ(p.why(), PowerSense::Why::Fall);
}

TEST(PowerSense, UnpluggedAfterTheChargerFinishedIsBatteryOnceItHasFallenAWhile) {
  PowerSense p;
  p.reading(4195, 0);
  ASSERT_TRUE(p.onPower());
  // Unplugged: no step, only a slow slide.
  uint16_t mv = 4165;
  uint32_t t = 10000;
  for (; t < 9 * kMin; t += PowerSense::kEveryMs) p.reading(mv -= (t % 60000 == 0), t);
  EXPECT_TRUE(p.onPower()) << "not yet ten minutes";
  for (; t < 30 * kMin && p.onPower(); t += PowerSense::kEveryMs) p.reading(mv -= (t % 60000 == 0), t);
  EXPECT_FALSE(p.onPower());
  EXPECT_EQ(p.why(), PowerSense::Why::Drained);
}

TEST(PowerSense, NoReadingIsBattery) {
  PowerSense p;
  p.reading(4190, 0);
  p.reading(12, 10000);
  EXPECT_FALSE(p.onPower());
  EXPECT_EQ(p.why(), PowerSense::Why::NoReading);
}

// ---- the schedule ----------------------------------------------------------------------

using wifiota::Facts;
using wifiota::Hold;
using wifiota::Schedule;
using wifiota::Window;
constexpr Facts kQuietOnPower{true, true, true, false, false};

TEST(Schedule, ACheckIsDueAtOnceOnPowerIdleAndAlone) {
  Schedule s;
  EXPECT_EQ(s.checkHold(kQuietOnPower, 5), Hold::Open);
  Facts f = kQuietOnPower;
  f.enabled = false;
  EXPECT_EQ(s.checkHold(f, 5), Hold::Disabled);
  f = kQuietOnPower, f.onPower = false;
  EXPECT_EQ(s.checkHold(f, 5), Hold::OnBattery);
  f = kQuietOnPower, f.idle = false;
  EXPECT_EQ(s.checkHold(f, 5), Hold::NotIdle);
  f = kQuietOnPower, f.someoneThere = true;
  EXPECT_EQ(s.checkHold(f, 5), Hold::SomeoneThere);
  f = kQuietOnPower, f.radioBusy = true;
  EXPECT_EQ(s.checkHold(f, 5), Hold::RadioBusy);
}

TEST(Schedule, AReadManifestWaitsSixHours) {
  Schedule s;
  s.checked(1000);
  EXPECT_EQ(s.checkHold(kQuietOnPower, 1000 + Schedule::kCheckEveryMs - 1), Hold::NotDue);
  EXPECT_EQ(s.checkHold(kQuietOnPower, 1000 + Schedule::kCheckEveryMs), Hold::Open);
}

TEST(Schedule, AFailureTriesAgainInTenMinutes) {
  Schedule s;
  s.failed(1000);
  EXPECT_EQ(s.dueIn(1000), 10u * 60 * 1000);
  EXPECT_EQ(s.checkHold(kQuietOnPower, 1000 + Schedule::kRetryMs - 1), Hold::NotDue);
  EXPECT_EQ(s.checkHold(kQuietOnPower, 1000 + Schedule::kRetryMs), Hold::Open);
  s.failed(1000 + Schedule::kRetryMs);
  EXPECT_EQ(s.dueIn(1000 + Schedule::kRetryMs), Schedule::kRetryMs) << "the same wait every time";
}

TEST(Schedule, ACheckClosesForAnyoneButTheTrialsConfirmNeverDoes) {
  Facts f = kQuietOnPower;
  EXPECT_TRUE(Schedule::keep(Window::Check, f));
  f.onPower = false;
  EXPECT_TRUE(Schedule::keep(Window::Check, f)) << "a sagging reading does not cut a download";
  f = kQuietOnPower, f.idle = false;
  EXPECT_FALSE(Schedule::keep(Window::Check, f));
  EXPECT_TRUE(Schedule::keep(Window::Forced, f)) << "DEBUG ota check runs with a hand on it";
  f = kQuietOnPower, f.someoneThere = true;
  EXPECT_FALSE(Schedule::keep(Window::Check, f));
  EXPECT_TRUE(Schedule::keep(Window::Provision, f)) << "the phone that sent the network is there";
  f = kQuietOnPower, f.radioBusy = true;
  for (Window w : {Window::Check, Window::Forced, Window::Provision}) EXPECT_FALSE(Schedule::keep(w, f)) << int(w);
  EXPECT_TRUE(Schedule::keep(Window::Confirm, f));
}

// ---- the unattended update, through ota::Session ---------------------------------------

struct Signer {
  EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
  uint8_t pub[65] = {};
  Signer() {
    size_t n = 0;
    EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, pub, sizeof pub, &n);
  }
  ~Signer() { EVP_PKEY_free(key); }
  void sign(ota::Image& im) const {
    char sha[44];
    ota::b64url(im.sha256, sizeof im.sha256, sha);
    char text[160];
    const int len = std::snprintf(text, sizeof text, "blorbota2 %s %u.%u.%u %u %u %u %s", im.board,
                                  unsigned(im.version.major), unsigned(im.version.minor), unsigned(im.version.patch),
                                  unsigned(im.flags), unsigned(im.fmt), unsigned(im.size), sha);
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    size_t n = 0;
    EVP_DigestSignInit(md, nullptr, EVP_sha256(), nullptr, key);
    EVP_DigestSign(md, nullptr, &n, reinterpret_cast<const uint8_t*>(text), size_t(len));
    Bytes der(n);
    EVP_DigestSign(md, der.data(), &n, reinterpret_cast<const uint8_t*>(text), size_t(len));
    EVP_MD_CTX_free(md);
    const uint8_t* p = der.data();
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, long(n));
    BN_bn2binpad(ECDSA_SIG_get0_r(sig), im.sig, 32);
    BN_bn2binpad(ECDSA_SIG_get0_s(sig), im.sig + 32, 32);
    ECDSA_SIG_free(sig);
  }
};

struct MemSlot : ota::Slot {
  Bytes written;
  bool opened = false, activated = false, discarded = false;
  std::vector<std::string>* order = nullptr;
  uint32_t capacity() override { return 1 << 20; }
  bool open(uint32_t) override {
    order->push_back("open");
    opened = true;
    written.clear();
    return true;
  }
  bool write(const uint8_t* p, size_t n) override { return written.insert(written.end(), p, p + n), true; }
  bool close() override { return true; }
  bool activate() override { return activated = true; }
  void discard() override { discarded = true, activated = false; }
};

struct FakeHost : ota::Host {
  bool isBusy = false, saveOk = true, trial = false;
  std::optional<ota::Token> handedToken;
  std::vector<std::string>* order = nullptr;
  int forgets = 0;
  bool busy() override { return isBusy; }
  bool save() override { return order->push_back("save"), saveOk; }
  uint16_t chunk() override { return 240; }
  bool onTrial() override { return trial; }
  bool handOver(ota::Version, const ota::Token& t) override { return handedToken = t, true; }
  void forgetHandover() override { ++forgets; }
  ota::Confirm confirmTrial(const ota::Token&) override { return ota::Confirm::NotOnTrial; }
  std::optional<ota::Version> rolledBack() override { return std::nullopt; }
};

constexpr ota::Peer kWifiPeer = 0xffffffffu;   // src/wifi_ota.h: never a Bluetooth connection number

struct WifiRig {
  Signer signer;
  std::vector<std::string> order;
  MemSlot slot;
  FakeHost host;
  blorbtest::ScriptedLink link;
  ota::Session session{slot, host, link, ota::Config{"lcd128", ota::Version{0, 7, 12}, signer.pub}};
  Bytes bytes = Bytes(5000);
  ota::Image image;
  ota::Token token;
  uint32_t now = 10000;

  WifiRig() {
    slot.order = host.order = &order;
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i * 7 + 3);
    std::snprintf(image.board, sizeof image.board, "lcd128");
    image.version = ota::Version{0, 7, 13};
    image.fmt = 1;
    image.size = uint32_t(bytes.size());
    ota::Sha256 h;
    h.update(bytes.data(), bytes.size());
    h.finish(image.sha256);
    signer.sign(image);
    const uint8_t r[12] = {7};
    token = wifiota::wifiToken(r);
  }
  // As the fetch task hands the body on: offset, then up to 240 bytes.
  void feed(uint32_t from, uint32_t to, ota::Peer peer = kWifiPeer) {
    for (uint32_t at = from; at < to; at += 240) {
      const size_t n = std::min<size_t>(240, to - at);
      Bytes p{uint8_t(at), uint8_t(at >> 8), uint8_t(at >> 16), uint8_t(at >> 24)};
      p.insert(p.end(), bytes.begin() + at, bytes.begin() + at + n);
      session.data(p.data(), p.size(), now, peer);
    }
  }
};

TEST(WifiInstall, ASignedNewerImageSavesOpensReceivesAndIsReadyWithNoQuestion) {
  WifiRig rig;
  EXPECT_FALSE(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer));
  EXPECT_FALSE(rig.session.asking()) << "nobody is asked";
  EXPECT_EQ(rig.order, (std::vector<std::string>{"save", "open"})) << "the pet is saved before the slot opens";
  EXPECT_TRUE(rig.session.ownedBy(kWifiPeer));
  EXPECT_FALSE(rig.session.ownedBy(1));
  rig.feed(0, 2400, 1);
  EXPECT_TRUE(rig.slot.written.empty()) << "a Bluetooth connection's bytes never reach a Wi-Fi session";
  rig.feed(0, uint32_t(rig.bytes.size()));
  EXPECT_EQ(rig.slot.written, rig.bytes);
  EXPECT_TRUE(rig.session.ready());
  EXPECT_TRUE(rig.slot.activated);
  ASSERT_TRUE(rig.host.handedToken);
  EXPECT_TRUE(wifiota::isWifiToken(*rig.host.handedToken)) << "so the new image confirms over Wi-Fi";
  EXPECT_FALSE(rig.session.rebootDue(rig.now + ota::Session::kReadyMs - 1));
  EXPECT_TRUE(rig.session.rebootDue(rig.now + ota::Session::kReadyMs));
  EXPECT_TRUE(rig.session.prepareRestart());
}

TEST(WifiInstall, TheSameRefusalsAsBegin) {
  {
    WifiRig rig;
    rig.image.sig[5] ^= 1;
    const std::optional<ota::Refusal> r = rig.session.install(rig.image, rig.token, rig.now, kWifiPeer);
    ASSERT_TRUE(r);
    EXPECT_STREQ(r->why, "BAD_SIGNATURE");
    EXPECT_FALSE(rig.slot.opened);
  }
  {
    WifiRig rig;
    Signer stranger;
    stranger.sign(rig.image);
    EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer)->why, "BAD_SIGNATURE");
  }
  {
    WifiRig rig;
    rig.host.trial = true;
    EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer)->why, "TRIAL");
  }
  {
    WifiRig rig;
    rig.host.isBusy = true;
    EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer)->why, "BUSY");
  }
  {
    WifiRig rig;
    rig.host.saveOk = false;
    EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer)->why, "save_failed");
    EXPECT_FALSE(rig.slot.opened) << "no save, no slot";
  }
}

TEST(WifiInstall, NeverADowngradeEvenASignedOne) {
  WifiRig rig;
  rig.image.version = ota::Version{0, 7, 1};
  rig.image.flags = ota::kAllowOlder;
  rig.signer.sign(rig.image);
  EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer)->why, "NOT_NEWER");
  rig.image.version = ota::Version{0, 7, 12};
  rig.signer.sign(rig.image);
  EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now + 5000, kWifiPeer)->why, "NOT_NEWER");
}

TEST(WifiInstall, APagesSessionAndAWifiOneExcludeEachOther) {
  WifiRig rig;
  ASSERT_FALSE(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer));
  rig.link.sent.clear();
  EXPECT_TRUE(rig.session.handle("#1 OTA STATUS", rig.now, 1));
  EXPECT_EQ(rig.link.sent.at(0).substr(0, 24), "#1 OK state=receiving at") << "a page sees it, and that it is busy";
  EXPECT_STREQ(rig.session.install(rig.image, rig.token, rig.now + 5000, kWifiPeer)->why, "OTA_ACTIVE");
}

TEST(WifiInstall, AbortDiscardsTheSlotAndForgetsTheHandover) {
  WifiRig rig;
  ASSERT_FALSE(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer));
  rig.feed(0, uint32_t(rig.bytes.size()));
  ASSERT_TRUE(rig.session.ready());
  rig.session.abort();
  EXPECT_FALSE(rig.session.active());
  EXPECT_TRUE(rig.slot.discarded);
  EXPECT_FALSE(rig.slot.activated);
  EXPECT_EQ(rig.host.forgets, 1) << "or the next boot reads it as a rollback";
  EXPECT_EQ(rig.session.last(), ota::Last::Aborted);
  rig.session.abort();
  EXPECT_EQ(rig.host.forgets, 1) << "idle: nothing more to undo";
}

TEST(WifiInstall, ACorruptedDownloadEndsBadHashAndNeverSwitches) {
  WifiRig rig;
  ASSERT_FALSE(rig.session.install(rig.image, rig.token, rig.now, kWifiPeer));
  rig.bytes[1234] ^= 0x40;
  rig.feed(0, uint32_t(rig.bytes.size()));
  EXPECT_FALSE(rig.session.active());
  EXPECT_EQ(rig.session.last(), ota::Last::BadHash);
  EXPECT_FALSE(rig.slot.activated);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
