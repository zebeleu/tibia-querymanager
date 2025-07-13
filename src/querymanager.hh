#ifndef TIBIA_QUERYMANAGER_HH_
#define TIBIA_QUERYMANAGER_HH_ 1

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef size_t usize;

#define STATIC_ASSERT(expr) static_assert((expr), "static assertion failed: " #expr)
#define NARRAY(arr) (int)(sizeof(arr) / sizeof(arr[0]))
#define ISPOW2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define KB(x) ((usize)(x) << 10)
#define MB(x) ((usize)(x) << 20)
#define GB(x) ((usize)(x) << 30)

#if defined(_WIN32)
#	define OS_WINDOWS 1
#elif defined(__linux__) || defined(__gnu_linux__)
#	define OS_LINUX 1
#else
#	error "Operating system not supported."
#endif

#if defined(_MSC_VER)
#	define COMPILER_MSVC 1
#elif defined(__GNUC__)
#	define COMPILER_GCC 1
#elif defined(__clang__)
#	define COMPILER_CLANG 1
#endif

#if COMPILER_GCC || COMPILER_CLANG
#	define ATTR_FALLTHROUGH __attribute__((fallthrough))
#	define ATTR_PRINTF(x, y) __attribute__((format(printf, x, y)))
#else
#	define ATTR_FALLTHROUGH
#	define ATTR_PRINTF(x, y)
#endif

#if COMPILER_MSVC
#	define TRAP() __debugbreak()
#elif COMPILER_GCC || COMPILER_CLANG
#	define TRAP() __builtin_trap()
#else
#	define TRAP() abort()
#endif

#define ASSERT_ALWAYS(expr) if(!(expr)) { TRAP(); }
#if BUILD_DEBUG
#	define ASSERT(expr) ASSERT_ALWAYS(expr)
#else
#	define ASSERT(expr) ((void)(expr))
#endif

#define LOG(...)		LogAdd("INFO", __VA_ARGS__)
#define LOG_WARN(...)	LogAddVerbose("WARN", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERR(...)	LogAddVerbose("ERR", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define PANIC(...)																\
	do{																			\
		LogAddVerbose("PANIC", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);	\
		TRAP();																	\
	}while(0)

// Time
extern int  g_MonotonicTimeMS;

// Database CONFIG
extern char g_DatabaseFile[1024];
extern int  g_MaxCachedStatements;

// Connection CONFIG
extern char g_Password[30];
extern int  g_Port;
extern int  g_MaxConnections;
extern int  g_MaxConnectionIdleTime;
extern int  g_MaxConnectionPacketSize;
extern int  g_UpdateRate;

void LogAdd(const char *Prefix, const char *Format, ...) ATTR_PRINTF(2, 3);
void LogAddVerbose(const char *Prefix, const char *Function,
		const char *File, int Line, const char *Format, ...) ATTR_PRINTF(5, 6);

struct tm GetLocalTime(time_t t);
int64 GetClockMonotonicMS(void);
void SleepMS(int64 DurationMS);

bool StringEq(const char *A, const char *B);
bool StringEqCI(const char *A, const char *B);
bool StringCopyN(char *Dest, int DestCapacity, const char *Src, int SrcLength);
bool StringCopy(char *Dest, int DestCapacity, const char *Src);
bool ParseIPAddress(const char *String, int *OutAddr);

bool ReadBooleanConfig(bool *Dest, const char *Val);
bool ReadIntegerConfig(int *Dest, const char *Val);
bool ReadSizeConfig(int *Dest, const char *Val);
bool ReadStringConfig(char *Dest, int DestCapacity, const char *Val);
bool ReadConfig(const char *FileName);

// Buffer Utility
//==============================================================================
inline uint8 BufferRead8(const uint8 *Buffer){
	return Buffer[0];
}

inline uint16 BufferRead16LE(const uint8 *Buffer){
	return (uint16)Buffer[0]
		| ((uint16)Buffer[1] << 8);
}

inline uint16 BufferRead16BE(const uint8 *Buffer){
	return ((uint16)Buffer[0] << 8)
		| (uint16)Buffer[1];
}

inline uint32 BufferRead32LE(const uint8 *Buffer){
	return (uint32)Buffer[0]
		| ((uint32)Buffer[1] << 8)
		| ((uint32)Buffer[2] << 16)
		| ((uint32)Buffer[3] << 24);
}

