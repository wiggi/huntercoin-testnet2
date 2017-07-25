#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <string>
#ifndef Q_MOC_RUN
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#endif
#include "json/json_spirit_value.h"
#include "uint256.h"
#include "serialize.h"

namespace Game
{

static const int NUM_TEAM_COLORS = 4;
static const int MAX_WAYPOINTS = 100;                      // Maximum number of waypoints per character
static const unsigned char MAX_STAY_IN_SPAWN_AREA = 30;
static const int MAX_CHARACTERS_PER_PLAYER = 20;           // Maximum number of characters per player at the same time
static const int MAX_CHARACTERS_PER_PLAYER_TOTAL = 1000;   // Maximum number of characters per player in the lifetime

// Unique player name
typedef std::string PlayerID;

// Player name + character index
struct CharacterID
{
    PlayerID player;
    int index;
    
    CharacterID() : index(-1) { }
    CharacterID(const PlayerID &player_, int index_)
        : player(player_), index(index_)
    {
        if (index_ < 0)
            throw std::runtime_error("Bad character index");
    }

    std::string ToString() const;

    static CharacterID Parse(const std::string &s)
    {
        size_t pos = s.find('.');
        if (pos == std::string::npos)
            return CharacterID(s, 0);
        return CharacterID(s.substr(0, pos), atoi(s.substr(pos + 1).c_str()));
    }

    bool operator==(const CharacterID &that) const { return player == that.player && index == that.index; }
    bool operator!=(const CharacterID &that) const { return !(*this == that); }
    // Lexicographical comparison
    bool operator<(const CharacterID &that) const { return player < that.player || (player == that.player && index < that.index); }
    bool operator>(const CharacterID &that) const { return that < *this; }
    bool operator<=(const CharacterID &that) const { return !(*this > that); }
    bool operator>=(const CharacterID &that) const { return !(*this < that); }
};

class GameState;
class RandomGenerator;
class PlayerState;
class KilledByInfo;
class StepResult;

// Define STL types used for killed player identification later on.
typedef std::set<PlayerID> PlayerSet;
typedef std::multimap<PlayerID, KilledByInfo> KilledByMap;
typedef std::map<PlayerID, PlayerState> PlayerStateMap;

struct Coord
{
    int x, y;

    Coord() : x(0), y(0) { }
    Coord(int x_, int y_) : x(x_), y(y_) { }

    unsigned int GetSerializeSize(int = 0, int = VERSION) const
    {
        return sizeof(int) * 2;
    }

    template<typename Stream>
    void Serialize(Stream& s, int = 0, int = VERSION) const
    {
        WRITEDATA(s, x);
        WRITEDATA(s, y);
    }

    template<typename Stream>
    void Unserialize(Stream& s, int = 0, int = VERSION)
    {
        READDATA(s, x);
        READDATA(s, y);
    }

    bool operator==(const Coord &that) const { return x == that.x && y == that.y; }
    bool operator!=(const Coord &that) const { return !(*this == that); }
    // Lexicographical comparison
    bool operator<(const Coord &that) const { return y < that.y || (y == that.y && x < that.x); }
    bool operator>(const Coord &that) const { return that < *this; }
    bool operator<=(const Coord &that) const { return !(*this > that); }
    bool operator>=(const Coord &that) const { return !(*this < that); }
};

typedef std::vector<Coord> WaypointVector;

struct Move
{
    PlayerID player;

    // Updates to the player state
    boost::optional<std::string> message;
    boost::optional<std::string> address;
    boost::optional<std::string> addressLock;

    // playground -- bounties and voting
    boost::optional<std::string> addr_token;
    boost::optional<std::string> msg_vote;
    boost::optional<std::string> msg_request;
    boost::optional<std::string> msg_fee;
    boost::optional<std::string> msg_comment;

    /* For spawning moves.  */
    unsigned char color;
    int64 coinAmount;

    std::map<int, WaypointVector> waypoints;
    std::set<int> destruct;

    Move ()
      : color(0xFF), coinAmount(-1)
    {}

    std::string AddressOperationPermission(const GameState &state) const;

    bool IsSpawn() const { return color != 0xFF; }
    bool IsValid(const GameState &state) const;
    void ApplyCommon(GameState &state) const;
    void ApplySpawn(GameState &state, RandomGenerator &rnd) const;
    void ApplyWaypoints(GameState &state) const;
    bool IsAttack(const GameState &state, int character_index) const;
 
