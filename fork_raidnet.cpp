#include "fork_raidnet.h"
#include "fork.h"
#include "ckpt.h"
#include "crc32.h"
#include "moves.h"
#include "dex.h"
#include <Arduino.h>
#include <string.h>

// See fork_raidnet.h for the rules. Packets are [type][u16 length][payload].

// JOIN body
#define JB_PROTO 0
#define JB_BUILD 1        // u16
#define JB_PLAYER 3       // u32
#define JB_RUN 7          // u32, the run being rejoined (0: none)
#define JB_NAME 11
#define JB_LEN (JB_NAME + ADV_NET_NAME)

// STATE body. Everything from ST_SHARED on is the same for every guest; the
// part before it is addressed to one.
#define ST_SESSION 0      // u32
#define ST_GEN 4          // u32
#define ST_SEAT 8         // u8: yours, on the host's run
#define ST_LOANS 9        // ADV_LOANS x ADV_WIRE_MON: your three
#define ADV_WIRE_MON 8
#define ST_SHARED (ST_LOANS + ADV_LOANS * ADV_WIRE_MON)
// shared, relative to ST_SHARED:
#define SS_PICK 0         // u8: seats that have chosen this turn
#define SS_CATCH 1        // u8: seats that have decided on the catch
#define SS_LOAN 2         // u8: seats that have picked a loan (the lobby)
#define SS_TSTAGE 3       // u8: the last turn resolved: its stage (0xFF: none)
#define SS_TNO 4          // u16: and its battle turn
#define SS_EVN 6          // u8
#define SS_EV 7           // SS_EVN x RNET_EV_BYTES, then u16 blob length, blob

uint16_t raidNetBuildTag() {
  uint32_t h = 2166136261u;
  const uint16_t bits[] = {
    linkBuildTag(), (uint16_t)FORM_COUNT, (uint16_t)RAID_SEATS, (uint16_t)ADV_STAGES,
    (uint16_t)ADV_LOANS, (uint16_t)RNET_EV_BYTES, (uint16_t)RAID_EVENTS_MAX,
  };
  for (uint16_t b : bits) { h ^= b; h *= 16777619u; }
  return (uint16_t)(h ^ (h >> 16));
}

static uint32_t rnRandom() {
  uint32_t v = 0;
  while (!v) v = ((uint32_t)random(0x10000) << 16) ^ (uint32_t)random(0x10000);
  return v;
}

static void rnSend(RaidNet &n, uint8_t peer, uint8_t type, const uint8_t *body, uint16_t len) {
  if (!n.send || len > RNET_MAX_PAYLOAD) return;
  static uint8_t pkt[RNET_HDR + RNET_MAX_PAYLOAD];
  pkt[0] = type;
  ckptWr16(pkt + 1, len);
  if (len) memcpy(pkt + RNET_HDR, body, len);
  n.send(n.ctx, peer, pkt, (uint16_t)(RNET_HDR + len));
}

// --- a creature and an event on the wire ---

static void wrMon(uint8_t *p, const AdvMon &m) {
  ckptWr16(p, (uint16_t)m.dex);
  p[2] = m.form;
  p[3] = m.shiny ? 1 : 0;
  memcpy(p + 4, m.iv, 4);
}

// Untrusted, as everything off the air is (link.cpp, linkMonTo()): a bad one
// reads back as an empty slot, never as an index past a table.
static void rdMon(const uint8_t *p, AdvMon &m) {
  m = AdvMon();
  const int16_t dex = (int16_t)ckptRd16(p);
  if (dex < 1 || dex > DEX_COUNT) return;
  m.dex = dex;
  const uint8_t f = p[2];
  m.form = (forkForm(f) && FORM_TBL[f].dex == dex) ? f : 0;
  m.shiny = p[3] & 1;
  for (int i = 0; i < 4; i++) m.iv[i] = p[4 + i] > 31 ? 31 : p[4 + i];
}