inline uint32 BufferRead32BE(const uint8 *Buffer){
	return ((uint32)Buffer[0] << 24)
		| ((uint32)Buffer[1] << 16)
		| ((uint32)Buffer[2] << 8)
		| (uint32)Buffer[3];
}

inline void BufferWrite8(uint8 *Buffer, uint8 Value){
	Buffer[0] = Value;
}

inline void BufferWrite16LE(uint8 *Buffer, uint16 Value){
	Buffer[0] = (uint8)(Value >> 0);
	Buffer[1] = (uint8)(Value >> 8);
}

inline void BufferWrite16BE(uint8 *Buffer, uint16 Value){
	Buffer[0] = (uint8)(Value >> 8);
	Buffer[1] = (uint8)(Value >> 0);
}

inline void BufferWrite32LE(uint8 *Buffer, uint32 Value){
	Buffer[0] = (uint8)(Value >>  0);
	Buffer[1] = (uint8)(Value >>  8);
	Buffer[2] = (uint8)(Value >> 16);
	Buffer[3] = (uint8)(Value >> 24);
}

inline void BufferWrite32BE(uint8 *Buffer, uint32 Value){
	Buffer[0] = (uint8)(Value >> 24);
	Buffer[1] = (uint8)(Value >> 16);
	Buffer[2] = (uint8)(Value >>  8);
	Buffer[3] = (uint8)(Value >>  0);
}

struct TReadBuffer{
	uint8 *Buffer;
	int Size;
	int Position;

	TReadBuffer(uint8 *Buffer, int Size)
		: Buffer(Buffer), Size(Size), Position(0) {}

	bool CanRead(int Bytes){
		return (this->Position + Bytes) <= this->Size;
	}

	bool Overflowed(void){
		return this->Position > this->Size;
	}

	bool ReadFlag(void){
		return this->Read8() != 0x00;
	}

	uint8 Read8(void){
		uint8 Result = 0;
		if(this->CanRead(1)){
			Result = BufferRead8(this->Buffer + this->Position);
		}
		this->Position += 1;
		return Result;
	}

	uint16 Read16(void){
		uint16 Result = 0;
		if(this->CanRead(2)){
			Result = BufferRead16LE(this->Buffer + this->Position);
		}
		this->Position += 2;
		return Result;
	}

	uint16 Read16BE(void){
		uint16 Result = 0;
		if(this->CanRead(2)){
			Result = BufferRead16BE(this->Buffer + this->Position);
		}
		this->Position += 2;
		return Result;
	}

	uint32 Read32(void){
		uint32 Result = 0;
		if(this->CanRead(4)){
			Result = BufferRead32LE(this->Buffer + this->Position);
		}
		this->Position += 4;
		return Result;
	}

	uint32 Read32BE(void){
		uint32 Result = 0;
		if(this->CanRead(4)){
			Result = BufferRead32BE(this->Buffer + this->Position);
		}
		this->Position += 4;
		return Result;
	}

	void ReadString(char *Dest, int DestCapacity){
		int Length = (int)this->Read16();
		if(Length == 0xFFFF){
			Length = (int)this->Read32();
		}

		if(Dest != NULL && DestCapacity > 0){
			if(Length < DestCapacity && this->CanRead(Length)){
				memcpy(Dest, this->Buffer + this->Position, Length);
				Dest[Length] = 0;
			}else{
				Dest[0] = 0;
			}
		}

		this->Position += Length;
	}
};

struct TWriteBuffer{
	uint8 *Buffer;
	int Size;
	int Position;

	TWriteBuffer(uint8 *Buffer, int Size)
		: Buffer(Buffer), Size(Size), Position(0) {}

	bool CanWrite(int Bytes){
		return (this->Position + Bytes) <= this->Size;
	}

	bool Overflowed(void){
		return this->Position > this->Size;
	}

	void WriteFlag(bool Value){
		this->Write8(Value ? 0x01 : 0x00);
	}

	void Write8(uint8 Value){
		if(this->CanWrite(1)){
			BufferWrite8(this->Buffer + this->Position, Value);
		}
		this->Position += 1;
	}

	void Write16(uint16 Value){
		if(this->CanWrite(2)){
			BufferWrite16LE(this->Buffer + this->Position, Value);
		}
		this->Position += 2;
	}

