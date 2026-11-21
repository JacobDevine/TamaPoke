// Adventures, step 12e part 2: the raid protocol (fork_raidnet.h). One host
// and up to three guests on one run, each a PHONE of its own in this process:
// its own gAdv and its own store, swapped in while it runs (enter()/leave()),
// so a guest's mirror and a host's run are the real records, saved and
// reloaded by the real advSave()/advBegin(). Between them is a bus that drops,
// duplicates and delays packets on purpose.
//
// Driven through RaidNet and the run's own calls; a guest's mirror is never
// edited by the test. The host's boss is (as advrun_test's winFight() does)
// put one hit from the end where a test needs a fight won quickly.
#include "Arduino.h"
#include "Preferences.h"
#include "fork.h"
#include "fork_adv.h"
#include "fork_raidnet.h"
#include "battle.h"
#include "ckpt.h"
#include "moves.h"
#include "pet.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 23;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t gNowMs = 0;
uint32_t millis() { return gNowMs; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif

extern uint16_t gRegionArt;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static const uint32_t DAY = 20420;

// --- phones ---------------------------------------------------------------

struct Pkt { uint8_t from; std::vector<uint8_t> b; };
struct Phone {
  const char *name = "";
  uint32_t id = 0;
  AdvRun run;
  NvsStore store;
  RaidNet net;
  std::deque<Pkt> inbox;
  uint8_t addr = 0;        // the peer number the host's transport gives it
  bool offline = false;    // hears nothing and is heard by nobody
  bool frozen = false;     // does not run at all (a killed app)
};
static Phone ph[5];        // 0 hosts; 1..4 are guests
static Phone *cur = nullptr;
static uint32_t gNow = 1000;

static void enter(Phone &p) {
  cur = &p;
  std::swap(gAdv, p.run);
  gAdv.battle.hearts = &gAdv.hearts;
  std::swap(nvs(), p.store);
}
static void leave() {
  Phone &p = *cur;
  std::swap(nvs(), p.store);
  std::swap(gAdv, p.run);
  cur = nullptr;
}
// Runs `f` as that phone, then hands the process back.
template <typename F> static void as(Phone &p, F f) { enter(p); f(); leave(); }

// The lossy bus. Deterministic, so a failure replays.
static uint32_t gBus = 7;
static int gLossPct = 0, gDupPct = 0;
static uint32_t busRoll() { gBus = gBus * 1103515245u + 12345u; return (gBus >> 16) % 100; }
static std::vector<std::vector<uint8_t>> gSentActs;   // every guest ACT, for replay
static uint32_t gSent = 0, gDropped = 0;

static Phone *guestAt(uint8_t addr) {
  for (int i = 1; i < 5; i++)
    if (ph[i].addr == addr && ph[i].net.state != RNET_OFF && !ph[i].net.isHost) return &ph[i];
  return nullptr;
}
static void busSend(void *ctx, uint8_t peer, const uint8_t *buf, uint16_t len) {
  Phone *from = (Phone *)ctx;
  Phone *to = from == &ph[0] ? guestAt(peer) : &ph[0];
  gSent++;
  if (!to || from->offline || to->offline) return;
  if (from != &ph[0] && buf[0] == RN_ACT) gSentActs.emplace_back(buf, buf + len);
  if ((int)busRoll() < gLossPct) { gDropped++; return; }
  const uint8_t f = from == &ph[0] ? 0 : from->addr;
  to->inbox.push_back({ f, std::vector<uint8_t>(buf, buf + len) });
  if ((int)busRoll() < gDupPct) to->inbox.push_back({ f, std::vector<uint8_t>(buf, buf + len) });
}

static void wire(Phone &p) { p.net.send = busSend; p.net.ctx = &p; }

// One frame for everybody: what arrived, then tick.
static void step(uint32_t ms = 20) {
  gNow += ms;
  gNowMs = gNow;
  for (Phone &p : ph) {
    if (p.net.state == RNET_OFF) { p.inbox.clear(); continue; }
    if (p.frozen) continue;   // a killed app: relaunch() forgets what was waiting
    enter(p);
    std::deque<Pkt> in;
    in.swap(p.inbox);
    for (Pkt &k : in) p.net.onPacket(k.from, k.b.data(), (uint16_t)k.b.size());
    p.net.tick(gNow);
    leave();
  }
}
static void pump(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 20) step(); }
template <typename F> static bool pumpUntil(F done, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 20) {
    if (done()) return true;
    step();
  }
  return done();
}

// A killed app: nothing in memory, the record read back out of its store.
static void relaunch(Phone &p) {
  p.net = RaidNet();
  p.inbox.clear();
  as(p, [] { gAdv = AdvRun(); advBegin(); });
}