static void wrEvent(uint8_t *p, const RaidEvent &e) {
  p[0] = e.actor; p[1] = e.target;
  p[2] = (e.transformed ? 1 : 0) | (e.endTurn ? 2 : 0) | (e.shielded ? 4 : 0) |
         (e.barrier ? 8 : 0) | (e.staggered ? 16 : 0) | (e.revived ? 32 : 0);
  p[3] = e.shieldsBroken; p[4] = e.heartsLost;
  const TurnLog &l = e.log;
  p[5] = l.move;
  ckptWr16(p + 6, l.damage);
  ckptWr16(p + 8, l.effPct);
  p[10] = l.hits; p[11] = l.inflicted; p[12] = (uint8_t)l.stageDelta; p[13] = l.stageMask;
  p[14] = (l.missed ? 1 : 0) | (l.crit ? 2 : 0) | (l.immune ? 4 : 0) | (l.skipped ? 8 : 0) |
          (l.hurtSelf ? 16 : 0) | (l.charged ? 32 : 0) | (l.healed ? 64 : 0) |
          (l.targetFainted ? 128 : 0);
}

static uint8_t rdActor(uint8_t a) { return a <= RAID_BOSS_ACTOR ? a : RAID_BOSS_ACTOR; }

static void rdEvent(const uint8_t *p, RaidEvent &e) {
  e = RaidEvent();
  e.actor = rdActor(p[0]); e.target = rdActor(p[1]);
  e.transformed = p[2] & 1; e.endTurn = p[2] & 2; e.shielded = p[2] & 4;
  e.barrier = p[2] & 8; e.staggered = p[2] & 16; e.revived = p[2] & 32;
  e.shieldsBroken = p[3] > RAID_BARRIERS_MAX * 8 ? 0 : p[3];
  e.heartsLost = p[4] > RAID_SEATS ? RAID_SEATS : p[4];
  TurnLog &l = e.log;
  l.move = p[5] < MOVE_COUNT ? p[5] : 0;
  l.damage = ckptRd16(p + 6);
  l.effPct = ckptRd16(p + 8);
  l.hits = p[10];
  l.inflicted = p[11] <= AIL_CONFUSE ? p[11] : AIL_NONE;
  l.stageDelta = (int8_t)p[12]; l.stageMask = p[13];
  l.missed = p[14] & 1; l.crit = p[14] & 2; l.immune = p[14] & 4; l.skipped = p[14] & 8;
  l.hurtSelf = p[14] & 16; l.charged = p[14] & 32; l.healed = p[14] & 64;
  l.targetFainted = p[14] & 128;
}

// --- the guest's view: its own seat is seat 0 ---

static uint8_t swapSeat(uint8_t i, uint8_t k) { return i == 0 ? k : (i == k ? 0 : i); }
static uint8_t swapMask(uint8_t m, uint8_t k) {
  uint8_t out = 0;
  for (uint8_t i = 0; i < RAID_SEATS; i++)
    if ((m >> i) & 1) out |= (uint8_t)(1u << swapSeat(i, k));
  return out;
}

template <typename T> static void swapT(T &a, T &b) { T t = a; a = b; b = t; }

static void remap(AdvRun &v, uint8_t k) {
  if (k && k < RAID_SEATS) {
    swapT(v.seatMon[0], v.seatMon[k]);
    swapT(v.seat[0], v.seat[k]);
    swapT(v.battle.seat[0], v.battle.seat[k]);
    swapT(v.net.player[0], v.net.player[k]);
    char t[ADV_NET_NAME];
    memcpy(t, v.net.name[0], ADV_NET_NAME);
    memcpy(v.net.name[0], v.net.name[k], ADV_NET_NAME);
    memcpy(v.net.name[k], t, ADV_NET_NAME);
  }
  // Who chooses, seen from here: me, and the people elsewhere (the host's
  // seat included). The AI seats stay the host's AI's.
  v.net.remote = (uint8_t)(swapMask(v.net.remote, k) | (k ? (1u << k) : 0)) & 0x0E;
  for (uint8_t i = 0; i < RAID_SEATS; i++) {
    RaidSeat &s = v.battle.seat[i];
    if (!s.occupied()) continue;
    s.chooser = i == 0 ? SEAT_LOCAL : ((v.net.remote >> i) & 1) ? SEAT_REMOTE : SEAT_AI;
    s.pick = 0;
    s.transform = false;
  }
}

// Everything back to its initialiser except the transport, which belongs to
// whoever plugged it in (as Link::begin() keeps `send`).
void RaidNet::reset() {
  auto *s = send;
  void *c = ctx;
  *this = RaidNet();
  send = s;
  ctx = c;
}

// --- host ---

static void hostCatchReset(RaidNet &n) {
  n.catchMask = n.catchSwap = 0;
  n.catchStage = gAdv.stage;
}