	void Write16BE(uint16 Value){
		if(this->CanWrite(2)){
			BufferWrite16BE(this->Buffer + this->Position, Value);
		}
		this->Position += 2;
	}

	void Write32(uint32 Value){
		if(this->CanWrite(4)){
			BufferWrite32LE(this->Buffer + this->Position, Value);
		}
		this->Position += 4;
	}

	void Write32BE(uint32 Value){
		if(this->CanWrite(4)){
			BufferWrite32BE(this->Buffer + this->Position, Value);
		}
		this->Position += 4;
	}

	void WriteString(const char *String){
		int StringLength = 0;
		if(String != NULL){
			StringLength = (int)strlen(String);
		}

		if(StringLength < 0xFFFF){
			this->Write16((uint16)StringLength);
		}else{
			this->Write16(0xFFFF);
			this->Write32((uint32)StringLength);
		}

		if(StringLength > 0 && this->CanWrite(StringLength)){
			memcpy(this->Buffer + this->Position, String, StringLength);
		}

		this->Position += StringLength;
	}

	void Rewrite16(int Position, uint16 Value){
		if((Position + 2) <= this->Position){
			BufferWrite16LE(this->Buffer + Position, Value);
		}
	}

	void Insert32(int Position, uint32 Value){
		if(Position <= this->Position){
			if(this->CanWrite(4)){
				memmove(this->Buffer + Position + 4,
						this->Buffer + Position,
						this->Position - Position);
				BufferWrite32LE(this->Buffer + Position, Value);
			}

			this->Position += 4;
		}
	}
};

// Dynamic Array
//==============================================================================
template<typename T>
struct DynamicArray{
private:
	// IMPORTANT(fusion): This container is meant to be used with POD types.
	// Using it with anything else would most likely cause problems.
	STATIC_ASSERT(std::is_trivially_default_constructible<T>::value
			&& std::is_trivially_destructible<T>::value
			&& std::is_trivially_copyable<T>::value);

	T *m_Data;
	int m_Length;
	int m_Capacity;

	void EnsureCapacity(int Capacity){
		int OldCapacity = m_Capacity;
		if(Capacity > OldCapacity){
			// NOTE(fusion): Exponentially grow backing array.
			int NewCapacity = (OldCapacity > 0 ? OldCapacity : 8);
			while(NewCapacity < Capacity){
				if(NewCapacity > (INT_MAX - (NewCapacity / 2))){
					NewCapacity = INT_MAX;
					break;
				}

				NewCapacity += (NewCapacity / 2);
			}
			ASSERT(NewCapacity >= Capacity);

			T *NewData = (T*)realloc(m_Data, sizeof(T) * (usize)NewCapacity);
			if(NewData == NULL){
				PANIC("Failed to resize dynamic array from %d to %d", OldCapacity, NewCapacity);
				return;
			}

			// NOTE(fusion): Zero initialize newly allocated elements.
			memset(&NewData[OldCapacity], 0, sizeof(T) * (usize)(NewCapacity - OldCapacity));

			m_Data = NewData;
			m_Capacity = NewCapacity;
		}
	}

public:
	DynamicArray(void) : m_Data(NULL), m_Length(0), m_Capacity(0) {}
	~DynamicArray(void){
		if(m_Data != NULL){
			free(m_Data);
		}
	}

	// NOTE(fusion): Make it non copyable for simplicity. Implementing copy and
	// move operations could be useful on a general context but it won't make a
	// difference here since we're not gonna use it.
	DynamicArray(const DynamicArray &Other) = delete;
	void operator=(const DynamicArray &Other) = delete;

	bool Empty(void) const { return m_Length == 0; }
	int Length(void) const { return m_Length; }
	int Capacity(void) const { return m_Capacity; }

	void Reserve(int Capacity){
		EnsureCapacity(Capacity);
	}

	void Resize(int Length){
		ASSERT(Length >= 0);
		EnsureCapacity(Length);
		if(Length < m_Length){
			// NOTE(fusion): Maintain non-active elements zero initialized.
			memset(&m_Data[Length], 0, sizeof(T) * (usize)(m_Length - Length));
		}

		m_Length = Length;
	}