static uint8_t firstMove(const Combatant &c) {
  for (uint8_t s = 0; s < MOVE_SLOTS; s++)
    if (c.moves[s] && c.moves[s] < MOVE_COUNT && MOVE_TBL[c.moves[s]].cat != MC_STATUS) return s;
  for (uint8_t s = 0; s < MOVE_SLOTS; s++)
    if (c.moves[s] && c.moves[s] < MOVE_COUNT) return s;
  return 0;
}

static bool sameAdv(const AdvMon &a, const AdvMon &b) {
  return a.dex == b.dex && a.form == b.form && a.shiny == b.shiny && !memcmp(a.iv, b.iv, 4);
}

// Every person in the run picks their first attacking move, if they still
// have a pick to make this turn.
static void everyoneActs() {
  for (Phone &p : ph) {
    if (p.frozen || p.offline || p.net.state != RNET_IN) continue;
    as(p, [&] {
      if (gAdv.phase != ADV_FIGHT) return;
      const RaidSeat &s = gAdv.battle.seat[0];
      if (!s.mon.hp) return;
      if (p.net.isHost ? s.pick != 0 : (p.net.pickMask & 1) || p.net.pendN) return;
      p.net.act(firstMove(s.mon), false);
    });
  }
}

// The host's boss one hit from the end, as advrun_test's winFight() puts it.
static void bossNearlyDown() {
  as(ph[0], [] {
    RaidBoss &k = gAdv.battle.boss;
    k.shields = 0;
    k.barrier = RAID_BARRIERS_MAX;
    k.mon.hp = 1;
    advSave();
  });
}

static uint8_t hostPhase() { uint8_t v = 0; as(ph[0], [&] { v = gAdv.phase; }); return v; }
static AdvRun hostRun() { AdvRun v; as(ph[0], [&] { v = gAdv; }); return v; }
static AdvRun runOf(Phone &p) { AdvRun v; as(p, [&] { v = gAdv; }); return v; }

// Fights the host's current fight to its end, everybody acting.
static bool fightOut(bool quick) {
  for (int t = 0; t < 60 && hostPhase() == ADV_FIGHT; t++) {
    if (quick) bossNearlyDown();
    everyoneActs();
    pump(300);
  }
  return hostPhase() != ADV_FIGHT;
}

static void freshPhones() {
  const char *names[5] = { "HOST", "ANA", "BEN", "CAL", "DOT" };
  for (int i = 0; i < 5; i++) {
    ph[i].name = names[i];
    ph[i].id = i == 0 ? 1001 : 2000 + i;
    ph[i].store.clear();
    ph[i].net = RaidNet();
    ph[i].inbox.clear();
    ph[i].addr = (uint8_t)(i - 1);
    ph[i].offline = ph[i].frozen = false;
    as(ph[i], [] { nvs().clear(); gAdv = AdvRun(); advBegin(); });
  }
}

// A host with a NORMAL run started, hosting, and `guests` guests joined.
static bool lobby(int guests) {
  freshPhones();
  bool ok = true;
  as(ph[0], [&] {
    ok &= advStart(RAID_NORMAL, DAY);
    ok &= ph[0].net.host(ph[0].id, ph[0].name);
    wire(ph[0]);
  });
  for (int i = 1; i <= guests; i++) {
    as(ph[i], [&] { wire(ph[i]); ok &= ph[i].net.join(ph[i].id, ph[i].name, DAY); wire(ph[i]); });
  }
  ok &= pumpUntil([&] {
    for (int i = 1; i <= guests; i++) if (ph[i].net.state != RNET_IN) return false;
    return true;
  }, 5000);
  return ok;
}

// Everybody picks loan `i`, and the host starts.
static bool startRun(int guests) {
  bool ok = true;
  for (int i = 1; i <= guests; i++) as(ph[i], [&] { ok &= ph[i].net.pickLoan((uint8_t)(i % ADV_LOANS)); });
  as(ph[0], [&] { ok &= ph[0].net.pickLoan(0); });
  ok &= pumpUntil([] { bool c = false; as(ph[0], [&] { c = ph[0].net.canStart(); }); return c; }, 5000);
  as(ph[0], [&] { ok &= ph[0].net.start(); });
  ok &= pumpUntil([&] {
    for (int i = 1; i <= guests; i++) if (runOf(ph[i]).phase != ADV_DOOR) return false;
    return true;
  }, 5000);
  return ok;
}

static bool hostDoor() {
  bool ok = false;
  as(ph[0], [&] { ok = advPickDoor(0); });
  return ok && pumpUntil([] {
    for (int i = 1; i < 5; i++)
      if (ph[i].net.state == RNET_IN && runOf(ph[i]).phase != ADV_FIGHT) return false;
    return true;
  }, 5000);
}

static uint8_t seatOf(Phone &g) { return g.net.mySeat; }