    // Move must be empty before Parse and cannot be reused after Parse
    bool Parse(const PlayerID &player, const std::string &json);

    // Returns true if move is initialized (i.e. was parsed successfully)
    operator bool() { return !player.empty(); }
};

// Do not use for user-provided coordinates, as abs can overflow on INT_MIN.
// Use for algorithmically-computed coordinates that guaranteedly lie within the game map.
inline int distLInf(const Coord &c1, const Coord &c2)
{
    return std::max(abs(c1.x - c2.x), abs(c1.y - c2.y));
}

struct LootInfo
{
    int64 nAmount;
    // Time span over the which this loot accumulated
    // This is merely for informative purposes, plus to make
    // hash of the loot tx unique
    int firstBlock, lastBlock;

    LootInfo() : nAmount(0), firstBlock(-1), lastBlock(-1) { }
    LootInfo(int64 nAmount_, int nHeight) : nAmount(nAmount_), firstBlock(nHeight), lastBlock(nHeight) { }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(nAmount);
        READWRITE(firstBlock);
        READWRITE(lastBlock);
    )
};

struct CollectedLootInfo : public LootInfo
{
    /* Time span over which the loot was collected.  If this is a
       player refund bounty, collectedFirstBlock = -1 and collectedLastBlock
       is set to the refunding block height.  */
    int collectedFirstBlock, collectedLastBlock;
    
    CollectedLootInfo() : LootInfo(), collectedFirstBlock(-1), collectedLastBlock(-1) { }

    void Collect(const LootInfo &loot, int nHeight)
    {
        assert (!IsRefund ());

        if (loot.nAmount <= 0)
            return;

        nAmount += loot.nAmount;

        if (firstBlock < 0 || loot.firstBlock < firstBlock)
            firstBlock = loot.firstBlock;
        if (loot.lastBlock > lastBlock)
            lastBlock = loot.lastBlock;

        if (collectedFirstBlock < 0)
            collectedFirstBlock = nHeight;
        collectedLastBlock = nHeight;
    }

    /* Set the loot info to a state that means "this is a player refunding tx".
       They are used to give back coins if a player is killed for staying in
       the spawn area, and encoded differently in the game transactions.
       The block height is present to make the resulting tx unique.  */
    inline void
    SetRefund (int64 refundAmount, int nHeight)
    {
      assert (nAmount == 0);
      assert (collectedFirstBlock == -1 && collectedLastBlock == -1);
      nAmount = refundAmount;
      collectedLastBlock = nHeight;
    }

    /* Check if this is a player refund tx.  */
    inline bool
    IsRefund () const
    {
      return (nAmount > 0 && collectedFirstBlock == -1);
    }

    /* When this is a refund, return the refund block height.  */
    inline int
    GetRefundHeight () const
    {
      assert (IsRefund ());
      return collectedLastBlock;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(*(LootInfo*)this);
        READWRITE(collectedFirstBlock);
        READWRITE(collectedLastBlock);
        assert (!IsRefund ());
    )
};

struct CharacterState
{
    Coord coord;                        // Current coordinate
    unsigned char dir;                  // Direction of last move (for nice sprite orientation). Encoding: as on numeric keypad.
    Coord from;                         // Straight-line pathfinding for current waypoint
    WaypointVector waypoints;           // Waypoints (stored in reverse so removal of the first waypoint is fast)
    CollectedLootInfo loot;             // Loot collected by player but not banked yet
    unsigned char stay_in_spawn_area;   // Auto-kill players who stay in the spawn area too long