	void Insert(int Index, const T &Element){
		ASSERT(Index >= 0 && Index <= m_Length);
		EnsureCapacity(m_Length + 1);
		for(int i = m_Length; i > Index; i -= 1){
			m_Data[i] = m_Data[i - 1];
		}
		m_Data[Index] = Element;
		m_Length += 1;
	}

	void Push(const T &Element){
		EnsureCapacity(m_Length + 1);
		m_Data[m_Length] = Element;
		m_Length += 1;
	}

	void Remove(int Index){
		ASSERT(Index >= 0 && Index < m_Length);
		m_Length -= 1;
		for(int i = Index; i < m_Length; i += 1){
			m_Data[i] = m_Data[i + 1];
		}
		// NOTE(fusion): Maintain non-active elements zero initialized.
		memset(&m_Data[m_Length], 0, sizeof(T));
	}

	void Pop(void){
		ASSERT(m_Length > 0);
		m_Length -= 1;
		// NOTE(fusion): Maintain non-active elements zero initialized.
		memset(&m_Data[m_Length], 0, sizeof(T));
	}

	void SwapAndPop(int Index){
		ASSERT(Index >= 0 && Index < m_Length);
		m_Length -= 1;
		m_Data[Index] = m_Data[m_Length];
		// NOTE(fusion): Maintain non-active elements zero initialized.
		memset(&m_Data[m_Length], 0, sizeof(T));
	}

	T &operator[](int Index){
		ASSERT(Index >= 0 && Index < m_Length);
		return m_Data[Index];
	}

	const T &operator[](int Index) const {
		ASSERT(Index >= 0 && Index < m_Length);
		return m_Data[Index];
	}

	// ranged for loop
	T *begin(void) { return m_Data; }
	T *end(void) { return m_Data + m_Length; }
	const T *begin(void) const { return m_Data; }
	const T *end(void) const { return m_Data + m_Length; }
};

// connections.cc
//==============================================================================
enum : int {
	APPLICATION_TYPE_GAME	= 1,

	// TODO(fusion): There is definitely more application types that we don't
	// know about. From looking at the config loader on the server application,
	// we could perhaps infer a few, although we're gonna be limited to our own
	// usage cases (GAME and WEB).
	//APPLICATION_TYPE_WEB
	//APPLICATION_TYPE_FORUM
	//APPLICATION_TYPE_ADMIN
};

enum : int {
	QUERY_STATUS_OK			= 0,
	QUERY_STATUS_ERROR		= 1,
	QUERY_STATUS_FAILED		= 3,
};

enum : int {
	QUERY_LOGIN						= 0,
	QUERY_CHECK_ACCOUNT_PASSWORD	= 10,
	QUERY_LOGIN_ADMIN				= 12,
	QUERY_LOGIN_GAME				= 20,
	QUERY_LOGOUT_GAME				= 21,
	QUERY_SET_NAMELOCK				= 23,
	QUERY_BANISH_ACCOUNT			= 25,
	QUERY_SET_NOTATION				= 26,
	QUERY_REPORT_STATEMENT			= 27,
	QUERY_BANISH_IP_ADDRESS			= 28,
	QUERY_LOG_CHARACTER_DEATH		= 29,
	QUERY_ADD_BUDDY					= 30,
	QUERY_REMOVE_BUDDY				= 31,
	QUERY_DECREMENT_IS_ONLINE		= 32,
	QUERY_FINISH_AUCTIONS			= 33,
	QUERY_TRANSFER_HOUSES			= 35,
	QUERY_EVICT_FREE_ACCOUNTS		= 36,
	QUERY_EVICT_DELETED_CHARACTERS	= 37,
	QUERY_EVICT_EX_GUILDLEADERS		= 38,
	QUERY_INSERT_HOUSE_OWNER		= 39,
	QUERY_UPDATE_HOUSE_OWNER		= 40,
	QUERY_DELETE_HOUSE_OWNER		= 41,
	QUERY_GET_HOUSE_OWNERS			= 42,
	QUERY_GET_AUCTIONS				= 43,
	QUERY_START_AUCTION				= 44,
	QUERY_INSERT_HOUSES				= 45,
	QUERY_CLEAR_IS_ONLINE			= 46,
	QUERY_CREATE_PLAYERLIST			= 47,
	QUERY_LOG_KILLED_CREATURES		= 48,
	QUERY_LOAD_PLAYERS				= 50,
	QUERY_EXCLUDE_FROM_AUCTIONS		= 51,
	QUERY_CANCEL_HOUSE_TRANSFER		= 52,
	QUERY_LOAD_WORLD_CONFIG			= 53,
	QUERY_GET_KEPT_CHARACTERS		= 200,
	QUERY_GET_DELETED_CHARACTERS	= 201,
	QUERY_DELETE_OLD_CHARACTER		= 202,
	QUERY_GET_HIDDEN_CHARACTERS		= 203,
	QUERY_CREATE_HIGHSCORES			= 204,
	QUERY_CREATE_CENSUS				= 205,
	QUERY_CREATE_KILL_STATISTICS	= 206,
	QUERY_GET_PLAYERS_ONLINE		= 207,
	QUERY_GET_WORLDS				= 208,
	QUERY_GET_SERVER_LOAD			= 209,
	QUERY_INSERT_PAYMENT_DATA_OLD	= 210,
	QUERY_ADD_PAYMENT_OLD			= 211,
	QUERY_CANCEL_PAYMENT_OLD		= 212,
	QUERY_INSERT_PAYMENT_DATA_NEW	= 213,
	QUERY_ADD_PAYMENT_NEW			= 214,
	QUERY_CANCEL_PAYMENT_NEW		= 215,
};

