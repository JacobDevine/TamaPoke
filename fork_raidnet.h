#pragma once
#include <stdint.h>
#include "fork_adv.h"
#include "link.h"   // the resend and timeout constants, shared with the 1v1 link

// Adventures with other players: one host and up to three guests on one run
// (android/FEATURES_PLAN.md, step 12e part 2). Fork-only: the board never
// builds a run, so nothing here is ever reached there.
//
// link.cpp is two-party, and a raid is not, but its three rules carry over
// unchanged, and so does its reason for one device being in charge:
//
//   THE HOST RESOLVES EVERYTHING. Its gAdv is the run; a guest's gAdv is a
//   mirror of it (fork_adv.h, AdvNet), drawn by the same screens and moved
//   only by what the host sends. A guest sends CHOICES -- a loan, a move, a
//   catch -- and never an outcome.
//
//   1. Every choice is STAMPED (the stage and the battle turn it is for), so a
//      resend is never read as a second choice, and one that arrives late for
//      a turn already resolved is dropped.
//   2. Whatever is not yet confirmed is RESENT. A guest repeats its choice
//      until the host's state shows it was taken; the host repeats its state
//      until each guest acknowledges that generation of it.
//   3. The host sends ABSOLUTE STATE: the whole run, as its own save record
//      (advEncode()). A guest that missed ten packets lands on the right run
//      from the next one; only the narration of the turns it missed is lost.
//
// And every wait has a deadline. A guest the host has not heard from for
// RNET_TIMEOUT_MS is DROPPED: in the lobby its seat goes back to an AI
// companion, mid-run an AI plays it (the 12d team rules) until the same
// player rejoins and takes it back. A guest that stops hearing the host is
// LOST, and keeps its mirror: it can rejoin, or give up and keep what it
// caught, as a solo run's GIVE UP does.
//
// THE LOBBY IS THE LOAN PHASE. The host starts a run as a solo one is started
// (advStart(), which picks the difficulty and pays), then hosts it: guests
// join and each gets three loans of its own, rolled by the host; everyone
// picks; the host's start is advPickLoan() for itself. A seat nobody joined
// keeps the AI companion advStart() rolled for it.
//
// EACH PLAYER PAYS FOR THEIR OWN RUN. A guest's first state is when its run
// begins: that day's free run is marked taken in ITS record (freeDay), and
// `chargeDue` asks the caller to take `charge` coins -- after the mirror was
// saved, the same order a solo start uses, so a cut between the two gives a
// free run rather than taking coins for nothing.
//
// The transport is a function pointer with a PEER number, so tests can wire a
// host and three guests in one process over a lossy channel. A host's peers
// are whatever the transport numbers its senders (0..RNET_PEERS-1); a guest
// has one peer, the host, and ignores the number.

#define RNET_PROTO 1   // bump on ANY wire change; a mismatch is refused
#define RNET_HDR 3     // type, u16 length
#define RNET_MAX_PAYLOAD 1400   // a state is about 1 KB: one datagram on a LAN
// Transport slots: three guests and one more, so a guest coming back from a
// new address (a restarted app) can be heard before its old slot times out.
#define RNET_PEERS (RAID_SEATS)
#define RNET_EV_BYTES 15
#define RNET_RESEND_MS LINK_RESEND_MS
#define RNET_JITTER_MS LINK_JITTER_MS
#define RNET_JOIN_TIMEOUT_MS LINK_PAIR_TIMEOUT_MS
#define RNET_TIMEOUT_MS LINK_BATTLE_TIMEOUT_MS
// A connected player who has not chosen this long into a turn, with everybody
// else ready, has the AI play that one turn for them (part 3). Long enough to
// read the last turn's narration and think; short enough that three people
// are not left waiting on one.
#define RNET_TURN_MS 45000