    // playground -- extended character state
    unsigned char ai_npc_role;
    unsigned char ai_slot_spell;
    unsigned char rpg_slot_cooldown;
    unsigned char ai_slot_amulet;
    unsigned char ai_poi;
    unsigned char ai_state;
    unsigned char ai_state2;
    unsigned char ai_state3;
    unsigned char ai_chat;
    unsigned char ai_idle_time;
    unsigned char ai_mapitem_count;
    unsigned char ai_foe_count;
    unsigned char ai_foe_dist;
    unsigned char ai_fav_harvest_poi;
    unsigned char ai_slot_ring;
    //
    int64 ai_reserve64_1;
    int64 ai_reserve64_2;
    //
    unsigned char ai_retreat;
    unsigned char ai_queued_harvest_poi;
    unsigned char ai_duty_harvest_poi;
    int rpg_survival_points;
    int rpg_rations;
    int rpg_range_for_display;
    int ai_recall_timer;
    int ai_regen_timer;
    //
    int ai_order_time;
    unsigned char ai_marked_harvest_poi; // for mark+recall spell
    //
    int64 aux_storage_s1;
    int64 aux_storage_s2;
    uint64 aux_storage_u1;
    uint64 aux_storage_u2;
    //
    unsigned char ai_reason;
    unsigned char rpg_slot_armor;
    int aux_spawn_block;
    int aux_last_sale_block;
    int64 aux_stasis_block; // can be int
    int64 ai_reserve6;

    CharacterState ()
      : coord(0, 0), dir(0), from(0, 0),
        // playground -- extended character state
//        stay_in_spawn_area(0)
        stay_in_spawn_area(0),
        ai_npc_role(0),
        ai_slot_spell(0),
        rpg_slot_cooldown(0),
        ai_slot_amulet(0),
        ai_poi(0),
        ai_state(0),
        ai_state2(0),
        ai_state3(0),
        ai_chat(0),
        ai_idle_time(0),
        ai_mapitem_count(0),
        ai_foe_count(0),
        ai_foe_dist(0),
        ai_fav_harvest_poi(0),
        ai_slot_ring(0),
        //
        ai_reserve64_1(0),
        ai_reserve64_2(0),
        //
        ai_retreat(0),
        ai_queued_harvest_poi(0),
        ai_duty_harvest_poi(0),
        rpg_survival_points(0),
        rpg_rations(0),
        rpg_range_for_display(0),
        ai_recall_timer(0),
        ai_regen_timer(0),
        //
        ai_order_time(0),
        ai_marked_harvest_poi(0),
        //
        aux_storage_s1(0),
        aux_storage_s2(0),
        aux_storage_u1(0),
        aux_storage_u2(0),
        //
        ai_reason(0),
        rpg_slot_armor(0),
        aux_spawn_block(0),
        aux_last_sale_block(0),
        aux_stasis_block(0),
        ai_reserve6(0)
    {}

    IMPLEMENT_SERIALIZE
    (
        /* Last version change is beyond the last version where the game db
           is fully reconstructed.  */
        assert (nVersion >= 1000900);

        READWRITE(coord);
        READWRITE(dir);
        READWRITE(from);
        READWRITE(waypoints);
        READWRITE(loot);
        READWRITE(stay_in_spawn_area);

        // playground -- extended character state
        READWRITE(ai_npc_role);
        READWRITE(ai_slot_spell);
        READWRITE(rpg_slot_cooldown);
        READWRITE(ai_slot_amulet);
        READWRITE(ai_poi);
        READWRITE(ai_state);
        READWRITE(ai_state2);
        READWRITE(ai_state3);
        READWRITE(ai_chat);
        READWRITE(ai_idle_time);
        READWRITE(ai_mapitem_count);
        READWRITE(ai_foe_count);
        READWRITE(ai_foe_dist);
        READWRITE(ai_fav_harvest_poi);
        READWRITE(ai_slot_ring);
        //
        READWRITE(ai_reserve64_1);
        READWRITE(ai_reserve64_2);
        //
        READWRITE(ai_retreat);
        READWRITE(ai_queued_harvest_poi);
        READWRITE(ai_duty_harvest_poi);
        READWRITE(rpg_survival_points);
        READWRITE(rpg_rations);
        READWRITE(rpg_range_for_display);
        READWRITE(ai_recall_timer);
        READWRITE(ai_regen_timer);
        //
        READWRITE(ai_order_time);
        READWRITE(ai_marked_harvest_poi);
        //
        READWRITE(aux_storage_s1);
        READWRITE(aux_storage_s2);
        READWRITE(aux_storage_u1);
        READWRITE(aux_storage_u2);
        //
        READWRITE(ai_reason);
        READWRITE(rpg_slot_armor);
        READWRITE(aux_spawn_block);
        READWRITE(aux_last_sale_block);
        READWRITE(aux_stasis_block);
        READWRITE(ai_reserve6);
    )

    void Spawn(int color, RandomGenerator &rnd);