enum ConnectionState: int {
	CONNECTION_FREE			= 0,
	CONNECTION_READING		= 1,
	CONNECTION_PROCESSING	= 2,
	CONNECTION_WRITING		= 3,
};

struct TConnection{
	ConnectionState State;
	int Socket;
	int LastActive;
	int RWSize;
	int RWPosition;
	uint8 *Buffer;
	bool Authorized;
	int ApplicationType;
	int WorldID;
	char RemoteAddress[30];
};

int ListenerBind(uint16 Port);
int ListenerAccept(int Listener, uint32 *OutAddr, uint16 *OutPort);
void CloseConnection(TConnection *Connection);
void EnsureConnectionBuffer(TConnection *Connection);
void DeleteConnectionBuffer(TConnection *Connection);
TConnection *AssignConnection(int Socket, uint32 Addr, uint16 Port);
void ReleaseConnection(TConnection *Connection);
void CheckConnectionInput(TConnection *Connection, int Events);
void CheckConnectionOutput(TConnection *Connection, int Events);
void CheckConnection(TConnection *Connection, int Events);
void ProcessConnections(void);
bool InitConnections(void);
void ExitConnections(void);

TWriteBuffer PrepareResponse(TConnection *Connection, int Status);
void SendResponse(TConnection *Connection, TWriteBuffer *WriteBuffer);
void SendQueryStatusOk(TConnection *Connection);
void SendQueryStatusError(TConnection *Connection, int ErrorCode);
void SendQueryStatusFailed(TConnection *Connection);
void ProcessLoginQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCheckAccountPasswordQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLoginAdminQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLoginGameQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLogoutGameQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessSetNamelockQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessBanishAccountQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessSetNotationQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessReportStatementQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessBanishIpAddressQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLogCharacterDeathQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessAddBuddyQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessRemoveBuddyQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessDecrementIsOnlineQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessFinishAuctionsQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessTransferHousesQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessEvictFreeAccountsQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessEvictDeletedCharactersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessEvictExGuildleadersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessInsertHouseOwnerQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessUpdateHouseOwnerQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessDeleteHouseOwnerQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetHouseOwnersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetAuctionsQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessStartAuctionQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessInsertHousesQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessClearIsOnlineQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCreatePlayerlistQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLogKilledCreaturesQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLoadPlayersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessExcludeFromAuctionsQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCancelHouseTransferQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessLoadWorldConfigQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetKeptCharactersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetDeletedCharactersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessDeleteOldCharacterQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetHiddenCharactersQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCreateHighscoresQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCreateCensusQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCreateKillStatisticsQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetPlayersOnlineQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetWorldsQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessGetServerLoadQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessInsertPaymentDataOldQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessAddPaymentOldQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCancelPaymentOldQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessInsertPaymentDataNewQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessAddPaymentNewQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessCancelPaymentNewQuery(TConnection *Connection, TReadBuffer *Buffer);
void ProcessConnectionQuery(TConnection *Connection);