bool RaidNet::host(uint32_t me, const char *nm, RaidRoll r) {
  AdvRun &a = gAdv;
  const bool fresh = a.phase == ADV_LOAN && a.net.role == ADV_SOLO && !a.seatMon[0].dex;
  const bool resume = a.net.role == ADV_HOSTED && a.phase != ADV_IDLE;
  if (!me || (!fresh && !resume)) return false;
  reset();
  isHost = true;
  player = me;
  if (nm) strncpy(name, nm, ADV_NET_NAME - 1);
  roll = r;
  if (fresh) {
    a.net = AdvNet();
    a.net.role = ADV_HOSTED;
    a.net.runId = rnRandom();
  }
  a.net.player[0] = me;
  memset(a.net.name[0], 0, ADV_NET_NAME);
  strncpy(a.net.name[0], name, ADV_NET_NAME - 1);
  // Nobody is connected to a host that has only just started listening. In
  // the lobby nothing is lost by freeing their seats; mid-run they stay
  // theirs, played by the AI, until they rejoin.
  for (uint8_t i = 1; i < RAID_SEATS; i++) {
    if (a.phase == ADV_LOAN && a.net.player[i]) advClaimSeat(i, 0, nullptr, roll);
    else advSetRemote(i, false);
  }
  advSave();
  session = rnRandom();
  catchStage = a.stage;
  state = RNET_IN;
  return true;
}

void RaidNet::dropPeer(uint8_t p) {
  if (!isHost || p >= RNET_PEERS || !peer[p].used) return;
  const uint8_t k = peer[p].seat;
  peer[p] = RaidNetPeer();
  for (const RaidNetPeer &o : peer)
    if (o.used && o.seat == k) return;   // it is back already, from another address
  if (gAdv.phase == ADV_LOAN) {
    advClaimSeat(k, 0, nullptr, roll);
    loanMask &= (uint8_t)~(1u << k);
  } else {
    advSetRemote(k, false);
  }
}

static void hostRefuse(RaidNet &n, uint8_t p, uint8_t why) {
  rnSend(n, p, RN_REFUSE, &why, 1);
}

static void hostJoin(RaidNet &n, uint8_t p, const uint8_t *b, uint16_t len) {
  if (len < JB_LEN) return;
  if (b[JB_PROTO] != RNET_PROTO) { hostRefuse(n, p, RNR_PROTO); return; }
  if (ckptRd16(b + JB_BUILD) != raidNetBuildTag()) { hostRefuse(n, p, RNR_BUILD); return; }
  const uint32_t who = ckptRd32(b + JB_PLAYER), run = ckptRd32(b + JB_RUN);
  if (!who || who == n.player) return;
  char nm[ADV_NET_NAME];
  memcpy(nm, b + JB_NAME, ADV_NET_NAME);
  nm[ADV_NET_NAME - 1] = 0;
  AdvRun &a = gAdv;
  if (a.net.role != ADV_HOSTED || a.phase == ADV_IDLE) { hostRefuse(n, p, RNR_GONE); return; }

  RaidNetPeer &me = n.peer[p];
  if (me.used && me.player == who) { me.sendNow = true; return; }   // a resend
  if (me.used) n.dropPeer(p);   // the transport gave this slot to somebody new
  // Their seat, if they have one: the same person back, from this address or
  // a new one. Mid-run it must be THIS run they are rejoining; in the lobby
  // nothing has happened yet that a stranger could take.
  int8_t k = -1;
  for (uint8_t i = 1; i < RAID_SEATS; i++)
    if (a.net.player[i] == who) k = (int8_t)i;
  if (k >= 0 && a.phase != ADV_LOAN && run != a.net.runId) k = -1;
  if (k < 0) {
    if (a.phase != ADV_LOAN) {
      bool known = false;
      for (uint8_t i = 1; i < RAID_SEATS; i++) known |= a.net.player[i] == who;
      hostRefuse(n, p, known || a.phase == ADV_KEEP ? RNR_GONE : RNR_STARTED);
      return;
    }
    for (uint8_t i = 1; i < RAID_SEATS && k < 0; i++)
      if (!a.net.player[i]) k = (int8_t)i;
    if (k < 0) { hostRefuse(n, p, RNR_FULL); return; }
    if (!advClaimSeat((uint8_t)k, who, nm, n.roll)) return;
    advRollLoans(n.seatLoans[k], n.roll);
    n.loanMask &= (uint8_t)~(1u << k);
  } else {
    // a new address for somebody already seated: the old one is stale
    for (uint8_t q = 0; q < RNET_PEERS; q++)
      if (q != p && n.peer[q].used && n.peer[q].seat == (uint8_t)k) n.peer[q] = RaidNetPeer();
    strncpy(a.net.name[k], nm, ADV_NET_NAME - 1);
    if (a.phase == ADV_LOAN && !n.seatLoans[k][0].dex) advRollLoans(n.seatLoans[k], n.roll);
  }
  me = RaidNetPeer();
  me.used = true;
  me.seat = (uint8_t)k;
  me.player = who;
  me.sawRx = true;
  me.sendNow = true;
  advSetRemote((uint8_t)k, true);
}