    void StopMoving()
    {
        from = coord;
        waypoints.clear();
    }

    // playground -- extended version of MoveTowardsWaypoint
    void MoveTowardsWaypointX_Merchants(RandomGenerator &rnd, int color_of_moving_char, int out_height);
    void MoveTowardsWaypointX_Pathfinder(RandomGenerator &rnd, int color_of_moving_char, int out_height);

    void MoveTowardsWaypoint();
    WaypointVector DumpPath(const WaypointVector *alternative_waypoints = NULL) const;

    /**
     * Calculate total length (in the same L-infinity sense that gives the
     * actual movement time) of the outstanding path.
     * @param altWP Optionally provide alternative waypoints (for queued moves).
     * @return Time necessary to finish current path in blocks.
     */
    unsigned TimeToDestination(const WaypointVector *altWP = NULL) const;

    /* Collect loot by this character.  This takes the carrying capacity
       into account and only collects until this limit is reached.  All
       loot amount that *remains* will be returned.  */
    int64 CollectLoot (LootInfo newLoot, int nHeight, int64 carryCap);

    json_spirit::Value ToJsonValue(bool has_crown) const;
};

struct PlayerState
{
    /* Colour represents player team.  */
    unsigned char color;
    /* Value locked by the general's name.  This is the amount that will
       be placed back onto the map when the player dies, and it should
       match the actual coin value.  */
    int64 coinAmount;

    std::map<int, CharacterState> characters;   // Characters owned by the player (0 is the main character)
    int next_character_index;                   // Index of the next spawned character

    /* Number of blocks the player still lives if poisoned.  If it is 1,
       the player will be killed during the next game step.  -1 means
       that there is no poisoning yet.  It should never be 0.  */
    int remainingLife;

    std::string message;      // Last message, can be shown as speech bubble
    int message_block;        // Block number. Game visualizer can hide messages that are too old
    std::string address;      // Address for receiving rewards. Empty means receive to the name address
    std::string addressLock;  // "Admin" address for player - reward address field can only be changed, if player is transferred to addressLock

    // playground -- bounties and voting
    std::string addr_token;
    std::string msg_vote;
    int msg_vote_block;
    std::string msg_request;
    int msg_request_block;
    std::string msg_fee;
    std::string msg_comment;
    int64 coins_vote;
    int64 coins_request;
    int64 coins_fee;

    IMPLEMENT_SERIALIZE
    (
        /* Last version change is beyond the last version where the game db
           is fully reconstructed.  */
        assert (nVersion >= 1001100);

        READWRITE(color);
        READWRITE(characters);
        READWRITE(next_character_index);
        READWRITE(remainingLife);

        READWRITE(message);
        READWRITE(message_block);
        READWRITE(address);
        READWRITE(addressLock);

        // playground -- bounties and voting
        READWRITE(addr_token);
        READWRITE(msg_vote);
        READWRITE(msg_vote_block);
        READWRITE(msg_request);
        READWRITE(msg_request_block);
        READWRITE(msg_fee);
        READWRITE(msg_comment);
        READWRITE(coins_vote);
        READWRITE(coins_request);
        READWRITE(coins_fee);

        READWRITE(coinAmount);
    )

    PlayerState ()
      : color(0xFF), coinAmount(-1),
        next_character_index(0), remainingLife(-1), message_block(0)
      // playground -- bounties and voting
      , msg_vote_block(0), msg_request_block(0), coins_vote(0), coins_request(0), coins_fee(0)
    {}

    void SpawnCharacter(RandomGenerator &rnd);
    bool CanSpawnCharacter()
    {
        return characters.size() < MAX_CHARACTERS_PER_PLAYER && next_character_index < MAX_CHARACTERS_PER_PLAYER_TOTAL;
    }
    json_spirit::Value ToJsonValue(int crown_index, bool dead = false) const;
};

struct GameState
{
    GameState();

    // Player states
    PlayerStateMap players;

    // Last chat messages of dead players (only in the current block)
    // Minimum info is stored: color, message, message_block.
    // When converting to JSON, this array is concatenated with normal players.
    std::map<PlayerID, PlayerState> dead_players_chat;

    std::map<Coord, LootInfo> loot;

    std::set<Coord> hearts;
    Coord crownPos;
    CharacterID crownHolder;