// database.cc
//==============================================================================
struct THouseAuction{
	int HouseID;
	int BidderID;
	char BidderName[30];
	int BidAmount;
	int FinishTime;
};

struct THouseTransfer{
	int HouseID;
	int NewOwnerID;
	char NewOwnerName[30];
	int Price;
};

struct THouseEviction{
	int HouseID;
	int OwnerID;
};

struct THouseOwner{
	int HouseID;
	int OwnerID;
	char OwnerName[30];
	int PaidUntil;
};

struct THouse{
	int HouseID;
	char Name[50];
	int Rent;
	char Description[500];
	int Size;
	int PositionX;
	int PositionY;
	int PositionZ;
	char Town[30];
	bool GuildHouse;
};

struct TOnlineCharacter{
	char Name[30];
	int Level;
	char Profession[30];
};

struct TCharacterIndexEntry{
	char Name[30];
	int CharacterID;
};

struct TWorldConfig{
	int Type;
	int RebootTime;
	int Address;
	int Port;
	int MaxPlayers;
	int PremiumPlayerBuffer;
	int MaxNewbies;
	int PremiumNewbieBuffer;
};

struct TransactionScope{
private:
	const char *m_Context;
	bool m_Running;

public:
	TransactionScope(const char *Context);
	~TransactionScope(void);
	bool Begin(void);
	bool Commit(void);
};

// NOTE(fusion): Primary tables.
int GetWorldID(const char *WorldName);
bool GetWorldConfig(int WorldID, TWorldConfig *WorldConfig);
int GetCharacterID(int WorldID, const char *CharacterName);
bool GetCharacterRight(int CharacterID, const char *Right);
bool GetGuildLeaderStatus(int WorldID, int CharacterID);
bool DecrementIsOnline(int WorldID, int CharacterID);
bool ClearIsOnline(int WorldID, int *NumAffectedCharacters);
bool GetCharacterIndexEntries(int WorldID, int MinimumCharacterID,
		int MaxEntries, int *NumEntries, TCharacterIndexEntry *Entries);

// NOTE(fusion): House tables.
bool FinishHouseAuctions(int WorldID, DynamicArray<THouseAuction> *Auctions);
bool FinishHouseTransfers(int WorldID, DynamicArray<THouseTransfer> *Transfers);
bool GetFreeAccountEvictions(int WorldID, DynamicArray<THouseEviction> *Evictions);
bool GetDeletedCharacterEvictions(int WorldID, DynamicArray<THouseEviction> *Evictions);
bool InsertHouseOwner(int WorldID, int HouseID, int OwnerID, int PaidUntil);
bool UpdateHouseOwner(int WorldID, int HouseID, int OwnerID, int PaidUntil);
bool DeleteHouseOwner(int WorldID, int HouseID);
bool GetHouseOwners(int WorldID, DynamicArray<THouseOwner> *Owners);
bool GetHouseAuctions(int WorldID, DynamicArray<int> *Auctions);
bool StartHouseAuction(int WorldID, int HouseID);
bool DeleteHouses(int WorldID);
bool InsertHouses(int WorldID, int NumHouses, THouse *Houses);
bool ExcludeFromAuctions(int WorldID, int CharacterID, int Duration, int BanishmentID);

// NOTE(fusion): Banishment tables.
bool GetNamelockStatus(int CharacterID, bool *Approved);
bool InsertNamelock(int CharacterID, int IPAddress,
		int GamemasterID, const char *Reason, const char *Comment);

// NOTE(fusion): Info tables.
bool DeleteOnlineCharacters(int WorldID);
bool InsertOnlineCharacters(int WorldID, int NumCharacters, TOnlineCharacter *Characters);
bool CheckOnlineRecord(int WorldID, int NumCharacters, bool *NewRecord);

// NOTE(fusion): Internal database utility and initialization.
bool FileExists(const char *FileName);
bool ExecFile(const char *FileName);
bool ExecInternal(const char *Format, ...) ATTR_PRINTF(1, 2);
bool GetPragmaInt(const char *Name, int *OutValue);
bool InitDatabaseSchema(void);
bool UpgradeDatabaseSchema(int UserVersion);
bool CheckDatabaseSchema(void);
bool InitDatabase(void);
void ExitDatabase(void);

#endif //TIBIA_QUERYMANAGER_HH_