static int8_t hostSeatOf(RaidNet &n, uint8_t p) {
  return (p < RNET_PEERS && n.peer[p].used) ? (int8_t)n.peer[p].seat : -1;
}

static void hostPacket(RaidNet &n, uint8_t p, uint8_t type, const uint8_t *b, uint16_t len) {
  if (p >= RNET_PEERS) return;
  if (type == RN_JOIN) { hostJoin(n, p, b, len); return; }
  const int8_t k = hostSeatOf(n, p);
  if (k < 0) return;               // not one of ours: it has to JOIN first
  n.peer[p].sawRx = true;
  AdvRun &a = gAdv;
  switch (type) {
    case RN_ACK:
      if (len >= 8 && ckptRd32(b) == n.session) n.peer[p].acked = ckptRd32(b + 4);
      return;
    case RN_LOAN:
      if (len < 1 || a.phase != ADV_LOAN || b[0] >= ADV_LOANS) return;
      if (advSeatLoan((uint8_t)k, n.seatLoans[k][b[0]])) n.loanMask |= (uint8_t)(1u << k);
      return;
    case RN_ACT:
      // For this fight and this turn, or it is a resend of one already done.
      if (len < 5 || a.phase != ADV_FIGHT || b[0] != a.stage) return;
      if (ckptRd16(b + 1) != a.battle.turn) return;
      if (a.battle.seat[k].chooser != SEAT_REMOTE) return;
      advSeatAction((uint8_t)k, b[3], b[4] != 0);
      return;
    case RN_CATCH:
      if (len < 2 || a.phase != ADV_CATCH || b[0] != a.stage) return;
      n.catchMask |= (uint8_t)(1u << k);
      if (b[1]) n.catchSwap |= (uint8_t)(1u << k);
      else n.catchSwap &= (uint8_t)~(1u << k);
      return;
    case RN_BYE:
      n.dropPeer(p);
      return;
    default:
      return;
  }
}

// The part of a state every guest shares. Built once a tick; its CRC decides
// whether anything changed, so no caller has to remember to say so.
static uint16_t hostShared(RaidNet &n, uint8_t *out, size_t cap) {
  const AdvRun &a = gAdv;
  uint8_t pick = 0;
  if (a.phase == ADV_FIGHT)
    for (uint8_t i = 0; i < RAID_SEATS; i++)
      if (a.battle.seat[i].pick) pick |= (uint8_t)(1u << i);
  n.pickMask = pick;
  out[SS_PICK] = pick;
  out[SS_CATCH] = a.phase == ADV_CATCH ? n.catchMask : 0;
  out[SS_LOAN] = a.phase == ADV_LOAN ? (uint8_t)(n.loanMask | (n.hostLoan >= 0 ? 1 : 0)) : 0;
  out[SS_TSTAGE] = n.turnStage;
  ckptWr16(out + SS_TNO, n.turnNo);
  const uint8_t evn = n.turn.n;
  out[SS_EVN] = evn;
  uint8_t *p = out + SS_EV;
  for (uint8_t i = 0; i < evn; i++, p += RNET_EV_BYTES) wrEvent(p, n.turn.ev[i]);
  const size_t room = cap - (size_t)(p + 2 - out);
  const uint16_t blob = advEncode(a, p + 2, room, 0);
  if (!blob) return 0;
  ckptWr16(p, blob);
  return (uint16_t)(p + 2 + blob - out);
}