    /* Amount of coins in the "game fund" pool.  */
    int64 gameFund;

    // Number of steps since the game start.
    // State with nHeight==i includes moves from i-th block
    // -1 = initial game state (before genesis block)
    // 0  = game state immediately after the genesis block
    int nHeight;

    /* Block height (as per nHeight) of the last state that had a disaster.
       I. e., for a game state where disaster has just happened,
       nHeight == nDisasterHeight.  It is -1 before the first disaster
       happens.  */
    int nDisasterHeight;

    // Hash of the last block, moves from which were included
    // into this game state. This is meta-information (i.e. used
    // mainly for managing game states rather than as part of game
    // state, though it can be used as a random seed)
    uint256 hashBlock;

    // playground -- bounties and voting
    int64 dao_BestFee;
    int64 dao_BestFeeFinal; // for display only
    int64 dao_BestRequest;
    int64 dao_BestRequestFinal;
    std::string dao_BestName;
    std::string dao_BestNameFinal;
    int64 dao_BountyPreviousWeek; // for display only
    std::string dao_NamePreviousWeek; // for display only


    IMPLEMENT_SERIALIZE
    (
      /* Should be only ever written to disk.  */
      assert (nType & SER_DISK);

      /* Last version change is beyond the last version where the game db
         is fully reconstructed.  */
      assert (nVersion >= 1001100);

      READWRITE(players);
      READWRITE(dead_players_chat);
      READWRITE(loot);

      READWRITE(hearts);
      READWRITE(crownPos);
      READWRITE(crownHolder.player);
      if (!crownHolder.player.empty())
        READWRITE(crownHolder.index);
      READWRITE(gameFund);

      READWRITE(nHeight);
      READWRITE(nDisasterHeight);
      READWRITE(hashBlock);

      // playground -- bounties and voting
      READWRITE(dao_BestFee);
      READWRITE(dao_BestFeeFinal);
      READWRITE(dao_BestRequest);
      READWRITE(dao_BestRequestFinal);
      READWRITE(dao_BestName);
      READWRITE(dao_BestNameFinal);
      READWRITE(dao_BountyPreviousWeek);
      READWRITE(dao_NamePreviousWeek);
    )

    void UpdateVersion(int oldVersion);

    json_spirit::Value ToJsonValue() const;

    // Helper functions
    void AddLoot(Coord coord, int64 nAmount);
    void DivideLootAmongPlayers();
    void CollectHearts(RandomGenerator &rnd);
    void UpdateCrownState(bool &respawn_crown);
    void CollectCrown(RandomGenerator &rnd, bool respawn_crown);
    void CrownBonus(int64 nAmount);

    /**
     * Get the number of initial characters for players created in this
     * game state.  This was initially 3, and is changed in a hardfork
     * depending on the block height.
     * @return Number of initial characters to create (including general).
     */
    unsigned GetNumInitialCharacters () const;

    /* Handle loot of a killed character.  Depending on the circumstances,
       it may be dropped (with or without miner tax), refunded in a bounty
       transaction or added to the game fund.  */
    void HandleKilledLoot (const PlayerID& pId, int chInd,
                           bool hasTax, bool canRefund, StepResult& step);

    /* For a given list of killed players, kill all their characters
       and collect the tax amount.  The killed players are removed from
       the state's list of players.  */
    void FinaliseKills (StepResult& step);

    /* Check if a disaster should happen at the current state given
       the random numbers.  */
    bool CheckForDisaster (RandomGenerator& rng) const;

    /* Perform spawn deaths.  */
    void KillSpawnArea (StepResult& step);


    // playground -- ranged attacks
    void KillRangedAttacks (StepResult& step);

    void Pass0_CacheDataForGame ();
    void Pass1_DAO ();
    void Pass2_Melee ();
    void Pass3_PaymentAndHitscan ();
    void Pass4_Refund ();
    void PrintPlayerStats ();


    /* Apply poison disaster to the state.  */
    void ApplyDisaster (RandomGenerator& rng);
    /* Decrement poison life expectation and kill players whose has
       dropped to zero.  */
    void DecrementLife (StepResult& step);

    /* Return total amount of coins on the map (in loot and hold by players,
       excluding coins locked by generals since they appear in the UTXO set
       already).  */
    int64 GetCoinsOnMap () const;

};