int main() {
  forkSetFeatures(true);
  gRegionArt = 0xFFFF;

  // --- the record carries who plays which seat ------------------------------
  {
    freshPhones();
    bool ok = true;
    AdvRun saved;
    as(ph[0], [&] {
      ok &= advStart(RAID_EASY, DAY);
      ok &= ph[0].net.host(ph[0].id, "HOST");
      ok &= advClaimSeat(2, 4242, "ZED");
      saved = gAdv;
    });
    relaunch(ph[0]);
    const AdvRun back = runOf(ph[0]);
    ck(ok && back.net.role == ADV_HOSTED && back.net.runId == saved.net.runId && back.net.runId &&
       back.net.player[0] == 1001 && back.net.player[2] == 4242 && !strcmp(back.net.name[2], "ZED"),
       "a hosted run's seats, owners and run id come back from its record");
    uint8_t buf[ADV_BLOB_CAP];
    const uint16_t n = advEncode(saved, buf, sizeof(buf), 1);
    AdvRun old;
    // the block is the last thing in the body: a record from before it ends
    // where the block begins, and must read as the solo run it was
    const bool dec = advDecode(buf, (uint16_t)(n - (8 + 4 * RAID_SEATS + ADV_NET_NAME * RAID_SEATS)), old);
    ck(dec && old.net.role == ADV_SOLO && !old.net.runId && old.phase == saved.phase,
       "a record written before the seat block reads as a solo run");
    AdvRun mangled;
    buf[n - 2 - (ADV_NET_NAME * RAID_SEATS + 4 * RAID_SEATS + 8)] = 9;   // role 9
    ck(!advDecode(buf, n, mangled), "a record with a role that does not exist is refused");
  }

  // --- a guest's mirror is drawn, never moved -------------------------------
  {
    ck(lobby(1), "a guest joins a hosted lobby");
    bool refuse = true;
    as(ph[1], [&] {
      RaidTurn t;
      refuse &= gAdv.net.role == ADV_GUEST && gAdv.phase == ADV_LOAN;
      refuse &= !advPickLoan(0);
      refuse &= !advSeatLoan(0, gAdv.loan[0]);
      refuse &= !advClaimSeat(1, 7, "X");
      refuse &= !advTurn(0, false, t);
      refuse &= !advResolve(t);
      refuse &= !advCatchSeats(1);
      refuse &= !advPickDoor(0);
      refuse &= gAdv.phase == ADV_LOAN;
    });
    ck(refuse, "a guest's mirror refuses every call that would move the run itself");
  }

  // --- the lobby -------------------------------------------------------------
  {
    gLossPct = 0;
    // CAL already took today's free run, so joining costs it coins
    freshPhones();
    as(ph[3], [] { gAdv.freeDay = DAY; advSave(); });
    bool ok = true;
    as(ph[0], [&] { ok &= advStart(RAID_NORMAL, DAY) && ph[0].net.host(ph[0].id, "HOST"); wire(ph[0]); });
    // ANA's phone is a day ahead of the host's (another time zone): its record
    // must carry ITS day
    for (int i = 1; i <= 3; i++)
      as(ph[i], [&] { wire(ph[i]); ok &= ph[i].net.join(ph[i].id, ph[i].name, i == 1 ? DAY + 1 : DAY); });
    ok &= pumpUntil([] { return ph[1].net.state == RNET_IN && ph[2].net.state == RNET_IN &&
                                ph[3].net.state == RNET_IN; }, 5000);
    ck(ok, "three guests join the host's lobby");
    const AdvRun h = hostRun();
    bool seats = true, loans = true, mirror = true;
    uint8_t taken = 0;
    for (int i = 1; i <= 3; i++) {
      const uint8_t k = seatOf(ph[i]);
      const AdvRun g = runOf(ph[i]);
      seats &= k >= 1 && k < RAID_SEATS && h.net.player[k] == ph[i].id && !strcmp(h.net.name[k], ph[i].name);
      taken |= (uint8_t)(1u << k);
      mirror &= g.net.role == ADV_GUEST && g.net.seat == k && g.phase == ADV_LOAN &&
                g.net.runId == h.net.runId && g.net.player[0] == ph[i].id && g.net.player[k] == ph[0].id;
      for (int j = 0; j < ADV_LOANS; j++) loans &= sameAdv(g.loan[j], ph[0].net.seatLoans[k][j]);
    }
    ck(seats && taken == 0x0E, "each guest has a seat of its own, under its name, on the host's run");
    ck(mirror, "each mirror is the host's run, with its own seat as seat 0 and the host's at its seat");
    ck(loans, "each guest is offered the three loans the host rolled for its seat");
    bool differ = false;
    for (int j = 0; j < ADV_LOANS; j++)
      differ |= !sameAdv(runOf(ph[1]).loan[j], h.loan[j]) || !sameAdv(runOf(ph[2]).loan[j], h.loan[j]);
    ck(differ, "a guest's loans are rolled for it, not the host's three");
    ck(runOf(ph[1]).freeDay == DAY + 1 && !ph[1].net.chargeDue && !ph[1].net.charge,
       "a guest's first run of the day is its free run, marked taken in ITS record");
    ck(ph[3].net.chargeDue && ph[3].net.charge == ADV_RUN_PRICE,
       "a guest whose free run is spent is asked for the price, once it is in");
    uint8_t pl1 = 0, pl0 = 0;
    as(ph[1], [&] { pl1 = ph[1].net.players(); });
    as(ph[0], [&] { pl0 = ph[0].net.players(); });
    ck(pl1 == 4 && pl0 == 4, "everyone counts four players");

    // a fourth guest finds the seats taken, and says nothing changed for it
    as(ph[4], [&] { wire(ph[4]); ph[4].net.join(ph[4].id, "DOT", DAY); });
    pumpUntil([] { return ph[4].net.state != RNET_JOINING; }, 3000);
    const AdvRun dot = runOf(ph[4]);
    ck(ph[4].net.state == RNET_REFUSED && ph[4].net.refused == RNR_FULL && dot.phase == ADV_IDLE &&
       dot.net.role == ADV_SOLO, "a fifth player is refused (FULL) and its own record is untouched");

    // nobody can start until everybody has picked
    bool early = true;
    as(ph[0], [&] { early &= ph[0].net.pickLoan(1) && !ph[0].net.canStart() && !ph[0].net.start(); });
    as(ph[1], [&] { ph[1].net.pickLoan(2); });
    as(ph[2], [&] { ph[2].net.pickLoan(0); });
    pump(1500);
    as(ph[0], [&] { early &= !ph[0].net.canStart(); });
    ck(early, "the host cannot start while a guest has not picked its loan");
    as(ph[3], [&] { ph[3].net.pickLoan(1); });
    bool can = pumpUntil([] { bool c = false; as(ph[0], [&] { c = ph[0].net.canStart(); }); return c; }, 3000);
    ck(can, "once every guest has picked, the host can start");
    // a guest changes its mind before the start
    as(ph[1], [&] { ph[1].net.pickLoan(0); });
    pump(1500);
    bool picks = true;
    const AdvRun h2 = hostRun();
    const int want[4] = { 0, 0, 0, 1 };
    for (int i = 1; i <= 3; i++) {
      const AdvRun g = runOf(ph[i]);
      picks &= g.seatMon[0].dex == g.loan[want[i]].dex &&
               h2.seatMon[seatOf(ph[i])].dex == g.loan[want[i]].dex;
    }
    ck(picks, "each seat holds the loan its player picked, the last pick counting, on both sides");
    bool started = false;
    as(ph[0], [&] { started = ph[0].net.start(); });
    pumpUntil([] { return runOf(ph[1]).phase == ADV_DOOR && runOf(ph[3]).phase == ADV_DOOR; }, 3000);
    const AdvRun h3 = hostRun();
    ck(started && h3.phase == ADV_DOOR && runOf(ph[2]).phase == ADV_DOOR &&
       sameAdv(runOf(ph[2]).door[1], h3.door[1]), "the host starts, and every mirror is at the same doors");

    // too late now
    as(ph[4], [&] { ph[4].net.join(ph[4].id, "DOT", DAY); });
    pumpUntil([] { return ph[4].net.state != RNET_JOINING; }, 3000);
    ck(ph[4].net.state == RNET_REFUSED && ph[4].net.refused == RNR_STARTED,
       "a stranger cannot join once the run has started");
  }

  // --- a guest leaves the lobby ---------------------------------------------
  {
    ck(lobby(2), "two guests in a lobby");
    const uint8_t k = seatOf(ph[2]);
    as(ph[2], [&] { ph[2].net.leave(); });
    pump(200);
    const AdvRun h = hostRun();
    ck(!h.net.player[k] && h.seatMon[k].dex && !((h.net.remote >> k) & 1),
       "a guest leaving the lobby frees its seat for an AI companion");
    bool can = false;
    as(ph[1], [&] { ph[1].net.pickLoan(0); });
    as(ph[0], [&] { ph[0].net.pickLoan(0); });
    pumpUntil([&] { as(ph[0], [&] { can = ph[0].net.canStart(); }); return can; }, 3000);
    ck(can, "and the host is not kept waiting for its pick");
  }

  // --- turns: everybody chooses, then the host resolves ----------------------
  {
    ck(lobby(3) && startRun(3) && hostDoor(), "four players reach a fight");
    const uint16_t t0 = hostRun().battle.turn;
    // everyone but CAL
    as(ph[0], [&] { ph[0].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    for (int i = 1; i <= 2; i++)
      as(ph[i], [&] { ph[i].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    pump(2000);
    const AdvRun h = hostRun();
    ck(h.battle.turn == t0, "the turn waits for every player, however long one takes");
    ck((ph[1].net.pickMask & 1) && (ph[1].net.pickMask & (1u << seatOf(ph[1]))) &&
       !(ph[3].net.pickMask & 1), "every phone sees who has chosen (its own pick as seat 0)");
    ck(ph[1].net.pendN == 0, "a choice the host took is no longer resent");
    as(ph[3], [&] { ph[3].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    const bool moved = pumpUntil([&] { return hostRun().battle.turn == t0 + 1 &&
                                              runOf(ph[3]).battle.turn == t0 + 1; }, 3000);
    ck(moved, "the last choice resolves the turn, on the host and in the mirrors");
    const AdvRun h2 = hostRun();
    bool same = true;
    for (int i = 1; i <= 3; i++) {
      const AdvRun g = runOf(ph[i]);
      const uint8_t k = seatOf(ph[i]);
      same &= g.battle.boss.mon.hp == h2.battle.boss.mon.hp && g.battle.boss.shields == h2.battle.boss.shields &&
              g.hearts == h2.hearts && g.battle.seat[0].mon.hp == h2.battle.seat[k].mon.hp &&
              g.battle.seat[k].mon.hp == h2.battle.seat[0].mon.hp;
    }
    ck(same, "every mirror shows the host's numbers: the boss, the hearts, every seat");
    // the narration, remapped: the host's seat k acting is seat 0 on that guest
    bool told = ph[1].net.turnNew && ph[1].net.turn.n == ph[0].net.turn.n && ph[1].net.turn.n > 0;
    const uint8_t k1 = seatOf(ph[1]);
    for (uint8_t i = 0; told && i < ph[0].net.turn.n; i++) {
      const RaidEvent &a = ph[0].net.turn.ev[i], &b = ph[1].net.turn.ev[i];
      const uint8_t want = a.actor == k1 ? 0 : a.actor == 0 ? k1 : a.actor;
      told &= b.actor == want && b.log.move == a.log.move && b.log.damage == a.log.damage &&
              b.shieldsBroken == a.shieldsBroken && b.barrier == a.barrier;
    }
    ck(told, "each guest gets the turn's narration, with its own seat as seat 0");
    ph[1].net.turnNew = false;

    // the AI seats played; nobody's choice was taken twice
    const size_t acts = gSentActs.size();
    ck(acts >= 3, "the guests' choices went over the bus");
    // replaying every ACT already sent: each is stamped with a turn now past
    for (int i = 1; i <= 3; i++)
      for (const auto &a : gSentActs) as(ph[0], [&] { ph[0].net.onPacket(ph[i].addr, a.data(), (uint16_t)a.size()); });
    pump(1000);
    const AdvRun h3 = hostRun();
    bool none = h3.battle.turn == t0 + 1;
    for (int i = 0; i < RAID_SEATS; i++) none &= h3.battle.seat[i].pick == 0;
    ck(none, "a replayed choice for a turn already resolved is not taken as this turn's");
    ck(!ph[1].net.turnNew, "and nothing new is narrated for it");
  }

  // --- a guest drops mid-fight, an AI takes its seat, and it rejoins ---------
  {
    const uint8_t k = seatOf(ph[2]);
    ph[2].offline = true;
    const uint16_t t0 = hostRun().battle.turn;
    const bool dropped = pumpUntil([&] { return !((hostRun().net.remote >> k) & 1); }, RNET_TIMEOUT_MS + 3000);
    const AdvRun h = hostRun();
    ck(dropped && h.battle.seat[k].chooser == SEAT_AI && h.net.player[k] == ph[2].id,
       "a guest gone quiet mid-fight is dropped: an AI plays its seat, which stays its own");
    pumpUntil([] { return ph[2].net.state == RNET_LOST; }, 2000);
    ck(ph[2].net.state == RNET_LOST, "and the guest, hearing nothing, knows it is lost");
    uint8_t pl = 0;
    as(ph[1], [&] { pl = ph[1].net.players(); });
    ck(pl == 3, "the others see one player fewer");
    // the turn no longer waits for it, and the AI plans its seat only once
    // the people have chosen (the 12d team rules plan around their picks)
    as(ph[0], [&] { ph[0].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    as(ph[1], [&] { ph[1].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    pump(1000);
    const AdvRun mid = hostRun();
    ck(mid.battle.turn == t0 && mid.battle.seat[k].pick == 0 && mid.battle.seat[0].pick,
       "the AI does not choose for the dropped seat until every person has chosen");
    as(ph[3], [&] { ph[3].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    const bool on = pumpUntil([&] { return hostRun().battle.turn == t0 + 1; }, 3000);
    ck(on, "the turn resolves without it, the AI choosing for its seat");
    // back, from a new address (a restarted app gets a new port)
    relaunch(ph[2]);
    const AdvRun kept = runOf(ph[2]);
    ck(kept.net.role == ADV_GUEST && kept.net.seat == k && kept.phase == ADV_FIGHT,
       "a killed guest's mirror comes back from its record, knowing whose run it was in");
    ph[2].offline = false;
    ph[2].addr = 3;
    bool again = false;
    as(ph[2], [&] { wire(ph[2]); again = ph[2].net.join(ph[2].id, "BEN", DAY); });
    const bool back = pumpUntil([&] { return ph[2].net.state == RNET_IN &&
                                             runOf(ph[2]).battle.turn == hostRun().battle.turn; }, 5000);
    const AdvRun h2 = hostRun();
    ck(again && back && ((h2.net.remote >> k) & 1) && h2.battle.seat[k].chooser == SEAT_REMOTE &&
       ph[2].net.mySeat == k, "it rejoins from a new address and has its seat back");
    ck(!ph[2].net.chargeDue, "a rejoin is not charged for again");
    ck(!ph[2].net.turnNew && ph[0].net.turn.n > 0,
       "the turn the host last resolved is history to a rejoiner, not narrated as news");
    const uint16_t t1 = h2.battle.turn;
    as(ph[0], [&] { ph[0].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    for (int i : { 1, 3 }) as(ph[i], [&] { ph[i].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    pump(1500);
    ck(hostRun().battle.turn == t1, "the turn waits for it again");
    as(ph[2], [&] { ph[2].net.act(firstMove(gAdv.battle.seat[0].mon), false); });
    ck(pumpUntil([&] { return hostRun().battle.turn == t1 + 1; }, 3000), "and its choice resolves the turn");
  }

  // --- the catch: everybody decides, each for their own seat -----------------
  {
    const bool won = fightOut(true);
    ck(won && hostPhase() == ADV_CATCH, "the fight is won");
    pumpUntil([] { return runOf(ph[1]).phase == ADV_CATCH && runOf(ph[3]).phase == ADV_CATCH; }, 3000);
    const AdvRun h = hostRun();
    const AdvMon caught = h.caught[h.caughtN - 1];
    bool all = true;
    for (int i = 1; i <= 3; i++) {
      const AdvRun g = runOf(ph[i]);
      all &= g.phase == ADV_CATCH && g.caughtN == h.caughtN && sameAdv(g.caught[g.caughtN - 1], caught);
    }
    ck(all, "every player gets their own copy of the creature caught");
    const AdvMon before3 = h.seatMon[seatOf(ph[3])];
    as(ph[1], [&] { ph[1].net.catchChoice(true); });
    as(ph[0], [&] { ph[0].net.catchChoice(false); });
    as(ph[3], [&] { ph[3].net.catchChoice(false); });
    pump(1500);
    ck(hostPhase() == ADV_CATCH && (ph[1].net.catchSeen & 1),
       "the run waits at the catch for the player still deciding");
    as(ph[2], [&] { ph[2].net.catchChoice(false); });
    ck(pumpUntil([] { return hostPhase() == ADV_DOOR && runOf(ph[1]).phase == ADV_DOOR; }, 3000),
       "the last decision moves everyone on");
    const AdvRun h2 = hostRun();
    const AdvRun g1 = runOf(ph[1]);
    ck(h2.seatMon[seatOf(ph[1])].dex == caught.dex && g1.seatMon[0].dex == caught.dex &&
       g1.seat[0].hp == g1.seat[0].maxHp, "the guest who swapped fights on with the catch, at full HP");
    ck(sameAdv(h2.seatMon[seatOf(ph[3])], before3), "and one who did not keeps its creature");
    ck(h2.seatMon[0].dex != caught.dex || h.seatMon[0].dex == caught.dex,
       "the host's own seat is the host's decision alone");
  }

  // --- the host is killed, comes back, and its players rejoin -----------------
  {
    ck(hostDoor(), "the next fight starts");
    const AdvRun nf = hostRun();
    ck(nf.battle.seat[seatOf(ph[1])].chooser == SEAT_REMOTE && nf.battle.seat[seatOf(ph[3])].chooser == SEAT_REMOTE &&
       nf.battle.seat[0].chooser == SEAT_LOCAL, "in the next fight the guests' seats are still theirs to choose for");
    ph[0].frozen = true;
    pump(500);
    relaunch(ph[0]);
    ph[0].frozen = false;
    bool resumed = false;
    as(ph[0], [&] { resumed = ph[0].net.host(ph[0].id, "HOST"); wire(ph[0]); });
    const AdvRun h = hostRun();
    ck(resumed && h.phase == ADV_FIGHT && !h.net.remote && h.net.player[seatOf(ph[1])] == ph[1].id,
       "a killed host resumes its run: nobody connected yet, every seat still its player's");
    const bool lost = pumpUntil([] { return ph[1].net.state == RNET_LOST && ph[2].net.state == RNET_LOST &&
                                            ph[3].net.state == RNET_LOST; }, RNET_TIMEOUT_MS + 3000);
    ck(lost, "its guests notice it went away");
    for (int i = 1; i <= 3; i++)
      as(ph[i], [&] { ph[i].net.join(ph[i].id, ph[i].name, DAY); });
    const bool back = pumpUntil([] { return ph[1].net.state == RNET_IN && ph[2].net.state == RNET_IN &&
                                            ph[3].net.state == RNET_IN; }, 5000);
    ck(back && hostRun().net.remote == 0x0E, "and all three rejoin it");
  }

  // --- a mirror of another run does not get a seat ---------------------------
  {
    AdvRun other = runOf(ph[2]);
    as(ph[4], [&] { gAdv = other; gAdv.net.runId ^= 0x5A5A; advSave(); wire(ph[4]); });
    ph[4].id = ph[2].id;   // the same player, from a mirror of some other run
    ph[4].addr = 1;        // BEN's old address, free since it was dropped
    as(ph[4], [&] { ph[4].net.join(ph[4].id, "BEN", DAY); });
    pumpUntil([] { return ph[4].net.state != RNET_JOINING; }, 3000);
    ck(ph[4].net.state == RNET_REFUSED && ph[4].net.refused == RNR_GONE,
       "rejoining with a mirror of another run is refused, even under a seated player's id");
    ph[4].id = 2004;
    as(ph[4], [] { gAdv = AdvRun(); advSave(); });
    ph[4].net = RaidNet();
  }

  // --- the whole rest of the run, over a bad network -------------------------
  {
    gLossPct = 30;
    gDupPct = 10;
    gDropped = gSent = 0;
    bool ok = true;
    for (int guard = 0; guard < 40 && hostPhase() != ADV_KEEP; guard++) {
      const uint8_t ph0 = hostPhase();
      if (ph0 == ADV_DOOR) {
        as(ph[0], [&] { advPickDoor(1); });
        pump(400);
      } else if (ph0 == ADV_FIGHT) {
        ok &= fightOut(true);
      } else if (ph0 == ADV_CATCH) {
        for (Phone &p : ph)
          if (p.net.state == RNET_IN) as(p, [&] { if (gAdv.phase == ADV_CATCH) p.net.catchChoice(false); });
        pump(600);
      }
    }
    pumpUntil([] { return runOf(ph[1]).phase == ADV_KEEP && runOf(ph[2]).phase == ADV_KEEP &&
                          runOf(ph[3]).phase == ADV_KEEP; }, 8000);
    const AdvRun h = hostRun();
    printf("      lossy run: %u packets sent, %u dropped\n", (unsigned)gSent, (unsigned)gDropped);
    ck(ok && gDropped > 20 && h.phase == ADV_KEEP && h.won && h.caughtN == ADV_STAGES,
       "five stages and the boss, with a third of the packets lost and some doubled");
    bool keep = true;
    for (int i = 1; i <= 3; i++) {
      const AdvRun g = runOf(ph[i]);
      keep &= g.phase == ADV_KEEP && g.won && g.caughtN == ADV_STAGES &&
              sameAdv(g.caught[ADV_STAGES - 1], h.caught[ADV_STAGES - 1]);
    }
    ck(keep, "every player reaches the KEEP with all five catches, the boss among them");
    gLossPct = gDupPct = 0;
    // the host's state may still move after its KEEP (a guest leaving changes
    // who is connected); the guests at their KEEP screens still answer it
    pump(600);
    as(ph[0], [] { gAdv.hearts = gAdv.hearts ? gAdv.hearts - 1 : 1; advSave(); });
    const uint32_t g0 = ph[0].net.gen;
    step();   // the host notices on its next frame
    ck(ph[0].net.gen != g0, "a change after the KEEP is a new state generation");
    const bool seen = pumpUntil([] { bool s = false; as(ph[0], [&] { s = ph[0].net.allSeen(); }); return s; }, 3000);
    ck(seen, "the host can tell every guest has the KEEP before it finishes");

    // each keeps on their own phone, paid once
    AdvReward w;
    bool once = true;
    as(ph[1], [&] { w = advReward(0); once &= advFinish() && gAdv.phase == ADV_IDLE && gAdv.freeDay == DAY; });
    ck(once && w.coins > 0, "a guest keeps and is paid from its own mirror");
    // a stale KEEP arriving afterwards (a resend, a duplicate) changes nothing
    const AdvRun before = runOf(ph[1]);
    as(ph[0], [] { gAdv.hearts = gAdv.hearts ? gAdv.hearts - 1 : 1; advSave(); });   // a new generation
    pump(1500);
    const AdvRun after = runOf(ph[1]);
    ck(after.phase == ADV_IDLE && before.phase == ADV_IDLE && after.net.role == ADV_SOLO,
       "a KEEP that arrives after the keep was paid is not applied again");
    as(ph[0], [] { advFinish(); });
    pump(200);
    ck(ph[0].net.state == RNET_LEFT, "the host's run finished, it stops hosting");
  }

  // --- a guest gives up mid-run -----------------------------------------------
  {
    ck(lobby(1) && startRun(1) && hostDoor(), "a fresh run with one guest");
    const uint8_t k = seatOf(ph[1]);
    as(ph[1], [] { advForfeit(); });
    pump(200);
    const AdvRun h = hostRun();
    ck(runOf(ph[1]).phase == ADV_KEEP && ph[1].net.state == RNET_LEFT && !((h.net.remote >> k) & 1) &&
       h.battle.seat[k].chooser == SEAT_AI, "a guest that gives up says goodbye: an AI takes its seat at once");
  }

  // --- the wire refuses what it cannot read ------------------------------------
  {
    ck(lobby(0), "a lone host");
    const AdvRun before = hostRun();
    uint8_t j[RNET_HDR + 32];
    memset(j, 0, sizeof(j));
    j[0] = RN_JOIN;
    ckptWr16(j + 1, 11 + ADV_NET_NAME);
    j[3] = RNET_PROTO + 1;
    std::vector<uint8_t> got;
    ph[1].net = RaidNet();
    ph[1].net.state = RNET_JOINING;   // only to collect what the host answers
    as(ph[0], [&] { ph[0].net.onPacket(0, j, RNET_HDR + 11 + ADV_NET_NAME); });
    const Pkt *r = ph[1].inbox.empty() ? nullptr : &ph[1].inbox.back();
    ck(r && r->b[0] == RN_REFUSE && r->b[3] == RNR_PROTO, "another protocol is refused, loudly");
    ph[1].inbox.clear();
    j[3] = RNET_PROTO;
    ckptWr16(j + 4, (uint16_t)(raidNetBuildTag() ^ 1));
    as(ph[0], [&] { ph[0].net.onPacket(0, j, RNET_HDR + 11 + ADV_NET_NAME); });
    r = ph[1].inbox.empty() ? nullptr : &ph[1].inbox.back();
    ck(r && r->b[0] == RN_REFUSE && r->b[3] == RNR_BUILD, "another build's tables are refused");
    // runts, lies about length, unknown types, and choices from nobody
    const uint8_t runt[2] = { RN_JOIN, 0 };
    uint8_t liar[RNET_HDR] = { RN_STATE, 0xFF, 0xFF };
    uint8_t act[RNET_HDR + 5] = { RN_ACT, 5, 0, 0, 0, 0, 0, 0 };
    uint8_t odd[RNET_HDR + 1] = { 0x7E, 1, 0, 9 };
    as(ph[0], [&] {
      ph[0].net.onPacket(0, runt, 2);
      ph[0].net.onPacket(0, liar, RNET_HDR);
      ph[0].net.onPacket(1, act, sizeof(act));
      ph[0].net.onPacket(2, odd, sizeof(odd));
      ph[0].net.onPacket(200, act, sizeof(act));
    });
    const AdvRun after = hostRun();
    ck(after.phase == before.phase && after.net.remote == 0 && !ph[0].net.peerUsed(0) &&
       !ph[0].net.peerUsed(1), "runts, length lies, unknown types and strangers' choices change nothing");
    ph[1].net = RaidNet();
  }

  // --- a guest refuses a state it cannot trust ----------------------------------
  {
    ck(lobby(1), "a guest in a lobby");
    // capture the next state the host sends, corrupt its run, and hand it over
    std::vector<uint8_t> st;
    ph[1].frozen = true;
    as(ph[0], [] { advSeatLoan(0, gAdv.loan[2]); });
    step();
    ph[1].frozen = false;
    for (Pkt &k : ph[1].inbox) if (k.b[0] == RN_STATE) st = k.b;
    ph[1].inbox.clear();
    const AdvRun before = runOf(ph[1]);
    bool ok = !st.empty();
    if (ok) {
      std::vector<uint8_t> bent = st;
      bent[bent.size() - 5] ^= 0x40;   // inside the run blob, under its CRC
      ckptWr32(bent.data() + RNET_HDR + 4, ph[1].net.appliedGen + 5);
      as(ph[1], [&] { ph[1].net.onPacket(0, bent.data(), (uint16_t)bent.size()); });
    }
    const AdvRun after = runOf(ph[1]);
    bool names = true;
    for (int i = 0; i < RAID_SEATS; i++) names &= !strcmp(after.net.name[i], before.net.name[i]);
    ck(ok && names && sameAdv(after.seatMon[0], before.seatMon[0]) && after.net.runId == before.net.runId &&
       sameAdv(after.seatMon[seatOf(ph[1])], before.seatMon[seatOf(ph[1])]),
       "a state whose run fails its CRC is not applied");
    if (ok) {
      as(ph[1], [&] { ph[1].net.onPacket(0, st.data(), (uint16_t)st.size()); });
      ck(runOf(ph[1]).seatMon[seatOf(ph[1])].dex == hostRun().loan[2].dex,
         "and the same state intact is (the host's new pick shows at its seat)");
    }
  }

  printf("%s raidnet_test (%d failed)\n", bad ? "FAIL" : "PASS", bad);
  return bad ? 1 : 0;
}