static void hostSendState(RaidNet &n, uint8_t p, const uint8_t *shared, uint16_t sharedN) {
  static uint8_t body[RNET_MAX_PAYLOAD];
  if ((size_t)ST_SHARED + sharedN > sizeof(body)) return;
  const RaidNetPeer &q = n.peer[p];
  ckptWr32(body + ST_SESSION, n.session);
  ckptWr32(body + ST_GEN, n.gen);
  body[ST_SEAT] = q.seat;
  for (uint8_t i = 0; i < ADV_LOANS; i++)
    wrMon(body + ST_LOANS + i * ADV_WIRE_MON, n.seatLoans[q.seat][i]);
  memcpy(body + ST_SHARED, shared, sharedN);
  rnSend(n, p, RN_STATE, body, (uint16_t)(ST_SHARED + sharedN));
}

static uint32_t resendWait(uint32_t seed, uint8_t seq) {
  return RNET_RESEND_MS + (seed + seq * 37u) % RNET_JITTER_MS;
}

static void hostTick(RaidNet &n, uint32_t now) {
  AdvRun &a = gAdv;
  if (a.net.role != ADV_HOSTED) {   // the run was finished: nothing left to host
    for (uint8_t p = 0; p < RNET_PEERS; p++)
      if (n.peer[p].used) rnSend(n, p, RN_BYE, nullptr, 0);
    n.state = RNET_LEFT;
    return;
  }
  for (uint8_t p = 0; p < RNET_PEERS; p++) {
    RaidNetPeer &q = n.peer[p];
    if (!q.used) continue;
    if (!q.armed) { q.armed = true; q.lastRx = now; q.txAt = now; }
    if (q.sawRx) { q.sawRx = false; q.lastRx = now; }
    if (now - q.lastRx > RNET_TIMEOUT_MS) n.dropPeer(p);
  }

  // The turn: once every person has chosen, the AI plans around them.
  if (a.phase == ADV_FIGHT && raidPeopleReady(a.battle)) {
    const uint8_t stage = a.stage;
    if (advResolve(n.turn, n.roll)) {
      n.turnStage = stage;
      n.turnNo = a.battle.turn;
      n.turnNew = true;
    }
  }
  // The catch: once the host and every connected guest have decided.
  if (a.phase != ADV_CATCH || n.catchStage != a.stage) hostCatchReset(n);
  if (a.phase == ADV_CATCH) {
    const uint8_t need = (uint8_t)(1u | a.net.remote);
    if ((n.catchMask & need) == need && advCatchSeats(n.catchSwap & need, n.roll))
      hostCatchReset(n);
  }
  if (a.phase != ADV_LOAN) { n.loanMask = 0; n.hostLoan = -1; }

  static uint8_t shared[RNET_MAX_PAYLOAD];
  const uint16_t sn = hostShared(n, shared, sizeof(shared) - ST_SHARED);
  if (!sn) return;
  const uint32_t crc = crc32Update(0, shared, sn);
  if (crc != n.stateCrc || !n.gen) {
    n.stateCrc = crc;
    n.gen++;
    for (RaidNetPeer &q : n.peer) q.sendNow = q.used;
  }
  for (uint8_t p = 0; p < RNET_PEERS; p++) {
    RaidNetPeer &q = n.peer[p];
    if (!q.used) continue;
    const bool due = now - q.txAt >= resendWait(q.player, q.resendSeq);
    if (!q.sendNow && !due) continue;
    q.sendNow = false;
    q.txAt = now;
    q.resendSeq++;
    // Unconfirmed: the state again. Confirmed: a ping, so a guest reading the
    // screen for a minute is not taken for gone.
    if (q.acked != n.gen) hostSendState(n, p, shared, sn);
    else rnSend(n, p, RN_PING, nullptr, 0);
  }
}

// --- guest ---

bool RaidNet::join(uint32_t me, const char *nm, uint32_t today) {
  const AdvRun &a = gAdv;
  const bool fresh = a.phase == ADV_IDLE;
  const bool rejoin = a.net.role == ADV_GUEST && a.phase != ADV_IDLE && a.phase != ADV_KEEP;
  if (!me || (!fresh && !rejoin)) return false;
  reset();
  isHost = false;
  player = me;
  if (nm) strncpy(name, nm, ADV_NET_NAME - 1);
  day = today;
  bound = rejoin;
  charge = fresh ? advStartCost(today) : 0;
  if (rejoin) mySeat = a.net.seat;
  state = RNET_JOINING;
  uint8_t b[JB_LEN];
  memset(b, 0, sizeof(b));
  b[JB_PROTO] = RNET_PROTO;
  ckptWr16(b + JB_BUILD, raidNetBuildTag());
  ckptWr32(b + JB_PLAYER, player);
  ckptWr32(b + JB_RUN, rejoin ? a.net.runId : 0);
  memcpy(b + JB_NAME, name, ADV_NET_NAME);
  rnSend(*this, 0, RN_JOIN, b, JB_LEN);   // and again from tick() until answered
  return true;
}