struct StepData : boost::noncopyable
{
    int64 nTreasureAmount;
    uint256 newHash;
    std::vector<Move> vMoves;
};

/* Encode data for a banked bounty.  This includes also the payment address
   as per the player state (may be empty if no explicit address is set), so
   that the reward-paying game tx can be constructed even if the player
   is no longer alive (e. g., killed by a disaster).  */
struct CollectedBounty
{

  CharacterID character;
  CollectedLootInfo loot;
  std::string address;

  inline CollectedBounty (const PlayerID& p, int cInd,
                          const CollectedLootInfo& l,
                          const std::string& addr)
    : character(p, cInd), loot(l), address(addr)
  {}

  /* Look up the player in the given game state and if it is still
     there, update the address from the game state.  */
  void UpdateAddress (const GameState& state);

};

/* Encode data about why or by whom a player was killed.  Possibilities
   are a player (also self-destruct), staying too long in spawn area and
   due to poisoning after a disaster.  The information is used to
   construct the game transactions.  */
struct KilledByInfo
{

  /* Actual reason for death.  Since this is also used for ordering of
     the killed-by infos, the order here is crucial and determines
     how the killed-by info will be represented in the constructed game tx.  */
  enum Reason
  {
    KILLED_DESTRUCT = 1, /* Killed by destruct / some player.  */
    KILLED_SPAWN,        /* Staying too long in spawn area.  */
    KILLED_POISON        /* Killed by poisoning.  */
  } reason;

  /* The killing character, if killed by destruct.  */
  CharacterID killer;

  inline KilledByInfo (Reason why)
    : reason(why)
  {
    assert (why != KILLED_DESTRUCT);
  }

  inline KilledByInfo (const CharacterID& ch)
    : reason(KILLED_DESTRUCT), killer(ch)
  {}

  /* See if this killing reason pays out miner tax or not.  */
  inline bool
  HasDeathTax () const
  {
    return reason != KILLED_SPAWN;
  }

  /* Comparison necessary for STL containers.  */

  friend inline bool
  operator== (const KilledByInfo& a, const KilledByInfo& b)
  {
    if (a.reason != b.reason)
      return false;

    switch (a.reason)
      {
      case KILLED_DESTRUCT:
        return a.killer == b.killer;
      default:
        return true;
      }
  }

  friend inline bool
  operator< (const KilledByInfo& a, const KilledByInfo& b)
  {
    if (a.reason != b.reason)
      return (a.reason < b.reason);

    switch (a.reason)
      {
      case KILLED_DESTRUCT:
        return a.killer < b.killer;
      default:
        return false;
      }
  }

};

class StepResult
{

private:

    // The following arrays only contain killed players
    // (i.e. the main character)
    PlayerSet killedPlayers;
    KilledByMap killedBy;

public:

    std::vector<CollectedBounty> bounties;

    int64 nTaxAmount;

    StepResult() : nTaxAmount(0) { }

    /* Insert information about a killed player.  */
    inline void
    KillPlayer (const PlayerID& victim, const KilledByInfo& killer)
    {
      killedBy.insert (std::make_pair (victim, killer));
      killedPlayers.insert (victim);
    }

    /* Read-only access to the killed player maps.  */

    inline const PlayerSet&
    GetKilledPlayers () const
    {
      return killedPlayers;
    }

    inline const KilledByMap&
    GetKilledBy () const
    {
      return killedBy;
    }

};

// All moves happen simultaneously, so this function must work identically
// for any ordering of the moves, except non-critical cases (e.g. finding
// an empty cell to spawn new player)
bool PerformStep(const GameState &inState, const StepData &stepData, GameState &outState, StepResult &stepResult);

}


// playground -- variables declaration
#define ALTNAME_LEN_MAX 18
#define ALTNAME_ASCII_OK(C) ((C >= 32) && (C <= 126))
extern int Displaycache_devmode;
extern std::string Displaycache_devmode_npcname;
//extern std::string Rpg_TeamColorDesc[NUM_TEAM_COLORS];
extern std::string Rpg_TeamColorDesc[4];

// hunter messages
//#define ALLOW_H2H_PAYMENT
#define ALLOW_H2H_PAYMENT_NPCONLY
#define HUNTERMSG_CACHE_MAX 10000
extern std::string Huntermsg_pay_other[HUNTERMSG_CACHE_MAX];


#endif