// Not link.cpp's numbers (1..8): a packet meant for one protocol must never
// parse as the other's.
enum RnetMsg : uint8_t {
  RN_JOIN = 0x51,   // guest -> host: who I am, and the run I am rejoining (0: none)
  RN_STATE,         // host -> guest: the run, your seat, your loans, the last turn
  RN_REFUSE,        // host -> guest: why not
  RN_ACK,           // guest -> host: the state generation I hold (also my keepalive)
  RN_LOAN,          // guest -> host: my loan pick
  RN_ACT,           // guest -> host: my move, stamped with stage and turn
  RN_CATCH,         // guest -> host: swap for the catch or not, stamped with stage
  RN_BYE,           // either way: leaving on purpose
  RN_PING,          // host -> guest: nothing new, still here
};

enum RnetState : uint8_t {
  RNET_OFF = 0,
  RNET_JOINING,   // guest: asking
  RNET_IN,        // in the run (the host always is, once hosting)
  RNET_REFUSED,   // guest: told no, see `refused`
  RNET_LOST,      // guest: the host went quiet or said goodbye
  RNET_LEFT,      // left on purpose
};

enum RnetRefuse : uint8_t {
  RNR_NONE = 0,
  RNR_PROTO,     // another protocol
  RNR_BUILD,     // other tables: a move index would name another move
  RNR_FULL,      // every seat is taken
  RNR_STARTED,   // the run is past its lobby, and this player was not in it
  RNR_GONE,      // there is no run any more
};

struct RaidNetPeer {
  bool used = false;
  uint8_t seat = 0;            // on the host's run
  uint32_t player = 0;
  uint32_t acked = 0;          // the state generation it confirmed
  bool sawRx = false;
  bool armed = false;
  uint32_t lastRx = 0, txAt = 0;
  uint8_t resendSeq = 0;
  bool sendNow = false;
};

struct RaidNet {
  uint8_t state = RNET_OFF;
  bool isHost = false;
  uint32_t player = 0;                  // this phone's player id, never 0
  char name[ADV_NET_NAME] = "";
  RaidRoll roll = nullptr;              // the host's dice; nullptr = random()

  // --- host ---
  RaidNetPeer peer[RNET_PEERS];
  uint32_t session = 0;                 // changes when the host (re)starts hosting
  uint32_t gen = 0;                     // bumped whenever the state changes
  uint32_t stateCrc = 0;
  AdvMon seatLoans[RAID_SEATS][ADV_LOANS];
  uint8_t loanMask = 0;                 // lobby: seats that have picked a loan
  int8_t hostLoan = -1;                 // lobby: the host's own pick
  uint8_t catchMask = 0, catchSwap = 0; // CATCH: who has decided, and to swap
  uint8_t catchStage = 0xFF;
  uint32_t turnKey = 0, turnAt = 0;     // the turn being chosen (stage, turn) and since when
  uint8_t stalled = 0;                  // seats the AI has played a turn for, ever (diagnostics)

  // --- both: the last turn resolved, for the screen to narrate once ---
  RaidTurn turn;
  uint8_t turnStage = 0xFF;
  uint16_t turnNo = 0;
  bool turnNew = false;
  uint8_t pickMask = 0;                 // seats that have chosen this turn (my view)
  uint8_t catchSeen = 0;                // guest: CATCH decisions, my view
  uint8_t loanSeen = 0;                 // guest: lobby picks, my view

  // --- guest ---
  uint8_t refused = RNR_NONE;
  uint32_t hostSession = 0, appliedGen = 0;
  uint8_t mySeat = 0;                   // on the host's run
  bool bound = false;                   // the mirror is this host's run
  bool hostKeep = false;                // the host's run reached its KEEP
  uint32_t day = 0, charge = 0;
  bool chargeDue = false;
  uint32_t lastRx = 0, txAt = 0;
  bool sawRx = false, armed = false;
  uint8_t resendSeq = 0;
  uint8_t pend[RNET_HDR + 8];           // the choice not yet confirmed
  uint8_t pendN = 0;

  void (*send)(void *ctx, uint8_t peer, const uint8_t *buf, uint16_t len) = nullptr;
  void *ctx = nullptr;