static void guestJoinAgain(RaidNet &n) {
  const AdvRun &a = gAdv;
  uint8_t b[JB_LEN];
  memset(b, 0, sizeof(b));
  b[JB_PROTO] = RNET_PROTO;
  ckptWr16(b + JB_BUILD, raidNetBuildTag());
  ckptWr32(b + JB_PLAYER, n.player);
  ckptWr32(b + JB_RUN, n.bound ? a.net.runId : 0);
  memcpy(b + JB_NAME, n.name, ADV_NET_NAME);
  rnSend(n, 0, RN_JOIN, b, JB_LEN);
}

static void guestSetPend(RaidNet &n, uint8_t type, const uint8_t *body, uint8_t len) {
  n.pend[0] = type;
  ckptWr16(n.pend + 1, len);
  memcpy(n.pend + RNET_HDR, body, len);
  n.pendN = (uint8_t)(RNET_HDR + len);
  rnSend(n, 0, type, body, len);
}

static void guestAck(RaidNet &n) {
  uint8_t b[8];
  ckptWr32(b, n.hostSession);
  ckptWr32(b + 4, n.appliedGen);
  rnSend(n, 0, RN_ACK, b, 8);
}

// Has the host taken the choice we keep resending? Read off the newest state.
static void guestSettlePend(RaidNet &n) {
  if (!n.pendN) return;
  const AdvRun &a = gAdv;
  const uint8_t *b = n.pend + RNET_HDR;
  bool done = false;
  switch (n.pend[0]) {
    case RN_LOAN:
      done = a.phase != ADV_LOAN ||
             ((n.loanSeen & 1) && a.seatMon[0].dex == a.loan[b[0] < ADV_LOANS ? b[0] : 0].dex);
      break;
    case RN_ACT:
      done = a.phase != ADV_FIGHT || a.stage != b[0] || a.battle.turn != ckptRd16(b + 1) ||
             (n.pickMask & 1);
      break;
    case RN_CATCH:
      done = a.phase != ADV_CATCH || a.stage != b[0] || (n.catchSeen & 1);
      break;
    default:
      done = true;
  }
  if (done) n.pendN = 0;
}

static void guestState(RaidNet &n, const uint8_t *b, uint16_t len) {
  if (len < ST_SHARED + SS_EV + 2) return;
  const uint32_t sess = ckptRd32(b + ST_SESSION), g = ckptRd32(b + ST_GEN);
  const uint8_t seat = b[ST_SEAT];
  if (!seat || seat >= RAID_SEATS) return;
  if (sess != n.hostSession) { n.hostSession = sess; n.appliedGen = 0; }
  if (n.appliedGen && !generationAfter(g, n.appliedGen)) { guestAck(n); return; }
  const uint8_t *s = b + ST_SHARED;
  const uint8_t evn = s[SS_EVN];
  if (evn > RAID_EVENTS_MAX) return;
  const size_t blobAt = ST_SHARED + SS_EV + (size_t)evn * RNET_EV_BYTES;
  if (blobAt + 2 > len) return;
  const uint16_t bn = ckptRd16(b + blobAt);
  const uint8_t *blob = b + blobAt + 2;
  if (blobAt + 2 + bn > len || bn < CKPT_HDR + CKPT_CRC) return;
  if (ckptRd32(blob) != ADV_MAGIC || ckptRd16(blob + 6) != bn) return;
  if (ckptRd16(blob + bn - CKPT_CRC) != ckptCrc(blob, bn - CKPT_CRC)) return;
  AdvRun v;
  if (!advDecode(blob, bn, v) || v.net.role != ADV_HOSTED || !v.net.runId) return;
  if (v.phase == ADV_IDLE) return;

  // Is this state ours to apply? A guest with no run takes the first one; one
  // already in a run takes only that run's, and only until its KEEP -- from
  // there the keep is this phone's own business, and re-applying a KEEP after
  // it was paid would pay it twice.
  AdvRun &a = gAdv;
  if (n.bound) {
    if (a.net.role != ADV_GUEST || a.net.runId != v.net.runId) return;
    if (a.phase == ADV_KEEP) {
      // Still at the host's KEEP: what follows it is acknowledged, so the
      // host knows we have it, and never applied.
      if (n.hostKeep) { n.appliedGen = g; guestAck(n); }
      return;
    }
  } else if (a.phase != ADV_IDLE) {
    return;
  }

  remap(v, seat);
  v.net.role = ADV_GUEST;
  v.net.seat = seat;
  v.freeDay = n.bound ? a.freeDay : n.day;   // ours, never the host's
  for (uint8_t i = 0; i < ADV_LOANS; i++) rdMon(b + ST_LOANS + i * ADV_WIRE_MON, v.loan[i]);
  const bool first = !n.bound;
  // The first state of a connection (a join, a rejoin, a host come back)
  // carries whatever turn the host last resolved: history, not news.
  const bool fresh = !n.appliedGen;
  n.hostKeep = v.phase == ADV_KEEP;
  a = v;
  a.battle.hearts = &a.hearts;
  advSave();
  n.bound = true;
  n.mySeat = seat;
  n.appliedGen = g;
  if (n.state == RNET_JOINING) n.state = RNET_IN;
  if (first && n.charge) n.chargeDue = true;

  n.pickMask = swapMask(s[SS_PICK], seat);
  n.catchSeen = swapMask(s[SS_CATCH], seat);
  n.loanSeen = swapMask(s[SS_LOAN], seat);
  const uint8_t ts = s[SS_TSTAGE];
  const uint16_t tn = ckptRd16(s + SS_TNO);
  if (ts != 0xFF && (ts != n.turnStage || tn != n.turnNo)) {
    n.turn.n = fresh ? 0 : evn;
    for (uint8_t i = 0; i < n.turn.n; i++) {
      RaidEvent &e = n.turn.ev[i];
      rdEvent(s + SS_EV + i * RNET_EV_BYTES, e);
      if (e.actor < RAID_SEATS) e.actor = swapSeat(e.actor, seat);
      if (e.target < RAID_SEATS) e.target = swapSeat(e.target, seat);
    }
    n.turnStage = ts;
    n.turnNo = tn;
    n.turnNew = n.turn.n > 0;
  }
  guestSettlePend(n);
  guestAck(n);
}

static void guestPacket(RaidNet &n, uint8_t type, const uint8_t *b, uint16_t len) {
  if (n.state != RNET_JOINING && n.state != RNET_IN) return;
  n.sawRx = true;
  switch (type) {
    case RN_STATE: guestState(n, b, len); return;
    case RN_REFUSE:
      n.refused = len ? b[0] : RNR_GONE;
      n.state = RNET_REFUSED;
      n.pendN = 0;
      return;
    case RN_BYE:
      n.state = RNET_LOST;
      n.pendN = 0;
      return;
    default:
      return;   // a ping: proof of life, already stamped
  }
}

static void guestTick(RaidNet &n, uint32_t now) {
  if (!n.armed) { n.armed = true; n.lastRx = now; n.txAt = now; }
  if (n.sawRx) { n.sawRx = false; n.lastRx = now; }
  const uint32_t limit = n.state == RNET_JOINING ? RNET_JOIN_TIMEOUT_MS : RNET_TIMEOUT_MS;
  if (now - n.lastRx > limit) { n.state = RNET_LOST; n.pendN = 0; return; }
  // The run is over for us once this phone has kept (or gave up on its own):
  // goodbye, so the host stops waiting for us and, mid-run, an AI takes the
  // seat at once rather than after a timeout. At the host's KEEP we stay,
  // still acknowledging, until our own keep is done.
  const AdvRun &a = gAdv;
  if (n.bound && (a.net.role != ADV_GUEST || a.phase == ADV_KEEP)) {
    const bool atHostKeep = n.hostKeep && a.net.role == ADV_GUEST && a.phase == ADV_KEEP;
    if (!atHostKeep) { n.leave(); return; }
    n.pendN = 0;   // nothing left to choose; the acks go on until we keep
  }
  if (now - n.txAt < resendWait(n.player, n.resendSeq)) return;
  n.txAt = now;
  n.resendSeq++;
  if (n.state == RNET_JOINING) { guestJoinAgain(n); return; }
  guestAck(n);   // the keepalive, and a repeat of the last ack
  if (n.pendN) rnSend(n, 0, n.pend[0], n.pend + RNET_HDR, ckptRd16(n.pend + 1));
}