  // Host the run in gAdv: one just started (its LOAN, nothing picked yet), or
  // one this phone was already hosting when it was killed (its players come
  // back by rejoining). False for anything else -- a solo run already under
  // way stays solo.
  bool host(uint32_t player, const char *name, RaidRoll roll = nullptr);
  // Join a host: with no run of our own (`day` decides the price), or to
  // REJOIN the run our mirror belongs to. False otherwise.
  bool join(uint32_t player, const char *name, uint32_t day);
  void leave();                         // goodbye; a guest keeps its mirror
  void reset();                         // forget everything but the transport

  void onPacket(uint8_t peer, const uint8_t *buf, uint16_t len);
  void tick(uint32_t now);              // once a frame: resends, timeouts, the host's turn

  // My choices, on either side. The host's apply at once; a guest's are sent,
  // and show in its mirror once the host has taken them.
  bool pickLoan(uint8_t i);
  bool act(uint8_t slot, bool transform);
  bool catchChoice(bool swap);
  // Host: the lobby is complete -- it has picked, and so has every guest.
  bool canStart() const;
  bool start();
  // Host: every connected guest holds the newest state (say, the KEEP), so
  // the host may finish the run without stranding anybody's catches.
  bool allSeen() const;
  uint8_t players() const;              // seats a connected person plays, the host's included
  bool peerUsed(uint8_t p) const { return p < RNET_PEERS && peer[p].used; }
  void dropPeer(uint8_t p);             // the transport lost it; as a timeout
};

// Everything two builds must agree on to read each other's states. Built on
// linkBuildTag(), plus what only a raid sends.
uint16_t raidNetBuildTag();

// --- part 3. the game's side: one RaidNet, on the phone's socket -------------
//
// The sketch never touches the socket or the protocol's lifecycle; it calls
// these, and draws from `gRaid` and gAdv. forkRaidPoll() runs EVERY FRAME on
// every screen, not only on the adventure tab, so a host swiping to feed its
// pet still answers its guests and resolves their turns.
//
// Nothing here happens without forkFeatures() and the phone's transport
// (ForkHost::raidBegin): the board, the tests by default and a phone with no
// network all play adventures solo, exactly as before part 3. A run this phone
// was HOSTING when it was killed is hosted again by forkRaidPoll() as soon as
// it can be; with no transport, its players' seats go to the AI instead, so
// the solo calls can finish it.
extern RaidNet gRaid;

bool forkRaidOn();           // the switch, and a phone transport to play over
uint32_t forkRaidPlayer();   // this device's player number, never 0

// HOST: after advStart() (its LOAN, nothing picked), open the socket and host
// gAdv under `name`. False, with the run left solo, if that cannot be done.
bool forkRaidHost(const char *name);
// JOIN: open the socket as a guest and look for adventure hosts. The screen
// lists them (forkNetHosts()) or asks for an address (forkNetAskAddress());
// either way forkNetJoin() picks one, and forkRaidPoll() asks to join it --
// as a REJOIN when gAdv is already a mirror of somebody's run.
bool forkRaidBrowse(const char *name);
void forkRaidStop();         // goodbye, close the socket, forget the connection
// Once a frame. `today` prices a fresh guest's run (advStartCost()).
void forkRaidPoll(uint32_t now, uint32_t today, const char *name);

// What the adventure screen shows over the run, one answer for drawing and taps.
enum : uint8_t {
  RAIDV_NONE = 0,   // the run, as solo or connected
  RAIDV_BROWSE,     // a guest choosing a host
  RAIDV_JOINING,    // ...and waiting for its answer
  RAIDV_FAILED,     // refused, or never answered: say why (gRaid.refused), then back
  RAIDV_AWAY,       // a mirror of a run this phone is not connected to: REJOIN or GIVE UP
};
uint8_t forkRaidView();
// Am I the one being waited for? False when this phone has decided (or has
// nothing to decide); the screen shows "waiting for the others" instead.
bool forkRaidMyTurn();