// --- both ---

void RaidNet::onPacket(uint8_t p, const uint8_t *buf, uint16_t len) {
  if (state == RNET_OFF || len < RNET_HDR) return;
  const uint16_t n = ckptRd16(buf + 1);
  if (n > RNET_MAX_PAYLOAD || (uint32_t)RNET_HDR + n > len) return;   // truncated
  if (isHost) hostPacket(*this, p, buf[0], buf + RNET_HDR, n);
  else guestPacket(*this, buf[0], buf + RNET_HDR, n);
}

void RaidNet::tick(uint32_t now) {
  if (isHost) { if (state == RNET_IN) hostTick(*this, now); return; }
  if (state == RNET_JOINING || state == RNET_IN) guestTick(*this, now);
}

void RaidNet::leave() {
  if (state == RNET_OFF) return;
  if (isHost) {
    for (uint8_t p = 0; p < RNET_PEERS; p++)
      if (peer[p].used) rnSend(*this, p, RN_BYE, nullptr, 0);
  } else if (state == RNET_JOINING || state == RNET_IN) {
    rnSend(*this, 0, RN_BYE, nullptr, 0);
  }
  pendN = 0;
  state = RNET_LEFT;
}

bool RaidNet::pickLoan(uint8_t i) {
  if (state != RNET_IN || gAdv.phase != ADV_LOAN || i >= ADV_LOANS || !gAdv.loan[i].dex) return false;
  if (isHost) {
    if (!advSeatLoan(0, gAdv.loan[i])) return false;
    hostLoan = (int8_t)i;
    return true;
  }
  guestSetPend(*this, RN_LOAN, &i, 1);
  return true;
}

bool RaidNet::act(uint8_t slot, bool transform) {
  if (state != RNET_IN || gAdv.phase != ADV_FIGHT) return false;
  if (isHost) return advSeatAction(0, slot, transform);
  const RaidSeat &s = gAdv.battle.seat[0];
  if (slot >= MOVE_SLOTS || !s.mon.moves[slot] || s.mon.moves[slot] >= MOVE_COUNT) return false;
  if (!s.mon.hp || (transform && (s.formed || !s.form))) return false;
  uint8_t b[5];
  b[0] = gAdv.stage;
  ckptWr16(b + 1, gAdv.battle.turn);
  b[3] = slot;
  b[4] = transform ? 1 : 0;
  guestSetPend(*this, RN_ACT, b, 5);
  return true;
}

bool RaidNet::catchChoice(bool swap) {
  if (state != RNET_IN || gAdv.phase != ADV_CATCH) return false;
  if (isHost) {
    if (catchStage != gAdv.stage) hostCatchReset(*this);
    catchMask |= 1;
    catchSwap = (uint8_t)(swap ? (catchSwap | 1) : (catchSwap & ~1u));
    return true;
  }
  const uint8_t b[2] = { gAdv.stage, (uint8_t)(swap ? 1 : 0) };
  guestSetPend(*this, RN_CATCH, b, 2);
  return true;
}

bool RaidNet::canStart() const {
  if (!isHost || state != RNET_IN || gAdv.phase != ADV_LOAN || hostLoan < 0) return false;
  for (uint8_t i = 1; i < RAID_SEATS; i++)
    if (gAdv.net.player[i] && !((loanMask >> i) & 1)) return false;
  return true;
}

bool RaidNet::start() {
  if (!canStart()) return false;
  return advPickLoan((uint8_t)hostLoan, roll);
}

bool RaidNet::allSeen() const {
  if (!isHost) return false;
  for (const RaidNetPeer &q : peer)
    if (q.used && q.acked != gen) return false;
  return true;
}

uint8_t RaidNet::players() const {
  if (state != RNET_IN) return 0;
  // On either side `remote` is the OTHER people, seen from here (a guest's
  // view counts the host among them).
  uint8_t n = 1;
  for (uint8_t i = 1; i < RAID_SEATS; i++) n += (gAdv.net.remote >> i) & 1;
  return n;
}
